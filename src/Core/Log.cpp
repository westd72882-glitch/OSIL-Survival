#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
LogLevel g_level = LogLevel::INFO;
FILE* g_file = nullptr;
// Логи пишут и главный поток тика, и фоновые (сохранение мира, приём соединений на
// втором этапе). Без мьютекса строки перемешиваются посимвольно.
std::mutex g_mutex;

const char* levelTag(LogLevel level){
    switch(level){
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}
} // namespace

void logSetLevel(LogLevel level){ g_level = level; }
LogLevel logGetLevel(){ return g_level; }

void logSetFile(const std::string& path){
    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_file){ fclose(g_file); g_file = nullptr; }
    if(path.empty()) return;
    g_file = fopen(path.c_str(), "a");
    if(!g_file) fprintf(stderr, "[LOG] не удалось открыть файл лога: %s\n", path.c_str());
}

void logShutdown(){
    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_file){ fclose(g_file); g_file = nullptr; }
}

void logWrite(LogLevel level, const char* fmt, ...){
    if((int)level < (int)g_level) return;

    char body[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    time_t now = time(nullptr);
    struct tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);

    std::lock_guard<std::mutex> lock(g_mutex);
    fprintf(stdout, "[%s] %s | %s\n", stamp, levelTag(level), body);
    fflush(stdout);
    if(g_file){
        fprintf(g_file, "[%s] %s | %s\n", stamp, levelTag(level), body);
        fflush(g_file);
    }
}

LogLevel logLevelFromString(const std::string& name, LogLevel fallback){
    if(name == "trace") return LogLevel::TRACE;
    if(name == "debug") return LogLevel::DEBUG;
    if(name == "info")  return LogLevel::INFO;
    if(name == "warn")  return LogLevel::WARN;
    if(name == "error") return LogLevel::ERROR;
    return fallback;
}

const char* logLevelName(LogLevel level){
    switch(level){
        case LogLevel::TRACE: return "trace";
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO:  return "info";
        case LogLevel::WARN:  return "warn";
        case LogLevel::ERROR: return "error";
    }
    return "info";
}
