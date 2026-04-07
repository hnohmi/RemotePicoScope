#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include "render/DX11Context.h"
#include "ui/UIContext.h"
#include "ui/WaveformDisplay.h"
#include "ui/ChannelPanel.h"
#include "ui/HorizontalPanel.h"
#include "ui/TriggerPanel.h"
#include "ui/MeasurementPanel.h"
#include "ui/CursorOverlay.h"
#include "ui/StatusBar.h"
#include "ui/MidiConfigPanel.h"
#include "ui/MathPanel.h"
#include "ui/DeviceSelectPopup.h"
#include "ui/SigGenPanel.h"
#include "remote/RemoteServer.h"
#include "core/ScopeState.h"
#include "core/MathEngine.h"
#include "core/FFTEngine.h"
#include "core/SignalBuffer.h"
#include "signal/DummySignalSource.h"
#include "signal/PicoSignalSource.h"
#include "midi/MidiEngine.h"
#include "midi/MidiMapping.h"
#include "midi/MidiProfile.h"
#include "midi/MidiSettings.h"

#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static DX11Context g_dx11;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_dx11.resize(static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool g_firstFrame = true;

// Get the directory where the executable is located
static std::string getExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path p(path);
    return p.parent_path().string();
}

static void setupDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewportSize);

    ImGuiID dockMain = dockspaceId;

    // Use pixel-based ratios for fixed-height bars
    float topBarHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2 + ImGui::GetTextLineHeight();
    float bottomBarHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2;
    float topRatio = topBarHeight / viewportSize.y;
    float bottomRatio = bottomBarHeight / (viewportSize.y - topBarHeight);

    // Top toolbar for horizontal/trigger controls
    ImGuiID dockTop;
    ImGuiID dockRest;
    ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, topRatio, &dockTop, &dockRest);

    // Bottom status bar
    ImGuiID dockBottom;
    ImGuiID dockMiddle;
    ImGui::DockBuilderSplitNode(dockRest, ImGuiDir_Down, bottomRatio, &dockBottom, &dockMiddle);

    // Right panel for channels/measurements (25%)
    ImGuiID dockRight;
    ImGuiID dockCenter;
    ImGui::DockBuilderSplitNode(dockMiddle, ImGuiDir_Right, 0.25f, &dockRight, &dockCenter);

    // Split right: channels top (60%), measurements bottom (40%)
    ImGuiID dockRightTop;
    ImGuiID dockRightBottom;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.40f, &dockRightBottom, &dockRightTop);

    // Dock windows
    ImGui::DockBuilderDockWindow("Horizontal", dockTop);
    ImGui::DockBuilderDockWindow("Waveform", dockCenter);
    ImGui::DockBuilderDockWindow("Channels", dockRightTop);
    ImGui::DockBuilderDockWindow("Trigger", dockRightBottom);
    ImGui::DockBuilderDockWindow("Measurements", dockRightBottom);
    ImGui::DockBuilderDockWindow("Math", dockRightBottom);
    ImGui::DockBuilderDockWindow("Signal Generator", dockRightBottom);
    ImGui::DockBuilderDockWindow("Status", dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"RemotePicoScope";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"RemotePicoScope",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1600, 900,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_dx11.init(hwnd)) {
        g_dx11.shutdown();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    UIContext uiCtx;
    uiCtx.init(hwnd, g_dx11.device(), g_dx11.deviceContext());

    // Initialize state
    ScopeState state;
    state.initDefaults();
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++)
        state.analog[i].enabled = true;

    // Signal sources
    DummySignalSource dummySource;
    PicoSignalSource picoSource;
    SignalData signalData;

    // Math + FFT
    MathEngine mathEngine;
    FFTEngine fftEngine;
    AnalogBuffer mathBuffer;
    FFTResult fftResult;

    // MIDI
    MidiEngine midiEngine;
    MidiMapping midiMapping;
    MidiProfileInfo currentProfile;

    // Set up MIDI callback
    midiEngine.setCallback([&](const MidiMessage& msg) {
        midiMapping.applyCC(msg, state);
    });

    // Resolve profiles directory
    std::string profileDir = getExeDirectory() + "\\..\\..\\..\\profiles";
    if (!std::filesystem::exists(profileDir))
        profileDir = getExeDirectory() + "\\profiles";
    if (!std::filesystem::exists(profileDir))
        profileDir = "profiles"; // relative to cwd

    // MIDI settings (device/profile associations)
    // Save next to profiles directory for consistency
    MidiSettings midiSettings;
    std::string midiSettingsPath = profileDir + "\\midi_settings.json";
    midiSettings.load(midiSettingsPath);

    // Auto-detect MIDI device and load matching profile
    {
        auto ports = midiEngine.listInputPorts();
        auto profiles = MidiProfile::listProfiles(profileDir);

        // Strategy:
        // 1. Try the last-used device first
        // 2. Fall back to any connected device that has a matching profile
        // 3. For the selected device, load its last-used profile (from settings)
        // 4. If no saved profile, find the best matching profile by device_hint

        auto tryConnectDevice = [&](const std::string& deviceName) -> bool {
            for (int pi = 0; pi < static_cast<int>(ports.size()); pi++) {
                if (ports[pi] == deviceName) {
                    return midiEngine.openPort(pi);
                }
            }
            return false;
        };

        auto loadProfileForDevice = [&](const std::string& deviceName) {
            // First try the last-used profile for this device
            std::string savedProfile = midiSettings.getProfileForDevice(deviceName);
            if (!savedProfile.empty() && std::filesystem::exists(savedProfile)) {
                MidiProfile::load(savedProfile, midiMapping, currentProfile);
                return;
            }

            // Fall back to matching by device_hint
            for (const auto& prof : profiles) {
                if (!prof.deviceHint.empty() &&
                    deviceName.find(prof.deviceHint) != std::string::npos)
                {
                    MidiProfile::load(prof.filePath, midiMapping, currentProfile);
                    midiSettings.setDeviceProfile(deviceName, prof.filePath);
                    return;
                }
            }
        };

        // 1. Try last-used device
        if (!midiSettings.lastDeviceName.empty()) {
            if (tryConnectDevice(midiSettings.lastDeviceName)) {
                loadProfileForDevice(midiSettings.lastDeviceName);
            }
        }

        // 2. If not connected, try any device with a matching profile (prefer most recent port = last in list)
        if (!midiEngine.isOpen()) {
            for (int pi = static_cast<int>(ports.size()) - 1; pi >= 0; pi--) {
                // Check if this device has a saved profile or matching device_hint
                std::string savedProf = midiSettings.getProfileForDevice(ports[pi]);
                bool hasMatch = !savedProf.empty();

                if (!hasMatch) {
                    for (const auto& prof : profiles) {
                        if (!prof.deviceHint.empty() &&
                            ports[pi].find(prof.deviceHint) != std::string::npos)
                        {
                            hasMatch = true;
                            break;
                        }
                    }
                }

                if (hasMatch && midiEngine.openPort(pi)) {
                    midiSettings.lastDeviceName = ports[pi];
                    loadProfileForDevice(ports[pi]);
                    break;
                }
            }
        }
    }

    // UI panels
    WaveformDisplay waveformDisplay;
    ChannelPanel channelPanel;
    HorizontalPanel horizontalPanel;
    TriggerPanel triggerPanel;
    MeasurementPanel measurementPanel;
    StatusBar statusBar;
    MidiConfigPanel midiConfigPanel;
    MathPanel mathPanel;
    DeviceSelectPopup deviceSelectPopup;
    SigGenPanel sigGenPanel;

    // TCP remote control server
    RemoteServer remoteServer;
    remoteServer.start(5575);

    float clearColor[4] = { 0.06f, 0.06f, 0.08f, 1.00f };
    bool running = true;

    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        // Poll MIDI
        midiEngine.poll();

        uiCtx.beginFrame();

        // Dockspace
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
            ImGui::GetMainViewport()->ID,
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        if (g_firstFrame) {
            setupDefaultDockLayout(dockspaceId);
            deviceSelectPopup.show();
            g_firstFrame = false;
        }

        // Device selection popup (shown on launch and from menu)
        deviceSelectPopup.draw(state, picoSource);

        // Select active signal source
        ISignalSource* activeSource = &dummySource;
        if (state.signalSource == SignalSourceType::PicoScope && picoSource.isOpen())
            activeSource = &picoSource;

        // Don't acquire while device selection popup is open
        bool shouldAcquire = !deviceSelectPopup.isVisible();

        // Acquire signals
        if (shouldAcquire && state.runMode == RunMode::Run) {
            activeSource->configure(state);
            activeSource->acquire(signalData);
            state.triggerStatus = TriggerStatus::Auto;
        } else if (shouldAcquire && state.runMode == RunMode::Single) {
            activeSource->configure(state);
            activeSource->acquire(signalData);
            state.runMode = RunMode::Stop;
            state.triggerStatus = TriggerStatus::Triggered;
        } else {
            state.triggerStatus = TriggerStatus::Stopped;
        }

        // Process remote commands (main thread — safe to touch state)
        remoteServer.processCommands(state, signalData, picoSource);

        // Compute math channel
        if (state.mathChannel.enabled) {
            if (state.mathChannel.op == MathOp::FFT) {
                fftResult = fftEngine.compute(
                    signalData.analog[state.mathChannel.source1],
                    signalData.sampleRate, state.mathChannel.fftWindow);
            } else {
                mathEngine.compute(state.mathChannel, signalData, mathBuffer);
            }
        }

        // Draw all panels
        waveformDisplay.draw(state, signalData, &mathBuffer, &fftResult);
        channelPanel.draw(state);
        horizontalPanel.draw(state);
        triggerPanel.draw(state);
        mathPanel.draw(state);
        sigGenPanel.draw(picoSource);
        measurementPanel.draw(state, signalData);
        statusBar.draw(state, signalData, midiEngine, [&]() { midiConfigPanel.toggle(); },
                       activeSource->name());

        // Settings popup (hidden until settings button clicked)
        midiConfigPanel.draw(midiEngine, midiMapping, currentProfile, profileDir,
                             midiSettings, midiSettingsPath, &state, &picoSource);

        // Render
        g_dx11.beginFrame(clearColor);
        uiCtx.endFrame();
        if (!g_dx11.endFrame()) {
            // Device lost and recreation failed — reinit ImGui backend
            uiCtx.shutdown();
            uiCtx.init(hwnd, g_dx11.device(), g_dx11.deviceContext());
        }
    }

    // Stop remote server
    remoteServer.stop();

    // Save current MIDI mapping and settings before exit
    if (!currentProfile.name.empty() && !currentProfile.filePath.empty()) {
        MidiProfile::save(currentProfile.filePath, midiMapping, currentProfile);
    }
    if (midiEngine.isOpen()) {
        midiSettings.lastDeviceName = midiEngine.currentPortName();
        if (!currentProfile.filePath.empty()) {
            midiSettings.setDeviceProfile(midiEngine.currentPortName(), currentProfile.filePath);
        }
    }
    midiSettings.save(midiSettingsPath);

    uiCtx.shutdown();
    g_dx11.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
