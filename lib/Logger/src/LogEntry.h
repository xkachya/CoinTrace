#pragma once
#include <stdint.h>
#include "LogLevel.h"

// Fixed-size struct — no heap allocation in RingBuffer.
// Size: 4 + 1 + 20 + 192 = 217 bytes.
// RingBufferTransport(100) = ~21 KB — use PSRAM on ESP32-S3.
struct LogEntry {
    uint32_t timestampMs;
    LogLevel level;
    char     component[20];  // plugin/module name, max 19 chars + null
    char     message[192];   // formatted message, truncated with "..." if longer

    // Serialization — called by transports inside dispatch loop (no Logger::* allowed)
    void toText      (char* buf, uint16_t bufSize) const;  // [  1289ms] INFO  LDC1101         | msg
    void toJSON      (char* buf, uint16_t bufSize) const;  // {"t":1289,"l":1,"c":"LDC1101","m":"msg"}
    void toBLECompact(char* buf, uint16_t bufSize) const;  // I|1289|LDC1101|msg

    static const char* levelToString(LogLevel l);  // "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
    static const char* levelToChar  (LogLevel l);  // "D", "I", "W", "E", "F"
};
