#pragma once

#include "core/ScopeState.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <cstdio>

// Host-memory guesstimate for a given record length, so the user can size
// captures against available RAM (the 3406D offers up to 512 MS of shared
// capture memory — far more than the old 50 M app cap).
//
// Accounting (per sample of record length n):
//   - SignalData float buffers: 4 B x NUM_ANALOG_CHANNELS (all allocated,
//     enabled or not — SignalData::resize sizes every channel)
//   - SignalData combined digital buffer: 2 B (always allocated)
//   - driver ADC int16 buffers: 2 B per *enabled* analog channel (hw only)
//   - driver digital port buffers: 2 B per enabled port
//   - math output buffer: 4 B when math is enabled
namespace MemoryEstimate {

struct Breakdown {
    uint64_t analogFloat = 0; // display/CLI float buffers
    uint64_t analogAdc = 0;   // driver int16 buffers
    uint64_t digitalBuf = 0;  // combined uint16 + driver port buffers
    uint64_t staging = 0;     // chunked-retrieval staging (enabled ch + digital)
    uint64_t mathBuf = 0;
    uint64_t total() const {
        return analogFloat + analogAdc + digitalBuf + staging + mathBuf;
    }
};

inline Breakdown estimate(const ScopeState& s) {
    Breakdown b;
    uint64_t n = static_cast<uint64_t>(s.effectiveRecordLength());

    int enabledCh = 0;
    for (const auto& ch : s.analog)
        if (ch.enabled) enabledCh++;

    bool port0 = false, port1 = false;
    for (int i = 0; i < 8; i++) if (s.digital[i].enabled) port0 = true;
    for (int i = 8; i < NUM_DIGITAL_CHANNELS; i++) if (s.digital[i].enabled) port1 = true;

    b.analogFloat = n * 4ull * NUM_ANALOG_CHANNELS;
    b.analogAdc = n * 2ull * enabledCh;
    b.digitalBuf = n * 2ull; // combined buffer, always allocated
    b.digitalBuf += n * 2ull * ((port0 ? 1 : 0) + (port1 ? 1 : 0));
    // Chunked hardware retrieval builds into a staging copy before the
    // atomic swap into the live buffers.
    b.staging = n * 4ull * enabledCh;
    if (port0 || port1) b.staging += n * 2ull;
    if (s.mathChannel.enabled) b.mathBuf = n * 4ull;
    return b;
}

inline uint64_t hostAvailableBytes() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return ms.ullAvailPhys;
    return 0;
}

inline std::string formatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ull << 30))
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / (double)(1ull << 30));
    else if (bytes >= (1ull << 20))
        snprintf(buf, sizeof(buf), "%.0f MB", bytes / (double)(1ull << 20));
    else
        snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    return buf;
}

inline std::string formatCount(int64_t n) {
    char buf[32];
    if (n >= 1000000 && n % 1000000 == 0)
        snprintf(buf, sizeof(buf), "%lldM", static_cast<long long>(n / 1000000));
    else if (n >= 1000 && n % 1000 == 0)
        snprintf(buf, sizeof(buf), "%lldk", static_cast<long long>(n / 1000));
    else
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
    return buf;
}

} // namespace MemoryEstimate
