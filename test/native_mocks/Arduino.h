#pragma once

// Arduino mock for native testing
#ifdef NATIVE_BUILD

#include <stdint.h>
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <iostream>
#include <chrono>
#include <thread>

// Basic Arduino types
typedef bool boolean;
typedef uint8_t byte;

// PROGMEM support (for ArduinoJson compatibility)
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))

// Flash string helper class
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))

// Pin modes
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

// Digital pin states
#define LOW 0
#define HIGH 1

// Mock Arduino String class
class String {
private:
    std::string str;
    
public:
    String() : str("") {}
    String(const char* s) : str(s ? s : "") {}
    String(const std::string& s) : str(s) {}
    String(int i) : str(std::to_string(i)) {}
    String(unsigned int i) : str(std::to_string(i)) {}
    String(long l) : str(std::to_string(l)) {}
    String(unsigned long l) : str(std::to_string(l)) {}
    String(float f) : str(std::to_string(f)) {}
    String(double d) : str(std::to_string(d)) {}
    
    // Assignment operators
    String& operator=(const char* s) { str = (s ? s : ""); return *this; }
    String& operator=(const String& s) { str = s.str; return *this; }
    
    // Concatenation
    String operator+(const String& s) const { return String(str + s.str); }
    String operator+(const char* s) const { return String(str + (s ? s : "")); }
    String& operator+=(const String& s) { str += s.str; return *this; }
    String& operator+=(const char* s) { if (s) str += s; return *this; }
    
    // Comparison
    bool operator==(const String& s) const { return str == s.str; }
    bool operator==(const char* s) const { return str == (s ? s : ""); }
    bool operator!=(const String& s) const { return str != s.str; }
    
    // Access
    char charAt(unsigned int index) const { return index < str.length() ? str[index] : 0; }
    const char* c_str() const { return str.c_str(); }
    unsigned int length() const { return str.length(); }
    
    // Search
    int indexOf(const String& s) const { 
        auto pos = str.find(s.str);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int indexOf(const char* s) const {
        if (!s) return -1;
        auto pos = str.find(s);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    
    // Case conversion (mutates in place, like the Arduino String API)
    void toLowerCase() {
        for (auto& c : str) c = static_cast<char>(::tolower((unsigned char)c));
    }
    void toUpperCase() {
        for (auto& c : str) c = static_cast<char>(::toupper((unsigned char)c));
    }

    // Concatenation methods (for ArduinoJson compatibility)
    bool concat(const String& s) { str += s.str; return true; }
    bool concat(const char* s) { if (s) str += s; return true; }
    bool concat(char c) { str += c; return true; }

    // Modification
    void trim() {
        str.erase(0, str.find_first_not_of(" \t\n\r"));
        str.erase(str.find_last_not_of(" \t\n\r") + 1);
    }
    
    String substring(unsigned int begin) const {
        return begin < str.length() ? String(str.substr(begin)) : String();
    }
    
    String substring(unsigned int begin, unsigned int end) const {
        if (begin < str.length() && end > begin) {
            return String(str.substr(begin, end - begin));
        }
        return String();
    }
    
    // Conversion
    int toInt() const { return std::stoi(str); }
    float toFloat() const { return std::stof(str); }
    
    // For debugging
    friend std::ostream& operator<<(std::ostream& os, const String& s) {
        return os << s.str;
    }
};

// Mock Printable base class (for ArduinoJson compatibility)
class Print;
class Printable {
public:
    virtual ~Printable() {}
    virtual size_t printTo(Print& p) const = 0;
};

// Mock Print base class (for ArduinoJson compatibility)
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            n += write(*buffer++);
        }
        return n;
    }

    size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
    size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
    size_t print(const Printable& x) { return x.printTo(*this); }
    size_t println(const char* s) { size_t n = print(s); n += write('\n'); return n; }
    size_t println(const String& s) { size_t n = print(s); n += write('\n'); return n; }
    size_t println(const Printable& x) { size_t n = print(x); n += write('\n'); return n; }
    size_t println() { return write('\n'); }
};

// Mock Stream class (for ArduinoJson compatibility)
class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
};

// Mock Serial class
class MockSerial : public Stream {
public:
    void begin(unsigned long baud) {}
    void end() {}
    int available() override { return 0; }
    int read() override { return -1; }
    size_t write(uint8_t c) override { std::cout << (char)c; return 1; }
    size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            std::cout << (char)buffer[i];
        }
        return size;
    }
    size_t write(const char* str) { return write((const uint8_t*)str, strlen(str)); }
    void flush() {}
    int peek() override { return -1; }

    // ESPLogger emits every line through Serial.printf, so the mock needs it or
    // nothing that logs will build natively.
    size_t printf(const char* format, ...) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        int n = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (n <= 0) return 0;
        size_t len = (size_t)n < sizeof(buffer) ? (size_t)n : sizeof(buffer) - 1;
        std::cout << buffer;
        return len;
    }
};

extern MockSerial Serial;
extern MockSerial Serial2;

// Mock ESP class
class MockESP {
public:
    uint32_t getFreeHeap() { return 200000; } // Mock 200KB free
    uint32_t getHeapSize() { return 320000; }  // Mock 320KB total
    uint32_t getFreeSketchSpace() { return 1000000; } // Mock 1MB free
    void restart() {}
    String getChipModel() { return "ESP32"; }
    uint8_t getChipRevision() { return 1; }
    uint32_t getChipId() { return 0x12345678; }
};

extern MockESP ESP;

// Time functions
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);

// GPIO functions
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

// Memory functions
void* malloc(size_t size);
void free(void* ptr);

// Math functions.
//
// Templates, not macros, deliberately. A function-like min()/max() macro
// rewrites any later declaration of std::min/std::max, so every libstdc++
// header pulled in after this one fails to parse. libc++ on macOS happens to
// tolerate it and libstdc++ on Linux does not, which is why this only appeared
// once the native build started compiling in CI. Do not turn these back into
// macros.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

template <typename T, typename U>
inline auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }

template <typename T, typename U>
inline auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// NaN handling
#ifndef NAN
#define NAN (0.0f/0.0f)
#endif

#ifndef isnan
#define isnan(x) ((x) != (x))
#endif

// Yield function
void yield();

#endif // NATIVE_BUILD
