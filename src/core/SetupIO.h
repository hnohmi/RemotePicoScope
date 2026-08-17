#pragma once

#include "core/ScopeState.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <algorithm>

// Serialize/deserialize instrument setup (ScopeState configuration) to JSON.
// Used for save-setup/recall-setup commands and last-session persistence.
//
// Only configuration is stored — not runtime status (run mode, trigger status)
// or the active signal source, so recalling a setup never switches hardware.
// Header-only (inline) to avoid a CMake source-list change.
namespace SetupIO {

inline int clampIndex(int v, int count) {
    return (v < 0) ? 0 : (v >= count ? count - 1 : v);
}

inline nlohmann::json toJson(const ScopeState& s) {
    using nlohmann::json;
    json j;
    j["version"] = 1;

    json analog = json::array();
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        const ChannelState& c = s.analog[i];
        analog.push_back({
            {"enabled", c.enabled},
            {"volts_per_div_index", c.voltsPerDivIndex},
            {"vertical_offset", c.verticalOffset},
            {"coupling", static_cast<int>(c.coupling)},
            {"bandwidth_limit", c.bandwidthLimit},
            {"probe", c.probeAttenuation},
            {"invert", c.invert},
            {"label", c.label},
        });
    }
    j["analog"] = analog;

    json digital = json::array();
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++)
        digital.push_back(s.digital[i].enabled);
    j["digital_enabled"] = digital;
    j["digital_threshold"] = { s.digitalThreshold[0], s.digitalThreshold[1] };

    j["time_per_div_index"] = s.timePerDivIndex;
    j["horizontal_offset"] = s.horizontalOffset;
    j["record_length"] = s.recordLength;
    j["record_auto"] = s.recordAuto;

    j["trigger"] = {
        {"type", static_cast<int>(s.trigger.type)},
        {"source", s.trigger.source},
        {"level", s.trigger.level},
        {"edge", static_cast<int>(s.trigger.edge)},
        {"mode", static_cast<int>(s.trigger.mode)},
        {"digital_source", s.trigger.digitalSource},
        {"digital_pattern", s.trigger.digitalPattern},
    };

    j["math"] = {
        {"enabled", s.mathChannel.enabled},
        {"op", static_cast<int>(s.mathChannel.op)},
        {"source1", s.mathChannel.source1},
        {"source2", s.mathChannel.source2},
        {"fft_window", static_cast<int>(s.mathChannel.fftWindow)},
    };

    j["cursors"] = {
        {"enabled", s.cursors.enabled},
        {"x1", s.cursors.x1}, {"x2", s.cursors.x2},
        {"y1", s.cursors.y1}, {"y2", s.cursors.y2},
        {"source", s.cursors.source},
    };

    j["decode"] = {
        {"enabled", s.decode.enabled},
        {"protocol", s.decode.protocol},
        {"uart_lane", s.decode.uartLane},
        {"baud", s.decode.baud},
        {"i2c_scl", s.decode.i2cScl},
        {"i2c_sda", s.decode.i2cSda},
        {"spi_clk", s.decode.spiClk},
        {"spi_mosi", s.decode.spiMosi},
        {"spi_cs", s.decode.spiCs},
        {"spi_cpol", s.decode.spiCpol},
        {"spi_cpha", s.decode.spiCpha},
    };

    json buses = json::array();
    for (const BusConfig& bus : s.buses) {
        buses.push_back({
            {"enabled", bus.enabled},
            {"name", bus.name},
            {"display", bus.display},
            {"lanes", bus.lanes},
        });
    }
    j["buses"] = buses;

    return j;
}

inline void fromJson(const nlohmann::json& j, ScopeState& s) {
    using nlohmann::json;

    if (j.contains("analog") && j["analog"].is_array()) {
        const auto& analog = j["analog"];
        for (int i = 0; i < NUM_ANALOG_CHANNELS && i < static_cast<int>(analog.size()); i++) {
            const auto& c = analog[i];
            ChannelState& cs = s.analog[i];
            cs.enabled = c.value("enabled", cs.enabled);
            cs.voltsPerDivIndex = clampIndex(
                c.value("volts_per_div_index", cs.voltsPerDivIndex),
                Sequence125::VOLTS_PER_DIV_COUNT);
            cs.verticalOffset = c.value("vertical_offset", cs.verticalOffset);
            cs.coupling = static_cast<Coupling>(c.value("coupling", static_cast<int>(cs.coupling)));
            cs.bandwidthLimit = c.value("bandwidth_limit", cs.bandwidthLimit);
            cs.probeAttenuation = c.value("probe", cs.probeAttenuation);
            cs.invert = c.value("invert", cs.invert);
            cs.label = c.value("label", cs.label);
        }
    }

    if (j.contains("digital_enabled") && j["digital_enabled"].is_array()) {
        const auto& digital = j["digital_enabled"];
        for (int i = 0; i < NUM_DIGITAL_CHANNELS && i < static_cast<int>(digital.size()); i++)
            s.digital[i].enabled = digital[i].get<bool>();
    }
    if (j.contains("digital_threshold") && j["digital_threshold"].is_array()) {
        const auto& t = j["digital_threshold"];
        for (int i = 0; i < 2 && i < static_cast<int>(t.size()); i++)
            s.digitalThreshold[i] = t[i].get<float>();
    }

    s.timePerDivIndex = clampIndex(
        j.value("time_per_div_index", s.timePerDivIndex), Sequence125::TIME_PER_DIV_COUNT);
    s.horizontalOffset = j.value("horizontal_offset", s.horizontalOffset);
    s.recordLength = j.value("record_length", s.recordLength);
    s.recordAuto = j.value("record_auto", s.recordAuto);

    if (j.contains("trigger")) {
        const auto& t = j["trigger"];
        s.trigger.type = static_cast<TriggerType>(t.value("type", static_cast<int>(s.trigger.type)));
        s.trigger.source = t.value("source", s.trigger.source);
        s.trigger.level = t.value("level", s.trigger.level);
        s.trigger.edge = static_cast<TriggerEdge>(t.value("edge", static_cast<int>(s.trigger.edge)));
        s.trigger.mode = static_cast<TriggerMode>(t.value("mode", static_cast<int>(s.trigger.mode)));
        s.trigger.digitalSource = t.value("digital_source", s.trigger.digitalSource);
        if (t.contains("digital_pattern") && t["digital_pattern"].is_array()) {
            const auto& dp = t["digital_pattern"];
            for (int i = 0; i < NUM_DIGITAL_CHANNELS && i < static_cast<int>(dp.size()); i++)
                s.trigger.digitalPattern[i] = dp[i].get<int>();
        }
    }

    if (j.contains("math")) {
        const auto& m = j["math"];
        s.mathChannel.enabled = m.value("enabled", s.mathChannel.enabled);
        s.mathChannel.op = static_cast<MathOp>(m.value("op", static_cast<int>(s.mathChannel.op)));
        s.mathChannel.source1 = m.value("source1", s.mathChannel.source1);
        s.mathChannel.source2 = m.value("source2", s.mathChannel.source2);
        s.mathChannel.fftWindow = static_cast<FFTWindowType>(
            m.value("fft_window", static_cast<int>(s.mathChannel.fftWindow)));
    }

    if (j.contains("cursors")) {
        const auto& c = j["cursors"];
        s.cursors.enabled = c.value("enabled", s.cursors.enabled);
        s.cursors.x1 = c.value("x1", s.cursors.x1);
        s.cursors.x2 = c.value("x2", s.cursors.x2);
        s.cursors.y1 = c.value("y1", s.cursors.y1);
        s.cursors.y2 = c.value("y2", s.cursors.y2);
        s.cursors.source = c.value("source", s.cursors.source);
    }

    if (j.contains("decode")) {
        const auto& d = j["decode"];
        s.decode.enabled = d.value("enabled", s.decode.enabled);
        s.decode.protocol = d.value("protocol", s.decode.protocol);
        s.decode.uartLane = d.value("uart_lane", s.decode.uartLane);
        s.decode.baud = d.value("baud", s.decode.baud);
        s.decode.i2cScl = d.value("i2c_scl", s.decode.i2cScl);
        s.decode.i2cSda = d.value("i2c_sda", s.decode.i2cSda);
        s.decode.spiClk = d.value("spi_clk", s.decode.spiClk);
        s.decode.spiMosi = d.value("spi_mosi", s.decode.spiMosi);
        s.decode.spiCs = d.value("spi_cs", s.decode.spiCs);
        s.decode.spiCpol = d.value("spi_cpol", s.decode.spiCpol);
        s.decode.spiCpha = d.value("spi_cpha", s.decode.spiCpha);
    }

    if (j.contains("buses") && j["buses"].is_array()) {
        const auto& buses = j["buses"];
        for (int i = 0; i < static_cast<int>(s.buses.size()) &&
                        i < static_cast<int>(buses.size()); i++) {
            const auto& b = buses[i];
            s.buses[i].enabled = b.value("enabled", s.buses[i].enabled);
            s.buses[i].name = b.value("name", s.buses[i].name);
            s.buses[i].display = b.value("display", s.buses[i].display);
            if (b.contains("lanes") && b["lanes"].is_array())
                s.buses[i].lanes = b["lanes"].get<std::vector<int>>();
        }
    }
}

// Returns false on I/O error. Never throws.
inline bool save(const std::string& path, const ScopeState& s) {
    try {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << toJson(s).dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool load(const std::string& path, ScopeState& s) {
    try {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        nlohmann::json j;
        f >> j;
        fromJson(j, s);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace SetupIO
