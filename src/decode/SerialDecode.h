#pragma once

#include "core/SignalBuffer.h"
#include <vector>
#include <string>
#include <cstdio>

// Serial protocol decoders (UART, I2C, SPI). Each operates on a DigitalBuffer,
// the sample rate, and a lane/parameter config, and returns a flat list of
// decoded frames with absolute start/end times.
//
// Header-only (inline) to avoid a CMake source-list change.
namespace SerialDecode {

struct Frame {
    double tStart = 0.0; // seconds
    double tEnd = 0.0;
    std::string text;    // e.g. "0x48", "START", "ADDR 0x50 W ACK"
    bool error = false;  // framing/parity error, NAK, etc.
};

enum class Protocol { UART = 0, I2C = 1, SPI = 2 };

struct UartConfig {
    int lane = 0;
    float baud = 9600.0f;
    int dataBits = 8;
    bool lsbFirst = true;
    bool idleHigh = true;
};

struct I2cConfig {
    int sclLane = 1;
    int sdaLane = 2;
};

struct SpiConfig {
    int clkLane = 3;
    int mosiLane = 4;
    int csLane = 5;   // -1 = no chip-select (always active)
    int cpol = 0;
    int cpha = 0;
    int wordBits = 8;
    bool msbFirst = true;
};

inline int laneBit(const DigitalBuffer& b, int i, int lane) {
    return (b.samples[i] >> lane) & 1;
}

inline std::string hexByte(int v) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", v & 0xFF);
    return buf;
}

// ---------------- UART ----------------
inline std::vector<Frame> decodeUart(const DigitalBuffer& b, float sr, const UartConfig& cfg) {
    std::vector<Frame> out;
    if (b.count < 4 || sr <= 0.0f || cfg.baud <= 0.0f) return out;
    double spb = static_cast<double>(sr) / cfg.baud; // samples per bit
    if (spb < 1.0) return out;

    const int idle = cfg.idleHigh ? 1 : 0;
    auto bit = [&](int i) { return laneBit(b, i, cfg.lane); };

    int i = 1;
    while (i < b.count) {
        // Look for the leading edge of a start bit (idle -> !idle).
        if (bit(i) != idle && bit(i - 1) == idle) {
            double start = i;
            int value = 0;
            bool ok = true;
            for (int k = 0; k < cfg.dataBits; k++) {
                int si = static_cast<int>(start + (k + 1.5) * spb + 0.5);
                if (si >= b.count) { ok = false; break; }
                int bv = bit(si);
                if (cfg.lsbFirst) value |= (bv << k);
                else value = (value << 1) | bv;
            }
            if (!ok) break;

            // Stop bit should be idle level.
            int stopIdx = static_cast<int>(start + (cfg.dataBits + 1.5) * spb + 0.5);
            bool framingErr = (stopIdx < b.count) && (bit(stopIdx) != idle);

            Frame f;
            f.tStart = start / sr;
            f.tEnd = (start + (cfg.dataBits + 2) * spb) / sr;
            f.text = hexByte(value);
            f.error = framingErr;
            if (framingErr) f.text += " FE";
            out.push_back(f);

            i = static_cast<int>(start + (cfg.dataBits + 2) * spb);
        } else {
            i++;
        }
    }
    return out;
}

// ---------------- I2C ----------------
inline std::vector<Frame> decodeI2c(const DigitalBuffer& b, float sr, const I2cConfig& cfg) {
    std::vector<Frame> out;
    if (b.count < 4 || sr <= 0.0f) return out;
    auto scl = [&](int i) { return laneBit(b, i, cfg.sclLane); };
    auto sda = [&](int i) { return laneBit(b, i, cfg.sdaLane); };

    bool active = false;
    int value = 0, nbits = 0;
    bool addrPhase = false;
    double byteStart = 0.0;

    for (int i = 1; i < b.count; i++) {
        // START: SDA falls while SCL high.
        if (scl(i) == 1 && scl(i - 1) == 1 && sda(i) == 0 && sda(i - 1) == 1) {
            out.push_back({ i / (double)sr, i / (double)sr, "START", false });
            active = true; value = 0; nbits = 0; addrPhase = true;
            continue;
        }
        // STOP: SDA rises while SCL high.
        if (scl(i) == 1 && scl(i - 1) == 1 && sda(i) == 1 && sda(i - 1) == 0) {
            out.push_back({ i / (double)sr, i / (double)sr, "STOP", false });
            active = false; value = 0; nbits = 0;
            continue;
        }
        if (!active) continue;

        // Sample data on SCL rising edge.
        if (scl(i) == 1 && scl(i - 1) == 0) {
            if (nbits < 8) {
                if (nbits == 0) byteStart = i;
                value = (value << 1) | sda(i);
                nbits++;
            } else {
                // 9th clock = ACK/NAK.
                int nak = sda(i);
                Frame f;
                f.tStart = byteStart / (double)sr;
                f.tEnd = i / (double)sr;
                f.error = (nak != 0);
                char buf[48];
                if (addrPhase) {
                    snprintf(buf, sizeof(buf), "ADDR 0x%02X %s %s",
                             value >> 1, (value & 1) ? "R" : "W", nak ? "NAK" : "ACK");
                    addrPhase = false;
                } else {
                    snprintf(buf, sizeof(buf), "DATA 0x%02X %s", value, nak ? "NAK" : "ACK");
                }
                f.text = buf;
                out.push_back(f);
                value = 0; nbits = 0;
            }
        }
    }
    return out;
}

// ---------------- SPI ----------------
inline std::vector<Frame> decodeSpi(const DigitalBuffer& b, float sr, const SpiConfig& cfg) {
    std::vector<Frame> out;
    if (b.count < 4 || sr <= 0.0f) return out;
    auto lvl = [&](int i, int lane) { return laneBit(b, i, lane); };

    // Sampling edge: CPHA0 samples on the leading edge, CPHA1 on the trailing.
    // Leading edge is rising when CPOL0, falling when CPOL1.
    bool sampleRising = (cfg.cpha == 0) ? (cfg.cpol == 0) : (cfg.cpol != 0);

    int value = 0, nbits = 0;
    double byteStart = 0.0;

    for (int i = 1; i < b.count; i++) {
        bool active = (cfg.csLane < 0) || (lvl(i, cfg.csLane) == 0);
        if (!active) { value = 0; nbits = 0; continue; }

        int clk = lvl(i, cfg.clkLane), clkp = lvl(i - 1, cfg.clkLane);
        bool edge = sampleRising ? (clk == 1 && clkp == 0) : (clk == 0 && clkp == 1);
        if (!edge) continue;

        if (nbits == 0) byteStart = i;
        int mv = lvl(i, cfg.mosiLane);
        if (cfg.msbFirst) value = (value << 1) | mv;
        else value |= (mv << nbits);
        nbits++;

        if (nbits == cfg.wordBits) {
            Frame f;
            f.tStart = byteStart / (double)sr;
            f.tEnd = i / (double)sr;
            f.text = hexByte(value);
            out.push_back(f);
            value = 0; nbits = 0;
        }
    }
    return out;
}

} // namespace SerialDecode
