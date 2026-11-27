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

- [What's new](#whats-new)
- [Screens](#screens)
- [Features](#features)
- [How it works](#how-it-works)
- [What you need](#what-you-need)
- [Install](#install)
- [Settings page](#settings-page)
- [Mining (hobby)](#mining-hobby)
- [Updates](#updates)
- [Security and privacy](#security-and-privacy)
- [Troubleshooting](#troubleshooting)
- [Project layout](#project-layout)
- [Credits and licence](#credits-and-licence)

## What's new

The latest firmware is **0.9.5**. Clocks on 0.7.8 or newer update over the air from the System tab; older ones need one USB flash.

### Mining page: a miner at work

![Mining page](docs/images/mining.png)

The Mining page shows a little miner with a helmet lamp swinging a pickaxe at a crystal rock:
- **Swing speed:** faster as the hash rate rises.
- **Sparks:** every strike throws them.
- **Accepted shares:** pop a **D** coin out of the rock into the DUCO-earned counter.
- **Rejected shares:** make him say "Oops".
- **Paused:** his speech bubble says why ("Low memory", "Finding a pool...", ...).

Below the scene:
- hash rate;
- accepted and rejected shares, with the percentage good;
- a 24-hour line;
- today's INR value and your DUCO total;
- the odds of this clock finding a Bitcoin block today.

Only the arm, the sparks and the flying coin are redrawn, so mining keeps most of the processor.

### Badges in every page header

![Header badges](docs/images/badges.png)

While mining is on, a small gold pickaxe taps in the header of every page. It turns grey while mining is paused.
It sits next to the Spotify equaliser, which still shows whenever music plays. On Home, both appear in the
bottom-left corner.

The header lays out right to left: the time (or LIVE, or how old the data is), then the badges, then the page
title, which gets shortened with "…" if it doesn't fit. Nothing overlaps.

### Home network page and a password on the clock

![Network and System pages](docs/images/network_system.png)

- **Network page:** how many devices are on your network right now, whether the router answers, devices that
  joined since the clock started, and up to 8 devices by the end of their MAC address. The clock finds them
  itself every 5 minutes; nothing leaves your network.
- **System page:** now shows the settings-page **password** that the clock made, in place of "Last sync".

### Settings page: Mining and Password cards

![Mining and Password cards](docs/images/portal_mining_password.png)

- **Mining card** (Feeds tab):
  - switch it on and add your Duino-Coin account;
  - live hash rate, shares, difficulty and earnings;
  - what the pool itself sees for your rig;
  - the Bitcoin odds and a 24-hour chart.
- **Password card** (System tab): show the password on the clock, make a new one (logs out every other browser),
  or log out of this browser.

### Faster and leaner

| | Before | Now |
|---|---|---|
| Mining speed (real, in the clock's spare time) | not measured, and every share rejected (0.8.7) | **33–39 kH/s**, almost no rejects |
| Mining code speed while hashing | generic SHA-1 | **54.7 kH/s**: one-block kernel, precomputed rounds, in fast IRAM |
| Free memory on the clock | 10.5 KB | **12.0 KB** (183 log and format strings moved to flash) |
| Freeze each Spotify check (every 5 s while playing) | about 0.6 s | **none**: the reply is read in the background |
| Cloud update download | 161 s | **about 40 s** |

### Other changes

- **News:** Google News started blocking Cloudflare, so the relay now reads BBC and The Hindu RSS for topics and
  Bing News for your own searches.
- **Health report:** `http://<your relay>/clock.json` shows the version, uptime, memory, Wi-Fi signal, last
  restart reason, the last update error, and mining speed and shares, so you can check the clock without USB.
- **Connection page removed:** its "speed" was the clock's own link, never your internet line.
- **Home screen:** the conditions line ("Clear · Moderate air") fits inside the air-quality ring.

<details>
<summary>Version history</summary>

| Version | Change |
|---|---|
| 0.9.5 | The Mining card's 24-hour chart shows (it stayed hidden) |
| 0.9.4 | Faster mining kernel, 15 ms mining slices on still pages, `minebench` USB command, quieter pickaxe badge |
| 0.9.2 | Spotify checks no longer freeze the clock |
| 0.9.1 | 183 strings moved from RAM to flash: about 1.5 KB more free memory |
| 0.9.0 | One-block SHA-1 mining kernel in IRAM: 20–33 kH/s real rate |
| 0.8.9 | Mining pickaxe badge in every header; header lays itself out so nothing overlaps |
| 0.8.8 | Animated miner page; fixed every share being rejected ("Modified hashrate") |
| 0.8.7 | Duino-Coin hobby miner (off by default) and the Mining card |
| 0.8.6 | Home network page (device count) |
| 0.8.5 | Connection page removed |
| 0.8.4 | Health report at `/clock.json` |
| 0.8.3 | Settings-page password made by the clock, shown on the System page |
| 0.8.2 | Fixed the status reply sometimes being cut off |
| 0.8.1 | Conditions line on Home fits inside the gauge |
| 0.8.0 | Faster cloud updates |
</details>

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
- **Spotify:** track, artist, album art and progress. Shows only while something is playing; an animated
  equaliser appears in every page's header while music plays.
- **News:** up to 3 tabs, each a topic (world, India, business, technology, science, sport, health,
  entertainment) or your own search (for example your city).
- **Markets:** up to 4 Yahoo Finance symbols (NIFTY, SENSEX, stocks, ...) plus Bitcoin.
- **Cricket:** live scores of the team you follow, shown only while it plays. Free, with an optional key for
  overs and match status.
- **Quote of the day.**
- **Mining (hobby, off by default):** mines [Duino-Coin](https://duinocoin.com) in the clock's spare time at
  about 30–40 kH/s, with an animated miner, share results, a 24-hour history, earnings and a tongue-in-cheek
  "Bitcoin lottery". A small pickaxe in every header shows it's running. See [Mining](#mining-hobby).
- **Network:** how many devices are on your network right now, the router, and new arrivals. The clock
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
| **Feeds** | Cloud relay address, news tabs, markets, Bitcoin, news age, cricket team, **Mining** card (account, on/off, live stats, 24-hour chart) |
| **Focus** | Pomodoro (4 × 25 min), countdown timer, stopwatch, minutes focused today |
| **System** | Device info, firmware update, **Password** card (show on clock, new password, log out), city and time zone, Wi-Fi, restart |

## Mining (hobby)

The clock can mine [Duino-Coin](https://duinocoin.com) (DUCO), a coin made for small boards. **It doesn't make
money.** An ESP8266 earns small amounts of DUCO worth a tiny fraction of a rupee a day, and its power costs
more than that. It's here for the fun of watching it work, and every number shown is real.

| Coin | On this clock |
|---|---|
| Duino-Coin | Works: about 30–40 kH/s in the clock's spare time (0.9.4+); earns fractions of a cent a day |
| Bitcoin | Possible only as a lottery: the odds of finding a block are around 1 in quadrillions per day, so the page just shows them |
| Monero, Ethereum Classic and others | Can't run: their algorithms need far more memory than the clock's ~11 KB |

**Turn it on:**
1. Create a free account at [duinocoin.com](https://duinocoin.com) (a web wallet).
2. On the settings page, open **Feeds**, **Mining**: enter your Duino-Coin username, an optional mining key
   (if you set one in your wallet), and a rig name (default `caiden`). Press **Save account**.
3. Switch **Mine Duino-Coin** on. The **Mining** page joins the rotation.

**How it behaves:**
- **Background only:** it hashes in slices of 6 ms between screen updates (15 ms while the page is still), so pages, the web page and updates
  stay as smooth as before.
- **Automatic pauses:** it stops during firmware updates and pauses when free memory drops below 8 KB or Wi-Fi is
  weaker than -85 dBm.
- **Mining page:** an animated miner swinging a pickaxe; see the picture under [What's new](#whats-new).
  - Coins fly into the DUCO counter for accepted shares, and he says "Oops" for rejected ones.
  - Below: hash rate, shares, a 24-hour line, today's INR value and your total, and the Bitcoin odds.
- **Header badge:** a small gold pickaxe in every page header (grey while paused), next to the Spotify equaliser.
- **Settings page:** the same numbers, the pool's own view of your rig, and a 24-hour chart.
- **Remote check:** the relay's `/clock.json` also shows the hash rate and shares.
- **Measure it yourself:** with USB, `python -I tools/serial_cmd.py '{"cmd":"minebench"}'` (from `firmware/`)
  prints the raw hashing speed (about 54.7 kH/s on 0.9.4). The real rate is lower because the clock only mines
  in its spare time.
- **Earnings stay small:** Duino-Coin lowers the reward per share as a device gets faster, so more hash rate
  raises DUCO only a little.
- **Privacy:** your username and mining key stay in the clock's settings. The relay looks up your public Duino-Coin
  balance for the earnings line.

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
