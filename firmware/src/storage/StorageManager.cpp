#include "StorageManager.h"
#include <LittleFS.h>

StorageManager g_storage;

bool StorageManager::begin() {
    if (LittleFS.begin()) { ready_ = true; return true; }
    Serial.println(F("[fs] mount failed - formatting LittleFS (first boot)"));
    if (!LittleFS.format()) { Serial.println(F("[fs] format failed")); return false; }
    formatted_ = true;
    ready_ = LittleFS.begin();
    Serial.printf_P(PSTR("[fs] %s\n"), ready_ ? "ready" : "mount failed after format");
    return ready_;
}

size_t StorageManager::usedBytes() const {
    FSInfo i; if (!ready_ || !LittleFS.info(i)) return 0; return i.usedBytes;
}
size_t StorageManager::totalBytes() const {
    FSInfo i; if (!ready_ || !LittleFS.info(i)) return 0; return i.totalBytes;
}
