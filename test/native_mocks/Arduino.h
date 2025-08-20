#pragma once

// Arduino mock for native testing
#ifdef NATIVE_BUILD

#include <stdint.h>
#include <string>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

// Basic Arduino types
typedef bool boolean;
typedef uint8_t byte;

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

// Mock Serial class
class MockSerial {
public:
    void begin(unsigned long baud) {}
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t c) { return 1; }
    size_t write(const uint8_t* buffer, size_t size) { return size; }
    size_t write(const char* str) { return strlen(str); }
    void flush() {}
    int peek() { return -1; }
    
    // Print interface
    size_t print(const char* s) { std::cout << s; return strlen(s); }
    size_t print(const String& s) { std::cout << s.c_str(); return s.length(); }
    size_t println(const char* s) { std::cout << s << std::endl; return strlen(s) + 1; }
    size_t println(const String& s) { std::cout << s.c_str() << std::endl; return s.length() + 1; }
    size_t println() { std::cout << std::endl; return 1; }
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

// Math functions
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef max  
#define max(a,b) ((a)>(b)?(a):(b))
#endif

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
