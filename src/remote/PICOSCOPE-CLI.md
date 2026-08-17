# picoscope-cli — Remote Control for RemotePicoScope

This document describes `picoscope-cli.exe`, a command-line tool that gives you programmatic control over a real PicoScope 3406D MSO oscilloscope. The oscilloscope is connected to the PC and controlled through a GUI application (RemotePicoScope) that exposes a TCP command interface on `localhost:5575`. You use `picoscope-cli.exe` to send commands and receive JSON responses.

**The human user is watching the live waveform on the GUI simultaneously.** You control the instrument; they observe the display. This is a collaborative workflow.

## Architecture and Responsibilities

The oscilloscope system has two parts with distinct roles:

- **GUI app (RemotePicoScope):** Runs the TCP server on `localhost:5575`, owns the hardware, and displays the live waveform. The human user launches it.
- **CLI tool (picoscope-cli.exe):** A remote control with full access to instrument settings and data. It can enumerate devices, connect/disconnect hardware (or select the demo source), adjust all settings, and read analog, digital, math, and FFT waveform data.

When you change a setting via CLI, the GUI applies it to the hardware on the next display frame (~16ms) — the same as if the user turned a knob. The PicoScope 3000D series has internal relays for input protection and gain switching; each setting change triggers exactly one relay reconfiguration, then stabilizes. There is no repeated reinitialization.

**Verify the source before sending commands.** Call `get-state` first and check `signal_source`. If it is `"dummy"`, either the demo source is active or no hardware is connected — use `list-devices` and `connect` to attach a real PicoScope, or `connect --demo` to work with simulated signals. `connect` also dismisses the GUI's startup source-selection popup, so the instrument can be driven entirely from the CLI (headless).

## Setup

The tool should be placed in your project under `tools/picoscope/`:
```
tools/picoscope/
    picoscope-cli.exe
    PICOSCOPE-CLI.md    (this file)
```

**Prerequisites:** The RemotePicoScope GUI app must be running on the same machine with a PicoScope connected. The human user is responsible for launching the app and selecting the device. The TCP server starts automatically on port 5575.

## How to Call

```bash
tools/picoscope/picoscope-cli.exe <command> [args...]
```

All responses are single-line JSON. Successful commands return `{"status":"ok", ...}`. Failures return `{"status":"error","message":"..."}`. The exit code is 0 on success, 1 on error.

If the connection is refused, the GUI app is not running.

## Commands

### Get oscilloscope state

```bash
picoscope-cli get-state
```

Returns the full instrument configuration and signal metadata:
```json
{
  "status": "ok",
  "run_mode": "run",
  "signal_source": "picoscope",
  "device": {"connected": true, "description": "PicoScope 3406D MSO", "serial": "GX123/456"},
  "time_per_div": 0.001,
  "horizontal_offset": 0,
  "sample_rate": 62500000,
  "record_length": 10000,
  "channels": [
    {"name": "A", "enabled": true, "volts_per_div": 1.0, "coupling": "DC", "offset": 0,
     "bw_limit": false, "probe": 1, "invert": false, "label": ""},
    ...
  ],
  "digital": {"enabled": [true, true, false, ...], "threshold": [1.5, 1.5]},
  "trigger": {"type": "edge", "source": "A", "level": 0, "edge": "rising", "mode": "auto",
              "status": "auto", "digital_source": "D0", "pattern": ""},
  "math": {"enabled": false, "op": "+", "src1": "A", "src2": "B", "window": "Hanning", "has_data": false},
  "cursors": {"enabled": false, "x1": -2, "x2": 2, "y1": -1, "y2": 1},
  "siggen": {"enabled": false, "wave": "sine", "frequency": 1000, "amplitude_mv": 2000, "offset_mv": 0},
  "decode": {"enabled": false, "protocol": "uart"},
  "buses": [{"index": 0, "enabled": false, "name": "", "display": "hex", "lanes": []}, ...]
}
```

Key fields:
- `run_mode`: `"run"` (continuous), `"stop"` (halted), `"single"` (one-shot, auto-stops)
- `signal_source`: `"picoscope"` (real hardware) or `"dummy"` (simulated)
- `device`: connection state; `description`/`serial` present only when connected; `last_error` appears if the driver reported a problem (RunBlock/trigger/siggen failures)
- `time_per_div` / `horizontal_offset`: timebase (8 divisions) and horizontal pan, in seconds
- `sample_rate`: actual samples/second from the ADC
- `channels[].coupling`: `"DC"`, `"AC"`, or `"GND"`
- `digital.enabled`: 16-element array, one flag per digital lane D0–D15
- `trigger.status`: current acquisition state (`ready`/`armed`/`triggered`/`stopped`/`auto`)
- `math.has_data`: whether a math/FFT result is available to `capture`

### Device lifecycle

```bash
picoscope-cli list-devices            # Enumerate connected PicoScopes
picoscope-cli connect                 # Open the first available device
picoscope-cli connect --serial GX123/456   # Open a specific device
picoscope-cli connect --demo          # Use the built-in simulated source
picoscope-cli disconnect              # Close device, revert to demo
```

`connect` dismisses the GUI startup popup and switches `signal_source`. `list-devices` returns `{"devices":[{"serial":...,"description":...}],"count":N}`.

### Autoscale

```bash
picoscope-cli autoscale
# Returns: {"status":"ok","channels_detected":2,"trigger_source":"A","time_per_div":2.5e-07}
```

Analyzes the current acquisition and adjusts vertical scale/offset (fits each
signal to ~6 divisions, centered), timebase (~3 periods of the strongest
channel), and trigger (strongest channel, level at its midpoint). Requires
acquisition to be running with a live signal; returns an error if nothing is
detected.

### Save / recall setup

```bash
picoscope-cli save-setup --file C:/tmp/setup.json
picoscope-cli recall-setup --file C:/tmp/setup.json
```

Saves or restores the full instrument configuration (channels incl.
probe/invert/label, digital enables, timebase, trigger, math, cursors, record
length) as JSON. Runtime state (run/stop, active hardware source) is **not**
included, so recalling a setup never switches the signal source or starts/stops
acquisition. The app also auto-saves the session on graceful exit and restores
it on next launch (`last_session.json` next to the executable); launch with
`--fresh` to skip restoring.

### Configure channels

```bash
# Enable channel A with 500 mV/div range, DC coupling
picoscope-cli set-channel --ch A --enable --range 0.5 --coupling DC

# Disable channel C
picoscope-cli set-channel --ch C --disable

# Set vertical offset, ground coupling, bandwidth limit
picoscope-cli set-channel --ch A --offset 1.5
picoscope-cli set-channel --ch B --coupling GND
picoscope-cli set-channel --ch A --bwlimit on

# Probe attenuation, invert, and a custom label
picoscope-cli set-channel --ch A --probe 10 --invert on --label CLK
```

Channels are `A`, `B`, `C`, `D`. The `--range` value is volts/div and snaps to the nearest standard 1-2-5 value (10mV to 50V). `--coupling` accepts `DC`, `AC`, or `GND`. `--bwlimit` accepts `on`/`off` (a bare `--bwlimit` toggles, but prefer the explicit form for scripting).

- `--probe` sets probe attenuation: `1`, `10`, or `100`. **This scales all voltage readings** — with a 10× probe you must set `--probe 10` or every measurement/capture is off by 10×.
- `--invert on|off` negates the trace.
- `--label <text>` sets a display label (shown on the waveform legend and in `get-state`).

All three are reported per channel in `get-state` as `probe`, `invert`, `label`.

### Digital channels

```bash
picoscope-cli set-digital --ch all --enable    # Enable all 16 lanes
picoscope-cli set-digital --ch 3 --disable     # Disable lane D3

# Logic threshold per group (0 = D0-D7, 1 = D8-D15). --group avoids the
# client's reserved --port flag.
picoscope-cli set-digital --threshold 1.65 --group all   # 3.3V CMOS
picoscope-cli set-digital --threshold 1.5 --group 0      # TTL on D0-D7
```

Digital lanes are `0`–`15` (D0–D15) or `all`. Enable state and per-group thresholds are reported under `digital` in `get-state`. Capture digital data with `capture --ch D` (see below), or export to VCD (see Serial decode).

### Set timebase

```bash
# Set to 1 ms/div
picoscope-cli set-timebase --value 0.001

# Set to 10 us/div
picoscope-cli set-timebase --value 0.00001

# Pan the view away from the trigger (horizontal offset, in seconds)
picoscope-cli set-timebase --offset 0.0005
```

The value is seconds/div and snaps to the nearest 1-2-5 value. `--value` and
`--offset` may be set together or independently. Each sweep captures ~10
display windows (trigger at the center, capped at 10 s total), so the offset
can pan roughly ±4.5 windows through real captured data; it is clamped to the
captured span. Panning is a pure view operation — it never changes the data
returned by `capture`/`measure`.

### Set trigger

```bash
# Trigger on channel A, rising edge, at 1.5V
picoscope-cli set-trigger --source A --level 1.5 --edge rising

# Trigger on falling edge with normal mode (waits for trigger, doesn't auto-fire)
picoscope-cli set-trigger --source B --level 0 --edge falling --mode normal
```

`--mode` accepts `auto` (free-run if no trigger), `normal` (wait for trigger), or `single` (one shot).

Trigger types (`--type`):

```bash
# Analog edge (default)
picoscope-cli set-trigger --type edge --source A --level 1.6 --edge rising

# Digital edge on a single lane
picoscope-cli set-trigger --type digital --dsource 3 --edge falling

# Digital pattern: trigger when all listed lanes match (1=high, 0=low, others=don't care)
picoscope-cli set-trigger --type pattern --pattern "D0=1,D3=0,D5=1"
picoscope-cli set-trigger --type pattern --pattern clear      # reset to all don't-care
```

`get-state.trigger` reports `type`, `digital_source`, and `pattern`. In demo mode
(and as a software-trigger status on any source) `trigger.status` becomes
`triggered` when the condition is found in the current capture, else `armed`.
Digital and pattern triggers use the per-group logic thresholds. **Note:** the
hardware digital/pattern trigger path is built against the PicoScope SDK but has
not been validated on a physical 3406D — verify on-device before relying on it.

### Math channel and cursors

```bash
# A + B time-domain math
picoscope-cli set-math --enable --op add --src1 A --src2 B

# FFT of channel A with a flat-top window
picoscope-cli set-math --enable --op fft --src1 A --window flattop

# Cursors: enable and position (values are divisions from center)
picoscope-cli set-cursor --enable --x1 -1.5 --x2 2.5 --source B
picoscope-cli get-cursors
# Returns positions plus dx_div, dy_div, dt_s, freq_hz (1/dt),
# and voltage readouts resolved through the --source channel:
# y1_v, y2_v (absolute, offset-aware) and dv_v (offset-independent)
```

Cursors are display-plane objects (divisions). `--source` selects which
channel's V/div and display offset the voltage readouts use — matching how
that channel's trace is drawn. Waveform data from `capture`/`measure` is never
affected by cursors or display offsets.

Math `--op`: `add`, `sub`, `mul`, `div`, `fft`, `ddt`, `integ`, `sqrt`. FFT `--window`: `rect`, `hanning`, `hamming`, `blackman`, `flattop`. Retrieve the computed math/FFT waveform with `capture --ch MATH` or `capture --ch FFT`.

### Acquisition control

```bash
picoscope-cli run       # Start continuous acquisition
picoscope-cli stop      # Stop acquisition
picoscope-cli single    # Acquire one shot, then auto-stop
```

### Take measurements

```bash
# All measurements on channel A
picoscope-cli measure --ch A
# Returns: frequency, period, vpp, vavg, vrms, vmax, vmin, rise_time, fall_time, duty_cycle

# Specific measurement
picoscope-cli measure --ch A --type frequency
# Returns: {"status":"ok","value":1000.0,"unit":"Hz"}

picoscope-cli measure --ch B --type vpp
# Returns: {"status":"ok","value":3.3,"unit":"V"}
```

Available `--type` values: `frequency`, `period`, `vpp`, `vrms`, `vavg`, `vmax`, `vmin`, `risetime`, `falltime`, `duty`, `all` (default).

### Set record length

```bash
# Auto (default): the sweep is sized for responsive live display; the
# refresh rate follows time/div (like a real scope), never a memory setting.
picoscope-cli set-record-length --auto

# Fixed: exact per-sweep record length (deep captures; update rate degrades)
picoscope-cli set-record-length --value 1000000

# Device maximum (512 million samples, shared)
picoscope-cli set-record-length --value 512000000
# Returns: {"status":"ok","record_mode":"fixed","record_length":512000000,
#           "bytes_estimate":...,"host_free_bytes":...}
```

`get-state` reports `record_mode` (`auto`/`fixed`) and `record_length` (the
**effective** per-sweep count — in auto mode it varies with time/div, capped
at ~300 k points over the 3-window live span). In auto mode the scope runs
rapid-block batches: each sweep is a distinct hardware trigger, animated one
per display frame. Use fixed mode when the CLI needs an exact deep record;
use auto for interactive work.

The record length controls how many samples per acquisition. Range: 1,000 to
512,000,000 (the 3406D's full capture memory, **shared across enabled
channels and digital ports** — the driver clamps the real count per timebase
and channel configuration). The response includes `bytes_estimate` — the host
RAM the app will need for buffers at this length — and `host_free_bytes`; a
`warning` field appears if the estimate exceeds free memory. `get-state`
reports both `record_length` and `record_bytes_estimate`. The GUI shows the
same estimate next to the Rec preset selector (hover for a breakdown).

Notes:
- Long records at slow timebases take correspondingly long to acquire (one
  block must fill before data updates) and transfer over USB.
- In **demo mode** the record length is capped at 2 M samples (the simulated
  source regenerates every sample in software each frame).

### Capture waveform data

The `--ch` argument accepts a single source or a comma-separated list. Sources:
`A`–`D` (analog), `D` (all 16 digital lanes as a bitmask), `D0`–`D15` (a single
lane, 0/1), `MATH` (time-domain math output), and `FFT` (spectrum).

```bash
# Single analog channel — up to 10000 samples inline
picoscope-cli capture --ch A
# Returns: {"status":"ok","samples":10000,"sample_rate":62500000,"source":"A","data":[0.012,...]}

# Limit sample count
picoscope-cli capture --ch A --samples 500

# All 16 digital lanes as per-sample bitmask integers (bit i = Di)
picoscope-cli capture --ch D
# Returns: {..."source":"D","data":[25621,25620,...]}

# A single digital lane as 0/1
picoscope-cli capture --ch D0 --samples 8
# Returns: {..."source":"D0","data":[1,0,0,0,...]}

# Math or FFT result (enable the math channel first)
picoscope-cli capture --ch MATH
picoscope-cli capture --ch FFT --samples 512
# FFT returns: {"status":"ok","bins":512,"bin_hz":122.07,"max_frequency":500000,"data_db":[...]}

# Multiple sources from ONE acquisition (atomic — same time base)
picoscope-cli capture --ch A,B,D0 --samples 1000
# Returns: {..."channels":{"A":[...],"B":[...],"D0":[...]}}

# Save to CSV (no inline sample cap)
picoscope-cli capture --ch A,B,D --file C:/tmp/waveform.csv
```

Response shapes:
- **Single source:** `"source":"<label>","data":[...]` (backwards compatible with the original analog-only form).
- **Multiple sources:** `"channels":{"<label>":[...],...}` — all columns come from the same acquisition, so they are time-aligned. Prefer this over separate `capture` calls when comparing signals.
- **FFT:** frequency-domain, so it cannot be combined with time-domain sources. Returns `bins`, `bin_hz` (Hz per bin), `max_frequency`, and `data_db` (dBVrms). `--samples` caps the number of bins.

CSV format: `time_s` followed by one column per source (analog/math as volts, `D` as a bitmask integer, `Dn` as 0/1). FFT CSV is `frequency_hz,magnitude_db`. Inline JSON is capped at 10,000 points; use `--file` for the full record (up to 50M samples). `MATH`/`FFT` require the math channel enabled with a matching op, and digital sources require digital data to be present.

### Serial protocol decode

Decode I2C, SPI, or UART from the digital lanes. Configure the protocol and lane
assignments with `set-decode`, then read frames with `get-decode`.

```bash
# UART on D0 at 9600 baud (8N1)
picoscope-cli set-decode --protocol uart --lane 0 --baud 9600 --enable
picoscope-cli get-decode
# {"status":"ok","protocol":"uart","frames":2,"data":[
#   {"t_start":2.1e-4,"t_end":1.25e-3,"text":"0x48","error":false}, ...]}

# I2C on SCL=D1, SDA=D2
picoscope-cli set-decode --protocol i2c --scl 1 --sda 2
picoscope-cli get-decode
# frames like "START", "ADDR 0x50 W ACK", "DATA 0xA5 ACK", "STOP"

# SPI on CLK=D3, MOSI=D4, CS=D5, mode 0
picoscope-cli set-decode --protocol spi --clk 3 --mosi 4 --cs 5 --cpol 0 --cpha 0
picoscope-cli get-decode --file frames.csv   # CSV: t_start_s,t_end_s,text,error
```

`get-decode` runs the decoder on the **current** capture, so ensure acquisition is
running and the timebase is wide enough to contain complete frames (e.g. a full
UART byte at 9600 baud spans ~1 ms). Each frame has `t_start`, `t_end`, `text`,
and an `error` flag (framing error, NAK, etc.). In **demo mode** the simulated
digital lanes carry live traffic — UART "Hi" on D0, an I2C write to 0x50 on
D1/D2, and SPI bytes 0x3C/0xF0 on D3/D4/D5 — so decode works without hardware.
The active protocol is also reported under `decode` in `get-state`, and a "Serial
Decode" table appears in the GUI when decode is enabled.

### VCD export (digital)

```bash
picoscope-cli capture --ch D --file capture.vcd
picoscope-cli capture --ch D --file capture.vcd --format vcd --samples 100000
```

Writes the enabled digital lanes as a Value Change Dump, loadable in GTKWave and
HDL simulators. Triggered by a `.vcd` file extension or `--format vcd`.

### Recording (gapless streaming to disk)

Continuous capture limited only by USB throughput and disk space — hours at
moderate rates. Uses the driver's streaming mode; block acquisition (and the
normal trigger) is suspended while recording, and a rolling window of the
live stream feeds the display.

```bash
# Record channels A and B at 1 MS/s
picoscope-cli start-recording --file C:/data/run1.bin --rate 1000000 --ch A,B
# {"status":"ok","file":"C:/data/run1.bin","actual_rate":1e+06}

picoscope-cli recording-status
# {"status":"ok","active":true,"samples_per_channel":4096000,"seconds":4.096,
#  "bytes":16384064,"actual_rate":1e+06,"overflow_events":0}

picoscope-cli stop-recording
# {"status":"ok","samples_per_channel":9373540,"seconds":9.37,"bytes":37494224,
#  "overflow_events":0,"sidecar":".../run1.bin.json"}
```

File format: 64-byte header (`PSRECv1`, channel count/mask, actual rate,
volts-per-LSB scale per channel, sample count) followed by interleaved int16
ADC frames; multiply by `scale` for probe-tip volts. A `.json` sidecar with
the same metadata is written on stop. `--ch` defaults to all enabled
channels. Disk math: bytes/s = rate × 2 × channels (1 MS/s × 2ch ≈ 14 GB/h).
`overflow_events` > 0 means the host couldn't keep up — lower the rate.
Requires real hardware; digital ports are not recorded (v1). While recording,
`connect`/`disconnect` are refused and settings changes don't reach the
hardware until `stop-recording`.

### Digital buses

Group digital lanes into a named multi-bit bus (up to 2). The bus renders as a
value track in the GUI and can be captured as value transitions.

```bash
picoscope-cli set-bus --index 0 --name ADDR --lanes "0,1,2,3" --display hex --enable
picoscope-cli capture --ch BUS0
# {"status":"ok","bus":"ADDR","display":"hex","transitions":3,
#  "data":[{"t":0,"value":"0x1"},{"t":0.005,"value":"0x0"}, ...]}
```

`--lanes` is a comma-separated, LSB-first lane list. `--display` is `hex`, `bin`,
or `dec`. `capture --ch BUS0|BUS1` returns only the transitions (value changes),
which is compact for long records. Buses appear under `buses` in `get-state`.

### Signal generator (requires real PicoScope)

The PicoScope 3406D has a built-in signal generator output (BNC on rear panel). Use it to generate test signals.

```bash
# 1 kHz sine wave, 2V peak-to-peak
picoscope-cli siggen --wave sine --freq 1000 --amplitude 2000

# 10 kHz square wave, 3.3V amplitude, 1.65V offset (0 to 3.3V)
picoscope-cli siggen --wave square --freq 10000 --amplitude 3300 --offset 1650

# DC level at 1.8V
picoscope-cli siggen --wave dc --freq 0 --amplitude 0 --offset 1800

# Turn off signal generator
picoscope-cli siggen --off
```

Waveform types: `sine`, `square`, `triangle`, `rampup`, `rampdown`, `sinc`, `gaussian`, `halfsine`, `dc`. Amplitude and offset are in **millivolts**.

## Recommended Workflows

### Verifying FPGA output

**Always start by checking/attaching the source.** The GUI must be running.

```bash
# 1. Verify the source, connecting hardware if needed
picoscope-cli get-state
# Check: "signal_source":"picoscope" and "run_mode":"run"
# If signal_source is "dummy" and you need real hardware:
picoscope-cli list-devices
picoscope-cli connect           # or: connect --serial <SN>

# 2. Configure for your signal: 3.3V logic, ~1 MHz expected clock
picoscope-cli set-channel --ch A --enable --range 1.0 --coupling DC
picoscope-cli set-timebase --value 0.0000005    # 500 ns/div
picoscope-cli set-trigger --source A --level 1.6 --edge rising

# 3. Measure (acquisition should already be running)
picoscope-cli measure --ch A --type frequency
picoscope-cli measure --ch A --type vpp

# 4. Capture data for analysis
picoscope-cli capture --ch A --samples 2000
```

### Checking a clock signal

```bash
picoscope-cli set-channel --ch A --enable --range 1.0
picoscope-cli set-timebase --value 0.00000002   # 20 ns/div for 50 MHz clock
picoscope-cli set-trigger --source A --level 1.6 --edge rising
picoscope-cli run
picoscope-cli measure --ch A --type frequency    # Should read ~50 MHz
picoscope-cli measure --ch A --type duty          # Should read ~50%
```

### Using signal generator as stimulus

```bash
# Generate a 100 kHz test clock from PicoScope's sig gen output
picoscope-cli siggen --wave square --freq 100000 --amplitude 3300 --offset 1650

# Monitor FPGA's response on channel A
picoscope-cli set-channel --ch A --enable --range 1.0
picoscope-cli set-timebase --value 0.000005   # 5 us/div
picoscope-cli run
picoscope-cli measure --ch A --type frequency
```

### Comparing two signals

```bash
picoscope-cli set-channel --ch A --enable --range 1.0   # Input
picoscope-cli set-channel --ch B --enable --range 1.0   # Output
picoscope-cli set-trigger --source A --level 1.6 --edge rising
picoscope-cli set-timebase --value 0.0000001   # 100 ns/div
picoscope-cli run

# Measure both
picoscope-cli measure --ch A --type frequency
picoscope-cli measure --ch B --type frequency
picoscope-cli measure --ch A --type vpp
picoscope-cli measure --ch B --type vpp
```

## Choosing Timebase and Range

**Timebase (seconds/div):** The display shows 8 divisions horizontally. To see N full cycles of a signal at frequency F:
- `time_per_div = N / (8 * F)`
- Example: 2 cycles of 1 MHz = `2 / (8 * 1e6)` = 250 ns/div = `0.00000025`

**Range (volts/div):** The display shows 8 divisions vertically (4 above and 4 below center). To fit a signal with amplitude V:
- `volts_per_div >= V / 4` (for centered signal)
- Use a value from the 1-2-5 sequence: 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50

**Trigger level:** Set to approximately the midpoint of your signal. For 3.3V logic: `--level 1.6`. For 1.8V logic: `--level 0.9`.

## Error Handling

- Connection refused: GUI app is not running. Ask the user to launch it.
- `"channel X not enabled"`: Enable the channel with `set-channel --enable` before measuring/capturing.
- `"no data captured"` / `"no digital data captured"`: Acquisition hasn't run yet. Send `run` or `single` first, then retry after a brief pause (~16ms).
- `"math channel has no data (enable math first)"`: Run `set-math --enable --op ...` and wait a frame before `capture --ch MATH`.
- `"no FFT data ..."`: Set the math op to `fft` (`set-math --enable --op fft`) before `capture --ch FFT`.
- `"no PicoScope connected"`: Signal generator requires real hardware. Use `connect` to attach a device (`connect --demo` won't provide a physical sig-gen output).
- `"measurement failed - no signal data"`: The buffer is empty. Start acquisition and wait a moment.

## Important Notes

- Commands execute on the GUI's main thread at the display frame rate (~60 Hz). After sending `run`, wait at least one frame (~20ms) before capturing or measuring.
- The `single` command acquires one waveform and auto-stops. Good for capturing a stable snapshot.
- Inline capture data is limited to 10,000 samples. For longer records, use `--file` to write CSV.
- Use `set-record-length` to increase the acquisition depth up to 50M samples before capturing with `--file`.
- The signal generator output is a physical BNC connector. It must be wired to whatever you're testing. It does not feed back into the input channels automatically.
- All four channels (A-D) are enabled by default. Disabling unused channels can increase the maximum sample rate.
- MIDI control-surface configuration is intentionally **not** exposed over the CLI. It is a local input-mapping concern (mapping physical knobs to parameters), not instrument functionality, and is configured in the GUI settings panel.
