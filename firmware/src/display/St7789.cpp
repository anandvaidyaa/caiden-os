#include "St7789.h"
#include <SPI.h>

namespace {
constexpr uint8_t SWRESET = 0x01, SLPIN = 0x10, SLPOUT = 0x11,
                  INVOFF = 0x20, INVON = 0x21, DISPON = 0x29, CASET = 0x2A,
                  RASET = 0x2B, RAMWR = 0x2C, MADCTL = 0x36, COLMOD = 0x3A;
}

void St7789::begin_() {
    SPI.beginTransaction(SPISettings(cfg_.spiHz, MSBFIRST, cfg_.spiMode == 0 ? SPI_MODE0 : SPI_MODE3));
    if (cfg_.cs >= 0) digitalWrite(cfg_.cs, LOW);
}
void St7789::end_() {
    if (cfg_.cs >= 0) digitalWrite(cfg_.cs, HIGH);
    SPI.endTransaction();
}
void St7789::cmd(uint8_t c) {
    digitalWrite(cfg_.dc, LOW);
    SPI.write(c);
    digitalWrite(cfg_.dc, HIGH);
}
void St7789::data(const uint8_t* d, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) SPI.write(d[i]);
}

void St7789::begin(const PanelConfig& cfg) {
    cfg_ = cfg;
    pinMode(cfg_.dc, OUTPUT);
    digitalWrite(cfg_.dc, HIGH);
    if (cfg_.cs >= 0) { pinMode(cfg_.cs, OUTPUT); digitalWrite(cfg_.cs, HIGH); }
    if (cfg_.bl >= 0) { pinMode(cfg_.bl, OUTPUT); setBacklight(0); }
    SPI.begin();

    if (cfg_.rst >= 0) {
        pinMode(cfg_.rst, OUTPUT);
        digitalWrite(cfg_.rst, HIGH); delay(5);
        digitalWrite(cfg_.rst, LOW);  delay(20);
        digitalWrite(cfg_.rst, HIGH); delay(150);
    }
    // Stock CAIDEN init table, recovered from the factory firmware (file 0xBED33):
    // cmd, nargs (|0x80 = delay byte follows), args...  - keeps the factory gamma.
    static const uint8_t PROGMEM init[] = {
        0x11, 0x80, 255,                                   // SLPOUT
        0x3A, 0x81, 0x55, 10,                              // COLMOD 16-bit
        0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,             // porch
        0xB7, 1, 0x35,  0xBB, 1, 0x1F,  0xC0, 1, 0x2C,     // gate / VCOM / LCM
        0xC2, 1, 0x01,  0xC3, 1, 0x12,  0xC4, 1, 0x20,     // VDV/VRH
        0xC6, 1, 0x0F,  0xD0, 2, 0xA4, 0xA1,               // frame rate 60 Hz, power
        0xE0, 14, 0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2D,
        0xE1, 14, 0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0B,0x16,0x14,0x2F,0x31,
        0xE4, 3, 0x1D, 0x00, 0x00,                         // gate lines = 240
        0x13, 0x80, 10,                                    // NORON
    };
    begin_(); cmd(SWRESET); end_(); delay(150);
    for (size_t i = 0; i < sizeof(init);) {
        uint8_t c = pgm_read_byte(&init[i++]);
        uint8_t n = pgm_read_byte(&init[i++]);
        bool hasDelay = n & 0x80; n &= 0x7F;
        uint8_t a[16];
        for (uint8_t k = 0; k < n; k++) a[k] = pgm_read_byte(&init[i++]);
        begin_(); cmd(c); data(a, n); end_();
        if (hasDelay) { uint8_t ms = pgm_read_byte(&init[i++]); delay(ms == 255 ? 500 : ms); }
    }
    setRotation(cfg_.rotation);
    setInvert(cfg_.invert);
    // Vertical scroll area = whole 320-row RAM (TFA 0, VSA 320, BFA 0), start at row 0.
    begin_();
    cmd(0x33); { uint8_t d[6] = {0, 0, uint8_t(RAM_ROWS >> 8), uint8_t(RAM_ROWS & 0xFF), 0, 0}; data(d, 6); }
    end_();
    setScroll(0);
    mapBase_ = 0;
    begin_(); cmd(DISPON); end_();
    delay(20);
}

void St7789::setScroll(uint16_t ramRow) {
    scroll_ = ramRow % RAM_ROWS;
    uint8_t d[2] = {uint8_t(scroll_ >> 8), uint8_t(scroll_ & 0xFF)};
    begin_(); cmd(0x37); data(d, 2); end_();
}

void St7789::setRotation(uint8_t r) {
    cfg_.rotation = r & 3;
    // MADCTL: MY MX MV ML RGB ...  The panel RAM is 240x320, a 240x240 glass
    // shows rows 0..239 - rotations that flip Y need an 80 row offset.
    static const uint8_t mad[4] = {0x00, 0x60, 0xC0, 0xA0};
    xoff_ = yoff_ = 0;
    if (cfg_.height == 240 && cfg_.width == 240) {
        if (cfg_.rotation == 2) yoff_ = 80;
        if (cfg_.rotation == 3) xoff_ = 80;
    }
    begin_();
    cmd(MADCTL); data(&mad[cfg_.rotation], 1);
    end_();
}

void St7789::setInvert(bool on) {
    cfg_.invert = on;
    begin_(); cmd(on ? INVON : INVOFF); end_();
}

void St7789::setBacklight(uint8_t level) {
    blLevel_ = level;
    if (cfg_.bl < 0) return;
    uint8_t v = cfg_.blActiveLow ? 255 - level : level;
    // Stock firmware: analogWrite(5, 1023 - 10*brightness) at 1 kHz -> active-LOW PWM.
    if (v == 0) { analogWrite(cfg_.bl, 0); digitalWrite(cfg_.bl, LOW); }
    else if (v == 255) { analogWrite(cfg_.bl, 0); digitalWrite(cfg_.bl, HIGH); }
    else { analogWriteRange(255); analogWriteFreq(1000); analogWrite(cfg_.bl, v); }
}

void St7789::sleep(bool on) {
    begin_(); cmd(on ? SLPIN : SLPOUT); end_();
    delay(on ? 5 : 120);
}

void St7789::setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x0 = x + xoff_, x1 = x + w - 1 + xoff_;
    uint16_t y0 = uint16_t((y + mapBase_) % RAM_ROWS) + yoff_;
    uint16_t y1 = y0 + h - 1;
    uint8_t ca[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
    uint8_t ra[4] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
    begin_();
    cmd(CASET); data(ca, 4);
    cmd(RASET); data(ra, 4);
    cmd(RAMWR);
    end_();
}

void St7789::pushPixelsSwap(uint16_t* px, uint32_t count) {
    uint32_t t0 = micros();
    for (uint32_t i = 0; i < count; i++) px[i] = (px[i] >> 8) | (px[i] << 8);
    uint32_t t1 = micros();
    begin_();
    SPI.writeBytes(reinterpret_cast<uint8_t*>(px), count * 2);
    end_();
    swapUs += t1 - t0;
    spiUs += micros() - t1;
}

void St7789::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    uint16_t room = rowsUntilWrap(y);
    if (h > room) { fillRect(x, y, w, room, color); fillRect(x, y + room, w, h - room, color); return; }
    setWindow(x, y, w, h);
    uint16_t line[64];
    uint16_t sw = (color >> 8) | (color << 8);
    for (auto& p : line) p = sw;
    uint32_t n = uint32_t(w) * h;
    begin_();
    while (n) {
        uint32_t k = n > 64 ? 64 : n;
        SPI.writeBytes(reinterpret_cast<uint8_t*>(line), k * 2);
        n -= k;
    }
    end_();
}
