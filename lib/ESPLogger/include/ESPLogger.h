#ifndef ESP_LOGGER_H
#define ESP_LOGGER_H

#include <Arduino.h>
#include <stdarg.h>

class ESPLogger
{
public:
    enum LogLevel
    {
        ERROR = 0, // Most severe
        WARN = 1,
        INFO = 2,
        DEBUG = 3 // Least severe
    };

    // Define callback function type
    typedef void (*LogCallback)(LogLevel level, const char *message, unsigned long timestamp);

    static void begin(LogLevel level = INFO);
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    static void debug(const char *format, ...);
    static void info(const char *format, ...);
    static void warn(const char *format, ...);
    static void error(const char *format, ...);
    static void log(LogLevel level, const char *format, ...);

    static void print(LogLevel level, const char *message);
    static void vprintf(LogLevel level, const char *format, va_list args);

    static const char *logLevelToString(LogLevel level);
    static LogLevel stringToLogLevel(const char *level);

    // Add/remove callback methods
    static void setLogCallback(LogCallback callback);
    static void removeLogCallback();

private:
    static LogLevel currentLevel;
    static const char *getLevelString(LogLevel level);
    static LogCallback logCallback;
};

#define LOG_DEBUG(...) ESPLogger::debug(__VA_ARGS__)
#define LOG_INFO(...) ESPLogger::info(__VA_ARGS__)
#define LOG_WARN(...) ESPLogger::warn(__VA_ARGS__)
#define LOG_ERROR(...) ESPLogger::error(__VA_ARGS__)

#endif
