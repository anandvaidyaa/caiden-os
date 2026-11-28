#pragma once
// Persistent settings in /config.json on LittleFS (ArduinoJson).
#include <Arduino.h>

struct Config {
    char     ssid[33] = "";
    char     pass[65] = "";
    char     city[33] = "Pune";
    float    lat = 18.52f, lon = 73.86f;      // resolved via geocoding when the city changes
    bool     geoResolved = false;
    bool     geoFailed = false;               // city not found by the geocoder
    char     tz[40] = "IST-5:30";             // POSIX TZ string (default India)
    bool     h24 = false;
    char     theme[12] = "midnight";
    uint16_t rotateSec = 0;                   // 0 = no auto-rotation
    uint8_t  storySec = 9;                    // News: seconds per headline (4..60)
    uint8_t  holdMin = 5;                     // a page picked by hand stays this long (1..60 min)
    uint8_t  brightness = 70;                 // 0..100
    uint8_t  glowPct = 100;                   // ambient glow strength, % of design value
    uint8_t  nightFrom = 0, nightTo = 0;      // night screensaver hours; from == to = OFF (default)
    uint16_t pageMask = 0xBFFF;               // dashboards in the rotation (bit = page index; home always on)
    char     bridge[48] = "";                 // unused since 0.7.0 (the PC bridge was replaced by the relay)
    char     relay[72] = "";                  // cloud relay host (relay/worker.js): news, markets, Spotify...
    // Feed settings, sent to the relay with each request (relay/worker.js parseSettings):
    // news tabs "TAB~s~search words|TAB~t~TOPIC" (max 3), markets "SYMBOL~Name|..." (max 4)
    char     feeds[160] = "WORLD~t~WORLD|TECH~t~TECHNOLOGY|BUSINESS~t~BUSINESS";
    char     mkts[128] = "^NSEI~NIFTY 50|^BSESN~SENSEX|RELIANCE.NS~RELIANCE|TCS.NS~TCS";
    bool     btc = true;                      // Bitcoin row on Markets
    uint8_t  newsAge = 24;                    // skip stories older than this (hours)
    char     team[25] = "India";              // cricket: follow live matches of this team
    // Duino-Coin mining (hobby; off by default). Account details live only in the clock's config.
    bool     mineOn = false;
    char     ducoUser[33] = "";
    char     ducoKey[33] = "";                // optional mining key
    char     ducoRig[25] = "caiden";
    char     apPass[12] = "";                 // random setup-AP password, generated on first boot
    char     webPass[12] = "";                // settings-page password, made by the clock, shown on System
};

class ConfigurationManager {
public:
    bool load();
    bool save();
    Config& get() { return cfg_; }
    bool hasWifi() const { return cfg_.ssid[0] != 0; }
private:
    Config cfg_;
};

extern ConfigurationManager g_config;
