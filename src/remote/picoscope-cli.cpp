// picoscope-cli — Command-line client for the PicoScope remote control server.
// Connects to localhost:5575 (or --port N), sends a command, prints the JSON response.
//
// Usage:
//   picoscope-cli help
//   picoscope-cli get-state
//   picoscope-cli set-channel --ch 1 --enabled true --range 2.0
//   picoscope-cli run
//   picoscope-cli capture --ch 1 --count 1000
//   picoscope-cli measure --ch 1 --type frequency
//   picoscope-cli siggen --wave sine --freq 1000 --amp 1000

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Version.h"

static void printUsage() {
    fprintf(stderr,
        "picoscope-cli — Remote control for RemotePicoScope\n"
        "\n"
        "Usage: picoscope-cli [--port PORT] <command> [args...]\n"
        "\n"
        "Options:\n"
        "  --port PORT   Server port (default: 5575)\n"
        "  --version     Print client and protocol version and exit\n"
        "\n"
        "Commands:\n"
        "  help                              List available commands (from server)\n"
        "  get-state                         Get full oscilloscope state\n"
        "  set-channel --ch A-D [--enable|--disable] [--range V] [--coupling DC|AC|GND]\n"
        "                                    [--offset V] [--bwlimit on|off]\n"
        "  set-timebase --value S [--offset S]\n"
        "  set-trigger --source A-D --level V [--edge rising|falling] [--mode auto|normal|single]\n"
        "  set-digital --ch 0-15|all --enable|--disable\n"
        "  set-math [--enable|--disable] [--op add|sub|mul|div|fft|ddt|integ|sqrt]\n"
        "                                    [--src1 A-D] [--src2 A-D] [--window NAME]\n"
        "  set-cursor [--enable|--disable] [--x1 D] [--x2 D] [--y1 D] [--y2 D]\n"
        "  get-cursors                       Cursor positions + deltas (dt, 1/dt)\n"
        "  run | stop | single               Acquisition control\n"
        "  measure --ch A-D [--type TYPE]    Take measurement\n"
        "  set-record-length --value N       Set record length (1000-50M)\n"
        "  capture --ch SRC[,SRC...] [--samples N] [--file PATH]\n"
        "                                    SRC = A-D | D | D0-D15 | MATH | FFT\n"
        "  siggen --wave TYPE --freq HZ --amplitude MV [--offset MV] | --off\n"
        "  list-devices                      Enumerate connected PicoScopes\n"
        "  connect [--serial SN] [--demo]    Connect hardware or demo source\n"
        "  disconnect                        Close device, revert to demo\n"
        "\n"
        "The server runs inside the RemotePicoScope GUI application.\n"
    );
}

int main(int argc, char* argv[]) {
    // Parse --port and collect remaining args as the command line
    int port = 5575;
    std::vector<std::string> cmdArgs;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--version" || arg == "-V") {
            printf("picoscope-cli %s (protocol %s)\n",
                   Version::CLI, Version::CLI_PROTOCOL);
            return 0;
        } else {
            cmdArgs.push_back(arg);
        }
    }

    if (cmdArgs.empty()) {
        printUsage();
        return 1;
    }

    // Build the command string (single line to send to server)
    std::string command;
    for (size_t i = 0; i < cmdArgs.size(); i++) {
        if (i > 0) command += ' ';
        command += cmdArgs[i];
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Error: WSAStartup failed\n");
        return 1;
    }

    // Connect to server
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Error: Cannot create socket\n");
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Error: Cannot connect to localhost:%d — is the PicoScope app running?\n", port);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Send command + newline
    command += '\n';
    int sent = send(sock, command.c_str(), static_cast<int>(command.size()), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "Error: Failed to send command\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Read response until newline or connection close
    std::string response;
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, n);
        // Response is newline-terminated JSON
        if (response.find('\n') != std::string::npos)
            break;
    }

    closesocket(sock);
    WSACleanup();

    // Print response (trim trailing newline for cleaner output)
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r'))
        response.pop_back();

    if (!response.empty()) {
        printf("%s\n", response.c_str());
    }

    // Return non-zero only on a top-level error status (not on data fields
    // that merely contain an "error" key, e.g. decode frames).
    if (response.find("\"status\":\"error\"") != std::string::npos)
        return 1;

    return 0;
}
