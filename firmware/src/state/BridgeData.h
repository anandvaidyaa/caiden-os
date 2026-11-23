#pragma once
// Data received from the cloud relay (relay/worker.js). Named "bridge" for history: the PC bridge
// it replaced sent the same document.
#include <stdint.h>

struct SpotifyData {
    bool     on = false, playing = false;
    char     title[64] = "", artist[64] = "";
    uint32_t progressMs = 0, durationMs = 0, atMs = 0;   // atMs = millis() when progress was sampled
    char     artId[44] = "";                             // artwork id (Spotify image hash, 40 hex)
    char     artReady[44] = "";                          // artwork id stored in /art.bin
    uint16_t c1 = 0, c2 = 0;                             // dominant artwork colours (RGB565)
};

struct MarketRow {
    char    name[14] = "", value[16] = "", prev[16] = "";
    float   change = 0, delta = 0;
    uint8_t spark[24] = {0};
    uint8_t sparkN = 0;
    int16_t prevPos = -1;                                // previous close on the 0..255 spark scale
};

struct NewsItem {
    char     cat[12] = "", head[112] = "", src[32] = "";
    int32_t  ageMin = -1;
};

struct CalEvent {
    char     title[44] = "", where[32] = "";
    uint32_t start = 0, end = 0;                         // unix time
    bool     allDay = false;
};

struct Innings { char team[8] = ""; int16_t runs = 0, wkts = 0; float overs = 0; };

struct CricketData {
    bool    live = false;
    char    type[8] = "";            // T20 / ODI / TEST
    uint8_t innings = 0;
    char    t1[8] = "", t2[8] = "";  // batting team, other team (short names)
    char    status[72] = "";
    Innings inn[2];                  // up to the two most recent innings
    int16_t target = 0, need = 0, balls = 0;
};

struct QuoteData { char text[152] = "", author[34] = ""; };

struct LanDevice { char name[18] = ""; uint8_t ip = 0; bool isNew = false; };

struct LanData {                     // home network scan by the bridge
    uint8_t   count = 0, newCount = 0, devN = 0;
    int16_t   gwMs = -1;             // router ping, -1 = no reply
    LanDevice dev[8];
};

struct BridgeData {
    bool      online = false;
    uint16_t  version = 0;          // bumps when content changes (screens redraw)
    uint16_t  spVersion = 0;        // bumps on track / play-state / art change
    uint32_t  lastOkMs = 0;
    bool      relayOk = false;      // last cloud relay request succeeded
    bool      spSetup = true;       // the relay has a Spotify sign-in
    // unix time each section last arrived (bridge or relay), 0 = unknown; headers show the age
    uint32_t  mkAt = 0, nwAt = 0, qtAt = 0, lnAt = 0;
    // cloud firmware update offered by the relay (/fw.json), already checked against this clock's
    // update key; empty fwV = nothing newer. Installed only when the user presses Install.
    char      fwV[12] = "";
    uint32_t  fwSize = 0;
    char      fwMd5[33] = "";
    char      hash[12] = "";     // bridge content hash: version bumps only when it changes
    SpotifyData sp;
    uint8_t   markets = 0;  MarketRow mk[5];
    uint8_t   news = 0;     NewsItem nw[8];
    uint8_t   events = 0;   CalEvent ca[3];
    uint8_t   notify = 0;   // upcoming reminders (events starting within an hour)
    CricketData cr;
    QuoteData qt;
    LanData   ln;
};
