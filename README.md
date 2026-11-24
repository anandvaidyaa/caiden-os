<p align="center"><img src="docs/images/hero_home.png" width="360" alt="CAIDEN OS home screen"></p>

<h1 align="center">CAIDEN OS</h1>

<p align="center">
Open firmware for the <b>CAIDEN Smart Weather Clock</b>, the small white cube with a 240×240 colour screen.<br>
Weather, air quality, Spotify, news, markets, live cricket and focus timers, set up from your phone<br>
and updated over the air from anywhere.
</p>

> **Unofficial.** This project isn't made or supported by the CAIDEN brand. Installing it replaces the clock's
> stock firmware. Back the stock firmware up first ([guide](docs/BACKUP_AND_RESTORE.md)) so you can always
> go back. You do this at your own risk.

## Contents

- [Screens](#screens)
- [Features](#features)
- [How it works](#how-it-works)
- [What you need](#what-you-need)
- [Install](#install)
- [Settings page](#settings-page)
- [Updates](#updates)
- [Security and privacy](#security-and-privacy)
- [Troubleshooting](#troubleshooting)
- [Project layout](#project-layout)
- [Credits and licence](#credits-and-licence)

## Screens

![Clock pages](docs/images/screens.png)

Four themes: Midnight Luxe, Aurora, Crystal and Hypertech.

![Themes](docs/images/themes.png)

The settings page on your phone (`http://caiden.local`), from left to right: the lock screen, Home, Display and Feeds.

![Settings page](docs/images/portal.png)

<sub>Clock images are renders from the CAIDEN design simulator, which uses the same drawing maths and fonts as
the firmware; the clock looks very close to them. Sample data is shown.</sub>

## Features

- **Home:** large clock with a sun-path ring (sunrise, sunset, the sun's position now), date, temperature,
  air quality, conditions and your city.
- **Weather:** temperature, feels-like, humidity, wind, UV and today's curve. From
  [Open-Meteo](https://open-meteo.com/); no account or key needed.
- **Air quality:** NAQI with PM2.5 and PM10, plus a 24-hour history.
- **Sun & Moon:** daylight arc and moon phase.
- **World clock:** London, New York, Tokyo and Dubai.
- **Spotify:** track, artist, album art and progress. Shows only while something is playing; a small animated
  icon appears in every page's header while music plays.
- **News:** up to 3 tabs, each a topic (world, India, business, technology, science, sport, health,
  entertainment) or your own search (for example your city).
- **Markets:** up to 4 Yahoo Finance symbols (NIFTY, SENSEX, stocks, ...) plus Bitcoin.
- **Cricket:** live scores of the team you follow, shown only while it plays. Free, with an optional key for
  overs and match status.
- **Quote of the day.**
- **Home network:** how many devices are on your network right now, the router, and new arrivals. The clock
  scans its own network every 5 minutes; nothing leaves your network.
- **Focus:** Pomodoro, countdown timer and stopwatch, controlled from your phone.
- **System:** Wi-Fi signal, IP address, uptime, free memory and the settings-page password.
- **Four themes**, brightness, ambient glow, 12/24-hour clock, night screensaver, page rotation (choose pages
  and timing).
- **Over-the-air updates from anywhere**, signed with your clock's own key; you press Install.
- **Password-protected settings page** (the clock makes the password and shows it on its screen).
- **Health report**: check version, memory and signal from anywhere, without USB.

## How it works

```
                        plain HTTP                           HTTPS
   ┌─────────────┐  ───────────────────►  ┌──────────────────────────┐  ──────►  BBC, Bing News, Yahoo Finance,
   │ CAIDEN OS   │                         │ Your cloud relay         │           CoinGecko, ZenQuotes, Spotify,
   │ (the clock) │  ◄───────────────────   │ Cloudflare Worker (free) │  ◄──────  Cricinfo, firmware updates
   └─────────────┘                         └──────────────────────────┘
      │      ▲
      │      │  http://caiden.local (password)       Open-Meteo: weather & air quality (plain HTTP, direct)
      ▼      │
   your phone or PC on the same Wi-Fi
```

The clock (an ESP8266 with about 11 KB of free memory) can't do HTTPS. Weather comes straight from
Open-Meteo over plain HTTP. Everything else goes through **your own relay**: a small Cloudflare Worker on your
free Cloudflare account that fetches the data over HTTPS and hands it to the clock. There is no shared server
and no PC needs to stay on. The clock works on any Wi-Fi: home, office or a phone hotspot.

## What you need

- A CAIDEN Smart Weather Clock (ESP8266EX, 4 MB flash, CH340C USB; see [docs/HARDWARE.md](docs/HARDWARE.md))
- A USB-C **data** cable (for the first install only)
- A computer with [Python 3](https://www.python.org/) and [PlatformIO Core](https://platformio.org/install/cli)
- 2.4 GHz Wi-Fi (the clock has no 5 GHz)
- For news, markets, Spotify, cricket and remote updates: a free [Cloudflare](https://dash.cloudflare.com/sign-up)
  account and [Node.js](https://nodejs.org/)

Install esptool (it brings pyserial with it):

```
python -m pip install esptool
```

## Install

### 1. Back up the stock firmware

Follow [docs/BACKUP_AND_RESTORE.md](docs/BACKUP_AND_RESTORE.md). It takes about 15 minutes, and it is the
only way back to the stock firmware. Keep the backup private: it contains your Wi-Fi password.

### 2. Build and flash CAIDEN OS (USB, once)

```
git clone https://github.com/<you>/<this-repo>.git
cd <this-repo>/firmware
python -I tools/gen_portal.py
pio run -e caiden
python -m esptool --port PORT --baud 115200 write-flash 0x0 .pio/build/caiden/firmware.bin
```

Replace `PORT` with the clock's port (for example `COM5`, `/dev/ttyUSB0` or `/dev/cu.wchusbserial*`; see the
backup guide). Use 115200 baud; faster speeds are unreliable on this clock. The first boot takes a few seconds
longer, because the clock sets up its storage.

### 3. Connect it to Wi-Fi

With no Wi-Fi saved, the clock starts its own setup network and shows its name and password on screen.

**Option A, phone:** join the network shown on the clock (`CAIDEN-xxxx`, password on the screen), open
`http://192.168.4.1`, then pick your Wi-Fi, enter its password, your city and time zone, and save. The clock
restarts and joins your network.

**Option B, USB:** keep the clock plugged in and run, from `firmware/`:

```
python tools/usb_setup.py
```

It asks for your Wi-Fi name and password (typed hidden, never printed), city, time zone, clock format and
theme, and sends them over the cable. Some Android phones won't open pages on a network without internet;
use this option then.

### 4. Open the settings page

On a phone or computer on the same Wi-Fi, open **http://caiden.local** (or the IP address shown on the clock's
System screen). The page asks for a password: it is on the clock's **System** screen, in the **Password**
tile. Press **Show it on the clock** if the clock is showing another page.

### 5. Set up your relay (recommended)

Follow [relay/README.md](relay/README.md) (about 5 minutes with `npx wrangler`). Then, on the settings page,
open **Feeds** and paste your relay's address into **Cloud relay**. News, markets and the quote appear within
a minute. Spotify and cricket are optional steps in the same guide.

### 6. Make it yours

On the settings page you can:
- **Display:** choose the theme, brightness, glow and 12/24-hour clock.
- **Pages:** choose which pages rotate and how long each stays, and set the night screensaver.
- **Feeds:** choose news tabs, market symbols and your cricket team.
- **System:** set your city and time zone.

## Settings page

| Tab | What it does |
|---|---|
| **Home** | Shows any page on the clock right now; live clock and status |
| **Display** | Theme, brightness, ambient glow, 12/24-hour clock |
| **Pages** | Rotation interval, how long a picked page stays, which pages rotate, night screensaver |
| **Feeds** | Cloud relay address, news tabs, markets, Bitcoin, news age, cricket team |
| **Focus** | Pomodoro (4 × 25 min), countdown timer, stopwatch, minutes focused today |
| **System** | Device info, firmware update, password, city and time zone, Wi-Fi, restart |

## Updates

After the first install, new versions don't need the cable. You build an image on your PC, sign it with your
clock's own key, publish it to your relay, then press **Install** on the settings page. The clock downloads it
(about 40 seconds), checks the signature and checksum, and restarts. A failed download changes nothing.

Full steps, including the one-time key setup and home-network updates: [docs/UPDATES.md](docs/UPDATES.md).

Check what the clock runs, from anywhere: `http://<your relay>/clock.json`.

## Security and privacy

- **Settings page:** locked with a password that the clock makes itself and shows on its System screen.
  Five wrong guesses lock login for a while. The page uses plain HTTP (the chip can't do HTTPS), so use it on a
  Wi-Fi network you trust.
- **Updates:** only images signed with your clock's own secret key (`firmware/tools/ota_key.txt`, kept on your
  PC) install, only newer versions, and only after you press Install.
- **Your relay** runs on your own Cloudflare account. Spotify sign-in, keys and the update image live there.
  Anyone who knows its address can read the same public data, your current Spotify track and the health
  report, so don't publish the address.
- **Never commit or share:**
  - `backups/`: the stock firmware dump holds your Wi-Fi password;
  - `firmware/tools/ota_key.txt`;
  - `relay/wrangler.toml`;
  - your `ADMIN_KEY` and `CRICKET_KEY`.

  `.gitignore` already excludes the files; the keys live only in Cloudflare.
- **Setup network:** WPA2, with a random password shown on the clock.

## Troubleshooting

| Problem | Try |
|---|---|
| `esptool` can't connect | Use a data cable (not charge-only), another USB port, and `--baud 115200`. Unplug and replug. |
| `caiden.local` doesn't open | Use the IP address from the clock's System screen. Some Android versions don't resolve `.local` names. |
| Clock shows the setup screen | It can't reach the saved Wi-Fi (wrong password, 5 GHz-only network, out of range). Set it up again (Install step 3). |
| News, markets or quote missing | Check the relay address under **Feeds**, then open `http://<relay>/state.json` in a browser. |
| Spotify stays empty | Repeat the Spotify sign-in link from the relay guide; check the Redirect URI matches your relay exactly. |
| Update fails | Check `http://<relay>/clock.json` for `ue` (the last update error). A weak Wi-Fi signal (`ss` below -80) slows or breaks downloads. |
| Anything else | Read the clock's log over USB: `python -I tools/serial_cmd.py '{"cmd":"status"}'` from `firmware/`. |
| Want the stock firmware back | [docs/BACKUP_AND_RESTORE.md](docs/BACKUP_AND_RESTORE.md#restore) |

## Project layout

```
firmware/              CAIDEN OS (PlatformIO, Arduino core for ESP8266)
  src/                 display driver, graphics engine, screens, Wi-Fi, services
  portal/index.html    the settings page (built into the firmware by tools/gen_portal.py)
  tools/               install, Wi-Fi setup, update and debug scripts
  fonts/               where to get the fonts, if you want to change them
relay/                 the Cloudflare Worker (worker.js) and its setup guide
docs/                  hardware, backup/restore and update guides; images for this README
```

## Credits and licence

Weather and air quality: [Open-Meteo](https://open-meteo.com/). News: BBC and The Hindu RSS feeds and Bing News search. Markets: Yahoo Finance and
CoinGecko. Quotes: ZenQuotes. Cricket: ESPNcricinfo's live-score feed or [CricketData.org](https://cricketdata.org/).
Album art resizing: [wsrv.nl](https://wsrv.nl/). Fonts: Inter, Outfit, Ubuntu and Source Code Pro.

Code: [MIT licence](LICENSE). The fonts keep their own licences (SIL OFL 1.1 and the Ubuntu Font Licence).
