#pragma once
#include "ILogTransport.h"
#include "LogEntry.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

#define LOGGER_MAX_TRANSPORTS     4
#define LOGGER_FORMAT_BUFFER_SIZE 256   // stack-local у dispatch() — не член класу

class Logger {
public:
    Logger();
    ~Logger();

    // Ініціалізує FreeRTOS mutex. ОБОВ'ЯЗКОВО викликати з setup(),
    // ПЕРЕД addTransport(). (Конструктор не створює mutex — він викликається
    // як статичний глобал до старту FreeRTOS scheduler.)
    bool begin();

    // === Управління транспортами ===
    // Logger НЕ бере ownership transport*. Caller управляє lifetime.
    // begin() транспорту викликається автоматично при addTransport().
    bool addTransport   (ILogTransport* transport);
    void removeTransport(ILogTransport* transport);  // thread-safe під mutex

    void    setGlobalMinLevel(LogLevel level);
    uint8_t getTransportCount() const;

    // === Logging API ===
    void debug  (const char* component, const char* fmt, ...);
    void info   (const char* component, const char* fmt, ...);
    void warning(const char* component, const char* fmt, ...);
    void error  (const char* component, const char* fmt, ...);
    void fatal  (const char* component, const char* fmt, ...);
    void log    (LogLevel level, const char* component, const char* fmt, ...);

private:
    ILogTransport*    transports_[LOGGER_MAX_TRANSPORTS] = {};
    uint8_t           transportCount_ = 0;
    LogLevel          globalMinLevel_ = LogLevel::DEBUG;
    SemaphoreHandle_t mutex_          = nullptr;

    void dispatch(LogLevel level, const char* component,
                  const char* fmt, va_list args);
};
