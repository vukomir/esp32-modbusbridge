#pragma once

#ifdef NATIVE_BUILD

#include "Arduino.h"
#include <map>
#include <string>
#include <fstream>
#include <filesystem>

// Mock File class
//
// Write-back semantics: when opened in write mode, MockFile holds a pointer
// to MockLittleFS::files so close()/destructor commits buffered writes back to
// the parent map. Without this, writes are lost when the value-returned MockFile
// goes out of scope, which broke any test that did `f = open("w"); f.print(...);
// f.close();` then expected the content to be readable on a subsequent open.
class MockFile {
private:
    std::string content;
    std::string path;
    size_t position;
    bool isOpen;
    bool isWriting;
    std::map<std::string, std::string>* writeBackTarget;  // null in read mode

    void commit() {
        if (isOpen && isWriting && writeBackTarget) {
            (*writeBackTarget)[path] = content;
        }
    }

public:
    MockFile() : position(0), isOpen(false), isWriting(false), writeBackTarget(nullptr) {}
    // Read-mode constructor.
    MockFile(const std::string& p, const std::string& c)
        : content(c), path(p), position(0), isOpen(true),
          isWriting(false), writeBackTarget(nullptr) {}
    // Write-mode constructor (commits to `target` on close/destruction).
    MockFile(const std::string& p, std::map<std::string, std::string>* target)
        : content(""), path(p), position(0), isOpen(true),
          isWriting(true), writeBackTarget(target) {}

    ~MockFile() { commit(); }

    operator bool() const { return isOpen; }

    size_t write(uint8_t c) {
        if (!isOpen) return 0;
        if (position >= content.size()) {
            content.resize(position + 1);
        }
        content[position++] = c;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) {
        if (!isOpen) return 0;
        for (size_t i = 0; i < size; i++) {
            write(buffer[i]);
        }
        return size;
    }

    size_t print(const char* s) {
        if (!isOpen || !s) return 0;
        return write((const uint8_t*)s, strlen(s));
    }

    size_t print(const String& s) {
        return print(s.c_str());
    }

    int read() {
        if (!isOpen || position >= content.size()) return -1;
        return content[position++];
    }

    size_t readBytes(char* buffer, size_t length) {
        if (!isOpen) return 0;
        size_t bytesRead = 0;
        for (size_t i = 0; i < length && position < content.size(); i++) {
            buffer[i] = content[position++];
            bytesRead++;
        }
        return bytesRead;
    }

    int available() {
        return isOpen ? (content.size() - position) : 0;
    }

    void flush() { commit(); }
    void close() { commit(); isOpen = false; }

    size_t size() { return content.size(); }
    String name() { return String(path.c_str()); }
};

// Mock LittleFS class
class MockLittleFS {
private:
    static std::map<std::string, std::string> files;
    static bool mounted;
    
public:
    bool begin(bool formatOnFail = false) {
        mounted = true;
        return true;
    }
    
    void end() {
        mounted = false;
    }
    
    bool format() {
        files.clear();
        return true;
    }
    
    MockFile open(const String& path, const char* mode = "r") {
        if (!mounted) return MockFile();

        std::string pathStr = path.c_str();

        if (mode && (strcmp(mode, "w") == 0 || strcmp(mode, "w+") == 0)) {
            // Write mode: pre-create the entry so existence checks see it,
            // and pass &files so the file commits its buffered content on
            // close/destruct.
            files[pathStr] = "";
            return MockFile(pathStr, &files);
        } else {
            // Read mode
            auto it = files.find(pathStr);
            if (it != files.end()) {
                return MockFile(pathStr, it->second);
            }
            return MockFile(); // File doesn't exist
        }
    }
    
    bool exists(const String& path) {
        if (!mounted) return false;
        return files.find(path.c_str()) != files.end();
    }
    
    bool remove(const String& path) {
        if (!mounted) return false;
        auto it = files.find(path.c_str());
        if (it != files.end()) {
            files.erase(it);
            return true;
        }
        return false;
    }
    
    size_t totalBytes() { return 1000000; } // Mock 1MB
    size_t usedBytes() { 
        size_t total = 0;
        for (const auto& file : files) {
            total += file.second.size();
        }
        return total;
    }
    
    // Helper for tests to manipulate filesystem
    void writeFile(const String& path, const String& content) {
        files[path.c_str()] = content.c_str();
    }
    
    String readFile(const String& path) {
        auto it = files.find(path.c_str());
        return (it != files.end()) ? String(it->second.c_str()) : String();
    }
};

extern MockLittleFS LittleFS;

#endif // NATIVE_BUILD
