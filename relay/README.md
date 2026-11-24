# CAIDEN cloud relay

The clock can't open HTTPS connections (it has about 11 KB of free memory, and TLS needs more). So
everything that needs HTTPS or an account goes through `worker.js`, a small
[Cloudflare Worker](https://workers.cloudflare.com/) that runs **free on your own Cloudflare account**. The
worker fetches the data over HTTPS and serves it to the clock over plain HTTP, on any Wi-Fi.

| Page | Source |
|---|---|
| News, Markets, Quote | Relay (BBC and The Hindu RSS, Bing News search, Yahoo Finance, CoinGecko, ZenQuotes) |
| Spotify (with album art) | Relay, after a one-time Spotify sign-in |
| Cricket | Relay (free Cricinfo feed, or cricketdata.org with your key) |
| Firmware updates, health report | Relay (images signed on your PC) |
| Weather, Air quality, Sun & Moon, World clock | The clock itself (Open-Meteo, no account) |

News tabs, markets and the cricket team are set on the clock's settings page (**Feeds** tab). The clock
sends them with each request, so you never edit the worker for them.

You need a free Cloudflare account and [Node.js](https://nodejs.org/) (for `npx`). The free plan is enough:
the clock makes about 10,000 requests a day, and the free plan allows 100,000.

## 1. Deploy the relay (5 minutes)

From this `relay/` folder:

1. Log in to Cloudflare (opens your browser once):
   ```
   npx wrangler login
   ```
2. Create the storage:
   ```
   npx wrangler kv namespace create caiden
   ```
   It prints an `id`.
3. Copy `wrangler.toml.example` to `wrangler.toml` and paste that id where it says
   `PASTE_YOUR_KV_NAMESPACE_ID_HERE`. You can also change the worker's `name`.
4. Deploy:
   ```
   npx wrangler deploy
   ```
   It prints your relay's address, for example `https://caiden-relay.yourname.workers.dev`.
5. Check it: open `http://caiden-relay.yourname.workers.dev/state.json` in a browser. It should show news.
6. On the clock's settings page, open **Feeds**, paste the address **without** `http://` into **Cloud relay**,
   and press **Save relay**. News, markets and the quote appear within a minute.

`wrangler.toml` holds your own namespace id, so `.gitignore` keeps it out of git. Only the
`.example` file is shared.

<details>
<summary>Prefer the Cloudflare dashboard?</summary>

1. **Workers & Pages**, **Create**, **Create Worker**, name it, **Deploy**.
2. **Edit code**: replace everything with `worker.js`, **Deploy**.
3. **Storage & Databases**, **KV**, **Create** a namespace called `caiden`.
4. Your worker, **Settings**, **Bindings**, **Add**, **KV namespace**: variable name `CAIDEN`, namespace `caiden`.
</details>

## 2. Spotify (optional)

1. At https://developer.spotify.com/dashboard, create an app with **Web API** ticked.
2. Under the app's **Settings**, add this Redirect URI (with your relay's address) and save:
   `https://caiden-relay.yourname.workers.dev/spotify/callback`
3. Give the relay the app's **Client ID**: uncomment the `[vars]` lines in `wrangler.toml`, paste it there,
   and run `npx wrangler deploy` again. In the dashboard, the same is a **Text** variable `SPOTIFY_CLIENT_ID`.
4. Make up a long random password that protects the sign-in link, and store it as a secret:
   ```
   npx wrangler secret put ADMIN_KEY
   ```
5. Open `https://caiden-relay.yourname.workers.dev/spotify/login?key=<your ADMIN_KEY>` in a browser, sign in
   to Spotify and allow access. The page says "Spotify connected".

Sign-in uses PKCE, so no Spotify client secret is needed. The sign-in is kept in KV; deploys don't remove it.

## 3. Cricket (optional)

Without a key, live scores come free from Cricinfo's live-score feed: teams, runs and wickets, but no overs
or match status. For those, get a free key at https://cricketdata.org and store it:
```
npx wrangler secret put CRICKET_KEY
```
The relay caches results in KV and calls the API at most every 4 minutes during a live match and hourly
otherwise, within the free 100 calls a day.

## Firmware updates from anywhere

`python -I firmware/tools/publish_update.py --sha256 <image SHA-256>` uploads a built image to KV
(`fw_bin`, `fw_meta`). The clock checks `/fw.json` every 6 hours (or with **Check for update** on the System
tab) and offers **Install CAIDEN OS x.y.z**; nothing installs until you press it. The image is signed with
your clock's own update key (`firmware/tools/ota_key.txt`, which never leaves your PC), so the clock ignores
images not made on your PC, older versions and damaged downloads. Full steps:
[docs/UPDATES.md](../docs/UPDATES.md).

## Checking the clock from anywhere

With each request the clock sends a short health report: firmware version, uptime, free memory, Wi-Fi
signal, why it last restarted, and the last update error. Open `http://<your relay>/clock.json` to see it.
`age` is the seconds since the clock last reported (about every 10 minutes), and `prev` is the version and
uptime before its last restart.

## Updating the relay

After changing `worker.js`, run `npx wrangler deploy` again. Variables, secrets and everything in KV stay.

## Privacy

- The worker runs on **your** Cloudflare account. Keys and the Spotify sign-in are stored there, as secrets
  and in KV; they never pass through the clock.
- Album art is resized by wsrv.nl, a free public image service, so it sees the cover image addresses of what
  you play.
- Anyone who knows your relay's address can read the same public data, your current Spotify track and the
  clock's health report (version, uptime, memory, signal; no addresses or names). Don't share the address if
  that matters to you.

## Notes

- Use the `workers.dev` address. A custom domain with "Always Use HTTPS" redirects plain HTTP to HTTPS, and
  the clock can't follow that.
- If Yahoo Finance blocks Cloudflare's servers, stock rows may be missing while Bitcoin still shows. The clock
  keeps the last prices it received, with their age in the header.
