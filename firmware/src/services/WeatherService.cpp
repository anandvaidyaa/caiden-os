#include "WeatherService.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

WeatherService g_weather;

static const uint32_t WEATHER_EVERY = 15UL * 60 * 1000;   // Open-Meteo `current` updates every 15 min
static const uint32_t AIR_EVERY     = 30UL * 60 * 1000;   // air quality is hourly
static const uint16_t HTTP_TIMEOUT  = 3000;   // keep UI stalls short

// Back-off after failures: 30 s, 1 min, 2 min, ... capped at 15 min.
static uint32_t backoff(uint8_t fails) {
    uint32_t s = 30000UL << (fails > 5 ? 5 : fails);
    return s > 900000UL ? 900000UL : s;
}

// GET url, parse with a filter into doc. Streams the body: never buffers the whole response.
static bool getJson(const String& url, JsonDocument& doc, JsonDocument& filter) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT);
    http.useHTTP10(true);                 // no chunked encoding -> parse straight from the stream
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        ok = !e;
        if (e) Serial.printf_P(PSTR("[wx] json: %s\n"), e.c_str());
    } else {
        Serial.printf_P(PSTR("[wx] http %d\n"), code);
    }
    http.end();
    return ok;
}

static String urlEncode(const char* s) {
    String o;
    for (; *s; s++) {
        char c = *s;
        if (isalnum(uint8_t(c)) || c == '-' || c == '_' || c == '.') o += c;
        else { char b[4]; snprintf_P(b, sizeof b, PSTR("%%%02X"), uint8_t(c)); o += b; }
    }
    return o;
}

bool WeatherService::geocode() {
    Config& c = g_config.get();
    JsonDocument filter, doc;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;
    filter["results"][0]["name"] = true;
    String url = F("http://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=");
    url += urlEncode(c.city);
    if (!getJson(url, doc, filter)) return false;
    JsonVariant r = doc["results"][0];
    if (r.isNull()) {
        Serial.printf_P(PSTR("[wx] city '%s' not found\n"), c.city);
        c.geoResolved = true; c.geoFailed = true;
        g_config.save();
        g_state.weather.version++;            // let the home screen show "City not found"
        return true;
    }
    c.geoFailed = false;
    c.lat = r["latitude"] | c.lat;
    c.lon = r["longitude"] | c.lon;
    c.geoResolved = true;
    g_config.save();
    Serial.printf_P(PSTR("[wx] %s -> %.3f, %.3f\n"), c.city, c.lat, c.lon);
    return true;
}

bool WeatherService::fetchWeather() {
    Config& c = g_config.get();
    char url[480];
    snprintf_P(url, sizeof url,
             PSTR("http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,"
             "wind_direction_10m,is_day,uv_index,precipitation_probability&hourly=temperature_2m,precipitation_probability"
             "&daily=weather_code,sunrise,sunset,temperature_2m_max,temperature_2m_min,uv_index_max"
             "&forecast_days=5&timezone=auto"),
             c.lat, c.lon);
    JsonDocument filter, doc;
    JsonObject f = filter["current"].to<JsonObject>();
    for (const char* k : {"temperature_2m", "apparent_temperature", "relative_humidity_2m", "weather_code",
                          "wind_speed_10m", "wind_direction_10m", "is_day", "uv_index", "precipitation_probability"})
        f[k] = true;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["precipitation_probability"] = true;
    filter["utc_offset_seconds"] = true;
    JsonObject d = filter["daily"].to<JsonObject>();
    for (const char* k : {"weather_code", "sunrise", "sunset", "temperature_2m_max", "temperature_2m_min", "uv_index_max"})
        d[k] = true;
    if (!getJson(String(url), doc, filter)) return false;

    JsonObject cur = doc["current"];
    if (cur.isNull()) return false;
    WeatherData& w = g_state.weather;
    w.temp = cur["temperature_2m"] | 0.f;
    w.feels = cur["apparent_temperature"] | w.temp;
    w.humidity = cur["relative_humidity_2m"] | 0;
    w.code = cur["weather_code"] | -1;
    w.wind = cur["wind_speed_10m"] | 0.f;
    w.isDay = (cur["is_day"] | 1) == 1;
    w.uv = cur["uv_index"] | 0.f;
    w.rainProb = cur["precipitation_probability"] | 0;
    w.windDir = cur["wind_direction_10m"] | 0;
    w.utcOffset = doc["utc_offset_seconds"] | 0;
    // hourly: the first 24 values are today 00..23 (timezone=auto); keep 06..21
    JsonArray hr = doc["hourly"]["temperature_2m"];
    w.hourlyValid = hr.size() >= 22;
    for (int i = 0; i < 16 && w.hourlyValid; i++) {
        if (hr[6 + i].isNull()) { w.hourlyValid = false; break; }
        w.hourly[i] = int16_t(lroundf(hr[6 + i].as<float>() * 10));
    }
    // today's rain probability per hour (alerts)
    JsonArray rp = doc["hourly"]["precipitation_probability"];
    w.rainHValid = rp.size() >= 24;
    for (int i = 0; i < 24 && w.rainHValid; i++) w.rainH[i] = rp[i] | 0;
    // 5-day forecast
    JsonArray dc = doc["daily"]["weather_code"], dmx = doc["daily"]["temperature_2m_max"], dmn = doc["daily"]["temperature_2m_min"];
    w.days = 0;
    for (int i = 0; i < 5 && i < int(dc.size()); i++) {
        w.dayCode[i] = dc[i] | -1;
        w.dayMax[i] = int16_t(lroundf(dmx[i] | 0.f));
        w.dayMin[i] = int16_t(lroundf(dmn[i] | 0.f));
        w.days = uint8_t(i + 1);
    }
    JsonObject day = doc["daily"];
    w.tMax = day["temperature_2m_max"][0] | w.temp;
    w.tMin = day["temperature_2m_min"][0] | w.temp;
    w.uvMax = day["uv_index_max"][0] | w.uv;
    const char* sr = day["sunrise"][0] | "";   // "2026-11-20T06:41"
    const char* ss = day["sunset"][0] | "";
    if (strlen(sr) >= 16) { memcpy(w.sunrise, sr + 11, 5); w.sunrise[5] = 0; }
    if (strlen(ss) >= 16) { memcpy(w.sunset, ss + 11, 5); w.sunset[5] = 0; }
    w.valid = true;
    w.fetchedMs = millis();
    w.version++;
    g_state.lastSyncEpoch = time(nullptr);
    Serial.printf_P(PSTR("[wx] %.1f C code %d hum %u\n"), w.temp, w.code, w.humidity);
    return true;
}

// CPCB NAQI sub-index breakpoints. The top band is open-ended officially; we map
// PM2.5 250..380 and PM10 430..600 onto 401..500 (common practice) and clamp.
static int subIndex(float c, const float* bp) {
    static const int I[7] = {0, 50, 100, 200, 300, 400, 500};
    if (c <= 0) return 0;
    for (int i = 0; i < 6; i++) {
        if (c <= bp[i + 1]) return int(I[i] + (I[i + 1] - I[i]) * (c - bp[i]) / (bp[i + 1] - bp[i]) + 0.5f);
    }
    return 500;
}

int WeatherService::naqiFromPm(float pm25, float pm10) {
    static const float BP25[7] = {0, 30, 60, 90, 120, 250, 380};
    static const float BP10[7] = {0, 50, 100, 250, 350, 430, 600};
    int a = subIndex(pm25, BP25), b = subIndex(pm10, BP10);
    return a > b ? a : b;
}

bool WeatherService::fetchAir() {
    Config& c = g_config.get();
    char url[220];
    snprintf_P(url, sizeof url,
             PSTR("http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.3f&longitude=%.3f"
             "&current=us_aqi,pm2_5,pm10&hourly=pm2_5,pm10&past_days=1&forecast_days=1&timezone=auto"), c.lat, c.lon);
    JsonDocument filter, doc;
    JsonObject f = filter["current"].to<JsonObject>();
    f["us_aqi"] = true; f["pm2_5"] = true; f["pm10"] = true;
    filter["hourly"]["pm2_5"] = true;
    filter["hourly"]["pm10"] = true;
    filter["utc_offset_seconds"] = true;
    if (!getJson(String(url), doc, filter)) return false;
    JsonObject cur = doc["current"];
    if (cur.isNull() || (cur["pm2_5"].isNull() && cur["pm10"].isNull())) return false;   // no data != GOOD
    AirData& a = g_state.air;
    a.pm25 = cur["pm2_5"] | 0.f;
    a.pm10 = cur["pm10"] | 0.f;
    a.usAqi = cur["us_aqi"] | -1;
    a.naqi = naqiFromPm(a.pm25, a.pm10);
    // 24 h history: hourly arrays start yesterday 00:00 (past_days=1), so "now" = 24 + local hour
    JsonArray h25 = doc["hourly"]["pm2_5"], h10 = doc["hourly"]["pm10"];
    a.histCount = 0; a.peak = -1;
    // the hourly series is in the *location's* local time (timezone=auto)
    time_t tt = time(nullptr) + int32_t(doc["utc_offset_seconds"] | 0);
    struct tm lt;
    gmtime_r(&tt, &lt);
    int nowIdx = 24 + lt.tm_hour;
    if (int(h25.size()) > nowIdx && int(h10.size()) > nowIdx) {
        for (int i = 0; i < 24; i++) {
            int idx = nowIdx - 23 + i;
            bool n25 = h25[idx].isNull(), n10 = h10[idx].isNull();
            // missing data is not "Good": -1 marks a gap (drawn neutral, ignored for the peak)
            a.hist[i] = (n25 && n10) ? -1 : int16_t(naqiFromPm(n25 ? 0.f : h25[idx].as<float>(), n10 ? 0.f : h10[idx].as<float>()));
        }
        a.hist[23] = a.naqi;                 // current reading for the last bar (before the peak scan)
        for (int i = 0; i < 24; i++)
            if (a.hist[i] > a.peak) { a.peak = a.hist[i]; a.peakHour = uint8_t((lt.tm_hour - 23 + i + 48) % 24); }
        a.histCount = 24;
    }
    a.valid = true;
    a.fetchedMs = millis();
    a.version++;
    Serial.printf_P(PSTR("[air] pm2.5 %.1f pm10 %.1f naqi %d us %d\n"), a.pm25, a.pm10, a.naqi, a.usAqi);
    return true;
}

static inline bool due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }   // wrap-safe

void WeatherService::loop(bool online) {
    if (!online) return;
    if (ESP.getMaxFreeBlockSize() < 6000) return;   // too fragmented for a safe fetch right now
    uint32_t now = millis();
    Config& c = g_config.get();
    // never start a (blocking) request right around a minute rollover: the clock must tick on time
    int sec = int(time(nullptr) % 60);
    if (sec >= 58 || sec <= 1) return;
    if (!c.geoResolved) {
        if (due(now, nextGeo_)) { if (!geocode()) nextGeo_ = now + 60000; else forceRefresh(); }
        return;   // one request per loop
    }
    if (c.geoFailed) return;
    if (due(now, nextWeather_)) {
        if (fetchWeather()) { failW_ = 0; nextWeather_ = now + WEATHER_EVERY; }
        else nextWeather_ = now + backoff(failW_++);
        return;
    }
    if (due(now, nextAir_)) {
        if (fetchAir()) { failA_ = 0; nextAir_ = now + AIR_EVERY; }
        else nextAir_ = now + backoff(failA_++);
    }
}
