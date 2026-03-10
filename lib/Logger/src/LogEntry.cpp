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

void LogEntry::toJSON(char* buf, uint16_t bufSize) const {
    // Example: {"t":1289,"l":1,"c":"LDC1101","m":"RP=42180, match=1UAH_2023"}
    snprintf(buf, bufSize,
             "{\"t\":%u,\"l\":%u,\"c\":\"%s\",\"m\":\"%s\"}\n",
             (unsigned int)timestampMs,
             (unsigned int)static_cast<uint8_t>(level),
             component,
             message);
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
