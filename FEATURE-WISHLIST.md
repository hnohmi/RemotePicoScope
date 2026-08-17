# MSO Feature Gap Analysis & Wish List

Comparison of RemotePicoScope against commercial mixed-signal oscilloscopes
(Keysight InfiniiVision HD3, Tektronix MSO 2/4/5 Series, Rigol DHO900,
Siglent SDS2000X HD, R&S MXO4), with a prioritized wish list.

Hardware context: PicoScope 3406D MSO — 200 MHz, 4 analog + 16 digital
channels, 1 GS/s, 512 MS memory, built-in AWG. Many "missing" features below
are supported by the ps3000a driver and only need application-side work.

---

## 1. Where the app stands today

Implemented (from `src/` inventory):

| Area | Current state |
|---|---|
| Analog channels | 4 ch, 1 mV–100 V/div, AC/DC/GND coupling, offset, BW limit |
| Digital channels | 16 lanes, display only (on/off per lane) |
| Trigger | Edge only (rising/falling), analog sources only, auto/normal/single |
| Math | 1 channel: +, −, ×, ÷, FFT, d/dt, ∫, √ |
| FFT | 5 window functions, dedicated display |
| Measurements | 10 types (freq, period, Vpp, Vavg, Vrms, Vmax, Vmin, rise, fall, duty), whole-buffer |
| Cursors | One X pair + one Y pair with delta readout |
| Acquisition | Normal mode only, up to 50 M samples record length |
| Signal generator | Standard waveforms via built-in sig gen |
| Extras | MIDI control surface mapping, TCP remote + CLI, docking UI, demo mode |

This is a solid *DSO* core. The gaps below are what separate it from a
commercial *MSO*.

---

## 2. Gap analysis vs. commercial MSOs

### 2.1 Digital / MSO capabilities — the biggest gap

The app currently treats digital channels as passive display lanes. Every
commercial MSO treats them as first-class trigger/analysis sources.

| Feature | Keysight HD3 | Tek MSO 4 | Rigol DHO900 | RemotePicoScope |
|---|---|---|---|---|
| Configurable logic thresholds | ✔ | ✔ | ✔ | ✘ (driver supports per-port) |
| Trigger on digital channels | ✔ | ✔ | ✔ | ✘ |
| Pattern/logic trigger (analog+digital) | ✔ | ✔ | ✔ | ✘ |
| Bus grouping w/ hex/binary readout | ✔ | ✔ | ✔ | ✘ |
| Serial protocol decode | ✔ (HW-accelerated) | ✔ | ✔ | ✘ |
| Protocol-aware trigger | ✔ | ✔ | ✔ | ✘ |
| Digital channel labels | ✔ | ✔ | ✔ | ✘ |

### 2.2 Trigger system

Commercial scopes ship 8–15 trigger types. The app has one.

Missing: pulse width, runt, window, slope/rise-time, timeout (dropout),
setup & hold, Nth edge, pattern, logic, serial/protocol, video, zone
(Keysight's draw-a-box touch trigger); plus trigger holdoff, noise reject /
HF reject coupling, hysteresis control, force-trigger, EXT trigger input
(the 3406D has one), and A→B trigger sequencing.

Note: the ps3000a API has hardware pulse-width qualifiers and logic trigger
conditions (`ps3000aSetPulseWidthQualifier`, trigger channel conditions
including digital ports), so several of these are driver-native.

### 2.3 Acquisition modes

Missing vs. all major brands: average, peak detect, high-resolution
(boxcar decimation), envelope, roll mode for slow timebases, segmented
memory / rapid-block capture (ps3000a rapid block is driver-native),
history buffer with waveform playback (standard on Rigol/Siglent),
equivalent-time sampling (ETS, driver-native).

### 2.4 Display & analysis

Missing: zoom window (main + magnified split view), XY mode, persistence
display (variable/infinite, intensity-graded), reference waveforms
(save/recall traces for visual comparison), search & navigate across deep
memory with an event mark table, mask / pass-fail limit testing with
counters, waveform histograms, spectrogram view (Tek "Spectrum View"),
FFT peak table, Bode plot / frequency response analysis (Keysight, Rigol,
Siglent all bundle FRA — very reachable here since the sig gen is already
controllable), power analysis package (ripple, switching loss, harmonics).

### 2.5 Measurements

Missing measurement types: overshoot/preshoot, +width/−width, burst width,
phase & delay between channels, skew, amplitude/top/base, area, cycle RMS,
counter/totalizer.
Missing infrastructure: measurement statistics (min/max/mean/σ/count over
acquisitions), gating (measure only between cursors/zoom), selectable
thresholds (10/90 vs 20/80), DVM mode, hardware frequency counter readout.

### 2.6 Usability & system

Missing basics that every bench scope has: **autoscale/auto-setup**,
**probe attenuation factors (1×/10×/100×)**, channel labels & invert,
channel deskew, save/recall instrument setups, session persistence across
restarts, screenshot/waveform export from the GUI (CSV exists via CLI
only), undo. For remote control, a SCPI-compatible command layer would let
standard tooling (pyvisa, LabVIEW) drive the app alongside the custom CLI.

---

## 3. Prioritized wish list

### Tier 1 — "It's not an MSO without these" (high value, driver-supported)

1. **Digital logic thresholds** — per-port threshold setting (TTL, 3.3 V /
   1.8 V CMOS, user), exposed in UI + CLI.
2. **Digital triggering & pattern trigger** — edge on any D0–D15; combined
   analog+digital pattern conditions.
3. **Serial protocol decode: I2C, SPI, UART** — decode on analog *or*
   digital sources, overlay on lanes + decode table with export. (CAN/LIN
   as a follow-up.)
4. **Bus grouping** — group digital lanes into a named bus with hex/binary
   value track.
5. **Pulse-width trigger + holdoff** — hardware-native via
   `ps3000aSetPulseWidthQualifier`.
6. **Probe attenuation & channel labels** — table-stakes correctness fix:
   without 10× support, all voltage readouts are wrong with a standard probe.
7. **Autoscale** — one-command signal find; also valuable for the
   AI-driven CLI workflow.

### Tier 2 — Daily-driver features

8. **Zoom window** (main + zoomed split view, wheel-driven).
9. **Acquisition modes**: average, peak detect, high-resolution.
10. **Roll mode** for timebases ≥ ~100 ms/div.
11. **Reference waveforms** — save/recall up to 4 traces.
12. **Measurement statistics + gating** + expanded measurement set
    (overshoot, phase/delay, width, amplitude/top/base).
13. **Segmented memory / rapid block** capture with segment browser.
14. **Save/recall setups & session persistence** (JSON, matching the
    existing profile pattern).
15. **Persistence display** with intensity grading.
16. **Search & navigate** — find edges/pulses/patterns in deep memory,
    jump between marks.
17. **More trigger types**: runt, window, timeout, Nth edge.

### Tier 3 — Differentiators

18. **Bode plot / FRA** — drive the built-in sig gen through a frequency
    sweep, plot gain/phase. Few DIY scope apps have this; the hardware and
    sig gen control already exist.
19. **Mask / pass-fail testing** with counters and action-on-fail
    (stop/screenshot/CLI event) — pairs well with remote AI workflows.
20. **History mode** — ring buffer of recent acquisitions with playback.
21. **XY mode** and waveform histograms.
22. **Protocol trigger** (trigger on I2C address, UART byte, etc.).
23. **Spectrogram view** for the FFT path.
24. **VCD export** for digital captures (feeds GTKWave/sim tooling — strong
    fit for the FPGA verification workflow in `PICOSCOPE-CLI.md`).
25. **SCPI command subset** on a second port for pyvisa/LabVIEW interop.
26. **AWG arbitrary waveform upload** — the 3406D has an AWG; the app only
    exposes standard shapes. Add CSV upload + sweep mode.
27. **DVM + frequency counter widget** (Keysight bundles a 3-digit DVM and
    8-digit counter).

### Explicitly out of scope (hardware-limited)

- Bandwidths > 200 MHz, >1 GS/s real-time — fixed by the 3406D.
- 50 Ω input path (3000 series is 1 MΩ only).
- 8-bit ADC vs. the 12–14-bit HD scopes (Keysight HD3 is 14-bit,
  Rigol DHO900 12-bit) — can be *mitigated* in software with the
  high-resolution acquisition mode in Tier 2.
- Jitter/eye analysis — needs bandwidth/sample rate this hardware lacks.

---

## 4. Suggested next step

Tier 1 items 1–4 (digital thresholds, digital/pattern trigger, I2C/SPI/UART
decode, bus grouping) convert the app from "DSO with logic lanes" into an
actual MSO and are the highest leverage. Item 6 (probe attenuation) is a
small correctness fix worth doing first.

## Sources

- [Keysight InfiniiVision HD3 Series press kit](https://www.keysight.com/us/en/about/newsroom/press-kits/infiniivision-hd3-series.html)
- [Keysight InfiniiVision HD3 Series product page](https://www.keysight.com/zz/en/products/oscilloscopes/infiniivision-2-4-channel-digital-oscilloscopes/infiniivision-hd3-series-oscilloscopes.html)
- [Tektronix: Rigol DHO800/DHO900 vs 2 Series MSO comparison](https://www.tek.com/en/documents/competitive/rigol-dho800-and-dho900-versus-2-series-mso-comparison)
- [Rigol scope comparison tool](https://www.rigolna.com/ScopeCompare/)
- [Rigol DHO800/900/MHO900 comparison](https://www.batterfly.com/shop/en/blog-posts/rigol-dho800-dho900-mho900-comparison)
