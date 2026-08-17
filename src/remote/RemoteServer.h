#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include "core/FrameResults.h"
#include "signal/PicoSignalSource.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// Lightweight TCP command server for remote control of the oscilloscope.
// Runs a listener thread on localhost. Each command is a single line;
// the response is a JSON object terminated by a newline.
class RemoteServer {
public:
    RemoteServer();
    ~RemoteServer();

    // Start listening on the given port. Non-blocking — spawns a thread.
    bool start(int port = 5575);
    void stop();
    bool isRunning() const { return m_running; }
    int port() const { return m_port; }

    // Called from the main loop each frame to process queued commands.
    // This runs on the main thread so it can safely touch ScopeState/SignalData.
    void processCommands(ScopeState& state, const SignalData& data,
                         const FrameResults& results, PicoSignalSource& picoSource);

    // True once after a remote connect/disconnect selected a signal source,
    // so the main loop can dismiss the startup device-select popup. Consumes
    // the flag (returns to false).
    bool takeSourceSelected() { return m_sourceSelected.exchange(false); }

private:
    struct PendingCommand {
        uintptr_t clientSocket;
        std::string command;
    };

    void listenerThread();
    void handleClient(uintptr_t clientSocket);
    std::string executeCommand(const std::string& cmd, ScopeState& state,
                               const SignalData& data, const FrameResults& results,
                               PicoSignalSource& picoSource);

    // Command handlers
    std::string cmdGetState(const ScopeState& state, const SignalData& data,
                            const FrameResults& results, const PicoSignalSource& picoSource);
    std::string cmdSetChannel(const std::string& args, ScopeState& state);
    std::string cmdSetTimebase(const std::string& args, ScopeState& state);
    std::string cmdSetTrigger(const std::string& args, ScopeState& state);
    std::string cmdSetDigital(const std::string& args, ScopeState& state);
    std::string cmdSetMath(const std::string& args, ScopeState& state);
    std::string cmdSetCursor(const std::string& args, ScopeState& state);
    std::string cmdGetCursors(const ScopeState& state);
    std::string cmdRun(ScopeState& state);
    std::string cmdStop(ScopeState& state);
    std::string cmdSingle(ScopeState& state);
    std::string cmdMeasure(const std::string& args, const ScopeState& state,
                           const SignalData& data);
    std::string cmdSetRecordLength(const std::string& args, ScopeState& state);
    std::string cmdCapture(const std::string& args, const ScopeState& state,
                           const SignalData& data, const FrameResults& results);
    std::string cmdSigGen(const std::string& args, ScopeState& state,
                          PicoSignalSource& picoSource);
    std::string cmdListDevices();
    std::string cmdConnect(const std::string& args, ScopeState& state,
                           PicoSignalSource& picoSource);
    std::string cmdDisconnect(ScopeState& state, PicoSignalSource& picoSource);
    std::string cmdAutoscale(ScopeState& state, const SignalData& data);
    std::string cmdSaveSetup(const std::string& args, const ScopeState& state);
    std::string cmdRecallSetup(const std::string& args, ScopeState& state);
    std::string cmdSetDecode(const std::string& args, ScopeState& state);
    std::string cmdGetDecode(const std::string& args, const ScopeState& state,
                             const SignalData& data);
    std::string cmdSetBus(const std::string& args, ScopeState& state);
    std::string cmdStartRecording(const std::string& args, const ScopeState& state,
                                  PicoSignalSource& picoSource);
    std::string cmdStopRecording(PicoSignalSource& picoSource);
    std::string cmdRecordingStatus(const PicoSignalSource& picoSource);
    std::string cmdHelp();

    int m_port = 5575;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_sourceSelected{false};
    std::thread m_listenerThread;
    uintptr_t m_listenSocket = ~uintptr_t(0);

    std::mutex m_queueMutex;
    std::vector<PendingCommand> m_pendingCommands;

    // Completed responses to send back (main thread writes, client threads read)
    struct PendingResponse {
        uintptr_t clientSocket;
        std::string response;
    };
    std::mutex m_responseMutex;
    std::vector<PendingResponse> m_pendingResponses;
};
