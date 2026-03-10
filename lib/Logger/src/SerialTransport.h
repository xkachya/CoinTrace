#pragma once
#include "ILogTransport.h"
#include <Print.h>  // common base for HardwareSerial, HWCDC (USB CDC on Boot), etc.

class SerialTransport : public ILogTransport {
public:
    enum class Format { TEXT, JSON };

    explicit SerialTransport(Print&   serial,
                              Format   format   = Format::TEXT,
                              uint32_t baudRate = 115200);

    // begin() is a no-op: Serial.begin() must be called in main.cpp before
    // addTransport() because Serial output is needed from the very first log.
    bool        begin()  override;
    void        write(const LogEntry& entry) override;  // sync, ~60 µs @ 115200
    const char* getName() const override { return "Serial"; }

    // КОНТРАКТ: write() НЕ ВИКЛИКУЄ Logger::* — прямий Print::print()

private:
    Print&   serial_;
    Format   format_;
    uint32_t baudRate_;
    char     lineBuf_[320];  // TEXT: ~34+192, JSON: ~30+192 — 320 з запасом
};
