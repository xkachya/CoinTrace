#include "LogEntry.h"
#include <stdio.h>
#include <string.h>

const char* LogEntry::levelToString(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO ";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "?????";
    }
}

const char* LogEntry::levelToChar(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG:   return "D";
        case LogLevel::INFO:    return "I";
        case LogLevel::WARNING: return "W";
        case LogLevel::ERROR:   return "E";
        case LogLevel::FATAL:   return "F";
        default:                return "?";
    }
}

void LogEntry::toText(char* buf, uint16_t bufSize) const {
    // Example: [     0ms] INFO  System         | CoinTrace v1.0.0 starting
    snprintf(buf, bufSize,
             "[%6ums] %-5s %-15s| %s\n",
             (unsigned int)timestampMs,
             levelToString(level),
             component,
             message);
}

// LA-3: Escape JSON special chars to prevent injection via log messages.
// Only " and \ need escaping for minimal valid JSON string content.
static void jsonEscape(const char* src, char* dst, size_t dstLen) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstLen; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j + 2 >= dstLen) break;
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

void LogEntry::toJSON(char* buf, uint16_t bufSize) const {
    // Example: {"t":1289,"l":1,"c":"LDC1101","m":"RP=42180, match=1UAH_2023"}
    // LA-3: escape component and message to prevent JSON injection
    char ec[sizeof(component) * 2 + 1];  // 20 × 2 + 1 = 41 bytes
    char em[sizeof(message)  * 2 + 1];  // 192 × 2 + 1 = 385 bytes
    jsonEscape(component, ec, sizeof(ec));
    jsonEscape(message,   em, sizeof(em));
    snprintf(buf, bufSize,
             "{\"t\":%u,\"l\":%u,\"c\":\"%s\",\"m\":\"%s\"}\n",
             (unsigned int)timestampMs,
             (unsigned int)static_cast<uint8_t>(level),
             ec,
             em);
}

void LogEntry::toBLECompact(char* buf, uint16_t bufSize) const {
    // Example: I|1289|CoinAnalyzer|RP=42180, match=1UAH_2023
    snprintf(buf, bufSize,
             "%s|%u|%s|%s",
             levelToChar(level),
             (unsigned int)timestampMs,
             component,
             message);
}
