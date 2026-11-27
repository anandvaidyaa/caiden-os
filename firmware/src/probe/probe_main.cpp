// CAIDEN OS - Milestone 1 hardware probe.
//
// Verifies, on the real device, what the stock firmware disassembly suggests:
//   display  ST7789 240x240, SPI MOSI13/SCLK14, CS15, DC0, RST2, mode 3, 40 MHz
//   backlight GPIO5 PWM, active-LOW
//   input    GPIO4 (INPUT_PULLUP) - suspected touch pad
// Also benchmarks the band renderer. Nothing is written to flash at runtime.
//
// Serial 115200 commands:
//   1..4 page   r rotate   i invert   p flip backlight polarity   + / - brightness
//   c toggle CS (15 <-> none)   m toggle SPI mode 0/3   f SPI 40/27/20 MHz
//   s status line   h help
// Pressing the touch pad (if it works) also advances the page.
#include <Arduino.h>
#include "../display/St7789.h"
#include "../graphics/Renderer.h"
#include "../graphics/fonts/Fonts.h"

static St7789 lcd;
static Renderer* R;
static PanelConfig cfg;
static uint8_t page = 0, brightness = 180;
static uint32_t lastFrameUs = 0, pressCount = 0, lastEdgeMs = 0;
static int lastIn = -1, lastIn16 = -1, a0 = 0;

static const uint16_t BG = rgb(0x06080C), TXT = rgb(0xF3F5F8), SUB = rgb(0x8B94A5),
                      FAINT = rgb(0x3A4250), ACC = rgb(0x6C8CFF), WARN = rgb(0xFFB25B);

static void drawCalibration(Canvas& c) {
    c.fill(BG);
    // 1 px frame on the very edge: all four sides must be visible (offset check)
    c.fillRect(0, 0, 240, 1, TXT); c.fillRect(0, 239, 240, 1, TXT);
    c.fillRect(0, 0, 1, 240, TXT); c.fillRect(239, 0, 1, 240, TXT);
    // corner swatches: TL red, TR green, BL blue, BR white (orientation + RGB order)
    c.fillRect(3, 3, 14, 14, rgb(0xFF0000)); c.fillRect(223, 3, 14, 14, rgb(0x00FF00));
    c.fillRect(3, 223, 14, 14, rgb(0x0000FF)); c.fillRect(223, 223, 14, 14, rgb(0xFFFFFF));
    c.text(120, 34, "CAIDEN OS", FONT_TITLE, TXT, Align::Center);
    c.text(120, 50, "M1 HARDWARE PROBE", FONT_LABEL, SUB, Align::Center, 1);
    // smooth gradient (dither check) and AA shapes
    c.vgradient(20, 62, 200, 30, rgb(0x0B1640), rgb(0x6C8CFF));
    c.arc(60, 132, 26, 5, 135, 380, rgb(0xA5D63A));
    c.fillCircle(120, 132, 20, ACC);
    c.fillRoundRect(152, 110, 64, 44, 12, rgb(0x171C25));
    c.text(184, 138, "24\xC2\xB0", FONT_NUM28, TXT, Align::Center);
    c.line(30, 178, 210, 168, 2, WARN);
    c.text(120, 200, "Text 15 \xC2\xB7 smooth anti-aliasing", FONT_BODY, TXT, Align::Center);
    char b[48];
    snprintf_P(b, sizeof b, PSTR("frame %lu ms  heap %u"), (unsigned long)(lastFrameUs / 1000), ESP.getFreeHeap());
    c.text(120, 220, b, FONT_MONO, SUB, Align::Center);
}

static void drawTouch(Canvas& c) {
    c.fill(BG);
    c.text(120, 40, "TOUCH PAD TEST", FONT_LABEL, SUB, Align::Center, 1);
    bool pressed = lastIn == 0;
    c.fillCircle(120, 110, 44, pressed ? ACC : rgb(0x171C25));
    c.ring(120, 110, 44, 2, pressed ? TXT : FAINT);
    c.text(120, 118, pressed ? "PRESSED" : "touch", FONT_BODYM, TXT, Align::Center);
    char b[48];
    snprintf_P(b, sizeof b, PSTR("GPIO4 = %d   presses %lu"), lastIn, (unsigned long)pressCount);
    c.text(120, 186, b, FONT_MONO, TXT, Align::Center);
    snprintf_P(b, sizeof b, PSTR("GPIO16 = %d   A0 = %d"), lastIn16, a0);
    c.text(120, 206, b, FONT_MONO, SUB, Align::Center);
}

static void drawBacklight(Canvas& c) {
    c.fill(BG);
    c.text(120, 40, "BACKLIGHT", FONT_LABEL, SUB, Align::Center, 1);
    char b[16]; snprintf_P(b, sizeof b, PSTR("%u"), brightness);
    c.text(120, 128, b, FONT_NUM44, TXT, Align::Center);
    c.fillRoundRect(30, 160, 180, 6, 3, FAINT);
    c.fillRoundRect(30, 160, 180.f * brightness / 255.f, 6, 3, WARN);
    c.text(120, 200, cfg.blActiveLow ? "polarity: active-LOW" : "polarity: active-HIGH", FONT_MONO, SUB, Align::Center);
    c.text(120, 218, "+ / -  to change,  p  to flip", FONT_SMALL, FAINT, Align::Center);
}

static void drawBench(Canvas& c) {
    static float ph = 0;
    c.fill(BG);
    c.glow(60 + 40 * sinf(ph), 70, 110, rgb(0x1A2A55), 200);
    c.arc(120, 120, 70, 6, -90, -90 + fmodf(ph * 60, 360), ACC);
    char b[32]; snprintf_P(b, sizeof b, PSTR("%lu ms"), (unsigned long)(lastFrameUs / 1000));
    c.text(120, 132, b, FONT_NUM28, TXT, Align::Center);
    c.text(120, 156, "full-frame render", FONT_SMALL, SUB, Align::Center);
    ph += 0.15f;
}

static void render() {
    switch (page) {
        case 0: lastFrameUs = R->draw(Rect(0, 0, 240, 240), drawCalibration); break;
        case 1: lastFrameUs = R->draw(Rect(0, 0, 240, 240), drawTouch); break;
        case 2: lastFrameUs = R->draw(Rect(0, 0, 240, 240), drawBacklight); break;
        case 3: lastFrameUs = R->draw(Rect(0, 0, 240, 240), drawBench); break;
    }
}

static void status() {
    Serial.printf_P(PSTR("{\"page\":%u,\"dc\":%d,\"rst\":%d,\"cs\":%d,\"bl\":%d,\"blActiveLow\":%d,\"rot\":%u,"
                  "\"invert\":%d,\"spiHz\":%lu,\"mode\":%u,\"bright\":%u,\"gpio4\":%d,\"gpio16\":%d,\"a0\":%d,"
                  "\"presses\":%lu,\"frameMs\":%lu,\"heap\":%u}\n"),
                  page, cfg.dc, cfg.rst, cfg.cs, cfg.bl, cfg.blActiveLow, cfg.rotation, cfg.invert,
                  (unsigned long)cfg.spiHz, cfg.spiMode, brightness, lastIn, lastIn16, a0,
                  (unsigned long)pressCount, (unsigned long)(lastFrameUs / 1000), ESP.getFreeHeap());
}

static void reinit() {
    lcd.begin(cfg);
    lcd.setBacklight(brightness);
    render();
    status();
}

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println(F("\n[CAIDEN] M1 hardware probe - h for help"));
    pinMode(4, INPUT_PULLUP);
    pinMode(16, INPUT);
    static Renderer r(lcd);
    R = &r;
    reinit();
}

void loop() {
    // --- input sampling (debounced edges logged to serial)
    int in = digitalRead(4), in16 = digitalRead(16);
    a0 = analogRead(A0);
    if (in != lastIn) {
        if (lastIn != -1) Serial.printf_P(PSTR("[IN] gpio4 %d -> %d at %lu ms\n"), lastIn, in, millis());
        if (lastIn == 1 && in == 0 && millis() - lastEdgeMs > 120) {
            pressCount++; lastEdgeMs = millis();
            if (page != 1) { page = (page + 1) % 4; }
        }
        lastIn = in;
        render();
    }
    if (in16 != lastIn16) {
        if (lastIn16 != -1) Serial.printf_P(PSTR("[IN] gpio16 %d -> %d\n"), lastIn16, in16);
        lastIn16 = in16;
    }

    if (Serial.available()) {
        char k = Serial.read();
        switch (k) {
            case '1': case '2': case '3': case '4': page = k - '1'; render(); break;
            case 'r': cfg.rotation = (cfg.rotation + 1) & 3; reinit(); break;
            case 'i': cfg.invert = !cfg.invert; lcd.setInvert(cfg.invert); status(); break;
            case 'p': cfg.blActiveLow = !cfg.blActiveLow; reinit(); break;
            case '+': brightness = brightness > 235 ? 255 : brightness + 20; lcd.setBacklight(brightness); if (page == 2) render(); status(); break;
            case '-': brightness = brightness < 20 ? 0 : brightness - 20; lcd.setBacklight(brightness); if (page == 2) render(); status(); break;
            case 'c': cfg.cs = cfg.cs >= 0 ? -1 : 15; reinit(); break;
            case 'm': cfg.spiMode = cfg.spiMode == 3 ? 0 : 3; reinit(); break;
            case 'f': cfg.spiHz = cfg.spiHz == 40000000 ? 27000000 : (cfg.spiHz == 27000000 ? 20000000 : 40000000); reinit(); break;
            case 's': status(); break;
            case 'h': Serial.println(F("1-4 page | r rot | i invert | p bl polarity | +/- bright | c cs | m spi mode | f spi MHz | s status")); break;
        }
    }

    static uint32_t tick = 0;
    if (page == 3 && millis() - tick > 30) { tick = millis(); render(); }        // benchmark animates
    if (page == 1 && millis() - tick > 250) { tick = millis(); render(); }       // live A0
}
