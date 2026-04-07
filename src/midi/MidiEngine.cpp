#include "midi/MidiEngine.h"
#include <rtmidi/RtMidi.h>

MidiEngine::MidiEngine() {
    try {
        m_midiIn = std::make_unique<RtMidiIn>();
    } catch (RtMidiError& error) {
        // RtMidi failed to initialize
        m_midiIn = nullptr;
    }
}

MidiEngine::~MidiEngine() {
    closePort();
}

std::vector<std::string> MidiEngine::listInputPorts() {
    std::vector<std::string> ports;
    if (!m_midiIn) return ports;

    try {
        unsigned int count = m_midiIn->getPortCount();
        for (unsigned int i = 0; i < count; i++) {
            ports.push_back(m_midiIn->getPortName(i));
        }
    } catch (RtMidiError&) {
        // ignore
    }
    m_cachedPorts = ports;
    return ports;
}

bool MidiEngine::openPort(int portIndex) {
    if (!m_midiIn) return false;

    closePort();

    try {
        unsigned int count = m_midiIn->getPortCount();
        if (portIndex < 0 || portIndex >= static_cast<int>(count))
            return false;

        m_midiIn->openPort(portIndex);
        m_midiIn->ignoreTypes(true, true, true); // ignore sysex, timing, active sensing
        m_portOpen = true;
        m_currentPortIndex = portIndex;
        m_currentPortName = m_midiIn->getPortName(portIndex);
        return true;
    } catch (RtMidiError&) {
        m_portOpen = false;
        return false;
    }
}

void MidiEngine::closePort() {
    if (m_midiIn && m_portOpen) {
        try {
            m_midiIn->closePort();
        } catch (RtMidiError&) {}
        m_portOpen = false;
        m_currentPortIndex = -1;
        m_currentPortName.clear();
    }
}

void MidiEngine::poll() {
    if (!m_midiIn || !m_portOpen || !m_callback) return;

    std::vector<unsigned char> message;
    while (true) {
        double stamp = m_midiIn->getMessage(&message);
        (void)stamp;
        if (message.empty()) break;

        if (message.size() >= 3) {
            MidiMessage msg;
            unsigned char status = message[0] & 0xF0;
            msg.channel = message[0] & 0x0F;
            msg.data1 = message[1];
            msg.data2 = message[2];

            switch (status) {
                case 0x90: msg.type = (msg.data2 > 0) ? MidiMessage::NoteOn : MidiMessage::NoteOff; break;
                case 0x80: msg.type = MidiMessage::NoteOff; break;
                case 0xB0: msg.type = MidiMessage::ControlChange; break;
                default:   msg.type = MidiMessage::Other; break;
            }

            m_callback(msg);
        }
    }
}

void MidiEngine::refreshPorts() {
    listInputPorts();
}
