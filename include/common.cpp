#include "common.h"

std::string modeToString(Mode mode) {
    switch (mode) {
        case Mode::GAMING: return "Gaming";
        case Mode::PRODUCTIVITY: return "Productivity";
        case Mode::POWER_SAVING: return "PowerSaving";
        default: return "Unknown";
    }
}