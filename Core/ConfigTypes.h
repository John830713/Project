//==============================================================================
// ConfigTypes.h - Shared config field types
// Purpose:   Defines the field types and field definition structure used by
//            modules to describe their configurable settings.
//==============================================================================

#pragma once

#include <string>

enum class ConfigValueType {
    Bool,
    Int,
    String,
    Directory
};

struct ConfigFieldDefinition {
    std::wstring key;          // Internal key name
    std::wstring label;        // Display label for settings UI
    ConfigValueType type;      // Field type (Bool / Int / String)
    std::wstring defaultValue; // Default value
    int minValue;              // Minimum (for Int type)
    int maxValue;              // Maximum (for Int type)
};
