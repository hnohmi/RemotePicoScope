# Data / Display Architecture

Two strictly separated layers. The invariant: **display settings never change
signal data; signal data never depends on how it is displayed.**

```
                    ┌──────────────────────────────────────────────┐
                    │ SIGNAL LAYER  (volts at the probe tip)       │
 hardware ──ADC──▶  │  PicoSignalSource / DummySignalSource        │
                    │  · probe attenuation & invert folded in at   │
                    │    ADC→volts conversion — exactly once       │
                    │  · trigger level stored in tip volts;        │
                    │    converted to BNC volts / ADC counts only  │
                    │    at the driver call                        │
                    │  · Measurements, MathEngine, FFTEngine       │
                    │  · CLI / remote: capture, measure, get-state │
                    └──────────────────┬───────────────────────────┘
                                       │  ChannelView (render/ViewTransform.h)
                                       │  stage 1: volts → divisions (offset-free)
                                       │  stage 2: divisions → pixels (+ offsetDiv)
                    ┌──────────────────▼───────────────────────────┐
                    │ DISPLAY LAYER  (screen XY)                   │
                    │  · per-channel vertical offset applied HERE, │
                    │    uniformly to that channel's waveform,     │
                    │    trigger level line, 0 V ground marker     │
                    │  · cursors live on this plane (divisions);   │
                    │    voltage readouts resolve through the      │
                    │    cursor source channel's view              │
                    │  · XY drag control (pan time / move offset)  │
                    └──────────────────────────────────────────────┘
```

## Rules

1. **Sample buffers hold probe-tip volts.** Probe attenuation and invert are
   applied once, inside the signal source, at ADC→volts conversion
   (`PicoSignalSource::retrieveData`, `DummySignalSource::acquire`).
   Never post-process the persistent buffer: the Pico source keeps the
   previous capture on frames where no new block is ready, so an in-place
   pass would re-apply itself to stale data.
2. **Vertical offset is display-only.** It lives in `ChannelState`, is applied
   only inside `ChannelView` (stage 2), is *not* in the hardware
   `ConfigSnapshot` (changing it never stops/restarts acquisition), and never
   appears in CLI `capture`/`measure` output. Channel N's offset cannot affect
   channel M in any way.
3. **`ChannelView` is the only volts→screen mapping.** All drawing (waveform,
   trigger indicator via the *trigger source's* view, ground markers, cursor
   readouts) goes through it. If you need volts on screen, build a
   `ChannelView` — do not hand-roll the formula.
4. **Instrument control values are data-domain; every hardware call converts
   to API units.** `trigger.level` is tip volts → BNC volts
   (`level / userScale`, direction flips when inverted) → ADC counts.
   V/div is tip volts → the BNC input range is `voltsPerDiv / probe`
   (keeps full ADC resolution with a 10× probe; the round trip back is
   `rangeToVolts(range) * userScale` in `retrieveData`). Digital thresholds
   are volts → ADC counts at `ps3000aSetDigitalPort`. Note: the ps3000a API
   itself has **no probe setting** — the driver always works in BNC volts,
   so these conversions are the application's responsibility (same as
   PicoScope 6).
5. **Cursors are display-plane objects** (divisions from grid center) with a
   `source` channel. Their *voltage* readouts legitimately depend on that
   channel's V/div and offset (they describe screen positions); ΔV is
   offset-independent. They never touch signal data.

## Validated on hardware (3406D, siggen split into CH A/B)

- Offset changes: data unchanged (vavg stays ~0 with 1 V offset), other
  channels unaffected, no acquisition restart, amplitude rock-stable under
  offset churn.
- Probe 10× on A: A reads ×10, B unchanged; CLI values scale exactly once.
- Trigger in tip volts: with probe 10×, level 5 V triggers at 0.5 V BNC
  (verified v[trigger sample] ≈ level for levels 0.5, −0.3, 5, 8; starved at
  15 V as expected).
- Note for future validation: with a repetitive signal and a working trigger,
  consecutive captures are **bit-identical** (phase-locked). Do not use
  "captures differ" as a trigger-alive check; check that the sample at the
  trigger index equals the trigger level instead.
