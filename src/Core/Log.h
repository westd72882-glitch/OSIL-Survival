#pragma once
// ==================== ЛОГИРОВАНИЕ ====================
// В A.N.O.D.E логи шли через SDL_Log и оседали в отладочной консоли поверх игры
// (src/Engine/Core/Console.*). Выделенному серверу SDL не нужен вовсе: он консольный,
// поэтому здесь свой минимальный логгер — уровни, метка времени, вывод в stdout и,
// если задан, в файл. Формат одной строкой и без цвета: так его удобно грепать и
// скармливать systemd/journald.
#include <string>

enum class LogLevel { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4 };

// Ниже этого уровня сообщения отбрасываются (по умолчанию INFO).
void logSetLevel(LogLevel level);
LogLevel logGetLevel();
// Дублировать вывод в файл. Пустая строка — выключить файловый вывод.
void logSetFile(const std::string& path);
void logShutdown();

// printf-подобный интерфейс: logWrite(LogLevel::INFO, "игроков: %d", n).
void logWrite(LogLevel level, const char* fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
;

#define LOG_TRACE(...) logWrite(LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) logWrite(LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  logWrite(LogLevel::INFO,  __VA_ARGS__)
#define LOG_WARN(...)  logWrite(LogLevel::WARN,  __VA_ARGS__)
#define LOG_ERROR(...) logWrite(LogLevel::ERROR, __VA_ARGS__)

// Разбор уровня из конфига ("trace"/"debug"/"info"/"warn"/"error").
LogLevel logLevelFromString(const std::string& name, LogLevel fallback = LogLevel::INFO);
const char* logLevelName(LogLevel level);
