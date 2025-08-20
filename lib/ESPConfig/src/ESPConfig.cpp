#include "ESPConfig.h"
#include <ESPLogger.h>

Preferences ESPConfig::preferences;
bool ESPConfig::initialized = false;

void ESPConfig::begin(const char *namespace_name)
{
    LOG_INFO("----------------------------------------");
    LOG_INFO("Configuration System Initialization");
    LOG_INFO("----------------------------------------");

    if (!initialized)
    {
        LOG_DEBUG("Opening preferences with namespace: %s", namespace_name);
        preferences.begin(namespace_name, false);
        initialized = true;
        LOG_INFO("Configuration system initialized successfully");
    }
    else
    {
        LOG_WARN("Configuration system already initialized");
    }
}

bool ESPConfig::save()
{
    LOG_DEBUG("Save called (preferences save automatically)");
    return true;
}

String ESPConfig::getString(const char *key, const String &defaultValue)
{
    if (!initialized)
    {
        LOG_WARN("Cannot get string '%s' - not initialized", key);
        return defaultValue;
    }

    String value = preferences.getString(key, defaultValue);
    LOG_DEBUG("Get string '%s': %s", key, value.c_str());
    return value;
}

bool ESPConfig::putString(const char *key, const char *value)
{
    if (!initialized)
    {
        LOG_WARN("Cannot put string '%s' - not initialized", key);
        return false;
    }

    LOG_DEBUG("Setting string '%s' = '%s'", key, value);
    return preferences.putString(key, value);
}

int32_t ESPConfig::getInt(const char *key, int32_t defaultValue)
{
    if (!initialized)
    {
        LOG_WARN("Cannot get int '%s' - not initialized", key);
        return defaultValue;
    }

    int32_t value = preferences.getInt(key, defaultValue);
    LOG_DEBUG("Get int '%s': %d", key, value);
    return value;
}

bool ESPConfig::putInt(const char *key, int32_t value)
{
    if (!initialized)
    {
        LOG_WARN("Cannot put int '%s' - not initialized", key);
        return false;
    }

    LOG_DEBUG("Setting int '%s' = %d", key, value);
    return preferences.putInt(key, value);
}

uint16_t ESPConfig::getUShort(const char *key, uint16_t defaultValue)
{
    if (!initialized)
    {
        LOG_WARN("Cannot get ushort '%s' - not initialized", key);
        return defaultValue;
    }

    uint16_t value = preferences.getUShort(key, defaultValue);
    LOG_DEBUG("Get ushort '%s': %u", key, value);
    return value;
}

bool ESPConfig::putUShort(const char *key, uint16_t value)
{
    if (!initialized)
    {
        LOG_WARN("Cannot put ushort '%s' - not initialized", key);
        return false;
    }

    LOG_DEBUG("Setting ushort '%s' = %u", key, value);
    return preferences.putUShort(key, value);
}

bool ESPConfig::getBool(const char *key, bool defaultValue)
{
    if (!initialized)
    {
        LOG_WARN("Cannot get bool '%s' - not initialized", key);
        return defaultValue;
    }

    bool value = preferences.getBool(key, defaultValue);
    LOG_DEBUG("Get bool '%s': %s", key, value ? "true" : "false");
    return value;
}

bool ESPConfig::putBool(const char *key, bool value)
{
    if (!initialized)
    {
        LOG_WARN("Cannot put bool '%s' - not initialized", key);
        return false;
    }

    LOG_DEBUG("Setting bool '%s' = %s", key, value ? "true" : "false");
    return preferences.putBool(key, value);
}

bool ESPConfig::hasKey(const char *key)
{
    if (!initialized)
    {
        LOG_WARN("Cannot check key '%s' - not initialized", key);
        return false;
    }

    bool exists = preferences.isKey(key);
    LOG_DEBUG("Key check '%s': %s", key, exists ? "exists" : "not found");
    return exists;
}

void ESPConfig::remove(const char *key)
{
    if (!initialized)
    {
        LOG_WARN("Cannot remove key '%s' - not initialized", key);
        return;
    }

    LOG_DEBUG("Removing key: %s", key);
    preferences.remove(key);
}

void ESPConfig::clear()
{
    if (!initialized)
    {
        LOG_WARN("Cannot clear preferences - not initialized");
        return;
    }

    LOG_INFO("Clearing all preferences");
    preferences.clear();
}
