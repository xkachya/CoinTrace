#pragma once
#include "LogEntry.h"

class ILogTransport {
public:
    virtual ~ILogTransport() = default;

    virtual bool begin() { return true; }
    virtual void end()   {}

    // ╔══════════════════════════════════════════════════════════════╗
    // ║  КРИТИЧНИЙ КОНТРАКТ: write() НІКОЛИ не повинен:             ║
    // ║  1. Викликати Logger::debug/info/warning/error/fatal()       ║
    // ║  2. Викликати будь-що що веде до Logger::dispatch()          ║
    // ║                                                              ║
    // ║  Порушення → рекурсивний deadlock (mutex вже захоплений).    ║
    // ║  Внутрішні помилки → зберігати в полях, НЕ логувати.         ║
    // ║  write() не повинен блокуватись довше 200 мкс.               ║
    // ║  Повільні транспорти (SD, WS) — кладуть у чергу і виходять. ║
    // ╚══════════════════════════════════════════════════════════════╝
    virtual void write(const LogEntry& entry) = 0;

    virtual LogLevel    getMinLevel() const          { return minLevel_; }
    virtual void        setMinLevel(LogLevel level)  { minLevel_ = level; }
    virtual const char* getName()    const = 0;
    virtual bool        isActive()   const { return true; }

    // Кількість скинутих записів (через повну чергу або помилку).
    // НЕ логується через Logger — читається зовні для діагностики.
    virtual uint32_t getDroppedCount() const { return 0; }

protected:
    LogLevel minLevel_ = LogLevel::DEBUG;
};
