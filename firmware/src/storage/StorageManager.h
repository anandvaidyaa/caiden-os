#pragma once
// LittleFS wrapper. Partition: 0x200000-0x3FA000 (eagle.flash.4m2m.ld, 8 KB blocks).
// On first boot of CAIDEN OS the region still holds part of the stock filesystem,
// which does not mount -> it is formatted once. (The stock data is in the backup.)
#include <Arduino.h>

class StorageManager {
public:
    bool begin();
    bool ready() const { return ready_; }
    bool formattedOnBoot() const { return formatted_; }
    size_t usedBytes() const;
    size_t totalBytes() const;
private:
    bool ready_ = false, formatted_ = false;
};

extern StorageManager g_storage;
