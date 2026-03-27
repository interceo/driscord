#pragma once

#include "driscord_core.hpp"

// Global singleton: owns the DriscordCore instance.
struct DriscordState {
    DriscordCore core;
    static DriscordState& get();
};
