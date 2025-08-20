#ifdef NATIVE_BUILD

#include "LittleFS.h"

// Static member definitions
std::map<std::string, std::string> MockLittleFS::files;
bool MockLittleFS::mounted = false;

// Global instance
MockLittleFS LittleFS;

#endif // NATIVE_BUILD
