# Implementation Plan

Companion to [FEATURE-WISHLIST.md](FEATURE-WISHLIST.md). Ordered so each
phase is independently shippable. Phase 0 (CLI parity) comes first because
it is a stated project goal: **the CLI must expose every GUI capability
except waveform display, including all wave data.**

## Architecture notes for the implementer (read first)

- **Single source of truth**: `ScopeState` (`src/core/ScopeState.h`). UI
  panels and the remote server both mutate it directly; `PicoSignalSource`
  / `DummySignalSource` read it in `configure()` each frame.
- **Remote commands run on the main thread.** `RemoteServer::handleClient`
  queues the command; `RemoteServer::processCommands(state, data, picoSource)`
  executes it in the frame loop (`src/main.cpp` ~line 330). New commands
  follow this pattern — never touch `ScopeState` from the socket thread.
- **Math/FFT results are frame-locals in `main.cpp`** (`mathBuffer`,
  `fftResult`) and are *not* visible to the remote server. Phase 0 changes
  the `processCommands` signature to pass them in (or bundles them into a
  `FrameResults` struct — preferred).
- **Hardware reconfig is change-detected** via
  `PicoSignalSource::ConfigSnapshot`. Every new `ScopeState` field that
  affects hardware must be added to the snapshot, `configChanged()`, and
  `snapshotConfig()`.
- **Demo mode is the test bench.** `DummySignalSource` must simulate every
  new feature (digital patterns, protocol traffic, trigger behavior) so
  everything is testable without hardware. CI-style verification is:
  build, launch app in demo mode, drive it with `picoscope-cli.exe`,
  assert on JSON output.
- **Protocol versioning**: bump `Version::CLI_PROTOCOL` minor
  (`src/core/Version.h`) for additive changes; update `cmdHelp()` and
  `src/remote/PICOSCOPE-CLI.md` in the same commit as each new command.
- Line protocol is one-command-per-connection JSON over TCP 5575; keep
  responses single-line JSON.

---

## Phase 0 — CLI parity & wave-data access ✅ DONE

Goal: CLI ≥ GUI (minus rendering). Closes the audit gaps below.

**Status:** Implemented and verified via `tests/cli_smoke.ps1` (25 checks, all
passing in demo mode). Delivered:
- `FrameResults` plumbing (`core/FrameResults.h`) — math/FFT results now reach
  the remote server; math is computed before `processCommands` in `main.cpp`.
- Capture of digital (`D`, `D0`–`D15`), `MATH`, `FFT`, and atomic
  multi-source lists (`A,B,D0`) — CSV and inline JSON.
- Device lifecycle: `list-devices`, `connect [--serial|--demo]`, `disconnect`.
  `connect`/`disconnect` also dismiss the startup device-select popup
  (via `RemoteServer::takeSourceSelected()` → `DeviceSelectPopup::hide()`),
  enabling fully headless CLI operation.
- New setters: `set-digital`, `set-math`, `set-cursor`, `get-cursors`.
- Parity fixes: `set-timebase --offset`, GND coupling, `--bwlimit on|off`,
  trigger `--mode single`; `get-state` expanded with device/digital/math/
  cursors/siggen/trigger-status/horizontal-offset.
- Sig-gen reporting shadow (`SigGenState` in `ScopeState`).
- `PICOSCOPE-CLI.md` updated (connect flow replaces "ask the user").

**Deferred from Phase 0** (belongs with later hardware work, not needed for
parity): probe attenuation, labels/invert (Phase 1). One caveat still open:
`PicoSignalSource::open()` runs on the main thread and can block a few hundred
ms during `connect`.

Original task breakdown, for reference:

### 0.1 Expose digital waveform data (largest gap)

`SignalData.digital` (per-sample `uint16_t` bitmask) is currently
unreachable via CLI.

- Extend `capture` to accept digital sources:
  `capture --ch D` (all 16) or `--ch D0-D7`.
  - JSON: `"data":[3,3,1,0,...]` (bitmask ints) + `"channels":["D0",...]`.
  - CSV: `time_s,d0,...,d15` columns.
- New in `get-state`: `"digital":{"enabled":[true,...]}`.
- New: `set-digital --ch <0-15|all> --enable|--disable`.

### 0.2 Atomic multi-channel capture

Two sequential `capture` calls can return different acquisitions while
running. Add multi-source capture in one command/response:
`capture --ch A,B,D --file out.csv` — one time column, one column per
source, all from the same `SignalData` snapshot. Inline JSON:
`"channels":{"A":[...],"B":[...],"D":[...]}`.

### 0.3 Expose math & FFT results

- Introduce `struct FrameResults { const AnalogBuffer* math; const FFTResult* fft; }`
  in `main.cpp`, pass to `processCommands`.
- New: `capture --ch MATH` (same JSON/CSV shape as analog).
- New: `capture --ch FFT` → `"bins":N,"bin_hz":Δf,"data_db":[...]`.
- New: `set-math --enable|--disable --op <add|sub|mul|div|fft|ddt|integ|sqrt>
  --src1 <A-D> --src2 <A-D> [--window <rect|hanning|hamming|blackman|flattop>]`.
- `get-state` gains a `"math":{...}` block.

### 0.4 Device lifecycle over CLI

Currently GUI-only (`DeviceSelectPopup`); `PICOSCOPE-CLI.md` tells the
agent to ask the user. Add:

- `list-devices` → `PicoSignalSource::enumerateDevices()` output.
- `connect [--serial <sn>]` → `picoSource.open()`, set
  `state.signalSource = PicoScope`. `connect --demo` switches to dummy.
- `disconnect`.
- `get-state` gains `"device":{"description":...,"serial":...}`.
- Caution: `open()` can block a few hundred ms — acceptable on the main
  thread for now; note it in the doc.

### 0.5 Small parity/correctness items

| Item | Fix |
|---|---|
| Horizontal offset not settable | `set-timebase --offset <s>` + report in `get-state` |
| `--bwlimit` toggles (non-idempotent) | change to `--bwlimit on\|off` (protocol minor bump; keep bare flag as toggle for one release, warn in doc) |
| GND coupling missing | accept `--coupling gnd` |
| Trigger `single` mode missing in `set-trigger` | accept `--mode single` |
| Cursors GUI-only | `set-cursor --x1/--x2/--y1/--y2 <div>`, `get-cursors` returning positions **and computed deltas** (Δt, 1/Δt, ΔV) |
| `get-state` omissions | add: trigger status, horizontal offset, digital block, math block, cursor block, siggen block, app run/stop of acquisition |
| Sig gen state not queryable | track last-applied siggen settings in a small struct (in `ScopeState` or `PicoSignalSource`) and report in `get-state` |
| `measure` on disabled channel errors | fine, but also add `--stat` later (Phase 4) |

MIDI configuration is *intentionally excluded* from CLI parity (it is a
local control-surface concern, not scope functionality). Note this in
`PICOSCOPE-CLI.md`.

### 0.6 Docs & tests

- Update `PICOSCOPE-CLI.md` (remove "ask the user to connect" workaround).
- Add `tests/cli_smoke.ps1`: launch app in demo mode headless-ish, run
  every command, assert `"status":"ok"` and shape of responses.

**Acceptance**: every `ScopeState` field readable and writable via CLI;
analog, digital, math, and FFT sample data all retrievable; two-channel
capture returns samples from the same acquisition.

---

## Phase 1 — Correctness & usability table stakes ✅ DONE

**Status:** Implemented and verified via `tests/cli_smoke.ps1` (34 checks total).
Delivered:
- **Probe attenuation / invert / label** on `ChannelState`, applied **inside the
  signal sources at data-production time** (`PicoSignalSource::retrieveData`,
  `DummySignalSource::acquire`) so rendering, measurements, math, and CLI
  capture all stay consistent. Exposed via `set-channel --probe|--invert|
  --label`, the `ChannelPanel` UI, the waveform legend, and `get-state`.
  *(Follow-up fix: the original implementation post-multiplied the persistent
  signal buffer in `main.cpp` every frame; on real hardware, frames where no
  new block was ready re-multiplied stale data — the waveform "gained up"
  wildly, most visibly while dragging the offset slider because offset was in
  `ConfigSnapshot` and forced block restarts. Fixed by moving scaling to the
  sources and removing `verticalOffset` from `ConfigSnapshot` — vertical
  offset is a pure display-layer Y translation and never touches hardware or
  sample data. An XY drag control was also added to the waveform display:
  drag X to pan time, grab a trace and drag Y to move its offset.)*
- **Autoscale** (`core/AutoScale.h`, header-only): `autoscale` CLI command +
  "Auto" button in the StatusBar. Single-pass over current data — fits vertical
  to ~6 div, timebase to ~3 periods, trigger to strongest channel midpoint.
- **Setup save/recall + session persistence** (`core/SetupIO.h`, header-only,
  nlohmann/json): `save-setup`/`recall-setup` commands; auto-save on graceful
  exit and restore on launch (`last_session.json`; `--fresh` to skip). Only
  configuration is serialized, never the live source or run state.

Notes for the implementer:
- New modules are **header-only** deliberately, to avoid a CMake source-list
  change (the memory records a MinGW/cmake reconfigure conflict on this box).
  If future work adds real `.cpp` files, reconfigure with
  `-DVCPKG_MANIFEST_INSTALL=OFF`.
- Probe scaling is software-only (applied at ADC→volts conversion in the
  sources); the ps3000a API has no probe concept. The hardware input range is
  scaled by the probe factor (`voltsPerDiv / probe`) to preserve full ADC
  resolution — hardware-validated.
- Session save runs only on graceful shutdown (WM_CLOSE), not on force-kill or
  crash. Consider a debounced periodic autosave if that matters.

Original task detail follows:

### 1.1 Probe attenuation (do first — readings are wrong with 10× probes)

- `ChannelState` gains `float probeAttenuation = 1.0f` (1/10/100).
- Apply as a scale factor when converting ADC→volts in
  `PicoSignalSource::retrieveData` and in `DummySignalSource`; all
  downstream (measurements, cursors, math, CLI capture) then inherit it.
- UI: combo in `ChannelPanel`; CLI: `set-channel --probe <1|10|100>`;
  include in `get-state`; add to `ConfigSnapshot`.

### 1.2 Channel labels & invert

- `ChannelState`: `std::string label`, `bool invert`.
- Invert = negate samples at retrieve time. Label shown in `ChannelPanel`,
  waveform legend, CLI state.

### 1.3 Autoscale

- New `autoscale` command + GUI button (StatusBar).
- Algorithm (runs over ~3 acquisitions on the main thread as a small state
  machine, not blocking): enable channels with detected signal → binary
  search V/div so Vpp spans 3–6 div → set timebase to show 2–4 periods of
  the fastest signal (use `Measurements::compute` frequency) → trigger at
  50% of Vpp on the strongest channel.

### 1.4 Setup save/recall & session persistence

- Serialize `ScopeState` ⇄ JSON (nlohmann already a dependency; mirror the
  `MidiSettings` pattern). `save-setup --file x.json` / `recall-setup`,
  GUI menu items, and auto-save `last_session.json` on exit / load on
  start (skip with `--fresh`).

---

## Phase 2 — Make it a real MSO  ✅ DONE

**Status:** All of 2.1–2.5 done and verified via `tests/cli_smoke.ps1` (53 checks
total). 2.2's hardware trigger path is the one piece not validated on-device
(see caveat below).

Delivered:
- **2.1 Digital thresholds** — per-group (`digitalThreshold[2]`) in volts,
  applied to hardware via `ps3000aSetDigitalPort` (added to `ConfigSnapshot`).
  CLI `set-digital --threshold <V> --group <0|1|all>` (named `--group` because
  the CLI client reserves `--port`), presets in the `ChannelPanel` digital tab,
  reported in `get-state`, persisted in setups.
- **2.4 Serial decode** (`decode/SerialDecode.h`, header-only) — UART, I2C, SPI
  decoders. `DummySignalSource` now emits real protocol traffic (UART "Hi" on
  D0, I2C write to 0x50 on D1/D2, SPI 0x3C/0xF0 on D3/D4/D5) so it's fully
  demo-testable. CLI `set-decode`/`get-decode` (JSON or CSV), `DecodeConfig` in
  `ScopeState`, a "Serial Decode" GUI table (inline in `main.cpp`), persisted in
  setups.
- **2.5 VCD export** (`core/VcdExport.h`, header-only) — `capture --ch D
  --file x.vcd` (or `--format vcd`) writes GTKWave-compatible VCD.

Fix made along the way: the CLI client flagged any response containing the
substring `"error"` as a failure (exit 1); it now checks for `"status":"error"`
specifically, so `get-decode` frames (which carry an `error` flag) exit 0.

- **2.3 Bus grouping** — `BusConfig` (up to 2 buses) in `ScopeState`; CLI
  `set-bus` + `capture --ch BUS0|BUS1` (returns value transitions); a value-box
  render track in `WaveformRenderer::drawDigitalChannels`; reported in
  `get-state`; persisted in setups.
- **2.2 Pattern/digital trigger** — `TriggerConfig` extended with `type`
  (edge/digital/pattern), `digitalSource`, `digitalPattern[16]`. CLI
  `set-trigger --type|--dsource|--pattern`; `TriggerPanel` type selector +
  pattern grid; software-trigger status evaluation in `main.cpp`
  (`evalDigitalTrigger`) so the trigger is observable in demo (`status` →
  `triggered`/`armed`); hardware path via `ps3000aSetTriggerDigitalPort
  Properties` + `ps3000aSetTriggerChannelConditionsV2` (edge path untouched).
  Persisted in setups; added to `ConfigSnapshot`.

⚠️ **Caveat — 2.2 digital/pattern hardware trigger unvalidated:** the
digital/pattern trigger compiles against the SDK headers and the
state/CLI/UI/demo-status are fully verified, but the `ps3000a` digital-trigger
calls have **not** run against real digital inputs. The analog **edge** trigger
path was hardware-validated end-to-end (normal mode, level accuracy
via v[trigger-sample] ≈ level, probe-tip→BNC conversion with 10× probe,
starvation outside the signal).

**Data/display architecture:** see `ARCHITECTURE.md`. `ChannelView`
(`render/ViewTransform.h`) is now the single volts→screen mapping (waveform,
trigger line, ground markers, cursors). Hardware-validated on the 3406D with
the siggen split into CH A/B: offset isolation (data + cross-channel), offset
churn amplitude stability, probe isolation, trigger BNC conversion. Fixes in
this pass: hardware trigger threshold now converts tip→BNC volts (was 10× off
with a 10× probe); `Measurements` frequency crossing detector gained hysteresis
(read 2.5 kHz for a clean 1 kHz hardware sine due to noise at the threshold);
sig-gen PICO_BUSY retry (stop in-flight block, reapply); `CursorOverlay` was
dead code — now wired into the waveform display with a `source` channel
(`set-cursor --source`, `y1_v/y2_v/dv_v` in `get-cursors`); `get-state.device`
gains `last_error`.

Notes: demo SPI/I2C are generated at 100 kHz so they resolve at typical demo
sample rates; UART at 9600 baud. Decode needs a timebase wide enough to contain
whole frames (≥ ~2 ms/div for a UART byte). Bus/decode/trigger all run against
the current capture on demand.

Original task detail follows:

### 2.1 Digital thresholds

- `ScopeState`: `float digitalThreshold[2]` (per port: D0-7, D8-15) with
  presets (TTL 1.5 V, 3.3 V CMOS 1.65 V, 1.8 V CMOS 0.9 V, user).
- `ps3000aSetDigitalPort(handle, port, enabled, logicLevel)` — logicLevel
  is ADC counts: `(int16_t)(threshold_V / 5.0 * 32767)`, clamp ±5 V.
- `DummySignalSource`: threshold affects simulated lanes (generate analog-ish
  digital sources internally so threshold visibly matters in demo mode).
- UI in `ChannelPanel` digital section; CLI `set-digital --threshold <V> --port <0|1>`.

### 2.2 Digital & pattern trigger

- Extend `TriggerConfig`: `source` becomes an enum-tagged union —
  analog ch, digital ch (0–15), or pattern.
- Digital edge: `ps3000aSetTriggerDigitalPortProperties` with
  `PS3000A_DIGITAL_CHANNEL_DIRECTIONS`.
- Pattern trigger: array of 20 conditions (4 analog above/below level +
  16 digital H/L/X), combined via
  `ps3000aSetTriggerChannelConditionsV2`.
- Dummy source: implement the same semantics in software so demo mode
  triggers identically.
- CLI: `set-trigger --source D3 --edge rising`,
  `set-trigger --pattern "D0=1,D1=0,A>1.5"`.
- UI: `TriggerPanel` grows a source type selector + pattern grid.

### 2.3 Bus grouping

- `ScopeState`: `struct BusConfig { std::string name; std::vector<int> lanes; enum Display {Hex,Bin,Dec} }`,
  up to 2 buses. Pure app-side feature (works in demo mode for free).
- Render a bus track in `WaveformRenderer` (value boxes between
  transitions). CLI: `set-bus --name SPI --lanes 0,1,2,3`,
  `capture --ch BUS0` → array of `{t, value}` transitions.

### 2.4 Serial protocol decode: I2C, SPI, UART

- New `src/decode/` module: `IProtocolDecoder` interface;
  input = edge-list extracted from either a `DigitalBuffer` lane or an
  analog channel + threshold; output = `std::vector<DecodedFrame>`
  (`t_start, t_end, type, value, flags` e.g. ACK/NAK, parity error).
- Decoders: UART (baud, data bits, parity, polarity), I2C (SDA/SCL,
  address+R/W+data+ACK), SPI (CS/CLK/MOSI/MISO, CPOL/CPHA, word size).
- UI: decode config panel + frame overlay on waveform + scrollable decode
  table panel.
- CLI: `set-decode --protocol i2c --sda D0 --scl D1`,
  `get-decode [--file frames.csv]` → frame list. This makes the AI/CLI
  workflow dramatically stronger (assert on decoded bytes, not waveforms).
- `DummySignalSource`: generate valid I2C/SPI/UART traffic in demo mode —
  this is also the decoder unit-test vector source.
- Suggested implementation order: UART → SPI → I2C (rising complexity).

### 2.5 VCD export

- `capture --ch D --file out.vcd --format vcd` — straightforward writer
  over `DigitalBuffer`; feeds GTKWave and matches the FPGA workflow in
  `PICOSCOPE-CLI.md`.

---

## Phase 3 — Trigger & acquisition depth

1. **Pulse-width trigger + holdoff** — `ps3000aSetPulseWidthQualifier`;
   holdoff via `ps3000aSetTriggerDelay`/auto-trigger settings; extend
   `TriggerConfig` with `type`, `width`, `widthCondition`, `holdoff`.
   Implement same semantics in `DummySignalSource`.
2. **Acquisition modes** — average (N of 2..256), peak detect, high-res:
   app-side post-processing in a new `AcquisitionProcessor`
   (`src/core/`), applied between `acquire()` and rendering; ratio modes
   map to `ps3000a` `PS3000A_RATIO_MODE_AVERAGE`/`AGGREGATE` where
   possible. CLI: `set-acquisition --mode <normal|average|peak|highres> [--count N]`.
3. **Zoom window** — view-only: a second `WaveformDisplay` viewport with
   its own time window into the same buffers.
4. **Reference waveforms** — `save-ref --slot 1 --ch A`, render dashed;
   store as JSON/binary next to setups.
5. **Roll mode** — for time/div ≥ 100 ms switch `PicoSignalSource` to
   `ps3000aRunStreaming` path (new code path; the current block-mode state
   machine stays for fast timebases).

## Acquisition modes — live vs deep vs recording

Real-scope behavior question ("why does deep memory slow the display?")
resolved by splitting concerns:
- **Auto/live mode (default)** — per-sweep record sized for display (≤1 M
  points); waveform refresh depends on time/div + USB transfer only, like a
  bench scope. `set-record-length --auto`.
- **Fixed/deep mode** — exact per-sweep record up to 512 M for deep one-shot
  work (chunked retrieval keeps the UI responsive; update rate honestly
  drops). `set-record-length --value N`.
- **Recording mode ✅** — gapless streaming to disk
  (`ps3000aRunStreaming`, autoStop=0): `start-recording`/`stop-recording`/
  `recording-status`; int16 interleaved binary + JSON sidecar; rolling live
  view during recording (roll-mode display for free); block mode suspended
  and auto-resumed. Hardware-validated: 2 ch @ 1 MS/s for 9.37 s, byte-exact
  file size, 0 overflows, clean 1 kHz sine verified 5 M samples into the
  file. Digital ports not recorded in v1.

## Phase 4 — Analysis extras (summary)

Measurement statistics & gating; segmented memory via rapid block; Bode
plot (drive existing siggen through log sweep, measure gain/phase per
step — mostly composition of existing pieces); persistence rendering;
mask testing; history buffer. Detail these when Phase 0–3 land.

---

## Suggested commit/PR slicing for Opus

Each numbered item above is one PR-sized unit. Within Phase 0:
PR1 = FrameResults plumbing + digital/math/FFT capture (0.1–0.3),
PR2 = device lifecycle (0.4), PR3 = parity fixes + get-state expansion
(0.5), PR4 = docs + smoke test (0.6). Bump `CLI_PROTOCOL` once per phase,
not per PR.
