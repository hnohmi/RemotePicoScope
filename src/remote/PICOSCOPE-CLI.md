# picoscope-cli — Remote Control for RemotePicoScope

This document describes `picoscope-cli.exe`, a command-line tool that gives you programmatic control over a real PicoScope 3406D MSO oscilloscope. The oscilloscope is connected to the PC and controlled through a GUI application (RemotePicoScope) that exposes a TCP command interface on `localhost:5575`. You use `picoscope-cli.exe` to send commands and receive JSON responses.

**The human user is watching the live waveform on the GUI simultaneously.** You control the instrument; they observe the display. This is a collaborative workflow.

## Architecture and Responsibilities

The oscilloscope system has two parts with distinct roles:

- **GUI app (RemotePicoScope):** Owns the hardware connection. The human user launches it, selects the PicoScope device, and verifies the connection is working. The GUI displays the live waveform and runs the TCP server on `localhost:5575`.
- **CLI tool (picoscope-cli.exe):** A remote control that modifies oscilloscope settings and reads data. It does **not** open, close, or reinitialize the hardware connection. It only adjusts settings (channel range, timebase, trigger, etc.) and reads measurement/capture data.

When you change a setting via CLI, the GUI applies it to the hardware on the next display frame (~16ms) — the same as if the user turned a knob. The PicoScope 3000D series has internal relays for input protection and gain switching; each setting change triggers exactly one relay reconfiguration, then stabilizes. There is no repeated reinitialization.

**You must always verify the scope is connected before sending commands.** Call `get-state` first and check that `signal_source` is `"picoscope"` and `run_mode` is `"run"`. If `signal_source` is `"dummy"`, the human has not connected the real hardware yet — ask them to do so.

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
  "time_per_div": 0.001,
  "sample_rate": 62500000,
  "record_length": 10000,
  "channels": [
    {"name": "A", "enabled": true, "volts_per_div": 1.0, "coupling": "DC", "offset": 0, "bw_limit": false},
    {"name": "B", "enabled": true, "volts_per_div": 1.0, "coupling": "DC", "offset": 0, "bw_limit": false},
    {"name": "C", "enabled": true, "volts_per_div": 1.0, "coupling": "DC", "offset": 0, "bw_limit": false},
    {"name": "D", "enabled": true, "volts_per_div": 1.0, "coupling": "DC", "offset": 0, "bw_limit": false}
  ],
  "trigger": {"source": "A", "level": 0, "edge": "rising", "mode": "auto"}
}
```

Key fields:
- `run_mode`: `"run"` (continuous), `"stop"` (halted), `"single"` (one-shot, auto-stops)
- `signal_source`: `"picoscope"` (real hardware) or `"dummy"` (simulated)
- `time_per_div`: horizontal timebase in seconds per division (8 divisions total)
- `sample_rate`: actual samples/second from the ADC
- `channels[].volts_per_div`: vertical scale in volts per division (8 divisions total)
- `channels[].coupling`: `"DC"` or `"AC"`

### Configure channels

```bash
# Enable channel A with 500 mV/div range, DC coupling
picoscope-cli set-channel --ch A --enable --range 0.5 --coupling DC

# Disable channel C
picoscope-cli set-channel --ch C --disable

# Set vertical offset
picoscope-cli set-channel --ch A --offset 1.5
```

Channels are `A`, `B`, `C`, `D`. The `--range` value is volts/div and snaps to the nearest standard 1-2-5 value (10mV to 50V).

### Set timebase

```bash
# Set to 1 ms/div
picoscope-cli set-timebase --value 0.001

# Set to 10 us/div
picoscope-cli set-timebase --value 0.00001
```

The value is seconds/div and snaps to the nearest 1-2-5 value. Total display width is 8 divisions.

### Set trigger

```bash
# Trigger on channel A, rising edge, at 1.5V
picoscope-cli set-trigger --source A --level 1.5 --edge rising

# Trigger on falling edge with normal mode (waits for trigger, doesn't auto-fire)
picoscope-cli set-trigger --source B --level 0 --edge falling --mode normal
```

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

### Capture waveform data

```bash
# Get up to 10000 samples as JSON array
picoscope-cli capture --ch A
# Returns: {"status":"ok","samples":10000,"sample_rate":62500000,"data":[0.012,-0.003,...]}

# Limit sample count
picoscope-cli capture --ch A --samples 500

# Save to CSV file
picoscope-cli capture --ch A --file C:/tmp/waveform.csv
# Returns: {"status":"ok","file":"C:/tmp/waveform.csv","samples":10000,"sample_rate":62500000}
```

CSV format: `time_s,voltage_v` with one row per sample. Inline JSON is capped at 10000 samples; use `--file` for larger captures.

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

**Always start by checking the connection.** The human user must have already launched the GUI and connected the PicoScope.

```bash
# 1. Verify scope is connected and running
picoscope-cli get-state
# Check: "signal_source":"picoscope" and "run_mode":"run"
# If signal_source is "dummy", ask the user to connect the PicoScope in the GUI.

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
- `"channel not enabled"`: Enable the channel before measuring/capturing.
- `"no data captured"`: Acquisition hasn't run yet. Send `run` or `single` first, then retry after a brief pause (the GUI runs at ~60 fps, so data is available within ~16ms of starting acquisition).
- `"no PicoScope connected"`: Signal generator requires real hardware. The user needs to connect a PicoScope and select it in the GUI.
- `"measurement failed - no signal data"`: The buffer is empty. Start acquisition and wait a moment.

## Important Notes

- Commands execute on the GUI's main thread at the display frame rate (~60 Hz). After sending `run`, wait at least one frame (~20ms) before capturing or measuring.
- The `single` command acquires one waveform and auto-stops. Good for capturing a stable snapshot.
- Inline capture data is limited to 10000 samples. For longer records, use `--file` to write CSV.
- The signal generator output is a physical BNC connector. It must be wired to whatever you're testing. It does not feed back into the input channels automatically.
- All four channels (A-D) are enabled by default. Disabling unused channels can increase the maximum sample rate.
