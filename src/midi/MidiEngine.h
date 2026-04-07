#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

struct MidiMessage {
    enum Type { NoteOn, NoteOff, ControlChange, Other };
    Type type = Other;
    int channel = 0;
    int data1 = 0; // note or CC number
    int data2 = 0; // velocity or CC value
};

class RtMidiIn;

class MidiEngine {
public:
    using Callback = std::function<void(const MidiMessage&)>;

    MidiEngine();
    ~MidiEngine();

    // Device management
    std::vector<std::string> listInputPorts();
    bool openPort(int portIndex);
    void closePort();
    bool isOpen() const { return m_portOpen; }
    std::string currentPortName() const { return m_currentPortName; }
    int currentPortIndex() const { return m_currentPortIndex; }

    // Poll for messages (call from main loop)
    void poll();

    // Register callback for incoming messages
    void setCallback(Callback cb) { m_callback = cb; }

    // Auto-refresh port list periodically
    void refreshPorts();

private:
    std::unique_ptr<RtMidiIn> m_midiIn;
    Callback m_callback;
    bool m_portOpen = false;
    std::string m_currentPortName;
    int m_currentPortIndex = -1;
    std::vector<std::string> m_cachedPorts;
};
