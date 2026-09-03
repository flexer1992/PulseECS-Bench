#pragma once

#include <cstdio>
#include <utility>

namespace engine
{

// Минимальная замена engine/core/Logger.hpp для standalone-сборки.
// Бенчмарк и ядро ECS используют только макросы LOG_INFO/WARN/ERROR/CRITICAL
// и их клиентские варианты. Всё печатает в stderr, без внешних зависимостей.

namespace detail
{
    inline void logEmit(const char *level, const char *fmt)
    {
        std::fprintf(stderr, "[%s] %s\n", level, fmt);
    }

    template <typename T>
    inline void logFmt(char *out, std::size_t cap, const char *fmt, T &&v)
    {
        std::snprintf(out, cap, fmt, std::forward<T>(v));
    }

    template <typename T, typename... Rest>
    inline void logFmt(char *out, std::size_t cap, const char *fmt, T &&v, Rest &&...rest)
    {
        char tmp[512];
        std::snprintf(tmp, sizeof(tmp), fmt, std::forward<T>(v));
        std::snprintf(out, cap, "%s", tmp);
        logFmt(out, cap, "%s", std::forward<Rest>(rest)...);
    }
} // namespace detail

inline void logInfo(const char *fmt)
{
    std::fprintf(stderr, "[INFO] %s\n", fmt);
}
template <typename... Args>
inline void logInfo(const char *fmt, Args &&...args)
{
    char buf[2048];
    detail::logFmt(buf, sizeof(buf), "%s", fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "[INFO] %s\n", buf);
}

inline void logWarn(const char *fmt)
{
    std::fprintf(stderr, "[WARN] %s\n", fmt);
}
template <typename... Args>
inline void logWarn(const char *fmt, Args &&...args)
{
    char buf[2048];
    detail::logFmt(buf, sizeof(buf), "%s", fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "[WARN] %s\n", buf);
}

inline void logError(const char *fmt)
{
    std::fprintf(stderr, "[ERROR] %s\n", fmt);
}
template <typename... Args>
inline void logError(const char *fmt, Args &&...args)
{
    char buf[2048];
    detail::logFmt(buf, sizeof(buf), "%s", fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "[ERROR] %s\n", buf);
}

inline void logCritical(const char *fmt)
{
    std::fprintf(stderr, "[CRITICAL] %s\n", fmt);
}
template <typename... Args>
inline void logCritical(const char *fmt, Args &&...args)
{
    char buf[2048];
    detail::logFmt(buf, sizeof(buf), "%s", fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "[CRITICAL] %s\n", buf);
}

} // namespace engine

// Макросы совпадают по именам и сигнатурам с оригинальным Logger.hpp,
// чтобы World.hpp и остальной код собирался без правок.
#define LOG_DEBUG(...)      ::engine::logInfo(__VA_ARGS__)
#define LOG_TRACE(...)      ::engine::logInfo(__VA_ARGS__)
#define LOG_INFO(...)       ::engine::logInfo(__VA_ARGS__)
#define LOG_WARN(...)       ::engine::logWarn(__VA_ARGS__)
#define LOG_ERROR(...)      ::engine::logError(__VA_ARGS__)
#define LOG_CRITICAL(...)   ::engine::logCritical(__VA_ARGS__)

#define CLIENT_LOG_DEBUG(...)    ::engine::logInfo(__VA_ARGS__)
#define CLIENT_LOG_INFO(...)     ::engine::logInfo(__VA_ARGS__)
#define CLIENT_LOG_WARN(...)     ::engine::logWarn(__VA_ARGS__)
#define CLIENT_LOG_ERROR(...)    ::engine::logError(__VA_ARGS__)
#define CLIENT_LOG_CRITICAL(...) ::engine::logCritical(__VA_ARGS__)