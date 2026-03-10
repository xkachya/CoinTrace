#pragma once

// LOG_DEBUG компілюється в нуль-код (do-nothing) у production збірці.
// Визначте -DCOINTRACE_DEBUG=1 у build_flags тільки для [env:cointrace-dev].
#ifdef COINTRACE_DEBUG
  #define LOG_DEBUG(logger, comp, ...) (logger)->debug(comp, __VA_ARGS__)
#else
  #define LOG_DEBUG(logger, comp, ...) do {} while(0)
#endif

// INFO/WARNING/ERROR/FATAL — завжди активні (нульова вартість не потрібна).
#define LOG_INFO(logger, comp, ...)    (logger)->info(comp, __VA_ARGS__)
#define LOG_WARNING(logger, comp, ...) (logger)->warning(comp, __VA_ARGS__)
#define LOG_ERROR(logger, comp, ...)   (logger)->error(comp, __VA_ARGS__)
#define LOG_FATAL(logger, comp, ...)   (logger)->fatal(comp, __VA_ARGS__)
