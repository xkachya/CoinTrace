#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Controllable clock for deterministic timestamp tests.
// Reset to 0 at the start of each test suite if needed.
extern uint32_t g_mock_millis;
inline uint32_t millis() { return g_mock_millis; }

// No PSRAM on native — ps_malloc falls back to malloc
inline bool  psramFound()         { return false; }
inline void* ps_malloc(size_t sz) { return malloc(sz); }
