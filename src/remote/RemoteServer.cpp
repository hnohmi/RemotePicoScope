#include "remote/RemoteServer.h"
#include "core/Measurements.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

// ---------- helpers ----------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Split "command arg1 arg2" into {"command", "arg1 arg2"}
static std::pair<std::string, std::string> splitCommand(const std::string& line) {
    std::string trimmed = trim(line);
    size_t sp = trimmed.find(' ');
    if (sp == std::string::npos)
        return {toLower(trimmed), ""};
    return {toLower(trimmed.substr(0, sp)), trim(trimmed.substr(sp + 1))};
}

// Simple key=value parser from "key1=val1 key2=val2" or "--key1 val1 --key2 val2"
static std::map<std::string, std::string> parseArgs(const std::string& args) {
    std::map<std::string, std::string> result;
    std::istringstream iss(args);
    std::string token;
    std::string currentKey;

    while (iss >> token) {
        if (token.size() >= 2 && token[0] == '-' && token[1] == '-') {
            // --key or --key=value
            std::string key = token.substr(2);
            size_t eq = key.find('=');
            if (eq != std::string::npos) {
                result[toLower(key.substr(0, eq))] = key.substr(eq + 1);
                currentKey.clear();
            } else {
                currentKey = toLower(key);
                result[currentKey] = ""; // flag with no value
            }
        } else if (!currentKey.empty()) {
            result[currentKey] = token;
            currentKey.clear();
        } else {
            // positional — store as numbered keys
            result[std::to_string(result.size())] = token;
        }
    }
    return result;
}

static std::string jsonOk(const std::string& extraFields = "") {
    if (extraFields.empty())
        return "{\"status\":\"ok\"}\n";
    return "{\"status\":\"ok\"," + extraFields + "}\n";
}

static std::string jsonError(const std::string& msg) {
    return "{\"status\":\"error\",\"message\":\"" + msg + "\"}\n";
}

// ---------- RemoteServer ----------

RemoteServer::RemoteServer() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

RemoteServer::~RemoteServer() {
    stop();
    WSACleanup();
}

bool RemoteServer::start(int port) {
    if (m_running) return true;
    m_port = port;

    m_listenSocket = static_cast<uintptr_t>(
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (m_listenSocket == static_cast<uintptr_t>(INVALID_SOCKET))
        return false;

    // Allow port reuse
    int opt = 1;
    setsockopt(static_cast<SOCKET>(m_listenSocket), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(static_cast<SOCKET>(m_listenSocket),
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        return false;
    }

    if (listen(static_cast<SOCKET>(m_listenSocket), 4) == SOCKET_ERROR) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        return false;
    }

    m_running = true;
    m_listenerThread = std::thread(&RemoteServer::listenerThread, this);
    return true;
}

void RemoteServer::stop() {
    m_running = false;
    if (m_listenSocket != static_cast<uintptr_t>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        m_listenSocket = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (m_listenerThread.joinable())
        m_listenerThread.join();
}

void RemoteServer::listenerThread() {
    while (m_running) {
        // Use select() with timeout so we can check m_running periodically
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(static_cast<SOCKET>(m_listenSocket), &readSet);
        timeval tv{0, 200000}; // 200ms

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        SOCKET clientSock = accept(static_cast<SOCKET>(m_listenSocket), nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) continue;

        // Handle client in a detached thread (short-lived connection)
        std::thread(&RemoteServer::handleClient, this,
                    static_cast<uintptr_t>(clientSock)).detach();
    }
}

void RemoteServer::handleClient(uintptr_t clientSocket) {
    SOCKET sock = static_cast<SOCKET>(clientSocket);

    // Set recv timeout
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    // Read one line (command)
    std::string line;
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        line += buf;
        if (line.find('\n') != std::string::npos) break;
    }

    if (line.empty()) {
        closesocket(sock);
        return;
    }

    // Queue the command for main-thread processing
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingCommands.push_back({clientSocket, trim(line)});
    }

    // Wait for response (main thread will produce it)
    // Poll with timeout
    for (int i = 0; i < 100; i++) { // 10 seconds max
        Sleep(100);
        std::lock_guard<std::mutex> lock(m_responseMutex);
        for (auto it = m_pendingResponses.begin(); it != m_pendingResponses.end(); ++it) {
            if (it->clientSocket == clientSocket) {
                send(sock, it->response.c_str(),
                     static_cast<int>(it->response.size()), 0);
                m_pendingResponses.erase(it);
                closesocket(sock);
                return;
            }
        }
    }

    // Timeout
    std::string err = jsonError("timeout waiting for main thread");
    send(sock, err.c_str(), static_cast<int>(err.size()), 0);
    closesocket(sock);
}

void RemoteServer::processCommands(ScopeState& state, const SignalData& data,
                                    PicoSignalSource& picoSource) {
    std::vector<PendingCommand> commands;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        commands.swap(m_pendingCommands);
    }

    for (auto& cmd : commands) {
        std::string response = executeCommand(cmd.command, state, data, picoSource);
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingResponses.push_back({cmd.clientSocket, response});
    }
}

std::string RemoteServer::executeCommand(const std::string& cmd, ScopeState& state,
                                          const SignalData& data,
                                          PicoSignalSource& picoSource) {
    auto [command, args] = splitCommand(cmd);

    if (command == "help")        return cmdHelp();
    if (command == "get-state")   return cmdGetState(state, data);
    if (command == "set-channel") return cmdSetChannel(args, state);
    if (command == "set-timebase")return cmdSetTimebase(args, state);
    if (command == "set-trigger") return cmdSetTrigger(args, state);
    if (command == "run")         return cmdRun(state);
    if (command == "stop")        return cmdStop(state);
    if (command == "single")      return cmdSingle(state);
    if (command == "measure")     return cmdMeasure(args, state, data);
    if (command == "capture")     return cmdCapture(args, state, data);
    if (command == "siggen")      return cmdSigGen(args, picoSource);

    return jsonError("unknown command: " + command + ". Try 'help'.");
}

// ---------- Command implementations ----------

std::string RemoteServer::cmdHelp() {
    return
        "{\"status\":\"ok\",\"commands\":["
        "\"help\","
        "\"get-state\","
        "\"set-channel --ch <A-D> [--range <V/div>] [--coupling <DC|AC>] [--enable] [--disable] [--offset <V>] [--bwlimit]\","
        "\"set-timebase --value <s/div>\","
        "\"set-trigger --source <A-D> --level <V> [--edge <rising|falling>] [--mode <auto|normal>]\","
        "\"run\","
        "\"stop\","
        "\"single\","
        "\"measure --ch <A-D> [--type <frequency|vpp|vrms|vavg|vmax|vmin|period|risetime|falltime|duty>]\","
        "\"capture --ch <A-D> [--samples <N>] [--file <path.csv>]\","
        "\"siggen --wave <sine|square|triangle|rampup|rampdown|dc> --freq <Hz> --amplitude <mV> [--offset <mV>] [--off]\""
        "]}\n";
}

std::string RemoteServer::cmdGetState(const ScopeState& state, const SignalData& data) {
    std::ostringstream ss;
    ss << "{\"status\":\"ok\"";

    // Run mode
    const char* runStr = "run";
    if (state.runMode == RunMode::Stop) runStr = "stop";
    else if (state.runMode == RunMode::Single) runStr = "single";
    ss << ",\"run_mode\":\"" << runStr << "\"";

    // Source
    ss << ",\"signal_source\":\"" <<
        (state.signalSource == SignalSourceType::PicoScope ? "picoscope" : "dummy") << "\"";

    // Timebase
    ss << ",\"time_per_div\":" << state.timePerDiv();
    ss << ",\"sample_rate\":" << data.sampleRate;
    ss << ",\"record_length\":" << state.recordLength;

    // Channels
    ss << ",\"channels\":[";
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        if (i > 0) ss << ",";
        ss << "{\"name\":\"" << static_cast<char>('A' + i) << "\""
           << ",\"enabled\":" << (state.analog[i].enabled ? "true" : "false")
           << ",\"volts_per_div\":" << state.analog[i].voltsPerDiv()
           << ",\"coupling\":\"" << couplingToString(state.analog[i].coupling) << "\""
           << ",\"offset\":" << state.analog[i].verticalOffset
           << ",\"bw_limit\":" << (state.analog[i].bandwidthLimit ? "true" : "false")
           << "}";
    }
    ss << "]";

    // Trigger
    ss << ",\"trigger\":{\"source\":\"" << static_cast<char>('A' + state.trigger.source)
       << "\",\"level\":" << state.trigger.level
       << ",\"edge\":\"" << (state.trigger.edge == TriggerEdge::Rising ? "rising" : "falling")
       << "\",\"mode\":\"" << (state.trigger.mode == TriggerMode::Auto ? "auto" : "normal")
       << "\"}";

    ss << "}\n";
    return ss.str();
}

static int parseChannelIndex(const std::string& ch) {
    if (ch.empty()) return -1;
    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch[0])));
    if (c >= 'A' && c <= 'D') return c - 'A';
    if (c >= '1' && c <= '4') return c - '1';
    return -1;
}

std::string RemoteServer::cmdSetChannel(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    int ch = parseChannelIndex(params.count("ch") ? params["ch"] : "");
    if (ch < 0) return jsonError("missing or invalid --ch (A-D)");

    if (params.count("enable")) state.analog[ch].enabled = true;
    if (params.count("disable")) state.analog[ch].enabled = false;

    if (params.count("range")) {
        float v = std::stof(params["range"]);
        state.analog[ch].voltsPerDivIndex = Sequence125::findClosestIndex(
            Sequence125::VOLTS_PER_DIV, Sequence125::VOLTS_PER_DIV_COUNT, v);
    }

    if (params.count("coupling")) {
        std::string c = toLower(params["coupling"]);
        if (c == "ac") state.analog[ch].coupling = Coupling::AC;
        else if (c == "dc") state.analog[ch].coupling = Coupling::DC;
    }

    if (params.count("offset")) {
        state.analog[ch].verticalOffset = std::stof(params["offset"]);
    }

    if (params.count("bwlimit")) {
        state.analog[ch].bandwidthLimit = !state.analog[ch].bandwidthLimit;
    }

    return jsonOk();
}

std::string RemoteServer::cmdSetTimebase(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    std::string val = params.count("value") ? params["value"] : (params.count("0") ? params["0"] : "");
    if (val.empty()) return jsonError("missing --value <seconds/div>");

    float v = std::stof(val);
    state.timePerDivIndex = Sequence125::findClosestIndex(
        Sequence125::TIME_PER_DIV, Sequence125::TIME_PER_DIV_COUNT, v);

    std::ostringstream ss;
    ss << "\"time_per_div\":" << state.timePerDiv();
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSetTrigger(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);

    if (params.count("source")) {
        int ch = parseChannelIndex(params["source"]);
        if (ch >= 0) state.trigger.source = ch;
    }

    if (params.count("level")) {
        state.trigger.level = std::stof(params["level"]);
    }

    if (params.count("edge")) {
        std::string e = toLower(params["edge"]);
        if (e == "rising") state.trigger.edge = TriggerEdge::Rising;
        else if (e == "falling") state.trigger.edge = TriggerEdge::Falling;
    }

    if (params.count("mode")) {
        std::string m = toLower(params["mode"]);
        if (m == "auto") state.trigger.mode = TriggerMode::Auto;
        else if (m == "normal") state.trigger.mode = TriggerMode::Normal;
    }

    return jsonOk();
}

std::string RemoteServer::cmdRun(ScopeState& state) {
    state.runMode = RunMode::Run;
    return jsonOk();
}

std::string RemoteServer::cmdStop(ScopeState& state) {
    state.runMode = RunMode::Stop;
    return jsonOk();
}

std::string RemoteServer::cmdSingle(ScopeState& state) {
    state.runMode = RunMode::Single;
    return jsonOk();
}

std::string RemoteServer::cmdMeasure(const std::string& args, const ScopeState& state,
                                      const SignalData& data) {
    auto params = parseArgs(args);
    int ch = parseChannelIndex(params.count("ch") ? params["ch"] : "");
    if (ch < 0) return jsonError("missing or invalid --ch (A-D)");

    if (!state.analog[ch].enabled)
        return jsonError("channel not enabled");

    MeasurementResult m = Measurements::compute(data.analog[ch], data.sampleRate);
    if (!m.valid)
        return jsonError("measurement failed - no signal data");

    std::string type = params.count("type") ? toLower(params["type"]) : "all";

    std::ostringstream ss;
    if (type == "all") {
        ss << "\"frequency\":" << m.frequency
           << ",\"period\":" << m.period
           << ",\"vpp\":" << m.vpp
           << ",\"vavg\":" << m.vavg
           << ",\"vrms\":" << m.vrms
           << ",\"vmax\":" << m.vmax
           << ",\"vmin\":" << m.vmin
           << ",\"rise_time\":" << m.riseTime
           << ",\"fall_time\":" << m.fallTime
           << ",\"duty_cycle\":" << m.dutyCycle;
    } else if (type == "frequency" || type == "freq") {
        ss << "\"value\":" << m.frequency << ",\"unit\":\"Hz\"";
    } else if (type == "period") {
        ss << "\"value\":" << m.period << ",\"unit\":\"s\"";
    } else if (type == "vpp") {
        ss << "\"value\":" << m.vpp << ",\"unit\":\"V\"";
    } else if (type == "vrms") {
        ss << "\"value\":" << m.vrms << ",\"unit\":\"V\"";
    } else if (type == "vavg") {
        ss << "\"value\":" << m.vavg << ",\"unit\":\"V\"";
    } else if (type == "vmax") {
        ss << "\"value\":" << m.vmax << ",\"unit\":\"V\"";
    } else if (type == "vmin") {
        ss << "\"value\":" << m.vmin << ",\"unit\":\"V\"";
    } else if (type == "risetime") {
        ss << "\"value\":" << m.riseTime << ",\"unit\":\"s\"";
    } else if (type == "falltime") {
        ss << "\"value\":" << m.fallTime << ",\"unit\":\"s\"";
    } else if (type == "duty") {
        ss << "\"value\":" << m.dutyCycle << ",\"unit\":\"%\"";
    } else {
        return jsonError("unknown measurement type: " + type);
    }

    return jsonOk(ss.str());
}

std::string RemoteServer::cmdCapture(const std::string& args, const ScopeState& state,
                                      const SignalData& data) {
    auto params = parseArgs(args);
    int ch = parseChannelIndex(params.count("ch") ? params["ch"] : "");
    if (ch < 0) return jsonError("missing or invalid --ch (A-D)");

    if (!state.analog[ch].enabled)
        return jsonError("channel not enabled");

    const AnalogBuffer& buf = data.analog[ch];
    if (buf.count == 0)
        return jsonError("no data captured");

    int samples = buf.count;
    if (params.count("samples")) {
        int req = std::stoi(params["samples"]);
        if (req > 0 && req < samples) samples = req;
    }

    std::string filePath = params.count("file") ? params["file"] : "";

    if (!filePath.empty()) {
        // Write to CSV file
        std::ofstream f(filePath);
        if (!f.is_open())
            return jsonError("cannot open file: " + filePath);

        float dt = (data.sampleRate > 0) ? (1.0f / data.sampleRate) : 1.0f;
        f << "time_s,voltage_v\n";
        for (int i = 0; i < samples; i++) {
            f << (i * dt) << "," << buf.samples[i] << "\n";
        }

        std::ostringstream ss;
        ss << "\"file\":\"" << filePath << "\""
           << ",\"samples\":" << samples
           << ",\"sample_rate\":" << data.sampleRate;
        return jsonOk(ss.str());
    } else {
        // Return data inline as JSON array (limited to 10000 for sanity)
        int limit = (samples > 10000) ? 10000 : samples;
        float dt = (data.sampleRate > 0) ? (1.0f / data.sampleRate) : 1.0f;

        std::ostringstream ss;
        ss << "\"samples\":" << limit
           << ",\"sample_rate\":" << data.sampleRate
           << ",\"data\":[";
        for (int i = 0; i < limit; i++) {
            if (i > 0) ss << ",";
            ss << buf.samples[i];
        }
        ss << "]";
        return jsonOk(ss.str());
    }
}

std::string RemoteServer::cmdSigGen(const std::string& args, PicoSignalSource& picoSource) {
    if (!picoSource.isOpen())
        return jsonError("no PicoScope connected");

    auto params = parseArgs(args);

    // --off to disable
    if (params.count("off")) {
        picoSource.sigGenDisable();
        return jsonOk("\"siggen\":\"off\"");
    }

    std::string waveStr = params.count("wave") ? toLower(params["wave"]) : "sine";
    float freq = params.count("freq") ? std::stof(params["freq"]) : 1000.0f;
    float amp = params.count("amplitude") ? std::stof(params["amplitude"]) : 2000.0f;
    float offset = params.count("offset") ? std::stof(params["offset"]) : 0.0f;

    PicoSignalSource::SigGenWave wave = PicoSignalSource::SigGenWave::Sine;
    if (waveStr == "square") wave = PicoSignalSource::SigGenWave::Square;
    else if (waveStr == "triangle") wave = PicoSignalSource::SigGenWave::Triangle;
    else if (waveStr == "rampup") wave = PicoSignalSource::SigGenWave::RampUp;
    else if (waveStr == "rampdown") wave = PicoSignalSource::SigGenWave::RampDown;
    else if (waveStr == "sinc") wave = PicoSignalSource::SigGenWave::Sinc;
    else if (waveStr == "gaussian") wave = PicoSignalSource::SigGenWave::Gaussian;
    else if (waveStr == "halfsine") wave = PicoSignalSource::SigGenWave::HalfSine;
    else if (waveStr == "dc") wave = PicoSignalSource::SigGenWave::DC;

    if (!picoSource.sigGenEnable(wave, freq, amp, offset))
        return jsonError(picoSource.lastError());

    std::ostringstream ss;
    ss << "\"siggen\":\"on\",\"wave\":\"" << waveStr
       << "\",\"frequency\":" << freq
       << ",\"amplitude_mv\":" << amp
       << ",\"offset_mv\":" << offset;
    return jsonOk(ss.str());
}
