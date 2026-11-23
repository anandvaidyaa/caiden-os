#pragma once
// Wi-Fi lifecycle: STA with auto-reconnect; falls back to a WPA2 setup access point
// (captive portal) when no credentials are saved, the network can't be reached at boot,
// or the link stays down for 5 minutes. While the AP is up, STA retries happen only when
// no phone is connected, so the AP channel stays stable during setup.
#include <Arduino.h>

class NetworkManager {
public:
    void begin();
    void loop();
    bool online() const;
    bool restartPending() const { return restartAt_ != 0; }
    // Apply settings received over USB serial (same rules as the web form) and restart.
    bool applySetup(const char* ssid, const char* pass, const char* city, const char* tz, int h24, const char* theme);
    // Wireless updates: per-device key (created on first use, stored in /ota.key). Read it only
    // over USB ({"cmd":"otakey"}); tools/ota_update.py keeps a copy on the PC. The key itself
    // never crosses Wi-Fi: each update signs a one-time nonce with it (HMAC-SHA256).
    const char* otaKey();
    void (*onOtaProgress)() = nullptr;   // called while an update streams in (redraw the screen)
    // Settings-page password (home network): made by the clock, shown on its System screen, so
    // whoever can see the clock can log in. "New password" on the page makes another one.
    const char* webPassword();
    bool authed();                            // the request carries a valid login token
private:
    void startAP();
    void stopAP();
    void setupRoutes();
    void setMode(uint8_t m);
    void cacheScan();
    void goOnline();
    void otaUpload();
    void otaDrainClients();
    void sendSetupPage(const char* note);
    String setupPageHtml(const char* note);
    String setupSavedHtml(const String& ssid);
    void apHttpLoop();
    bool otaAuthOk(uint32_t size, const String& md5);
    void otaReject(const char* why);
    void otaFail(const char* why);
    uint32_t connectStart_ = 0, restartAt_ = 0, lastRssi_ = 0, offlineSince_ = 0;
    uint32_t nextStaTry_ = 0, staTryUntil_ = 0;
    bool apActive_ = false, routes_ = false, staTrying_ = false;
    String scanJson_;
    char otaKey_[33] = "";
    char otaErr_[32] = "";
    bool otaAuthed_ = false;
    uint8_t otaLastPct_ = 0;
    char otaNonce_[33] = "";
    uint32_t otaNonceAt_ = 0;
    String sessionToken();
    bool loginLocked(uint32_t now);
    void loginFailed(uint32_t now);
    uint8_t loginFails_ = 0;
    uint32_t lockUntil_ = 0;
};

extern NetworkManager g_net;
