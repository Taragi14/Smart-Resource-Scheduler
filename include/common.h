#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include "types.h"

enum class Mode { GAMING, PRODUCTIVITY, POWER_SAVING };

std::string modeToString(Mode mode);

#endif