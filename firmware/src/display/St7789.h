#pragma once
// Minimal, fast ST7789 driver for the CAIDEN clock.
// Pins are runtime-configurable so the probe firmware can test candidate wirings
// without reflashing. SPI data/clock use the ESP8266 HSPI block (MOSI=13, SCLK=14).
#include <Arduino.h>

struct PanelConfig {
    int8_t   dc        = 0;      // data/command
    int8_t   rst       = 2;      // reset (-1 = none)
    int8_t   cs        = 15;     // chip select (stock fw drives GPIO15)
    int8_t   bl        = 5;      // backlight (-1 = always on)
    bool     blActiveLow = true;  // stock: analogWrite(5, 1023 - 10*b)
    uint8_t  rotation  = 0;      // 0..3
    bool     invert    = true;   // IPS ST7789 panels need colour inversion
    uint32_t spiHz     = 40000000;
    uint8_t  spiMode   = 3;      // mode 3 is required when CS is tied low
    uint16_t width     = 240;
    uint16_t height    = 240;
};

class St7789 {
public:
    void begin(const PanelConfig& cfg);
    void setRotation(uint8_t r);
    void setInvert(bool on);
    void setBacklight(uint8_t level);          // 0..255, polarity handled here
    void sleep(bool on);

    // Pixel transfer. Pixels are native RGB565; they are byte-swapped in place
    // (buffer contents are destroyed) for the big-endian SPI stream.
    // Rows [y, y+h) must not cross the RAM wrap: use rowsUntilWrap(y) to split.
    void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    // --- hardware vertical scrolling (rotation 0 only). Panel RAM has 320 rows, the
    // glass shows 240 of them starting at the scroll register. Logical row y is stored
    // at RAM row (y + mapBase) % 320. Push transitions animate the scroll register while
    // drawing only the newly revealed rows, so they cost one frame of rendering in total.
    static constexpr uint16_t RAM_ROWS = 320;
    void setScroll(uint16_t ramRow);                    // first RAM row shown at the top
    void setMapBase(uint16_t ramRow) { mapBase_ = ramRow % RAM_ROWS; }
    uint16_t scroll() const { return scroll_; }
    uint16_t mapBase() const { return mapBase_; }
    uint16_t rowsUntilWrap(uint16_t y) const { return RAM_ROWS - (y + mapBase_) % RAM_ROWS; }
    void pushPixelsSwap(uint16_t* px, uint32_t count);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    const PanelConfig& config() const { return cfg_; }
    // Diagnostics: accumulated microseconds spent byte-swapping and in SPI transfers.
    uint32_t swapUs = 0, spiUs = 0;

private:
    void cmd(uint8_t c);
    void data(const uint8_t* d, uint8_t n);
    void begin_();
    void end_();
    PanelConfig cfg_;
    uint16_t xoff_ = 0, yoff_ = 0;
    uint8_t blLevel_ = 0;
    uint16_t scroll_ = 0, mapBase_ = 0;
};
