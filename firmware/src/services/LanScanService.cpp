#include "LanScanService.h"
#include <ESP8266WiFi.h>
#include <time.h>
#include <string.h>
#include "../state/AppState.h"
extern "C" {
#include <lwip/etharp.h>
#include <lwip/netif.h>
}

LanScanService g_lan;

static const uint32_t SCAN_EVERY_MS = 5UL * 60 * 1000;
static const uint8_t BATCH = 6;          // lwIP keeps only 10 ARP entries: ask a few, then collect
static const uint32_t BATCH_WAIT_MS = 350;

void LanScanService::loop(bool online) {
    const uint32_t now = millis();
    if (!online) { scanning_ = false; return; }
    if (!scanning_) {
        if (int32_t(now - nextScan_) >= 0) startScan();
        return;
    }
    step(now);
}

void LanScanService::startScan() {
    const IPAddress ip = WiFi.localIP(), gw = WiFi.gatewayIP(), mask = WiFi.subnetMask();
    if (!ip.isSet() || mask[0] != 255 || mask[1] != 255 || mask[2] != 255) {   // only a /24 (or smaller)
        nextScan_ = millis() + SCAN_EVERY_MS;
        return;
    }
    prefix_[0] = ip[0]; prefix_[1] = ip[1]; prefix_[2] = ip[2];
    self_ = ip[3];
    gw_ = (gw[0] == ip[0] && gw[1] == ip[1] && gw[2] == ip[2]) ? gw[3] : 0;
    foundN_ = 0;
    nextHost_ = 1;
    batchSent_ = false;
    scanning_ = true;
}

void LanScanService::step(uint32_t now) {
    struct netif* nif = netif_default;
    if (!nif) { scanning_ = false; nextScan_ = now + SCAN_EVERY_MS; return; }
    if (!batchSent_) {
        if (nextHost_ == 0 || nextHost_ > 254) { finish(); return; }
        for (uint8_t k = 0; k < BATCH && nextHost_ <= 254 && nextHost_ != 0; k++, nextHost_++) {
            if (nextHost_ == self_) continue;
            ip4_addr_t a;
            IP4_ADDR(&a, prefix_[0], prefix_[1], prefix_[2], nextHost_);
            etharp_request(nif, &a);
        }
        batchSent_ = true;
        batchAt_ = now;
        return;
    }
    if (now - batchAt_ < BATCH_WAIT_MS) return;
    harvest();
    batchSent_ = false;
}

void LanScanService::harvest() {
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t* ip = nullptr;
        struct netif* nif = nullptr;
        struct eth_addr* mac = nullptr;
        if (!etharp_get_entry(i, &ip, &nif, &mac) || !ip || !mac) continue;
        const uint32_t a = ip4_addr_get_u32(ip);   // network order: byte 0 is the first octet
        const uint8_t o0 = a & 0xFF, o1 = (a >> 8) & 0xFF, o2 = (a >> 16) & 0xFF, o3 = (a >> 24) & 0xFF;
        if (o0 != prefix_[0] || o1 != prefix_[1] || o2 != prefix_[2] || o3 == self_ || o3 == 0 || o3 == 255) continue;
        bool dup = false;
        for (uint8_t j = 0; j < foundN_ && !dup; j++) dup = found_[j].ip == o3;
        if (dup || foundN_ >= MAX_FOUND) continue;
        Found& f = found_[foundN_++];
        f.ip = o3;
        memcpy(f.mac, &mac->addr[3], 3);
    }
}

void LanScanService::finish() {
    harvest();
    scanning_ = false;
    nextScan_ = millis() + SCAN_EVERY_MS;
    LanData& l = g_state.bridge.ln;
    // new = a MAC not seen in an earlier scan since the clock started (the first scan is the baseline)
    bool isNew[MAX_FOUND];
    uint8_t newN = 0;
    for (uint8_t i = 0; i < foundN_; i++) {
        bool known = false;
        for (uint8_t j = 0; j < seenN_ && !known; j++) known = !memcmp(seen_[j], found_[i].mac, 3);
        isNew[i] = firstDone_ && !known;
        if (isNew[i]) newN++;
        if (!known) {
            if (seenN_ < SEEN_MAX) memcpy(seen_[seenN_++], found_[i].mac, 3);
            else { memmove(seen_[0], seen_[1], 3 * (SEEN_MAX - 1)); memcpy(seen_[SEEN_MAX - 1], found_[i].mac, 3); }
        }
    }
    firstDone_ = true;
    l.count = foundN_;
    l.newCount = uint8_t(l.newCount + newN);
    l.gwMs = -1;
    for (uint8_t i = 0; i < foundN_; i++) if (found_[i].ip == gw_) l.gwMs = 0;   // router answered
    // shown list: new devices first, then the router, then the rest by address
    l.devN = 0;
    for (int pass = 0; pass < 3 && l.devN < 8; pass++) {
        for (uint8_t i = 0; i < foundN_ && l.devN < 8; i++) {
            const bool router = found_[i].ip == gw_;
            if ((pass == 0) != isNew[i] || (pass == 1 && !router) || (pass == 2 && router)) continue;
            LanDevice& d = l.dev[l.devN++];
            d.ip = found_[i].ip;
            d.isNew = isNew[i];
            if (router) snprintf(d.name, sizeof d.name, "Router");
            else snprintf(d.name, sizeof d.name, "%02x:%02x:%02x", found_[i].mac[0], found_[i].mac[1], found_[i].mac[2]);
        }
    }
    const time_t t = time(nullptr);
    g_state.bridge.lnAt = t > 1600000000 ? uint32_t(t) : 0;
    g_state.bridge.version++;
    Serial.printf("[lan] %u devices on the network (%u new)\n", unsigned(foundN_), unsigned(newN));
}
