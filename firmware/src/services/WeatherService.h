#pragma once
// Open-Meteo weather + air quality over plain HTTP (no key, no TLS -> ~2 KB RAM per request).
// One request per loop() call at most, with short timeouts, so the UI never stalls for long.
#include <Arduino.h>

class WeatherService {
public:
    void loop(bool online);
    void forceRefresh() { nextWeather_ = nextAir_ = millis(); }
    static int naqiFromPm(float pm25, float pm10);   // Indian NAQI estimate (CPCB breakpoints)
private:
    bool geocode();
    bool fetchWeather();
    bool fetchAir();
    uint32_t nextWeather_ = 0, nextAir_ = 0, nextGeo_ = 0;
    uint8_t failW_ = 0, failA_ = 0;
};

extern WeatherService g_weather;
