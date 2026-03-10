#include "SerialTransport.h"

SerialTransport::SerialTransport(Print& serial, Format format, uint32_t baudRate)
    : serial_(serial), format_(format), baudRate_(baudRate) {}

bool SerialTransport::begin() {
    // Serial.begin() викликається в main.cpp до addTransport() —
    // транспорт починає працювати з першого write().
    return true;
}

void SerialTransport::write(const LogEntry& entry) {
    // КОНТРАКТ: не викликаємо Logger::* — прямий вивід в Serial
    if (format_ == Format::JSON) {
        entry.toJSON(lineBuf_, sizeof(lineBuf_));
    } else {
        entry.toText(lineBuf_, sizeof(lineBuf_));
    }
    serial_.print(lineBuf_);
}
