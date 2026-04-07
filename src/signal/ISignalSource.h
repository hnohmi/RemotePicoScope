#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include <string>

class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    virtual void configure(const ScopeState& state) = 0;
    virtual void acquire(SignalData& data) = 0;
    virtual std::string name() const = 0;
};
