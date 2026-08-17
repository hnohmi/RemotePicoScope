#include "remote/RemoteServer.h"
#include "core/Measurements.h"
#include "core/Version.h"
#include "core/AutoScale.h"
#include "core/SetupIO.h"
#include "core/VcdExport.h"
#include "core/MemoryEstimate.h"
#include "decode/SerialDecode.h"

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
#include <cstdlib>
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

// Escape a string for embedding in a JSON double-quoted value.
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string jsonOk(const std::string& extraFields = "") {
    if (extraFields.empty())
        return "{\"status\":\"ok\"}\n";
    return "{\"status\":\"ok\"," + extraFields + "}\n";
}

static std::string jsonError(const std::string& msg) {
    return "{\"status\":\"error\",\"message\":\"" + jsonEscape(msg) + "\"}\n";
}

// Split a comma-separated list, trimming each element.
static std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = trim(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

static const char* decodeProtocolName(int p);
static const char* busDisplayName(int d);

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
                                    const FrameResults& results,
                                    PicoSignalSource& picoSource) {
    std::vector<PendingCommand> commands;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        commands.swap(m_pendingCommands);
    }

    for (auto& cmd : commands) {
        std::string response = executeCommand(cmd.command, state, data, results, picoSource);
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingResponses.push_back({cmd.clientSocket, response});
    }
}

std::string RemoteServer::executeCommand(const std::string& cmd, ScopeState& state,
                                          const SignalData& data,
                                          const FrameResults& results,
                                          PicoSignalSource& picoSource) {
    auto [command, args] = splitCommand(cmd);

    if (command == "help")        return cmdHelp();
    if (command == "version") {
        std::ostringstream ss;
        ss << "{\"status\":\"ok\""
           << ",\"app_version\":\"" << Version::APP << "\""
           << ",\"protocol_version\":\"" << Version::CLI_PROTOCOL << "\""
           << "}\n";
        return ss.str();
    }
    if (command == "get-state")   return cmdGetState(state, data, results, picoSource);
    if (command == "set-channel") return cmdSetChannel(args, state);
    if (command == "set-timebase")return cmdSetTimebase(args, state);
    if (command == "set-trigger") return cmdSetTrigger(args, state);
    if (command == "set-digital") return cmdSetDigital(args, state);
    if (command == "set-math")    return cmdSetMath(args, state);
    if (command == "set-cursor")  return cmdSetCursor(args, state);
    if (command == "get-cursors") return cmdGetCursors(state);
    if (command == "run")         return cmdRun(state);
    if (command == "stop")        return cmdStop(state);
    if (command == "single")      return cmdSingle(state);
    if (command == "measure")     return cmdMeasure(args, state, data);
    if (command == "set-record-length") return cmdSetRecordLength(args, state);
    if (command == "capture")     return cmdCapture(args, state, data, results);
    if (command == "siggen")      return cmdSigGen(args, state, picoSource);
    if (command == "list-devices")return cmdListDevices();
    if (command == "connect")     return cmdConnect(args, state, picoSource);
    if (command == "disconnect")  return cmdDisconnect(state, picoSource);
    if (command == "autoscale")   return cmdAutoscale(state, data);
    if (command == "save-setup")  return cmdSaveSetup(args, state);
    if (command == "recall-setup")return cmdRecallSetup(args, state);
    if (command == "set-decode")  return cmdSetDecode(args, state);
    if (command == "get-decode")  return cmdGetDecode(args, state, data);
    if (command == "set-bus")     return cmdSetBus(args, state);
    if (command == "start-recording")  return cmdStartRecording(args, state, picoSource);
    if (command == "stop-recording")   return cmdStopRecording(picoSource);
    if (command == "recording-status") return cmdRecordingStatus(picoSource);

    return jsonError("unknown command: " + command + ". Try 'help'.");
}

// ---------- Command implementations ----------

std::string RemoteServer::cmdHelp() {
    return
        "{\"status\":\"ok\",\"commands\":["
        "\"help\","
        "\"version\","
        "\"get-state\","
        "\"set-channel --ch <A-D> [--range <V/div>] [--coupling <DC|AC|GND>] [--enable] [--disable] [--offset <V>] [--bwlimit <on|off>] [--probe <1|10|100>] [--invert <on|off>] [--label <text>]\","
        "\"set-timebase --value <s/div> [--offset <s>]\","
        "\"set-trigger [--type <edge|digital|pattern>] [--source <A-D> --level <V>] [--edge <rising|falling>] [--mode <auto|normal|single>] [--dsource <0-15>] [--pattern <D0=1,D3=0|clear>]\","
        "\"set-digital [--ch <0-15|all> --enable|--disable] [--threshold <V> --group <0|1|all>]\","
        "\"set-math [--enable] [--disable] [--op <add|sub|mul|div|fft|ddt|integ|sqrt>] [--src1 <A-D>] [--src2 <A-D>] [--window <rect|hanning|hamming|blackman|flattop>]\","
        "\"set-cursor [--enable] [--disable] [--x1 <div>] [--x2 <div>] [--y1 <div>] [--y2 <div>] [--source <A-D>]\","
        "\"get-cursors\","
        "\"run\","
        "\"stop\","
        "\"single\","
        "\"measure --ch <A-D> [--type <frequency|vpp|vrms|vavg|vmax|vmin|period|risetime|falltime|duty>]\","
        "\"set-record-length --value <N> | --auto  (fixed: 1000 to 512000000; auto sizes the sweep for live display)\","
        "\"capture --ch <A-D|D|D0-D15|MATH|FFT|comma-separated> [--samples <N>] [--file <path.csv>]\","
        "\"siggen --wave <sine|square|triangle|rampup|rampdown|dc> --freq <Hz> --amplitude <mV> [--offset <mV>] [--off]\","
        "\"list-devices\","
        "\"connect [--serial <SN>] [--demo]\","
        "\"disconnect\","
        "\"autoscale\","
        "\"save-setup --file <path.json>\","
        "\"recall-setup --file <path.json>\","
        "\"set-decode --protocol <uart|i2c|spi> [--enable] [--disable] [uart: --lane <n> --baud <n>] [i2c: --scl <n> --sda <n>] [spi: --clk <n> --mosi <n> --cs <n> --cpol <0|1> --cpha <0|1>]\","
        "\"get-decode [--file <frames.csv>]\","
        "\"set-bus --index <0|1> [--name <text>] [--lanes <l0,l1,...>] [--display <hex|bin|dec>] [--enable] [--disable]\","
        "\"start-recording --file <path.bin> --rate <Hz> [--ch <A,B,...>]  (gapless stream to disk)\","
        "\"stop-recording\","
        "\"recording-status\""
        "]}\n";
}

static const char* triggerModeStr(TriggerMode m) {
    switch (m) {
        case TriggerMode::Auto:   return "auto";
        case TriggerMode::Normal: return "normal";
        case TriggerMode::Single: return "single";
    }
    return "auto";
}

static const char* triggerStatusStr(TriggerStatus s) {
    switch (s) {
        case TriggerStatus::Ready:     return "ready";
        case TriggerStatus::Armed:     return "armed";
        case TriggerStatus::Triggered: return "triggered";
        case TriggerStatus::Stopped:   return "stopped";
        case TriggerStatus::Auto:      return "auto";
    }
    return "auto";
}

std::string RemoteServer::cmdGetState(const ScopeState& state, const SignalData& data,
                                       const FrameResults& results,
                                       const PicoSignalSource& picoSource) {
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

    // Connected device
    ss << ",\"device\":{\"connected\":" << (picoSource.isOpen() ? "true" : "false");
    if (picoSource.isOpen()) {
        ss << ",\"description\":\"" << picoSource.deviceInfo() << "\""
           << ",\"serial\":\"" << picoSource.serial() << "\"";
    }
    if (!picoSource.lastError().empty())
        ss << ",\"last_error\":\"" << jsonEscape(picoSource.lastError()) << "\"";
    ss << "}";

    // Timebase
    ss << ",\"time_per_div\":" << state.timePerDiv();
    ss << ",\"horizontal_offset\":" << state.horizontalOffset;
    ss << ",\"sample_rate\":" << data.sampleRate;
    ss << ",\"record_mode\":\"" << (state.recordAuto ? "auto" : "fixed") << "\"";
    ss << ",\"record_length\":" << state.effectiveRecordLength();
    ss << ",\"record_bytes_estimate\":" << MemoryEstimate::estimate(state).total();

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
           << ",\"probe\":" << state.analog[i].probeAttenuation
           << ",\"invert\":" << (state.analog[i].invert ? "true" : "false")
           << ",\"label\":\"" << jsonEscape(state.analog[i].label) << "\""
           << "}";
    }
    ss << "]";

    // Digital channels
    ss << ",\"digital\":{\"enabled\":[";
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (i > 0) ss << ",";
        ss << (state.digital[i].enabled ? "true" : "false");
    }
    ss << "],\"threshold\":[" << state.digitalThreshold[0] << ","
       << state.digitalThreshold[1] << "]}";

    // Trigger
    ss << ",\"trigger\":{\"type\":\"" << triggerTypeName(state.trigger.type) << "\""
       << ",\"source\":\"" << static_cast<char>('A' + state.trigger.source)
       << "\",\"level\":" << state.trigger.level
       << ",\"edge\":\"" << (state.trigger.edge == TriggerEdge::Rising ? "rising" : "falling")
       << "\",\"mode\":\"" << triggerModeStr(state.trigger.mode)
       << "\",\"status\":\"" << triggerStatusStr(state.triggerStatus)
       << "\",\"digital_source\":\"D" << state.trigger.digitalSource << "\"";
    // Pattern (only the specified lanes)
    ss << ",\"pattern\":\"";
    bool firstPat = true;
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.trigger.digitalPattern[i] == 0) continue;
        if (!firstPat) ss << ",";
        ss << "D" << i << "=" << (state.trigger.digitalPattern[i] == 1 ? "1" : "0");
        firstPat = false;
    }
    ss << "\"}";

    // Math channel
    ss << ",\"math\":{\"enabled\":" << (state.mathChannel.enabled ? "true" : "false")
       << ",\"op\":\"" << mathOpName(state.mathChannel.op) << "\""
       << ",\"src1\":\"" << static_cast<char>('A' + state.mathChannel.source1) << "\""
       << ",\"src2\":\"" << static_cast<char>('A' + state.mathChannel.source2) << "\""
       << ",\"window\":\"" << fftWindowName(state.mathChannel.fftWindow) << "\""
       << ",\"has_data\":" << ((results.math && results.math->count > 0) ||
                               (results.fft && results.fft->valid) ? "true" : "false")
       << "}";

    // Cursors
    ss << ",\"cursors\":{\"enabled\":" << (state.cursors.enabled ? "true" : "false")
       << ",\"source\":\"" << static_cast<char>('A' + state.cursors.source) << "\""
       << ",\"x1\":" << state.cursors.x1 << ",\"x2\":" << state.cursors.x2
       << ",\"y1\":" << state.cursors.y1 << ",\"y2\":" << state.cursors.y2 << "}";

    // Signal generator (reporting shadow)
    ss << ",\"siggen\":{\"enabled\":" << (state.sigGen.enabled ? "true" : "false")
       << ",\"wave\":\"" << state.sigGen.wave << "\""
       << ",\"frequency\":" << state.sigGen.frequency
       << ",\"amplitude_mv\":" << state.sigGen.amplitudeMv
       << ",\"offset_mv\":" << state.sigGen.offsetMv << "}";

    // Serial decode
    ss << ",\"decode\":{\"enabled\":" << (state.decode.enabled ? "true" : "false")
       << ",\"protocol\":\"" << decodeProtocolName(state.decode.protocol) << "\"}";

    // Streaming recorder
    {
        auto rec = picoSource.recordingStatus();
        ss << ",\"recording\":{\"active\":" << (rec.active ? "true" : "false");
        if (rec.active)
            ss << ",\"file\":\"" << jsonEscape(rec.file) << "\""
               << ",\"seconds\":" << rec.seconds
               << ",\"samples_per_channel\":" << rec.samples;
        ss << "}";
    }

    // Buses
    ss << ",\"buses\":[";
    for (size_t b = 0; b < state.buses.size(); b++) {
        const BusConfig& bus = state.buses[b];
        if (b > 0) ss << ",";
        ss << "{\"index\":" << b
           << ",\"enabled\":" << (bus.enabled ? "true" : "false")
           << ",\"name\":\"" << jsonEscape(bus.name) << "\""
           << ",\"display\":\"" << busDisplayName(bus.display) << "\""
           << ",\"lanes\":[";
        for (size_t k = 0; k < bus.lanes.size(); k++) {
            if (k > 0) ss << ",";
            ss << bus.lanes[k];
        }
        ss << "]}";
    }
    ss << "]";

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
        else if (c == "gnd") state.analog[ch].coupling = Coupling::GND;
        else return jsonError("invalid --coupling (DC|AC|GND)");
    }

    if (params.count("offset")) {
        state.analog[ch].verticalOffset = std::stof(params["offset"]);
    }

    if (params.count("bwlimit")) {
        // Accept explicit on/off; a bare --bwlimit toggles (deprecated).
        std::string v = toLower(params["bwlimit"]);
        if (v == "on" || v == "true" || v == "1") state.analog[ch].bandwidthLimit = true;
        else if (v == "off" || v == "false" || v == "0") state.analog[ch].bandwidthLimit = false;
        else if (v.empty()) state.analog[ch].bandwidthLimit = !state.analog[ch].bandwidthLimit;
        else return jsonError("invalid --bwlimit (on|off)");
    }

    if (params.count("probe")) {
        float p = std::stof(params["probe"]);
        if (p != 1.0f && p != 10.0f && p != 100.0f)
            return jsonError("invalid --probe (1|10|100)");
        state.analog[ch].probeAttenuation = p;
    }

    if (params.count("invert")) {
        std::string v = toLower(params["invert"]);
        if (v == "on" || v == "true" || v == "1") state.analog[ch].invert = true;
        else if (v == "off" || v == "false" || v == "0") state.analog[ch].invert = false;
        else if (v.empty()) state.analog[ch].invert = !state.analog[ch].invert;
        else return jsonError("invalid --invert (on|off)");
    }

    if (params.count("label")) {
        state.analog[ch].label = params["label"];
    }

    return jsonOk();
}

std::string RemoteServer::cmdSetTimebase(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    std::string val = params.count("value") ? params["value"] : (params.count("0") ? params["0"] : "");

    if (!val.empty()) {
        float v = std::stof(val);
        state.timePerDivIndex = Sequence125::findClosestIndex(
            Sequence125::TIME_PER_DIV, Sequence125::TIME_PER_DIV_COUNT, v);
    }

    if (params.count("offset")) {
        float off = std::stof(params["offset"]);
        float maxOff = state.maxHorizontalOffset();
        if (off > maxOff) off = maxOff;
        if (off < -maxOff) off = -maxOff;
        state.horizontalOffset = off;
    }

    if (val.empty() && !params.count("offset"))
        return jsonError("missing --value <seconds/div> and/or --offset <seconds>");

    std::ostringstream ss;
    ss << "\"time_per_div\":" << state.timePerDiv()
       << ",\"horizontal_offset\":" << state.horizontalOffset;
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
        else if (m == "single") state.trigger.mode = TriggerMode::Single;
        else return jsonError("invalid --mode (auto|normal|single)");
    }

    if (params.count("type")) {
        std::string t = toLower(params["type"]);
        if (t == "edge") state.trigger.type = TriggerType::Edge;
        else if (t == "digital") state.trigger.type = TriggerType::Digital;
        else if (t == "pattern") state.trigger.type = TriggerType::Pattern;
        else return jsonError("invalid --type (edge|digital|pattern)");
    }

    if (params.count("dsource")) {
        int lane = std::atoi(params["dsource"].c_str());
        if (lane < 0 || lane >= NUM_DIGITAL_CHANNELS)
            return jsonError("invalid --dsource (0-15)");
        state.trigger.digitalSource = lane;
    }

    if (params.count("pattern")) {
        std::string p = toLower(params["pattern"]);
        state.trigger.digitalPattern.fill(0);
        if (p != "clear" && p != "none") {
            for (const std::string& tok : splitList(p)) {
                size_t eq = tok.find('=');
                if (eq == std::string::npos) continue;
                std::string lhs = tok.substr(0, eq);
                std::string rhs = tok.substr(eq + 1);
                if (!lhs.empty() && lhs[0] == 'd') lhs = lhs.substr(1);
                int lane = std::atoi(lhs.c_str());
                if (lane < 0 || lane >= NUM_DIGITAL_CHANNELS) continue;
                if (rhs == "1" || rhs == "h") state.trigger.digitalPattern[lane] = 1;
                else if (rhs == "0" || rhs == "l") state.trigger.digitalPattern[lane] = 2;
                else state.trigger.digitalPattern[lane] = 0;
            }
        }
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

std::string RemoteServer::cmdSetRecordLength(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);

    if (params.count("auto")) {
        // Auto (live) mode: sweep sized for responsive display; refresh
        // follows time/div. Fixed lengths are for deep captures.
        state.recordAuto = true;
    } else {
        std::string val = params.count("value") ? params["value"] : (params.count("0") ? params["0"] : "");
        if (val.empty()) return jsonError("missing --value <N> (or --auto)");
        long long n = std::stoll(val);
        if (n < 1000) n = 1000;
        if (n > 512000000) n = 512000000; // device capture memory (shared)
        state.recordAuto = false;
        state.recordLength = static_cast<int>(n);
    }

    // Host-memory guesstimate so remote callers can size against RAM too.
    MemoryEstimate::Breakdown est = MemoryEstimate::estimate(state);
    uint64_t avail = MemoryEstimate::hostAvailableBytes();

    std::ostringstream ss;
    ss << "\"record_mode\":\"" << (state.recordAuto ? "auto" : "fixed") << "\""
       << ",\"record_length\":" << state.effectiveRecordLength()
       << ",\"bytes_estimate\":" << est.total()
       << ",\"host_free_bytes\":" << avail;
    if (avail > 0 && est.total() > avail)
        ss << ",\"warning\":\"estimate exceeds free host memory\"";
    return jsonOk(ss.str());
}

// A single resolved capture source (one column of time-domain data).
namespace {
enum class CapKind { Analog, DigitalAll, DigitalLane, Math };

struct CapSource {
    CapKind kind;
    int index;          // analog 0-3, or digital lane 0-15
    std::string label;  // "A".."D", "D", "D0".."D15", "MATH"
    bool isFloat;       // float column vs integer column
};

// Sample value for a source at index i, as a string (float or int).
std::string capValue(const CapSource& src, const SignalData& data,
                     const FrameResults& results, int i) {
    std::ostringstream v;
    switch (src.kind) {
        case CapKind::Analog:
            v << data.analog[src.index].samples[i];
            break;
        case CapKind::Math:
            v << (results.math ? results.math->samples[i] : 0.0f);
            break;
        case CapKind::DigitalLane:
            v << ((data.digital.samples[i] >> src.index) & 1);
            break;
        case CapKind::DigitalAll:
            v << static_cast<unsigned>(data.digital.samples[i]);
            break;
    }
    return v.str();
}
} // namespace

std::string RemoteServer::cmdCapture(const std::string& args, const ScopeState& state,
                                      const SignalData& data, const FrameResults& results) {
    auto params = parseArgs(args);
    std::string chArg = params.count("ch") ? params["ch"] : "";
    if (chArg.empty()) return jsonError("missing --ch");

    std::vector<std::string> tokens = splitList(chArg);
    if (tokens.empty()) return jsonError("missing --ch");

    std::string filePath = params.count("file") ? params["file"] : "";

    // --- VCD export (digital): --format vcd or a .vcd file extension ---
    bool wantVcd = (params.count("format") && toLower(params["format"]) == "vcd");
    if (!filePath.empty() && filePath.size() >= 4 &&
        toLower(filePath.substr(filePath.size() - 4)) == ".vcd")
        wantVcd = true;
    if (wantVcd) {
        if (filePath.empty()) return jsonError("VCD export requires --file <path.vcd>");
        if (data.digital.count == 0) return jsonError("no digital data captured");
        uint16_t laneMask = 0;
        for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++)
            if (state.digital[i].enabled) laneMask = static_cast<uint16_t>(laneMask | (1u << i));
        if (laneMask == 0) laneMask = 0xFFFF;
        int n = data.digital.count;
        if (params.count("samples")) {
            int req = std::stoi(params["samples"]);
            if (req > 0 && req < n) n = req;
        }
        if (!VcdExport::write(filePath, data.digital, data.sampleRate, laneMask, n))
            return jsonError("cannot write VCD file: " + filePath);
        std::ostringstream ss;
        ss << "\"file\":\"" << jsonEscape(filePath) << "\",\"format\":\"vcd\",\"samples\":" << n;
        return jsonOk(ss.str());
    }

    // --- Bus capture: capture --ch BUS0|BUS1 returns value transitions ---
    if (tokens.size() == 1) {
        std::string up = toLower(tokens[0]);
        if (up == "bus0" || up == "bus1") {
            int bi = (up == "bus1") ? 1 : 0;
            const BusConfig& bus = state.buses[bi];
            if (bus.lanes.empty())
                return jsonError("bus not configured (set-bus --index " +
                                 std::to_string(bi) + " --lanes ...)");
            if (data.digital.count == 0) return jsonError("no digital data captured");

            auto fmt = [&](uint32_t v) -> std::string {
                char buf[40];
                if (bus.display == 1) { // binary
                    std::string s = "0b";
                    int nbits = static_cast<int>(bus.lanes.size());
                    for (int b = nbits - 1; b >= 0; b--) s += ((v >> b) & 1) ? '1' : '0';
                    return s;
                } else if (bus.display == 2) { // decimal
                    snprintf(buf, sizeof(buf), "%u", v);
                } else { // hex
                    snprintf(buf, sizeof(buf), "0x%X", v);
                }
                return buf;
            };

            float dt = (data.sampleRate > 0) ? (1.0f / data.sampleRate) : 1.0f;
            int n = data.digital.count;

            // Collect transitions (value changes).
            struct Tr { int i; uint32_t v; };
            std::vector<Tr> trans;
            uint32_t prev = bus.value(data.digital.samples[0]);
            trans.push_back({ 0, prev });
            for (int i = 1; i < n; i++) {
                uint32_t v = bus.value(data.digital.samples[i]);
                if (v != prev) { trans.push_back({ i, v }); prev = v; }
            }

            if (!filePath.empty()) {
                std::ofstream out(filePath);
                if (!out.is_open()) return jsonError("cannot open file: " + filePath);
                out << "time_s,value\n";
                for (const auto& t : trans)
                    out << (t.i * dt) << ",\"" << fmt(t.v) << "\"\n";
                std::ostringstream ss;
                ss << "\"file\":\"" << jsonEscape(filePath) << "\",\"bus\":\""
                   << jsonEscape(bus.name) << "\",\"transitions\":" << trans.size();
                return jsonOk(ss.str());
            }

            std::ostringstream ss;
            ss << "\"bus\":\"" << jsonEscape(bus.name) << "\",\"display\":\""
               << busDisplayName(bus.display) << "\",\"transitions\":" << trans.size()
               << ",\"data\":[";
            for (size_t k = 0; k < trans.size(); k++) {
                if (k > 0) ss << ",";
                ss << "{\"t\":" << (trans[k].i * dt) << ",\"value\":\""
                   << fmt(trans[k].v) << "\"}";
            }
            ss << "]";
            return jsonOk(ss.str());
        }
    }

    // --- FFT is a frequency-domain source: only valid on its own ---
    bool wantsFft = false;
    for (auto& t : tokens) if (toLower(t) == "fft") wantsFft = true;
    if (wantsFft) {
        if (tokens.size() != 1)
            return jsonError("FFT cannot be combined with time-domain sources");
        if (!results.fft || !results.fft->valid)
            return jsonError("no FFT data (set math op to fft and let it acquire)");

        const FFTResult& f = *results.fft;
        if (!filePath.empty()) {
            std::ofstream out(filePath);
            if (!out.is_open()) return jsonError("cannot open file: " + filePath);
            out << "frequency_hz,magnitude_db\n";
            for (int i = 0; i < f.binCount; i++)
                out << (i * f.freqResolution) << "," << f.magnitudeDB[i] << "\n";
            std::ostringstream ss;
            ss << "\"file\":\"" << jsonEscape(filePath) << "\",\"bins\":" << f.binCount
               << ",\"bin_hz\":" << f.freqResolution
               << ",\"max_frequency\":" << f.maxFrequency;
            return jsonOk(ss.str());
        }
        int limit = (f.binCount > 10000) ? 10000 : f.binCount;
        if (params.count("samples")) {
            int req = std::stoi(params["samples"]);
            if (req > 0 && req < limit) limit = req;
        }
        std::ostringstream ss;
        ss << "\"bins\":" << limit << ",\"bin_hz\":" << f.freqResolution
           << ",\"max_frequency\":" << f.maxFrequency << ",\"data_db\":[";
        for (int i = 0; i < limit; i++) {
            if (i > 0) ss << ",";
            ss << f.magnitudeDB[i];
        }
        ss << "]";
        return jsonOk(ss.str());
    }

    // --- Resolve time-domain sources ---
    std::vector<CapSource> sources;
    int available = -1; // min sample count across sources

    for (auto& tok : tokens) {
        std::string up = tok;
        for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        CapSource src;
        if (up == "MATH") {
            if (!results.math || results.math->count == 0)
                return jsonError("math channel has no data (enable math first)");
            src = {CapKind::Math, 0, "MATH", true};
            available = (available < 0) ? results.math->count
                                        : (std::min)(available, results.math->count);
        } else if (up == "D" || up == "DIGITAL") {
            if (data.digital.count == 0) return jsonError("no digital data captured");
            src = {CapKind::DigitalAll, 0, "D", false};
            available = (available < 0) ? data.digital.count
                                        : (std::min)(available, data.digital.count);
        } else if (up.size() >= 2 && up[0] == 'D' &&
                   std::isdigit(static_cast<unsigned char>(up[1]))) {
            int lane = std::atoi(up.c_str() + 1);
            if (lane < 0 || lane >= NUM_DIGITAL_CHANNELS)
                return jsonError("invalid digital lane: " + tok + " (D0-D15)");
            if (data.digital.count == 0) return jsonError("no digital data captured");
            src = {CapKind::DigitalLane, lane, up, false};
            available = (available < 0) ? data.digital.count
                                        : (std::min)(available, data.digital.count);
        } else {
            int ch = parseChannelIndex(tok);
            if (ch < 0) return jsonError("invalid source: " + tok);
            if (!state.analog[ch].enabled)
                return jsonError(std::string("channel ") +
                                 static_cast<char>('A' + ch) + " not enabled");
            if (data.analog[ch].count == 0) return jsonError("no data captured");
            std::string lbl(1, static_cast<char>('A' + ch));
            src = {CapKind::Analog, ch, lbl, true};
            available = (available < 0) ? data.analog[ch].count
                                        : (std::min)(available, data.analog[ch].count);
        }
        sources.push_back(src);
    }

    if (available <= 0) return jsonError("no data captured");

    int samples = available;
    if (params.count("samples")) {
        int req = std::stoi(params["samples"]);
        if (req > 0 && req < samples) samples = req;
    }

    float dt = (data.sampleRate > 0) ? (1.0f / data.sampleRate) : 1.0f;

    // --- File (CSV) output: one time column plus one column per source ---
    if (!filePath.empty()) {
        std::ofstream out(filePath);
        if (!out.is_open()) return jsonError("cannot open file: " + filePath);

        out << "time_s";
        for (auto& s : sources) out << "," << s.label;
        out << "\n";
        for (int i = 0; i < samples; i++) {
            out << (i * dt);
            for (auto& s : sources) out << "," << capValue(s, data, results, i);
            out << "\n";
        }

        std::ostringstream ss;
        ss << "\"file\":\"" << jsonEscape(filePath) << "\""
           << ",\"samples\":" << samples
           << ",\"sample_rate\":" << data.sampleRate
           << ",\"sources\":" << sources.size();
        return jsonOk(ss.str());
    }

    // --- Inline JSON output (capped for sanity) ---
    int limit = (samples > 10000) ? 10000 : samples;
    std::ostringstream ss;
    ss << "\"samples\":" << limit << ",\"sample_rate\":" << data.sampleRate;

    if (sources.size() == 1) {
        // Backwards-compatible single-source shape: "data":[...]
        ss << ",\"source\":\"" << sources[0].label << "\",\"data\":[";
        for (int i = 0; i < limit; i++) {
            if (i > 0) ss << ",";
            ss << capValue(sources[0], data, results, i);
        }
        ss << "]";
    } else {
        ss << ",\"channels\":{";
        for (size_t s = 0; s < sources.size(); s++) {
            if (s > 0) ss << ",";
            ss << "\"" << sources[s].label << "\":[";
            for (int i = 0; i < limit; i++) {
                if (i > 0) ss << ",";
                ss << capValue(sources[s], data, results, i);
            }
            ss << "]";
        }
        ss << "}";
    }
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSigGen(const std::string& args, ScopeState& state,
                                     PicoSignalSource& picoSource) {
    if (!picoSource.isOpen())
        return jsonError("no PicoScope connected");

    auto params = parseArgs(args);

    // --off to disable
    if (params.count("off")) {
        picoSource.sigGenDisable();
        state.sigGen.enabled = false;
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

    // Update the reporting shadow so get-state reflects the generator.
    state.sigGen.enabled = true;
    state.sigGen.wave = waveStr;
    state.sigGen.frequency = freq;
    state.sigGen.amplitudeMv = amp;
    state.sigGen.offsetMv = offset;

    std::ostringstream ss;
    ss << "\"siggen\":\"on\",\"wave\":\"" << waveStr
       << "\",\"frequency\":" << freq
       << ",\"amplitude_mv\":" << amp
       << ",\"offset_mv\":" << offset;
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSetDigital(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);

    // Logic threshold: --threshold <V> [--group 0|1|all]
    // (Group 0 = D0-D7, group 1 = D8-D15. Named "group" not "port" because the
    //  CLI client reserves --port for the TCP server port.)
    if (params.count("threshold")) {
        float v = std::stof(params["threshold"]);
        if (v > 5.0f || v < -5.0f) return jsonError("threshold out of range (+/-5V)");
        std::string grp = params.count("group") ? toLower(params["group"]) : "all";
        if (grp == "0") state.digitalThreshold[0] = v;
        else if (grp == "1") state.digitalThreshold[1] = v;
        else if (grp == "all") { state.digitalThreshold[0] = v; state.digitalThreshold[1] = v; }
        else return jsonError("invalid --group (0|1|all)");
        std::ostringstream ss;
        ss << "\"threshold\":[" << state.digitalThreshold[0] << ","
           << state.digitalThreshold[1] << "]";
        return jsonOk(ss.str());
    }

    if (!params.count("ch")) return jsonError("missing --ch <0-15|all> or --threshold <V>");

    bool enable;
    if (params.count("enable")) enable = true;
    else if (params.count("disable")) enable = false;
    else return jsonError("specify --enable or --disable");

    std::string chStr = toLower(params["ch"]);
    if (chStr == "all") {
        for (auto& d : state.digital) d.enabled = enable;
        return jsonOk("\"digital\":\"all\"");
    }

    int lane = std::atoi(chStr.c_str());
    if (lane < 0 || lane >= NUM_DIGITAL_CHANNELS)
        return jsonError("invalid --ch (0-15|all)");
    state.digital[lane].enabled = enable;

    std::ostringstream ss;
    ss << "\"channel\":\"D" << lane << "\",\"enabled\":" << (enable ? "true" : "false");
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSetMath(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);

    if (params.count("enable")) state.mathChannel.enabled = true;
    if (params.count("disable")) state.mathChannel.enabled = false;

    if (params.count("op")) {
        std::string op = toLower(params["op"]);
        if (op == "add") state.mathChannel.op = MathOp::Add;
        else if (op == "sub" || op == "subtract") state.mathChannel.op = MathOp::Subtract;
        else if (op == "mul" || op == "multiply") state.mathChannel.op = MathOp::Multiply;
        else if (op == "div" || op == "divide") state.mathChannel.op = MathOp::Divide;
        else if (op == "fft") state.mathChannel.op = MathOp::FFT;
        else if (op == "ddt" || op == "derivative") state.mathChannel.op = MathOp::Derivative;
        else if (op == "integ" || op == "integral") state.mathChannel.op = MathOp::Integral;
        else if (op == "sqrt") state.mathChannel.op = MathOp::Sqrt;
        else return jsonError("invalid --op (add|sub|mul|div|fft|ddt|integ|sqrt)");
    }

    if (params.count("src1")) {
        int ch = parseChannelIndex(params["src1"]);
        if (ch < 0) return jsonError("invalid --src1 (A-D)");
        state.mathChannel.source1 = ch;
    }
    if (params.count("src2")) {
        int ch = parseChannelIndex(params["src2"]);
        if (ch < 0) return jsonError("invalid --src2 (A-D)");
        state.mathChannel.source2 = ch;
    }

    if (params.count("window")) {
        std::string w = toLower(params["window"]);
        if (w == "rect" || w == "rectangular") state.mathChannel.fftWindow = FFTWindowType::Rectangular;
        else if (w == "hanning") state.mathChannel.fftWindow = FFTWindowType::Hanning;
        else if (w == "hamming") state.mathChannel.fftWindow = FFTWindowType::Hamming;
        else if (w == "blackman" || w == "blackmanharris") state.mathChannel.fftWindow = FFTWindowType::BlackmanHarris;
        else if (w == "flattop" || w == "flat") state.mathChannel.fftWindow = FFTWindowType::FlatTop;
        else return jsonError("invalid --window (rect|hanning|hamming|blackman|flattop)");
    }

    std::ostringstream ss;
    ss << "\"enabled\":" << (state.mathChannel.enabled ? "true" : "false")
       << ",\"op\":\"" << mathOpName(state.mathChannel.op) << "\"";
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSetCursor(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);

    if (params.count("enable")) state.cursors.enabled = true;
    if (params.count("disable")) state.cursors.enabled = false;
    if (params.count("x1")) state.cursors.x1 = std::stof(params["x1"]);
    if (params.count("x2")) state.cursors.x2 = std::stof(params["x2"]);
    if (params.count("y1")) state.cursors.y1 = std::stof(params["y1"]);
    if (params.count("y2")) state.cursors.y2 = std::stof(params["y2"]);
    if (params.count("source")) {
        int ch = parseChannelIndex(params["source"]);
        if (ch < 0) return jsonError("invalid --source (A-D)");
        state.cursors.source = ch;
    }

    return cmdGetCursors(state);
}

std::string RemoteServer::cmdGetCursors(const ScopeState& state) {
    // Cursors are display-plane objects (divisions from grid center). Voltage
    // readouts are resolved through the cursor *source* channel: its V/div
    // scales deltas, its display offset shifts the absolute values (same as
    // its trace). Deltas are offset-independent.
    int src = state.cursors.source;
    if (src < 0 || src >= NUM_ANALOG_CHANNELS) src = 0;
    const ChannelState& ch = state.analog[src];

    float dxDiv = state.cursors.x2 - state.cursors.x1;
    float dyDiv = state.cursors.y2 - state.cursors.y1;
    float dt = dxDiv * state.timePerDiv();
    float freq = (dt != 0.0f) ? (1.0f / dt) : 0.0f;

    float offDiv = ch.verticalOffset / ch.voltsPerDiv();
    float y1v = (state.cursors.y1 - offDiv) * ch.voltsPerDiv();
    float y2v = (state.cursors.y2 - offDiv) * ch.voltsPerDiv();

    std::ostringstream ss;
    ss << "\"enabled\":" << (state.cursors.enabled ? "true" : "false")
       << ",\"source\":\"" << static_cast<char>('A' + src) << "\""
       << ",\"x1\":" << state.cursors.x1 << ",\"x2\":" << state.cursors.x2
       << ",\"y1\":" << state.cursors.y1 << ",\"y2\":" << state.cursors.y2
       << ",\"dx_div\":" << dxDiv << ",\"dy_div\":" << dyDiv
       << ",\"dt_s\":" << dt << ",\"freq_hz\":" << freq
       << ",\"y1_v\":" << y1v << ",\"y2_v\":" << y2v
       << ",\"dv_v\":" << (y2v - y1v);
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdListDevices() {
    auto devices = PicoSignalSource::enumerateDevices();
    std::ostringstream ss;
    ss << "\"devices\":[";
    for (size_t i = 0; i < devices.size(); i++) {
        if (i > 0) ss << ",";
        ss << "{\"serial\":\"" << devices[i].serial << "\""
           << ",\"description\":\"" << devices[i].description << "\"}";
    }
    ss << "],\"count\":" << devices.size();
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdConnect(const std::string& args, ScopeState& state,
                                      PicoSignalSource& picoSource) {
    auto params = parseArgs(args);

    if (picoSource.isRecording())
        return jsonError("stop recording before switching source");

    // Switch to the built-in demo/dummy source.
    if (params.count("demo")) {
        state.signalSource = SignalSourceType::Dummy;
        m_sourceSelected = true;
        return jsonOk("\"signal_source\":\"dummy\"");
    }

    std::string serial = params.count("serial") ? params["serial"] : "";
    if (!picoSource.isOpen()) {
        if (!picoSource.open(serial))
            return jsonError(picoSource.lastError().empty()
                             ? "failed to open PicoScope" : picoSource.lastError());
    }
    state.signalSource = SignalSourceType::PicoScope;
    m_sourceSelected = true;

    std::ostringstream ss;
    ss << "\"signal_source\":\"picoscope\",\"description\":\"" << picoSource.deviceInfo()
       << "\",\"serial\":\"" << picoSource.serial() << "\"";
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdDisconnect(ScopeState& state, PicoSignalSource& picoSource) {
    if (picoSource.isRecording())
        return jsonError("stop recording before disconnecting");
    if (picoSource.isOpen())
        picoSource.close();
    state.signalSource = SignalSourceType::Dummy;
    m_sourceSelected = true;
    return jsonOk("\"signal_source\":\"dummy\"");
}

std::string RemoteServer::cmdAutoscale(ScopeState& state, const SignalData& data) {
    int detected = AutoScale::apply(state, data);
    if (detected == 0)
        return jsonError("autoscale found no signal (is acquisition running?)");
    std::ostringstream ss;
    ss << "\"channels_detected\":" << detected
       << ",\"trigger_source\":\"" << static_cast<char>('A' + state.trigger.source) << "\""
       << ",\"time_per_div\":" << state.timePerDiv();
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSaveSetup(const std::string& args, const ScopeState& state) {
    auto params = parseArgs(args);
    std::string path = params.count("file") ? params["file"] : "";
    if (path.empty()) return jsonError("missing --file <path.json>");
    if (!SetupIO::save(path, state))
        return jsonError("cannot write setup file: " + path);
    return jsonOk("\"file\":\"" + jsonEscape(path) + "\"");
}

std::string RemoteServer::cmdRecallSetup(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    std::string path = params.count("file") ? params["file"] : "";
    if (path.empty()) return jsonError("missing --file <path.json>");
    if (!SetupIO::load(path, state))
        return jsonError("cannot read/parse setup file: " + path);
    return jsonOk("\"file\":\"" + jsonEscape(path) + "\"");
}

static const char* decodeProtocolName(int p) {
    switch (p) { case 1: return "i2c"; case 2: return "spi"; default: return "uart"; }
}

std::string RemoteServer::cmdSetDecode(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    DecodeConfig& d = state.decode;

    if (params.count("protocol")) {
        std::string p = toLower(params["protocol"]);
        if (p == "uart") d.protocol = 0;
        else if (p == "i2c") d.protocol = 1;
        else if (p == "spi") d.protocol = 2;
        else return jsonError("invalid --protocol (uart|i2c|spi)");
    }

    if (params.count("enable")) d.enabled = true;
    if (params.count("disable")) d.enabled = false;

    auto laneArg = [&](const char* k, int& dst) {
        if (params.count(k)) {
            int v = std::atoi(params[k].c_str());
            if (v >= 0 && v < NUM_DIGITAL_CHANNELS) dst = v;
        }
    };
    laneArg("lane", d.uartLane);
    laneArg("scl", d.i2cScl);
    laneArg("sda", d.i2cSda);
    laneArg("clk", d.spiClk);
    laneArg("mosi", d.spiMosi);
    laneArg("cs", d.spiCs);
    if (params.count("baud")) d.baud = std::stof(params["baud"]);
    if (params.count("cpol")) d.spiCpol = (params["cpol"] == "1") ? 1 : 0;
    if (params.count("cpha")) d.spiCpha = (params["cpha"] == "1") ? 1 : 0;

    std::ostringstream ss;
    ss << "\"enabled\":" << (d.enabled ? "true" : "false")
       << ",\"protocol\":\"" << decodeProtocolName(d.protocol) << "\"";
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdGetDecode(const std::string& args, const ScopeState& state,
                                        const SignalData& data) {
    auto params = parseArgs(args);
    const DecodeConfig& d = state.decode;

    if (data.digital.count == 0)
        return jsonError("no digital data captured");

    std::vector<SerialDecode::Frame> frames;
    if (d.protocol == 0) {
        SerialDecode::UartConfig c;
        c.lane = d.uartLane; c.baud = d.baud;
        frames = SerialDecode::decodeUart(data.digital, data.sampleRate, c);
    } else if (d.protocol == 1) {
        SerialDecode::I2cConfig c;
        c.sclLane = d.i2cScl; c.sdaLane = d.i2cSda;
        frames = SerialDecode::decodeI2c(data.digital, data.sampleRate, c);
    } else {
        SerialDecode::SpiConfig c;
        c.clkLane = d.spiClk; c.mosiLane = d.spiMosi; c.csLane = d.spiCs;
        c.cpol = d.spiCpol; c.cpha = d.spiCpha;
        frames = SerialDecode::decodeSpi(data.digital, data.sampleRate, c);
    }

    std::string filePath = params.count("file") ? params["file"] : "";
    if (!filePath.empty()) {
        std::ofstream f(filePath);
        if (!f.is_open()) return jsonError("cannot open file: " + filePath);
        f << "t_start_s,t_end_s,text,error\n";
        for (const auto& fr : frames)
            f << fr.tStart << "," << fr.tEnd << ",\"" << fr.text << "\","
              << (fr.error ? 1 : 0) << "\n";
        std::ostringstream ss;
        ss << "\"file\":\"" << jsonEscape(filePath) << "\",\"protocol\":\""
           << decodeProtocolName(d.protocol) << "\",\"frames\":" << frames.size();
        return jsonOk(ss.str());
    }

    std::ostringstream ss;
    ss << "\"protocol\":\"" << decodeProtocolName(d.protocol)
       << "\",\"frames\":" << frames.size() << ",\"data\":[";
    for (size_t i = 0; i < frames.size(); i++) {
        if (i > 0) ss << ",";
        ss << "{\"t_start\":" << frames[i].tStart
           << ",\"t_end\":" << frames[i].tEnd
           << ",\"text\":\"" << jsonEscape(frames[i].text) << "\""
           << ",\"error\":" << (frames[i].error ? "true" : "false") << "}";
    }
    ss << "]";
    return jsonOk(ss.str());
}

static const char* busDisplayName(int d) {
    switch (d) { case 1: return "bin"; case 2: return "dec"; default: return "hex"; }
}

std::string RemoteServer::cmdStartRecording(const std::string& args, const ScopeState& state,
                                             PicoSignalSource& picoSource) {
    // The PicoScope must be the *active* source — an open handle alone is not
    // enough (otherwise a demo-mode session would silently record the real
    // instrument in the background).
    if (state.signalSource != SignalSourceType::PicoScope || !picoSource.isOpen())
        return jsonError("recording requires the PicoScope source (use connect first)");

    auto params = parseArgs(args);
    std::string path = params.count("file") ? params["file"] : "";
    if (path.empty()) return jsonError("missing --file <path.bin>");
    if (!params.count("rate")) return jsonError("missing --rate <Hz>");
    float rate = std::stof(params["rate"]);

    uint16_t mask = 0; // 0 = all enabled channels
    if (params.count("ch")) {
        for (const std::string& tok : splitList(params["ch"])) {
            int ch = parseChannelIndex(tok);
            if (ch < 0) return jsonError("invalid channel: " + tok);
            mask = static_cast<uint16_t>(mask | (1u << ch));
        }
    }

    float viewWindow = state.timePerDiv() * GRID_DIVISIONS_X;
    std::string err;
    if (!picoSource.startRecording(path, rate, mask, viewWindow, err))
        return jsonError(err);

    auto st = picoSource.recordingStatus();
    std::ostringstream ss;
    ss << "\"file\":\"" << jsonEscape(st.file) << "\""
       << ",\"actual_rate\":" << st.actualRateHz;
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdStopRecording(PicoSignalSource& picoSource) {
    if (!picoSource.isRecording()) return jsonError("not recording");
    auto st = picoSource.recordingStatus(); // stats before teardown
    picoSource.stopRecording();
    std::ostringstream ss;
    ss << "\"file\":\"" << jsonEscape(st.file) << "\""
       << ",\"samples_per_channel\":" << st.samples
       << ",\"seconds\":" << st.seconds
       << ",\"bytes\":" << st.bytes
       << ",\"overflow_events\":" << st.overflowEvents
       << ",\"sidecar\":\"" << jsonEscape(st.file + ".json") << "\"";
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdRecordingStatus(const PicoSignalSource& picoSource) {
    auto st = picoSource.recordingStatus();
    std::ostringstream ss;
    ss << "\"active\":" << (st.active ? "true" : "false");
    if (st.active || st.samples > 0) {
        ss << ",\"file\":\"" << jsonEscape(st.file) << "\""
           << ",\"samples_per_channel\":" << st.samples
           << ",\"seconds\":" << st.seconds
           << ",\"bytes\":" << st.bytes
           << ",\"actual_rate\":" << st.actualRateHz
           << ",\"overflow_events\":" << st.overflowEvents;
    }
    return jsonOk(ss.str());
}

std::string RemoteServer::cmdSetBus(const std::string& args, ScopeState& state) {
    auto params = parseArgs(args);
    if (!params.count("index")) return jsonError("missing --index <0|1>");
    int idx = std::atoi(params["index"].c_str());
    if (idx < 0 || idx >= static_cast<int>(state.buses.size()))
        return jsonError("invalid --index (0|1)");
    BusConfig& bus = state.buses[idx];

    if (params.count("name")) bus.name = params["name"];

    if (params.count("lanes")) {
        std::vector<int> lanes;
        for (const std::string& tok : splitList(params["lanes"])) {
            int lane = std::atoi(tok.c_str());
            if (lane < 0 || lane >= NUM_DIGITAL_CHANNELS)
                return jsonError("invalid lane: " + tok + " (0-15)");
            lanes.push_back(lane);
        }
        bus.lanes = lanes;
    }

    if (params.count("display")) {
        std::string d = toLower(params["display"]);
        if (d == "hex") bus.display = 0;
        else if (d == "bin" || d == "binary") bus.display = 1;
        else if (d == "dec" || d == "decimal") bus.display = 2;
        else return jsonError("invalid --display (hex|bin|dec)");
    }

    if (params.count("enable")) bus.enabled = true;
    if (params.count("disable")) bus.enabled = false;

    std::ostringstream ss;
    ss << "\"index\":" << idx
       << ",\"enabled\":" << (bus.enabled ? "true" : "false")
       << ",\"name\":\"" << jsonEscape(bus.name) << "\""
       << ",\"lanes\":" << bus.lanes.size()
       << ",\"display\":\"" << busDisplayName(bus.display) << "\"";
    return jsonOk(ss.str());
}
