#include "ESPLogger.h"

ESPLogger::LogLevel ESPLogger::currentLevel = ESPLogger::INFO;
ESPLogger::LogCallback ESPLogger::logCallback = nullptr;

void ESPLogger::begin(LogLevel level)
{
    currentLevel = level;
    // ESPLogger initialization - using only ESPLogger for output [[memory:6293639]]
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Logger initialized at level %s", logLevelToString(level));
    print(INFO, buffer);
}

void ESPLogger::setLevel(LogLevel level)
{
    currentLevel = level;
}

ESPLogger::LogLevel ESPLogger::getLevel()
{
    return currentLevel;
}

void ESPLogger::setLogCallback(LogCallback callback)
{
    logCallback = callback;
}

void ESPLogger::removeLogCallback()
{
    logCallback = nullptr;
}

const char *ESPLogger::logLevelToString(LogLevel level)
{
    switch (level)
    {
    case ERROR:
        return "error";
    case WARN:
        return "warn";
    case INFO:
        return "info";
    case DEBUG:
        return "debug";
    default:
        return "unknown";
    }
}

const char *ESPLogger::getLevelString(LogLevel level)
{
    switch (level)
    {
    case ERROR:
        return "ERROR";
    case WARN:
        return "WARN";
    case INFO:
        return "INFO";
    case DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

ESPLogger::LogLevel ESPLogger::stringToLogLevel(const char *level)
{
    if (!level)
        return INFO;

    String levelStr = String(level);
    levelStr.toLowerCase();

    if (levelStr == "error")
        return ERROR;
    if (levelStr == "warn")
        return WARN;
    if (levelStr == "info")
        return INFO;
    if (levelStr == "debug")
        return DEBUG;

    return INFO;
}

void ESPLogger::print(LogLevel level, const char *message)
{
    if (level <= currentLevel)
    {
        // Using only ESPLogger for output, no Serial.println [[memory:6293639]]
        // Output to Serial for debugging, but callback is the primary output
        Serial.printf("[%lu][%s] %s\n",
                      millis(),
                      getLevelString(level),
                      message);

        if (logCallback != nullptr)
        {
            logCallback(level, message, millis());
        }
    }
}

void ESPLogger::vprintf(LogLevel level, const char *format, va_list args)
{
    if (level <= currentLevel)
    {
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        print(level, buffer);
    }
}

void ESPLogger::debug(const char *format, ...)
{
    if (DEBUG <= currentLevel)
    {
        va_list args;
        va_start(args, format);
        vprintf(DEBUG, format, args);
        va_end(args);
    }
}

void ESPLogger::info(const char *format, ...)
{
    if (INFO <= currentLevel)
    {
        va_list args;
        va_start(args, format);
        vprintf(INFO, format, args);
        va_end(args);
    }
}

void ESPLogger::warn(const char *format, ...)
{
    if (WARN <= currentLevel)
    {
        va_list args;
        va_start(args, format);
        vprintf(WARN, format, args);
        va_end(args);
    }
}

void ESPLogger::error(const char *format, ...)
{
    if (ERROR <= currentLevel)
    {
        va_list args;
        va_start(args, format);
        vprintf(ERROR, format, args);
        va_end(args);
    }
}

void ESPLogger::log(LogLevel level, const char *format, ...)
{
    if (level <= currentLevel)
    {
        va_list args;
        va_start(args, format);
        vprintf(level, format, args);
        va_end(args);
    }
}
