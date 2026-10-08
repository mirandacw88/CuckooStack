// Minimal logging with a platform-provided sink (logcat on Android, os_log/stderr elsewhere).
#pragma once

#include <cstdarg>
#include <cstdio>

namespace cs {

enum class LogLevel { Info, Warn, Error };
using LogSink = void (*)(LogLevel level, const char* message);

inline LogSink& logSink() {
    static LogSink sink = [](LogLevel level, const char* msg) {
        FILE* f = level == LogLevel::Info ? stdout : stderr;
        std::fprintf(f, "[CuckooStack] %s\n", msg);
        std::fflush(f);
    };
    return sink;
}
inline void setLogSink(LogSink sink) { logSink() = sink; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline void logf(LogLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    logSink()(level, buf);
}

} // namespace cs

#define CS_LOGI(...) ::cs::logf(::cs::LogLevel::Info, __VA_ARGS__)
#define CS_LOGW(...) ::cs::logf(::cs::LogLevel::Warn, __VA_ARGS__)
#define CS_LOGE(...) ::cs::logf(::cs::LogLevel::Error, __VA_ARGS__)
