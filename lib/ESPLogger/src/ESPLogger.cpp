#include "ESPLogger.h"

ESPLogger::LogLevel ESPLogger::currentLevel = ESPLogger::INFO;
ESPLogger::LogCallback ESPLogger::logCallbacks[ESPLogger::LOG_MAX_SINKS] = {};
volatile uint8_t ESPLogger::sinkCount = 0;
const uint8_t ESPLogger::LOG_MAX_SINKS;

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

bool ESPLogger::addLogCallback(LogCallback callback)
{
    if (callback == nullptr)
    {
        return false;
    }

    for (uint8_t i = 0; i < sinkCount; i++)
    {
        if (logCallbacks[i] == callback)
        {
            return false; // already registered
        }
    }

    if (sinkCount >= LOG_MAX_SINKS)
    {
        return false;
    }

    // Publish-after-write: the slot must be valid before sinkCount exposes it,
    // or a print() racing us on the other core could call through a stale
    // pointer. Ordering matters here; do not merge these two statements.
    logCallbacks[sinkCount] = callback;
    sinkCount = sinkCount + 1;
    return true;
}

bool ESPLogger::removeLogCallback(LogCallback callback)
{
    for (uint8_t i = 0; i < sinkCount; i++)
    {
        if (logCallbacks[i] != callback)
        {
            continue;
        }

        // Shrink the count first so a concurrent print() stops iterating the
        // tail slot before we move anything into it.
        uint8_t last = sinkCount - 1;
        sinkCount = last;
        logCallbacks[i] = logCallbacks[last];
        logCallbacks[last] = nullptr;
        return true;
    }
    return false;
}

void ESPLogger::setLogCallback(LogCallback callback)
{
    removeLogCallback();
    if (callback != nullptr)
    {
        addLogCallback(callback);
    }
}

void ESPLogger::removeLogCallback()
{
    sinkCount = 0;
    for (uint8_t i = 0; i < LOG_MAX_SINKS; i++)
    {
        logCallbacks[i] = nullptr;
    }
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
        // One millis() read for both Serial and the sinks, so a line's serial
        // timestamp and its stored timestamp always agree.
        unsigned long ts = millis();

        // Using only ESPLogger for output, no Serial.println [[memory:6293639]]
        // Output to Serial for debugging, but callback is the primary output
        Serial.printf("[%lu][%s] %s\n",
                      ts,
                      getLevelString(level),
                      message);

        // Snapshot the count once: a sink registering mid-loop must not extend
        // this iteration. Deliberately unlocked — see the note in the header.
        uint8_t count = sinkCount;
        for (uint8_t i = 0; i < count; i++)
        {
            LogCallback cb = logCallbacks[i];
            if (cb != nullptr)
            {
                cb(level, message, ts);
            }
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
