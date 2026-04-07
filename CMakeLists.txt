cmake_minimum_required(VERSION 3.21)
project(RemotePicoScope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vendored KissFFT (compiled as C)
enable_language(C)
add_library(kissfft STATIC
    src/vendor/kissfft/kiss_fft.c
    src/vendor/kissfft/kiss_fftr.c
)
target_include_directories(kissfft PUBLIC src/vendor)
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(RtMidi CONFIG REQUIRED)

add_executable(RemotePicoScope WIN32
    src/main.cpp
    src/render/DX11Context.cpp
    src/render/WaveformRenderer.cpp
    src/core/Measurements.cpp
    src/signal/DummySignalSource.cpp
    src/ui/UIContext.cpp
    src/ui/WaveformDisplay.cpp
    src/ui/ChannelPanel.cpp
    src/ui/HorizontalPanel.cpp
    src/ui/TriggerPanel.cpp
    src/ui/MeasurementPanel.cpp
    src/ui/CursorOverlay.cpp
    src/ui/StatusBar.cpp
    src/ui/MidiConfigPanel.cpp
    src/midi/MidiEngine.cpp
    src/midi/MidiMapping.cpp
    src/midi/MidiProfile.cpp
    src/midi/MidiSettings.cpp
    src/core/MathEngine.cpp
    src/core/FFTEngine.cpp
    src/ui/MathPanel.cpp
    src/ui/FFTDisplay.cpp
    src/signal/PicoSignalSource.cpp
    src/ui/DeviceSelectPopup.cpp
    src/ui/SigGenPanel.cpp
    src/remote/RemoteServer.cpp
)

# PicoScope SDK — set PICOSCOPE_SDK_PATH environment variable to your SDK install location
if(DEFINED ENV{PICOSCOPE_SDK_PATH})
    set(PICOSCOPE_SDK_PATH "$ENV{PICOSCOPE_SDK_PATH}")
else()
    message(FATAL_ERROR "PICOSCOPE_SDK_PATH environment variable is not set. "
        "Set it to your PicoScope SDK install location, e.g. "
        "C:/Program Files/Pico Technology/SDK")
endif()

target_include_directories(RemotePicoScope PRIVATE
    src
    "${PICOSCOPE_SDK_PATH}/inc"
)

target_link_libraries(RemotePicoScope PRIVATE
    imgui::imgui
    nlohmann_json::nlohmann_json
    RtMidi::rtmidi
    d3d11
    dxgi
    d3dcompiler
    kissfft
    ws2_32
    "${PICOSCOPE_SDK_PATH}/lib/ps3000a.lib"
)

# CLI remote control tool
add_executable(picoscope-cli
    src/remote/picoscope-cli.cpp
)
target_link_libraries(picoscope-cli PRIVATE ws2_32)
