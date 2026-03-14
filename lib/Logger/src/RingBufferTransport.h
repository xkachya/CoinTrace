#pragma once
#include "ILogTransport.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class RingBufferTransport : public ILogTransport {
public:
    explicit RingBufferTransport(uint16_t capacity = 100,
                                  bool     usePsram  = false);  // ESP32-S3FN8: no external PSRAM (LA-1)
    ~RingBufferTransport();

    // Allocates buffer (PSRAM preferred) and creates internal mutex.
    bool        begin()  override;
    void        write(const LogEntry& entry) override;  // thread-safe, ~5 µs
    const char* getName() const override { return "RingBuffer"; }

    // === Read-side API (for UI, diagnostics, tests) ===

    // Thread-safe copy of entries oldest→newest into caller-provided buf.
    // Returns number of entries copied. Filters by minLevel.
    uint16_t getEntries(LogEntry* buf, uint16_t maxCount,
                        LogLevel  minLevel = LogLevel::DEBUG) const;

    uint16_t getCount() const;
    void     clear();

    // Returns the most-recent ERROR or FATAL entry (for status bar display).
    // Returns false if none found.
    bool getLastError(LogEntry& out) const;

    // КОНТРАКТ: write() НЕ ВИКЛИКУЄ Logger::*

private:
    LogEntry*         buffer_   = nullptr;
    uint16_t          capacity_ = 0;
    uint16_t          head_     = 0;    // next write slot (oldest when buffer full)
    uint16_t          count_    = 0;    // current fill (≤ capacity_)
    bool              usePsram_ = true;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};
