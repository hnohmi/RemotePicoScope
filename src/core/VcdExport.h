#pragma once

#include "core/SignalBuffer.h"
#include "core/Types.h"
#include <fstream>
#include <string>
#include <array>

// Value Change Dump (VCD) export for digital captures. VCD is the standard
// interchange format for logic waveforms and loads directly into GTKWave and
// most HDL simulators — a natural fit for the FPGA verification workflow.
//
// Header-only (inline) to avoid a CMake source-list change.
namespace VcdExport {

// Write `count` samples of the digital buffer to a VCD file. Only the lanes
// whose bit is set in `laneMask` are emitted. Returns false on I/O error.
inline bool write(const std::string& path, const DigitalBuffer& buffer,
                  float sampleRate, uint16_t laneMask, int count) {
    if (count > buffer.count) count = buffer.count;
    if (count <= 0) return false;

    std::ofstream f(path);
    if (!f.is_open()) return false;

    // Each emitted lane gets a printable identifier char (ASCII 33..).
    std::array<char, NUM_DIGITAL_CHANNELS> id{};
    int nextId = 33;
    int emitted = 0;

    f << "$date RemotePicoScope capture $end\n";
    f << "$version RemotePicoScope VCD export $end\n";
    f << "$timescale 1ns $end\n";
    f << "$scope module digital $end\n";
    for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
        if (!(laneMask & (1u << lane))) continue;
        id[lane] = static_cast<char>(nextId++);
        f << "$var wire 1 " << id[lane] << " D" << lane << " $end\n";
        emitted++;
    }
    f << "$upscope $end\n";
    f << "$enddefinitions $end\n";

    if (emitted == 0) return false;

    double dtNs = (sampleRate > 0.0f) ? (1.0e9 / sampleRate) : 1.0;

    // Emit initial values at t=0, then only changes.
    uint16_t prev = 0;
    for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
        if (!(laneMask & (1u << lane))) continue;
        int bit = (buffer.samples[0] >> lane) & 1;
        prev |= static_cast<uint16_t>(bit << lane);
    }
    f << "#0\n";
    for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
        if (!(laneMask & (1u << lane))) continue;
        int bit = (buffer.samples[0] >> lane) & 1;
        f << bit << id[lane] << "\n";
    }

    for (int i = 1; i < count; i++) {
        uint16_t cur = buffer.samples[i];
        uint16_t changed = (cur ^ prev) & laneMask;
        if (!changed) continue;
        long long t = static_cast<long long>(i * dtNs + 0.5);
        f << "#" << t << "\n";
        for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
            if (!(changed & (1u << lane))) continue;
            int bit = (cur >> lane) & 1;
            f << bit << id[lane] << "\n";
        }
        prev = cur;
    }

    return true;
}

} // namespace VcdExport
