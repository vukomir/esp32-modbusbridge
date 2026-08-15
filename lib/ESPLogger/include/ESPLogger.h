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

    // --- Sinks ---
    //
    // A sink runs inside print(), on whatever FreeRTOS task emitted the line,
    // and on either core. A sink MUST be non-blocking, MUST NOT log anything
    // itself (re-entrancy), and MUST NOT touch the network.
    //
    // Two slots, deliberately: the WebUI WebSocket console and LogStore. If you
    // are here to add a third, note that library.json advertises "MQTT support"
    // that does not exist in this library — an MQTT sink would call
    // PubSubClient::publish() from arbitrary tasks, block on the network, and
    // re-enter this logger on failure. Don't.
    static const uint8_t LOG_MAX_SINKS = 2;

    // Registration is setup()-only and is NOT thread-safe. Emission is: a slot
    // is written before sinkCount exposes it, so a concurrent print() can never
    // observe a half-registered sink.
    static bool addLogCallback(LogCallback callback);    // false if null, duplicate, or full
    static bool removeLogCallback(LogCallback callback); // false if not registered

    // Legacy single-sink API. Both clear every slot; setLogCallback() then
    // installs the one given (nullptr just clears). Kept so pre-existing callers
    // keep their exact previous semantics.
    static void setLogCallback(LogCallback callback);
    static void removeLogCallback();

private:
    static LogLevel currentLevel;
    static const char *getLevelString(LogLevel level);
    static LogCallback logCallbacks[LOG_MAX_SINKS];
    static volatile uint8_t sinkCount;
};

#define LOG_DEBUG(...) ESPLogger::debug(__VA_ARGS__)
#define LOG_INFO(...) ESPLogger::info(__VA_ARGS__)
#define LOG_WARN(...) ESPLogger::warn(__VA_ARGS__)
#define LOG_ERROR(...) ESPLogger::error(__VA_ARGS__)

#endif
