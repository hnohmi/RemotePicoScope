# RemotePicoScope

A Windows-native Mixed Signal Oscilloscope application for the **PicoScope 3406D MSO**, built with Dear ImGui and DirectX 11.

Includes a built-in demo mode with simulated signals, so you can explore the UI without hardware.

## Features

- **4 analog channels** with configurable range (10 mV/div to 50 V/div), coupling (AC/DC), and offset
- **16 digital channels** (MSO) displayed as logic lanes
- **Math channels** — add, subtract, multiply, divide, derivative, integral, sqrt of any two channels
- **FFT spectrum analysis** with selectable window functions (Hanning, Hamming, Blackman-Harris, Flat Top)
- **Automatic measurements** — frequency, period, Vpp, Vrms, Vavg, Vmax, Vmin, rise/fall time, duty cycle
- **Cursors** — horizontal and vertical with delta readout
- **Trigger control** — edge trigger with configurable source, level, slope, and mode (auto/normal)
- **Signal generator control** — built-in sig gen output (sine, square, triangle, ramp, DC, etc.)
- **MIDI controller support** — map physical knobs/faders to oscilloscope parameters with JSON profiles
- **Dockable panel layout** — rearrange panels freely via ImGui docking
- **TCP remote control** — programmatic control via CLI tool (see below)

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
REM Check oscilloscope state
build\Release\picoscope-cli.exe get-state

REM Configure channel A for 3.3V logic
build\Release\picoscope-cli.exe set-channel --ch A --enable --range 1.0

REM Measure frequency
build\Release\picoscope-cli.exe measure --ch A --type frequency

REM Capture waveform data
build\Release\picoscope-cli.exe capture --ch A --samples 2000

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
  core/          State management, measurements, math, FFT
  render/        DirectX 11 context and waveform rendering
  signal/        Signal sources (PicoScope hardware + dummy)
  ui/            ImGui panels and display
  midi/          MIDI engine, mapping, profiles
  remote/        TCP server and CLI tool
  vendor/        Vendored libraries (KissFFT)
profiles/        MIDI controller profiles
```

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
