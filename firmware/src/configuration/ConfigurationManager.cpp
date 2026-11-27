#include "ConfigurationManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../storage/StorageManager.h"

ConfigurationManager g_config;

// Note: `doc["k"] | cfg_.field` returns the field itself when the key is missing -> skip self-copy.
static void copyStr(char* dst, size_t n, const char* src) {
    if (!src || src == dst) return;
    snprintf(dst, n, "%s", src);
}

bool ConfigurationManager::load() {
    if (!g_storage.ready()) return false;
    File f = LittleFS.open("/config.json", "r");
    if (!f) f = LittleFS.open("/config.tmp", "r");   // power cut between write and rename
    if (!f) { Serial.println(F("[cfg] no config, using defaults")); return false; }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("[cfg] parse error: %s\n", err.c_str()); return false; }
    copyStr(cfg_.ssid, sizeof cfg_.ssid, doc["ssid"] | "");
    copyStr(cfg_.pass, sizeof cfg_.pass, doc["pass"] | "");
    copyStr(cfg_.city, sizeof cfg_.city, doc["city"] | cfg_.city);
    cfg_.lat = doc["lat"] | cfg_.lat;
    cfg_.lon = doc["lon"] | cfg_.lon;
    cfg_.geoResolved = doc["geo"] | false;
    cfg_.geoFailed = doc["geoFail"] | false;
    copyStr(cfg_.apPass, sizeof cfg_.apPass, doc["ap"] | "");
    copyStr(cfg_.webPass, sizeof cfg_.webPass, doc["wp"] | "");
    copyStr(cfg_.tz, sizeof cfg_.tz, doc["tz"] | cfg_.tz);
    cfg_.h24 = doc["h24"] | cfg_.h24;
    copyStr(cfg_.theme, sizeof cfg_.theme, doc["theme"] | cfg_.theme);
    cfg_.rotateSec = doc["rotate"] | cfg_.rotateSec;
    cfg_.storySec = doc["story"] | cfg_.storySec;
    if (cfg_.storySec < 4 || cfg_.storySec > 60) cfg_.storySec = 9;
    cfg_.holdMin = doc["hold"] | cfg_.holdMin;
    if (cfg_.holdMin < 1 || cfg_.holdMin > 60) cfg_.holdMin = 5;
    cfg_.brightness = doc["bright"] | cfg_.brightness;
    cfg_.glowPct = doc["glow"] | cfg_.glowPct;
    cfg_.pageMask = uint16_t((doc["pages"] | int(cfg_.pageMask)) | 1);
    if ((doc["pagesV"] | 1) < 2) cfg_.pageMask |= 0x3C00;   // v2 added Sun, Quote, Internet, Network: on
    if ((doc["nightV"] | 0) >= 2) {               // v2: the user's own choice
        cfg_.nightFrom = doc["nightFrom"] | cfg_.nightFrom;
        cfg_.nightTo = doc["nightTo"] | cfg_.nightTo;
    }                                             // older configs: screensaver stays OFF
    copyStr(cfg_.bridge, sizeof cfg_.bridge, doc["bridge"] | "");
    copyStr(cfg_.relay, sizeof cfg_.relay, doc["relay"] | "");
    if (!doc["feeds"].isNull()) copyStr(cfg_.feeds, sizeof cfg_.feeds, doc["feeds"] | "");
    if (!doc["mkts"].isNull()) copyStr(cfg_.mkts, sizeof cfg_.mkts, doc["mkts"] | "");
    cfg_.btc = doc["btc"] | cfg_.btc;
    cfg_.newsAge = doc["nage"] | cfg_.newsAge;
    if (cfg_.newsAge < 1 || cfg_.newsAge > 168) cfg_.newsAge = 24;
    if (!doc["team"].isNull()) copyStr(cfg_.team, sizeof cfg_.team, doc["team"] | "");
    cfg_.mineOn = doc["mon"] | false;
    copyStr(cfg_.ducoUser, sizeof cfg_.ducoUser, doc["duser"] | "");
    copyStr(cfg_.ducoKey, sizeof cfg_.ducoKey, doc["dkey"] | "");
    if (!doc["drig"].isNull()) copyStr(cfg_.ducoRig, sizeof cfg_.ducoRig, doc["drig"] | "caiden");
    Serial.printf("[cfg] loaded: city=%s theme=%s wifi=%s\n", cfg_.city, cfg_.theme, cfg_.ssid[0] ? "set" : "none");
    return true;
}

bool ConfigurationManager::save() {
    if (!g_storage.ready()) return false;
    JsonDocument doc;
    doc["ssid"] = cfg_.ssid;
    doc["pass"] = cfg_.pass;
    doc["city"] = cfg_.city;
    doc["lat"] = cfg_.lat;
    doc["lon"] = cfg_.lon;
    doc["geo"] = cfg_.geoResolved;
    doc["geoFail"] = cfg_.geoFailed;
    doc["ap"] = cfg_.apPass;
    doc["wp"] = cfg_.webPass;
    doc["tz"] = cfg_.tz;
    doc["h24"] = cfg_.h24;
    doc["theme"] = cfg_.theme;
    doc["rotate"] = cfg_.rotateSec;
    doc["story"] = cfg_.storySec;
    doc["hold"] = cfg_.holdMin;
    doc["bright"] = cfg_.brightness;
    doc["glow"] = cfg_.glowPct;
    doc["pages"] = cfg_.pageMask;
    doc["pagesV"] = 2;
    doc["nightFrom"] = cfg_.nightFrom;
    doc["nightTo"] = cfg_.nightTo;
    doc["nightV"] = 2;
    doc["bridge"] = cfg_.bridge;
    doc["relay"] = cfg_.relay;
    doc["feeds"] = cfg_.feeds;
    doc["mkts"] = cfg_.mkts;
    doc["btc"] = cfg_.btc;
    doc["nage"] = cfg_.newsAge;
    doc["team"] = cfg_.team;
    doc["mon"] = cfg_.mineOn;
    doc["duser"] = cfg_.ducoUser;
    doc["dkey"] = cfg_.ducoKey;
    doc["drig"] = cfg_.ducoRig;
    // write to a temp file then rename: a power cut never leaves a half-written config
    File f = LittleFS.open("/config.tmp", "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    return LittleFS.rename("/config.tmp", "/config.json");   // atomic replace on LittleFS
}
