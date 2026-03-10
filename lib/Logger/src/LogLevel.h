#pragma once
#include <stdint.h>

enum class LogLevel : uint8_t {
    DEBUG   = 0,
    INFO    = 1,
    WARNING = 2,
    ERROR   = 3,
    FATAL   = 4
};
