#pragma once

#include "signal/PicoSignalSource.h"

class SigGenPanel {
public:
    void draw(PicoSignalSource& picoSource);

private:
    bool m_enabled = false;
    int m_waveType = 0;           // index into wave names
    float m_frequencyHz = 1000.0f;
    float m_amplitudeMv = 2000.0f; // peak-to-peak
    float m_offsetMv = 0.0f;
    bool m_needsApply = true;      // apply on first enable
};
