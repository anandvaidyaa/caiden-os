# Hardware: CAIDEN Smart Weather Clock

What CAIDEN OS expects to find inside the clock. Everything here was confirmed on a real unit:
with read-only `esptool` queries, by disassembling the stock firmware, and by test firmware on the device.

Status key: ✅ confirmed on the device · 🟡 strong evidence · ❓ unknown

## Board

| Part | Marking | Notes |
|---|---|---|
| Microcontroller | **ESP8266EX** | 26 MHz crystal, 2.4 GHz Wi-Fi only (no 5 GHz), PCB antenna |
| Flash | **T25S32** | 4 MB, JEDEC 0x5E / 0x4016, DIO, 40 MHz |
| USB-to-serial | **CH340C** | USB-C; `esptool` resets into the bootloader by itself (DTR/RTS) |
| Regulator | **AMS1117-3.3** | 5 V USB to 3.3 V |
| Backlight switch | SOT-23 "AFCP" | probably a P-MOSFET; fits the active-low backlight |
| Display | FPC `WA54BC049I-10Z` | 1.54", 240×240 IPS |

## Display ✅

| Item | Value |
|---|---|
| Controller | **ST7789**, IPS (needs INVON 0x21), 240×240, MADCTL 0x00, no RAM offset |
| SPI | hardware HSPI: MOSI **GPIO13**, SCLK **GPIO14**, **mode 3**, **40 MHz** |
| CS / DC / RST | **GPIO15** / **GPIO0** / **GPIO2** (reset: high 5 ms, low 20 ms, high 150 ms) |
| Gamma | the factory E0/E1 tables, reused by CAIDEN OS |

## Backlight ✅

**GPIO5**, PWM, **active-low** (stock firmware: `analogWrite(5, 1023 - 10 * brightness)`).

## Touch pad ❌

The round metal pad does not work on the units tested. The stock firmware sets `GPIO4` as `INPUT_PULLUP`,
but GPIO4 never changed when the pad or the case was touched. CAIDEN OS doesn't need touch: pages rotate by
themselves, and the settings page can show any page. GPIO4 is still watched, harmlessly.

## Pin map

```
GPIO13 MOSI   GPIO14 SCLK   GPIO15 CS   GPIO0 DC   GPIO2 RST
GPIO5  backlight (PWM, active-low)      GPIO4 pad (input, pull-up, not working)
GPIO12 MISO (unused)   GPIO16 free   A0 free
```

GPIO0, GPIO2 and GPIO15 are boot-strapping pins. The board's pull resistors are fine, because CAIDEN OS
drives them only after boot.

## Stock flash layout ✅

| Address | Contents |
|---|---|
| 0x000000 | eboot bootloader |
| 0x001000 | app (about 845 KB, ESP8266 SDK 2.2.2, firmware "SD V2.5.7") |
| 0x100000–0x3FA000 | LittleFS (web files, weather icons) |
| 0x3FA000, 0x3FB000 | stock settings: **your Wi-Fi name and password, city and an OpenWeatherMap key, in plain text** |
| 0x3FC000–0x3FFFFF | SDK RF calibration and system parameters |

> ⚠️ Because of those settings sectors, a backup of your clock holds your Wi-Fi password. Keep it private:
> never upload it, attach it to an issue or share it. See [BACKUP_AND_RESTORE.md](BACKUP_AND_RESTORE.md).

CAIDEN OS uses the "4M2M" layout: app at 0x0 (up to 1 MB), room for one update image, and a 2 MB LittleFS
at 0x200000. The first boot formats that LittleFS area; the stock data stays in your backup.
