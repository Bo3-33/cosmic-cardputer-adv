#pragma once
#include "../core/settings.h"

struct ISettingsStore {
    virtual ~ISettingsStore() = default;
    virtual bool load(Settings& s) = 0;
    virtual bool save(const Settings& s) = 0;
    virtual const char* backendName() const = 0;  // "SD" / "NVS" for status UI
};
