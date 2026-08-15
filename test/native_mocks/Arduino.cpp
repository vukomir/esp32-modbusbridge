#ifdef NATIVE_BUILD

#include "Arduino.h"
#include <chrono>
#include <thread>
#include <cstdlib>
#include <map>

// Global instances
MockSerial Serial;
MockSerial Serial2;
MockESP ESP;

// Mock pin states
static std::map<uint8_t, uint8_t> pinModes;
static std::map<uint8_t, uint8_t> pinStates;

// Time tracking
static auto startTime = std::chrono::steady_clock::now();

unsigned long millis() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    return static_cast<unsigned long>(duration.count());
}

unsigned long micros() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime);
    return static_cast<unsigned long>(duration.count());
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void delayMicroseconds(unsigned long us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void pinMode(uint8_t pin, uint8_t mode) {
    pinModes[pin] = mode;
}

void digitalWrite(uint8_t pin, uint8_t value) {
    pinStates[pin] = value;
}

int digitalRead(uint8_t pin) {
    // Return the written value, or LOW if never set
    auto it = pinStates.find(pin);
    return (it != pinStates.end()) ? it->second : LOW;
}

void yield() {
    // Allow other threads to run
    std::this_thread::yield();
}

#ifdef UNIT_TEST
// Every suite in test/ is written Arduino-style: it runs UNITY_BEGIN() ..
// UNITY_END() inside setup() and leaves loop() empty. A native binary needs a
// real entry point, so call setup() once and exit. PlatformIO reads the pass/
// fail counts from Unity's stdout, so the exit code does not carry the verdict.
extern void setup();

int main(int, char **) {
    setup();
    return 0;
}
#endif // UNIT_TEST

#endif // NATIVE_BUILD
