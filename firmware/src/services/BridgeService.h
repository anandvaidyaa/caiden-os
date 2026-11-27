#pragma once
// Cloud relay client (relay/worker.js on Cloudflare, plain HTTP). Everything that needs HTTPS or an
// account comes through it: news, markets, quote and cricket (/state.json, every 10 min), Spotify
// (/sp.json, every 5 s while a player is active) and album art (/art.bin, 120x120 RGB565, 28.8 KB,
// streamed to LittleFS /art.bin in 512 B chunks - the image is never held in RAM). Spotify polling is
// non-blocking (spStart/spPoll); the other requests are short and infrequent, so they still wait.
// The class keeps its old name: the PC bridge it replaced served the same documents.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

class BridgeService {
public:
    void loop(bool online);
    bool artRow(int16_t row, uint16_t* out, int16_t x0, int16_t w);   // RowSource for the artwork
    void closeArt();
    void relayChanged();                  // relay address or feed settings changed: ask again now
    // Cloud firmware update: the relay offers an image (/fw.json + /fw.bin) published with
    // tools/publish_update.py. It counts only if its HMAC matches this clock's update key and it is
    // newer than the running version. requestInstall() runs it on the next loop pass (user action).
    void checkUpdateNow() { fwNext_ = millis(); }
    bool requestInstall();
    static constexpr int16_t ART = 120;
private:
    bool fetchFwMeta();
    bool fetchDuco();
    void installUpdate();
    uint32_t fwNext_ = 120000;
    WiFiClient spCli_;
    uint8_t spSt_ = 0;                    // 0 idle, 1 reading headers, 2 reading the body
    uint32_t spSince_ = 0;
    char spLine_[40];
    uint8_t spLineN_ = 0;
    int16_t spLen_ = -1, spCode_ = 0;
    bool spKeep_ = true;
    char* spBody_ = nullptr;              // allocated only while a reply is being read
    uint16_t spGot_ = 0;
    uint32_t ducoNext_ = 0;
    bool ducoWanted_ = false;
    bool installReq_ = false;
    bool getJson(const String& path, uint16_t timeoutMs, JsonDocument& doc);
    bool fetchState();
    void applySpotify(JsonDocument& doc);
    // Spotify is polled every 5 s while music plays. A blocking request froze the clock ~0.6 s each time,
    // so the request is sent on a kept-open connection and the reply is collected over later loop passes.
    bool spStart();                       // send GET /sp.json; false if the relay can't be reached
    int8_t spPoll(uint32_t now);          // 0 still waiting, 1 done (applied), -1 failed
    void spClose();
    bool fetchArt(const char* id);
    void applyState(JsonDocument& doc);
    void okNow();
    uint32_t stateNext_ = 0, spNext_ = 0, artRetryAt_ = 0;
    uint8_t fails_ = 0, artFails_ = 0;
    char artFailId_[44] = "";
    char hash_[12] = "";
};

extern BridgeService g_bridge;
