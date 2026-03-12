#include "Logger.h"
#include <string.h>
#include <Arduino.h>

Logger::Logger() {
    // Навмисно порожній: xSemaphoreCreateMutex() тут ЗАБОРОНЕНИЙ,
    // бо статичні C++ конструктори виконуються до vTaskStartScheduler().
    // Викликайте begin() з setup() після старту FreeRTOS.
}

bool Logger::begin() {
    if (mutex_) return true;  // вже ініціалізовано
    mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr;
}

Logger::~Logger() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool Logger::addTransport(ILogTransport* transport) {
    if (!transport || transportCount_ >= LOGGER_MAX_TRANSPORTS) return false;
    transport->begin();
    transports_[transportCount_++] = transport;
    return true;
}

void Logger::removeTransport(ILogTransport* transport) {
    if (!mutex_) return;  // begin() not yet called — mutex not initialized (LA-6)
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;

    for (uint8_t i = 0; i < transportCount_; i++) {
        if (transports_[i] == transport) {
            // Compact array under mutex — dispatch is not iterating at this moment
            for (uint8_t j = i; j < transportCount_ - 1; j++) {
                transports_[j] = transports_[j + 1];
            }
            transports_[--transportCount_] = nullptr;
            break;
        }
    }

    xSemaphoreGive(mutex_);
}

void Logger::setGlobalMinLevel(LogLevel level) { globalMinLevel_ = level; }
uint8_t Logger::getTransportCount() const      { return transportCount_; }

void Logger::debug  (const char* c, const char* fmt, ...) { va_list a; va_start(a, fmt); dispatch(LogLevel::DEBUG,   c, fmt, a); va_end(a); }
void Logger::info   (const char* c, const char* fmt, ...) { va_list a; va_start(a, fmt); dispatch(LogLevel::INFO,    c, fmt, a); va_end(a); }
void Logger::warning(const char* c, const char* fmt, ...) { va_list a; va_start(a, fmt); dispatch(LogLevel::WARNING, c, fmt, a); va_end(a); }
void Logger::error  (const char* c, const char* fmt, ...) { va_list a; va_start(a, fmt); dispatch(LogLevel::ERROR,   c, fmt, a); va_end(a); }
void Logger::fatal  (const char* c, const char* fmt, ...) { va_list a; va_start(a, fmt); dispatch(LogLevel::FATAL,   c, fmt, a); va_end(a); }

void Logger::log(LogLevel level, const char* c, const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    dispatch(level, c, fmt, a);
    va_end(a);
}

void Logger::dispatch(LogLevel level, const char* component,
                      const char* fmt, va_list args) {

    // 1. Глобальний фільтр — без mutex (read-only)
    if (level < globalMinLevel_) return;

    // 2. Форматування в СТЕК до захоплення mutex.
    //    Кожна задача форматує у власний stack frame — без shared state.
    //    Розмір stack call: localBuf(256) + LogEntry(220) + va_list(4) ≈ 480 байт.
    char localBuf[LOGGER_FORMAT_BUFFER_SIZE];
    int written = vsnprintf(localBuf, sizeof(localBuf), fmt, args);

    // 3. LogEntry будується на стеку — без heap allocation
    LogEntry entry;
    entry.timestampMs = millis();
    entry.level       = level;
    strncpy(entry.component, component, sizeof(entry.component) - 1);
    entry.component[sizeof(entry.component) - 1] = '\0';
    strncpy(entry.message, localBuf, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    // LA-2: маркер обрізання на рівні entry.message (не localBuf).
    // localBuf=256, entry.message=192: стара логіка memcpy до localBuf[251] ніколи
    // не досягала entry.message (strncpy копіює лише 191 символ).
    if (written >= (int)sizeof(entry.message)) {
        memcpy(entry.message + sizeof(entry.message) - 4, "...", 4);
    }

    // 4. Mutex: null-guard якщо begin() ще не викликано (defense).
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;  // drop — транзитне перевантаження або deadlock protection
    }

    // 5. Dispatch до транспортів.
    //    ІНВАРІАНТ: transports[i]->write() НЕ ВИКЛИКУЄ Logger::* (контракт ILogTransport).
    //    SerialTransport: sync ~60 мкс — прийнятно всередині mutex.
    //    Інші транспорти: async queue put ~5 мкс.
    for (uint8_t i = 0; i < transportCount_; i++) {
        ILogTransport* t = transports_[i];
        if (t && level >= t->getMinLevel()) {
            t->write(entry);
        }
    }

    xSemaphoreGive(mutex_);
}
