# RemotePicoScope

A Windows-native Mixed Signal Oscilloscope application for the **PicoScope 3406D MSO**, built with Dear ImGui and DirectX 11.

Includes a built-in demo mode with simulated signals, so you can explore the UI without hardware.

## Features

### Analog
- **4 analog channels** — 1 mV/div to 100 V/div, AC/DC/GND coupling, display offset, bandwidth limit, **probe attenuation (1×/10×/100×)**, invert, custom labels
- **Trigger** — edge (analog), **digital edge**, and **digital pattern** trigger; auto/normal/single; probe-aware level conversion
- **Math channel** — add, subtract, multiply, divide, derivative, integral, sqrt
- **FFT spectrum analysis** with selectable window functions
- **Automatic measurements** with noise-robust frequency detection; cursors with per-channel voltage readouts
- **Autoscale** — one command/button signal find

### Mixed-signal (MSO)
- **16 digital channels** with configurable logic thresholds (TTL / 3.3 V / 1.8 V CMOS / custom, per port)
- **Serial protocol decode** — UART, I2C, SPI, with a live decode table and CLI frame export
- **Digital buses** — group lanes into named hex/bin/dec value tracks
- **VCD export** — digital captures load directly into GTKWave / HDL simulators

### Acquisition (three modes)
- **Live (auto, default)** — rapid-block batches: the hardware captures back-to-back triggered sweeps (~1 µs re-arm) and the display animates through them, one trigger per frame (skipping evenly when triggers outrun the frame rate); each sweep spans 3 windows — the active window plus one full window either side for timeshift
- **Deep (fixed)** — up to **512 M samples** per sweep (the 3406D's full memory) with a host-RAM estimate shown; chunked retrieval keeps the UI responsive
- **Recording** — **gapless streaming to disk** for hours (`start-recording`), int16 binary + JSON sidecar, with a rolling live view while recording

### Control & UX
- **Signal generator control** — built-in sig gen output (sine, square, triangle, ramp, DC, etc.)
- **Drag the waveform** to pan time (X) and move a channel's offset (Y); per-channel ground markers
- **Setup save/recall** + automatic session persistence
- **MIDI controller support** — map physical knobs/faders to oscilloscope parameters with JSON profiles
- **Dockable panel layout** — rearrange panels freely via ImGui docking
- **TCP remote control** — full CLI/automation parity: every setting and all wave data (analog, digital, math, FFT, decoded frames) is accessible remotely; designed for AI-assisted hardware bring-up

See [ARCHITECTURE.md](ARCHITECTURE.md) for the data/display layering that keeps
display settings (offset, probe scaling views) strictly out of the measurement
data path.

## Screenshots

*(Coming soon)*

## Requirements

- Windows 10/11 (x64)
- Visual Studio 2022 with C++ desktop workload
- [CMake](https://cmake.org/) 3.21+
- [vcpkg](https://github.com/microsoft/vcpkg) (as a subdirectory or system install)
- **PicoScope SDK** — install from [Pico Technology](https://www.picotech.com/downloads)

## Building

All commands below run in a standard **Windows Command Prompt** (cmd.exe). No MinGW, MSYS2, or PowerShell required.

### 1. Clone and set up vcpkg

```cmd
git clone https://github.com/user/RemotePicoScope.git
cd RemotePicoScope
git clone https://github.com/microsoft/vcpkg.git
vcpkg\bootstrap-vcpkg.bat
```

### 2. Set PicoScope SDK path

Set the `PICOSCOPE_SDK_PATH` environment variable to your PicoScope SDK install location:

```cmd
set PICOSCOPE_SDK_PATH=C:\Program Files\Pico Technology\SDK
```

Or set it permanently via System Properties > Environment Variables.

### 3. Build

```cmd
build.bat
```

This will configure CMake, install vcpkg dependencies, and build both `RemotePicoScope.exe` and `picoscope-cli.exe` in `build\Release\`.

To do a full clean rebuild:

```cmd
build.bat clean
```

> **Note:** If vcpkg fails during configure with a compiler detection error, add an exclusion in Windows Defender for the `vcpkg\buildtrees\` directory.

### 4. Run

```cmd
build\Release\RemotePicoScope.exe
```

On launch, select a connected PicoScope device or choose **Demo Mode** to use simulated signals.

## PicoScope SDK

Install the PicoScope SDK from [Pico Technology](https://www.picotech.com/downloads) and set the `PICOSCOPE_SDK_PATH` environment variable to the install location (e.g. `C:\Program Files\Pico Technology\SDK`). The build requires this variable — CMake will fail with a clear error if it is not set.

Supported hardware:
- PicoScope 3000A series (tested with 3406D MSO)

## Remote Control (CLI)

The application includes a TCP server (port 5575) and a companion CLI tool for programmatic control. This is designed for AI-assisted hardware development workflows — for example, having an AI agent control the oscilloscope while you monitor the display.

```cmd
REM Connect to the scope (no GUI interaction needed — fully headless)
build\Release\picoscope-cli.exe connect

REM Configure channel A for 3.3V logic with a 10x probe
build\Release\picoscope-cli.exe set-channel --ch A --enable --range 1.0 --probe 10

REM Digital: 3.3V CMOS thresholds, decode UART on D0
build\Release\picoscope-cli.exe set-digital --ch all --enable
build\Release\picoscope-cli.exe set-digital --threshold 1.65 --group all
build\Release\picoscope-cli.exe set-decode --protocol uart --lane 0 --baud 115200 --enable
build\Release\picoscope-cli.exe get-decode

REM Measure, capture (analog, digital, MATH, FFT, or several at once)
build\Release\picoscope-cli.exe measure --ch A --type frequency
build\Release\picoscope-cli.exe capture --ch A,B,D --file waveform.csv
build\Release\picoscope-cli.exe capture --ch D --file capture.vcd

REM Gapless recording to disk
build\Release\picoscope-cli.exe start-recording --file run1.bin --rate 1000000 --ch A,B
build\Release\picoscope-cli.exe stop-recording

REM Control signal generator
build\Release\picoscope-cli.exe siggen --wave sine --freq 1000 --amplitude 2000
```

See [PICOSCOPE-CLI.md](src/remote/PICOSCOPE-CLI.md) for the full command reference.

## MIDI Controller Support

Map any MIDI controller's knobs and faders to oscilloscope parameters. Profiles are stored as JSON files in the `profiles/` directory.

Supported controllers are auto-detected on launch. Configure mappings via the settings panel (gear icon in the status bar).

## Project Structure

```
src/
  core/          State management, measurements, math, FFT, setup I/O,
                 autoscale, memory estimation, VCD export
  decode/        Serial protocol decoders (UART, I2C, SPI)
  render/        DirectX 11 context, waveform rendering, view transform
  signal/        Signal sources (PicoScope hardware + demo) and the
                 streaming recorder
  ui/            ImGui panels and display
  midi/          MIDI engine, mapping, profiles
  remote/        TCP server and CLI tool
  vendor/        Vendored libraries (KissFFT)
profiles/        MIDI controller profiles
tests/           CLI smoke-test suite (runs against demo mode)
```

Additional docs: [ARCHITECTURE.md](ARCHITECTURE.md) (data/display layering),
[src/remote/PICOSCOPE-CLI.md](src/remote/PICOSCOPE-CLI.md) (full remote-control
reference), [IMPLEMENTATION-PLAN.md](IMPLEMENTATION-PLAN.md) and
[FEATURE-WISHLIST.md](FEATURE-WISHLIST.md) (roadmap).

## Dependencies

| Library | License | Purpose |
|---------|---------|---------|
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | MIT | UI framework |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | JSON serialization |
| [RtMidi](https://github.com/thestk/rtmidi) | MIT | MIDI input |
| [KissFFT](https://github.com/mborgerding/kissfft) | BSD-3-Clause | FFT computation (vendored) |

## Trademarks

PicoScope is a registered trademark of Pico Technology Ltd. This project is not affiliated with or endorsed by Pico Technology.

## License

[MIT](LICENSE)
