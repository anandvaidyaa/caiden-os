#pragma once
// Shared, plain-data application state. Services write it, screens read it.
// Every block carries a `version` counter so screens can detect changes cheaply.
#include <stdint.h>
#include "BridgeData.h"

struct WeatherData {
    bool     valid = false;
    uint16_t version = 0;
    uint32_t fetchedMs = 0;
    float    temp = 0, feels = 0, wind = 0, uv = 0, uvMax = 0, tMin = 0, tMax = 0;
    int16_t  code = -1;           // WMO weather code, -1 = unknown
    uint8_t  humidity = 0, rainProb = 0;
    bool     isDay = true;
    char     sunrise[6] = "--:--", sunset[6] = "--:--";
    uint16_t windDir = 0;           // degrees
    int32_t  utcOffset = 0;         // location offset (s) from Open-Meteo, for hourly/forecast alignment
    // today's hourly temperatures 06:00..21:00 (16 values, x10 C), for the weather curve
    bool     hourlyValid = false;
    int16_t  hourly[16] = {0};
    // 5-day forecast (today first)
    uint8_t  days = 0;
    int16_t  dayCode[5] = {0}, dayMax[5] = {0}, dayMin[5] = {0};
    // today's hourly rain probability 00..23 (%), for rain alerts
    bool     rainHValid = false;
    uint8_t  rainH[24] = {0};
};

// Weather / air alert shown as a banner over any page (main.cpp updateAlerts).
// kind: 0 none, 1 storm, 2 rain, 3 air, 4 heat, 5 UV, 6 wind, 7 cold
struct AlertData { uint8_t kind = 0; char text[48] = ""; };

struct AirData {
    bool     valid = false;
    uint16_t version = 0;
    uint32_t fetchedMs = 0;
    float    pm25 = 0, pm10 = 0;
    int16_t  usAqi = -1;
    int16_t  naqi = -1;           // Indian NAQI estimate from current PM2.5 / PM10
    // last 24 hours of NAQI (oldest first, last = current hour), from hourly PM
    uint8_t  histCount = 0;           // hist[i] = -1 where the hour had no data
    int16_t  hist[24] = {0};
    int16_t  peak = -1;
    uint8_t  peakHour = 0;
};

enum class NetMode : uint8_t { Booting, Connecting, Online, SetupAP, Offline };

struct NetState {
    NetMode  mode = NetMode::Booting;
    uint16_t version = 0;
    int8_t   rssi = 0;
    char     ip[16] = "";
    char     apName[24] = "";
    char     apPass[16] = "";
    char     ssid[33] = "";
};

// Wireless firmware update progress (drives the update screen).
enum class OtaPhase : uint8_t { Idle, Receiving, Restarting, Failed };
struct OtaState {
    OtaPhase phase = OtaPhase::Idle;
    uint8_t  pct = 0;
    uint32_t failedAt = 0;
    char     err[32] = "";
};

// Duino-Coin mining (MinerService on the clock, MiningData filled by the miner and the relay).
enum class MineStatus : uint8_t { Off, NoAccount, WaitingPool, Connecting, Mining, Paused, Error };
struct MiningData {
    MineStatus status = MineStatus::Off;
    char     why[28] = "";              // short reason for Paused / Error
    float    hr = 0;                    // effective hash rate, H/s (hashes per second of wall time)
    uint32_t acc = 0, rej = 0;          // shares since start-up
    uint16_t diff = 0;                  // current share difficulty
    uint32_t mineMs = 0;                // time spent mining since start-up
    uint16_t hist[24] = {0};            // hourly average hash rate, oldest first (H/s)
    uint8_t  histN = 0;
    uint32_t lastShareAt = 0;           // millis() of the last share result (screen flash)
    bool     lastGood = false;
    // from the relay (/duco.json), every 10 minutes while mining
    char     poolIp[16] = "";
    uint16_t poolPort = 0;
    char     poolName[24] = "";
    bool     relayOk = false;
    float    bal = 0, today = 0;        // DUCO balance and earned today (relay keeps the day's start)
    float    inr = 0;                   // price of 1 DUCO in INR (0 = unknown)
    float    btcHr = 0;                 // Bitcoin network hash rate, H/s
    float    poolHr = 0;                // what the pool's dashboard shows for this rig
    uint32_t poolAcc = 0, poolRej = 0;
    uint16_t version = 0;               // bumps when anything shown changes
};

struct AppState {
    WeatherData weather;
    AirData     air;
    NetState    net;
    BridgeData  bridge;
    OtaState    ota;
    AlertData   alert;
    MiningData  mine;
    bool        timeValid = false;
    uint32_t    lastSyncEpoch = 0;   // last successful data sync (unix time)
};

extern AppState g_state;
