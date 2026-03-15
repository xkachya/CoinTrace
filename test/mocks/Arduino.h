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

// ESP32 log macros — silent no-ops on native (tests verify logic, not log output).
// Using GNU-style ##__VA_ARGS__ to support zero-argument calls like log_e("msg").
#ifndef log_e
#define log_e(fmt, ...) do {} while(0)
#define log_w(fmt, ...) do {} while(0)
#define log_i(fmt, ...) do {} while(0)
#define log_d(fmt, ...) do {} while(0)
#define log_v(fmt, ...) do {} while(0)
#endif

// strlcpy: POSIX extension — not available in all native C standard libraries.
// Provided here so ESP32 code using strlcpy() compiles cleanly on native.
#ifndef strlcpy
inline size_t strlcpy(char* dst, const char* src, size_t dstsize) {
    size_t srclen = strlen(src);
    if (dstsize > 0) {
        size_t copylen = (srclen < dstsize - 1u) ? srclen : dstsize - 1u;
        memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}
#endif

// Arduino String class — minimal stub using std::string.
// NVSManager returns String from getDevName()/getLang(); only constructors
// and basic string storage are needed for native compilation.
#include <string>
class String : public std::string {
public:
    String() = default;
    String(const char* s) : std::string(s ? s : "") {}
    String(std::string s) : std::string(std::move(s)) {}
    const char* c_str() const { return std::string::c_str(); }
};
