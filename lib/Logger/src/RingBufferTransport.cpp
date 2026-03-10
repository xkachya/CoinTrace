#include "RingBufferTransport.h"
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>

RingBufferTransport::RingBufferTransport(uint16_t capacity, bool usePsram)
    : capacity_(capacity), usePsram_(usePsram) {}

RingBufferTransport::~RingBufferTransport() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (buffer_) {
        free(buffer_);
        buffer_ = nullptr;
    }
}

bool RingBufferTransport::begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return false;

    // Спробувати PSRAM (8 MB на M5Cardputer-Adv), fallback на heap.
    // capacity=100, sizeof(LogEntry)=217 → ~21 KB — добре поміщається в PSRAM.
    if (usePsram_ && psramFound()) {
        buffer_ = static_cast<LogEntry*>(ps_malloc(capacity_ * sizeof(LogEntry)));
    }
    if (!buffer_) {
        buffer_ = static_cast<LogEntry*>(malloc(capacity_ * sizeof(LogEntry)));
    }
    if (!buffer_) {
        // Graceful degradation: транспорт активний, але write() ігнорується.
        capacity_ = 0;
        return true;
    }

    memset(buffer_, 0, capacity_ * sizeof(LogEntry));
    return true;
}

void RingBufferTransport::write(const LogEntry& entry) {
    // КОНТРАКТ: НЕ ВИКЛИКУЄМО Logger::*
    if (!buffer_ || !mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;

    buffer_[head_] = entry;
    head_  = static_cast<uint16_t>((head_ + 1) % capacity_);
    if (count_ < capacity_) count_++;

    xSemaphoreGive(mutex_);
}

uint16_t RingBufferTransport::getCount() const { return count_; }

void RingBufferTransport::clear() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;

    head_  = 0;
    count_ = 0;
    if (buffer_) memset(buffer_, 0, capacity_ * sizeof(LogEntry));

    xSemaphoreGive(mutex_);
}

uint16_t RingBufferTransport::getEntries(LogEntry* buf, uint16_t maxCount,
                                          LogLevel  minLevel) const {
    if (!buffer_ || !mutex_ || !buf || maxCount == 0) return 0;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return 0;

    uint16_t copied = 0;
    // start = oldest entry: index 0 if buffer not yet full, head_ if full (wrap).
    uint16_t start = (count_ < capacity_) ? 0 : head_;

    for (uint16_t i = 0; i < count_ && copied < maxCount; i++) {
        uint16_t idx = static_cast<uint16_t>((start + i) % capacity_);
        if (buffer_[idx].level >= minLevel) {
            buf[copied++] = buffer_[idx];
        }
    }

    xSemaphoreGive(mutex_);
    return copied;
}

bool RingBufferTransport::getLastError(LogEntry& out) const {
    if (!buffer_ || !mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;

    bool     found = false;
    uint16_t start = (count_ < capacity_) ? 0 : head_;

    // Ітерація від найстарішого до найновішого.
    // Останній знайдений ERROR/FATAL = найсвіжіший (перезаписуємо out).
    for (uint16_t i = 0; i < count_; i++) {
        uint16_t idx = static_cast<uint16_t>((start + i) % capacity_);
        if (buffer_[idx].level >= LogLevel::ERROR) {
            out   = buffer_[idx];
            found = true;
        }
    }

    xSemaphoreGive(mutex_);
    return found;
}
