#pragma once

// RemotePicoScope version numbers.
//
// Three independent version strings are tracked:
//   APP_VERSION          - the GUI application (RemotePicoScope.exe)
//   CLI_VERSION          - the command-line client (picoscope-cli.exe)
//   CLI_PROTOCOL_VERSION - the TCP line protocol spoken between them
//
// The protocol version follows semver-ish rules: bump the minor for
// backwards-compatible additions, bump the major for breaking changes.
// Both the app and the CLI advertise which protocol version they support
// so a mismatch can be detected and reported to the user.

namespace Version {
    constexpr const char* APP          = "1.1.0";
    constexpr const char* CLI          = "1.1.0";
    constexpr const char* CLI_PROTOCOL = "1.1";
}
