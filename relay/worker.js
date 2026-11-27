// CAIDEN cloud relay: a Cloudflare Worker (free plan) that does the HTTPS work the clock cannot.
//
// The clock has about 11 KB of free memory, too little for TLS, and Spotify, every market source and
// most news sources are HTTPS only. This worker fetches them and serves the clock over plain HTTP:
//
//   GET /state.json?f=..&m=..&b=1&a=24&team=India   news, markets, quote, cricket (settings come
//                                                   from the clock: see parseSettings below)
//   GET /sp.json                                    Spotify now playing
//   GET /art.bin?id=<40 hex>                        album art, 120x120 RGB565 little-endian,
//                                                   colours in the X-C1 / X-C2 headers
//   GET /spotify/login?key=<ADMIN_KEY>              one-time Spotify sign-in (in your browser)
//
// Cloudflare settings (see README.md): KV namespace bound as CAIDEN, variable SPOTIFY_CLIENT_ID,
// secrets ADMIN_KEY and CRICKET_KEY. All optional: without them those features stay off.

const DEFAULTS = {
  feeds: [
    { tab: "WORLD", topic: "WORLD" },
    { tab: "TECH", topic: "TECHNOLOGY" },
    { tab: "BUSINESS", topic: "BUSINESS" },
  ],
  markets: [
    { sym: "^NSEI", name: "NIFTY 50" },
    { sym: "^BSESN", name: "SENSEX" },
    { sym: "RELIANCE.NS", name: "RELIANCE" },
    { sym: "TCS.NS", name: "TCS" },
  ],
  crypto: true,
  maxAgeH: 24,
  team: "India",
};
const NEWS_REGION = "hl=en-IN&gl=IN&ceid=IN:en";
const TOPICS = ["WORLD", "NATION", "BUSINESS", "TECHNOLOGY", "SCIENCE", "SPORTS", "HEALTH", "ENTERTAINMENT"];
const UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) CAIDEN-relay/2.0";
const NEWS_SLOTS = 8;                          // the clock holds 8 headlines
const ART = 120;                               // album art size on the clock

const QUOTES = [                               // used when ZenQuotes is unavailable, one per day
  ["The best way out is always through.", "Robert Frost"],
  ["Well begun is half done.", "Aristotle"],
  ["Simplicity is the ultimate sophistication.", "Leonardo da Vinci"],
  ["What we think, we become.", "Buddha"],
  ["Be the change that you wish to see in the world.", "Mahatma Gandhi"],
  ["It always seems impossible until it's done.", "Nelson Mandela"],
  ["Quality is not an act, it is a habit.", "Aristotle"],
  ["Arise, awake, and stop not till the goal is reached.", "Swami Vivekananda"],
  ["Dream, dream, dream. Dreams transform into thoughts and thoughts result in action.", "A. P. J. Abdul Kalam"],
  ["The journey of a thousand miles begins with one step.", "Lao Tzu"],
  ["Do what you can, with what you have, where you are.", "Theodore Roosevelt"],
];

// ---------------------------------------------------------------- helpers
const KEEP = new Set([..."°·µ³–−…₹←→↑↓•"]);
const enc = new TextEncoder();

// ASCII-fold for the clock's fonts; n = max UTF-8 bytes (the clock has fixed-size buffers).
function fold(s, n) {
  s = String(s || "").replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
    .replace(/—/g, "–").replace(/ /g, " ");
  let out = "";
  for (const ch of s) {
    const c = ch.codePointAt(0);
    out += (c >= 32 && c < 127) || KEEP.has(ch) ? ch : ch.normalize("NFKD").replace(/[^\x20-\x7E]/g, "");
  }
  out = out.replace(/\s+/g, " ").trim();
  if (n) while (out && enc.encode(out).length > n - 1) out = [...out].slice(0, -1).join("");
  return out;
}

function spark(values, n = 24) {
  let v = values.filter((x) => x != null);
  if (v.length < 2) return [];
  if (v.length > n) {
    const step = (v.length - 1) / (n - 1);
    v = Array.from({ length: n }, (_, i) => v[Math.round(i * step)]);
  }
  const lo = Math.min(...v), hi = Math.max(...v), span = hi - lo || 1;
  return v.map((x) => Math.round(((x - lo) / span) * 255));
}

// 24318.45 -> "24,318.45" with Indian lakh grouping (1,23,45,678.90)
function indianGroup(x, dec = 2) {
  const neg = x < 0;
  let [whole, frac] = Math.abs(x).toFixed(dec).split(".");
  if (whole.length > 3) whole = whole.slice(0, -3).replace(/\B(?=(\d{2})+$)/g, ",") + "," + whole.slice(-3);
  return (neg ? "-" : "") + whole + (frac ? "." + frac : "");
}

const round2 = (x) => Math.round(x * 100) / 100;

function entities(s) {
  return s.replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, "$1")
    .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16)))
    .replace(/&#(\d+);/g, (_, d) => String.fromCodePoint(+d))
    .replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
    .replace(/&nbsp;/g, " ").replace(/&amp;/g, "&");
}

const tag = (xml, name) => {
  const m = xml.match(new RegExp(`<${name}[^>]*>([\\s\\S]*?)</${name}>`));
  return m ? entities(m[1]).trim() : "";
};

async function getJson(url, headers = {}) {
  const r = await fetch(url, { headers: { "User-Agent": UA, Accept: "application/json", ...headers } });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

const json = (o, extra = {}) => new Response(JSON.stringify(o), {
  headers: { "Content-Type": "application/json", "Cache-Control": "no-store", ...extra },
});

// ---------------------------------------------------------------- settings sent by the clock
// f = feeds  "LOCAL~s~London|TECH~t~TECHNOLOGY"   (tab ~ s=search / t=topic ~ value), max 3
// m = markets "^NSEI~NIFTY 50|TCS.NS~TCS"            (Yahoo symbol ~ display name), max 4
// b = Bitcoin row 1/0, a = skip news older than a hours, team = cricket team to follow
function parseSettings(q) {
  const s = { ...DEFAULTS };
  if (q.has("f")) {
    s.feeds = q.get("f").split("|").slice(0, 3).map((x) => x.split("~")).filter((p) => p.length === 3 && p[2].trim())
      .map(([tab, kind, v]) => {
        const f = { tab: fold(tab, 7).toUpperCase() || "NEWS" };
        if (kind === "t" && TOPICS.includes(v.trim().toUpperCase())) f.topic = v.trim().toUpperCase();
        else f.search = v.trim().slice(0, 40);
        return f;
      });
  }
  if (q.has("m")) {
    s.markets = q.get("m").split("|").slice(0, 4).map((x) => x.split("~"))
      .filter((p) => /^[A-Za-z0-9^.=\-]{1,16}$/.test((p[0] || "").trim()))
      .map(([sym, name]) => ({ sym: sym.trim(), name: (name || sym).trim() }));
  }
  if (q.has("b")) s.crypto = q.get("b") === "1";
  if (q.has("a")) s.maxAgeH = Math.min(168, Math.max(1, parseInt(q.get("a"), 10) || 24));
  if (q.has("team")) s.team = q.get("team").trim().slice(0, 24);
  return s;
}

// ---------------------------------------------------------------- markets / crypto
async function yahoo(sym) {
  const path = `/v8/finance/chart/${encodeURIComponent(sym)}?interval=15m&range=1d`;
  try {
    return await getJson("https://query1.finance.yahoo.com" + path);
  } catch {
    return getJson("https://query2.finance.yahoo.com" + path);
  }
}

async function marketRow(m) {
  const r = (await yahoo(m.sym)).chart.result[0];
  const meta = r.meta;
  const price = +meta.regularMarketPrice;
  const prev = +(meta.chartPreviousClose || meta.previousClose || price);
  const closes = ((r.indicators?.quote || [{}])[0].close || []).filter((c) => c != null);
  const series = [...closes, price];
  const lo = Math.min(...series, prev), hi = Math.max(...series, prev);
  return {
    n: fold(m.name || m.sym, 12), v: indianGroup(price),
    c: prev ? round2(((price - prev) / prev) * 100) : 0, d: round2(price - prev),
    s: spark(series), pv: indianGroup(prev, 0), pc: Math.round(((prev - lo) / (hi - lo || 1)) * 255),
  };
}

async function bitcoin() {
  const j = (await getJson("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=inr&include_24hr_change=true")).bitcoin;
  let series = [];
  try {
    series = (await getJson("https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=inr&days=1")).prices.map((p) => p[1]);
  } catch { /* the row still shows without a sparkline */ }
  return { n: "BTC", v: `₹${(j.inr / 1e5).toFixed(1)} L`, c: round2(+j.inr_24h_change || 0), d: 0, s: spark(series), pc: -1 };
}

async function markets(s) {
  const jobs = s.markets.map(marketRow);
  if (s.crypto) jobs.push(bitcoin());
  const done = await Promise.allSettled(jobs);
  return done.filter((d) => d.status === "fulfilled").map((d) => d.value).slice(0, 5);
}

// ---------------------------------------------------------------- news
// Google News answers Cloudflare's servers with 503 "Sorry..." (bot check, seen 2026-11-24), so topics
// come from fixed RSS feeds and searches from Bing News. Google stays as the fallback in case it opens up.
const TOPIC_FEEDS = {
  WORLD: ["https://feeds.bbci.co.uk/news/world/rss.xml", "BBC"],
  NATION: ["https://www.thehindu.com/news/national/feeder/default.rss", "The Hindu"],
  BUSINESS: ["https://feeds.bbci.co.uk/news/business/rss.xml", "BBC"],
  TECHNOLOGY: ["https://feeds.bbci.co.uk/news/technology/rss.xml", "BBC"],
  SCIENCE: ["https://feeds.bbci.co.uk/news/science_and_environment/rss.xml", "BBC"],
  SPORTS: ["https://feeds.bbci.co.uk/sport/rss.xml", "BBC Sport"],
  HEALTH: ["https://feeds.bbci.co.uk/news/health/rss.xml", "BBC"],
  ENTERTAINMENT: ["https://feeds.bbci.co.uk/news/entertainment_and_arts/rss.xml", "BBC"],
};

function feedSources(f) {   // [url, source name or ""] in the order to try
  const google = f.search
    ? `https://news.google.com/rss/search?q=${encodeURIComponent(f.search + " when:1d")}&${NEWS_REGION}`
    : `https://news.google.com/rss/headlines/section/topic/${encodeURIComponent(f.topic)}?${NEWS_REGION}`;
  const first = f.search
    ? [`https://www.bing.com/news/search?q=${encodeURIComponent(f.search)}&format=rss&mkt=en-IN`, ""]
    : TOPIC_FEEDS[f.topic];
  return first ? [first, [google, ""]] : [[google, ""]];
}

async function feedRows(f, maxAgeH) {
  for (const [url, name] of feedSources(f)) {
    try {
      const rows = await feedRowsFrom(url, name, f, maxAgeH);
      if (rows.length) return rows;
    } catch { /* next source */ }
  }
  return [];
}

async function feedRowsFrom(url, srcName, f, maxAgeH) {
  const r = await fetch(url, { headers: { "User-Agent": UA } });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  const xml = await r.text();
  const cat = fold(f.tab, 7);
  const maxAge = maxAgeH * 60, now = Date.now(), seen = new Set(), rows = [];
  for (const m of xml.matchAll(/<item>([\s\S]*?)<\/item>/g)) {
    const item = m[1];
    const src = srcName || tag(item, "source") || tag(item, "News:Source");
    let title = tag(item, "title");
    if (src && title.endsWith(" - " + src)) title = title.slice(0, -(src.length + 3));
    title = fold(title, 112);
    if (!title || seen.has(title)) continue;
    seen.add(title);
    const pub = Date.parse(tag(item, "pubDate"));
    const age = Number.isNaN(pub) ? -1 : Math.floor((now - pub) / 60000);
    if (age > maxAge) continue;
    rows.push({ c: cat, h: title, s: fold(src, 30), a: age });
  }
  rows.sort((a, b) => (a.a >= 0 ? a.a : 1e6) - (b.a >= 0 ? b.a : 1e6));   // newest first
  return rows;
}

async function news(s) {
  const perTab = (await Promise.allSettled(s.feeds.map((f) => feedRows(f, s.maxAgeH))))
    .map((d) => (d.status === "fulfilled" ? d.value : []));
  // share the 8 slots: equal split, unused slots go to tabs that have more stories
  const take = perTab.map(() => 0);
  const total = () => take.reduce((a, b) => a + b, 0);
  while (total() < NEWS_SLOTS && perTab.some((r, i) => take[i] < r.length))
    perTab.forEach((r, i) => { if (take[i] < r.length && total() < NEWS_SLOTS) take[i]++; });
  return perTab.flatMap((r, i) => r.slice(0, take[i]));
}

// ---------------------------------------------------------------- quote of the day
async function quote() {
  try {
    const j = (await getJson("https://zenquotes.io/api/today"))[0];
    if (j && j.q && !/too many requests/i.test(j.q)) return { q: fold(j.q.trim(), 150), a: fold((j.a || "").trim(), 32) };
  } catch { /* fall through to the built-in list */ }
  const day = Math.floor((Date.now() + 5.5 * 3600e3) / 86400e3);   // changes at midnight IST
  const [q, a] = QUOTES[day % QUOTES.length];
  return { q: fold(q, 150), a: fold(a, 32) };
}

// ---------------------------------------------------------------- cricket (CricketData.org)
// The free key allows 100 calls a day, so results are kept in KV: every 4 to 30 minutes during a
// live match (spread over what is left of the day), hourly otherwise.
// Free source without a key: Cricinfo's live-score RSS (titles like "South Africa 52/1 * v Bangladesh
// 91/10", * = batting now). It has no overs or result text, so o = -1 (the clock then hides overs and
// run rates) and the status line is "<team> batting". Used when CRICKET_KEY is not set.
// The image stays in this worker's memory between requests, so the ~370 pieces of one download do
// not each read 750 KB from KV. It is kept only when it matches fw_meta (version and size); a stale
// KV read right after a publish is not kept, and the clock checks the MD5 of what it receives anyway.
let FW_CACHE = null;   // { v, buf }
async function fwImage(env, v) {
  if (!env.CAIDEN) return null;
  if (v && FW_CACHE && FW_CACHE.v === v) return FW_CACHE.buf;
  const [buf, meta] = await Promise.all([env.CAIDEN.get("fw_bin", "arrayBuffer"), env.CAIDEN.get("fw_meta", "json")]);
  if (buf && meta && v && meta.v === v && meta.size === buf.byteLength) FW_CACHE = { v, buf };
  return buf;
}

const CI_RSS ="https://static.cricinfo.com/rss/livescores.xml";

function shortTeam(name) {
  const w = name.replace(/\bWomen\b/i, "").trim().split(/\s+/).filter(Boolean);
  let s = w.length > 1 && !/^[A-Z]$/.test(w[w.length - 1]) ? w.map((x) => x[0]).join("") : (w[0] || "").slice(0, 3);
  if (w.length > 1 && /^[A-Z]$/.test(w[w.length - 1])) s = w[0].slice(0, 3) + " " + w[w.length - 1];   // "India A"
  if (/\bWomen\b/i.test(name)) s += " W";
  return s.toUpperCase().slice(0, 6);
}

function ciSide(txt) {          // "New South Wales 195/7 *" or "England 278 & 120/3" or "Desert Vipers"
  const bat = /\*\s*$/.test(txt);
  txt = txt.replace(/\*\s*$/, "").trim();
  const m = txt.match(/^(.*?)\s+((?:\d+(?:\/\d+)?d?\s*&\s*)?\d+(?:\/\d+)?d?)$/);
  const name = (m ? m[1] : txt).trim();
  const inns = m ? m[2].split("&").map((x) => { const [r, w] = x.trim().replace(/d$/, "").split("/"); return { r: +r || 0, w: w === undefined ? 10 : +w || 0 }; }) : [];
  return { name, bat, inns };
}

async function cricinfo(team) {
  const r = await fetch(CI_RSS, { headers: { "User-Agent": UA }, cf: { cacheTtl: 60 } });
  if (!r.ok) throw new Error("cricinfo " + r.status);
  const xml = await r.text();
  for (const it of xml.matchAll(/<item>[\s\S]*?<title>([\s\S]*?)<\/title>/g)) {
    const parts = it[1].replace(/&amp;/g, "&").trim().split(/\s+v\s+/);
    if (parts.length !== 2) continue;
    const a = ciSide(parts[0]), b = ciSide(parts[1]);
    if (![a, b].some((s) => s.name.toLowerCase() === team)) continue;
    const bat = a.bat ? a : b.bat ? b : null;
    if (!bat) return { on: false };                        // not started yet (or no live innings)
    const other = bat === a ? b : a;
    const sc = [];
    if (other.inns.length) sc.push({ t: shortTeam(other.name), r: other.inns.at(-1).r, w: other.inns.at(-1).w, o: -1 });
    sc.push({ t: shortTeam(bat.name), r: bat.inns.at(-1)?.r || 0, w: bat.inns.at(-1)?.w || 0, o: -1 });
    const innings = a.inns.length + b.inns.length || 1;
    const limited = a.inns.length <= 1 && b.inns.length <= 1;
    let tg = 0, nd = 0;
    if (limited && other.inns.length === 1 && bat.inns.length === 1) {   // second innings of a one-innings game: a chase
      tg = other.inns[0].r + 1;
      nd = Math.max(0, tg - bat.inns[0].r);
    }
    // the feed keeps the * on a finished chase: all out short of the target, or target reached
    let st = `${bat.name} batting` + (nd ? `, need ${nd} runs` : "");
    if (tg && bat.inns[0].w >= 10 && nd) { st = `${other.name} won by ${nd - 1} runs`; nd = 0; }
    else if (tg && !nd) st = `${bat.name} won`;
    return { on: true, mt: "", in: innings, t1: shortTeam(bat.name), t2: shortTeam(other.name),
      st: fold(st, 71), sc, tg, nd, bl: 0 };
  }
  return { on: false };
}

async function cricket(env, team) {
  if (!env.CRICKET_KEY) return cricinfo(team.toLowerCase()).catch(() => ({ on: false }));
  if (!env.CAIDEN) return { on: false };
  const key = "cr:" + team.toLowerCase();
  const now = Date.now() / 1000;
  const cached = await env.CAIDEN.get(key, "json");
  if (cached) {
    const left = Math.max(1, (cached.limit || 100) - (cached.used || 0));
    const every = cached.data.on ? Math.min(1800, Math.max(240, (86400 - (now % 86400)) / left)) : 3600;
    if (now - cached.at < every) return cached.data;
  }
  let data = { on: false }, used = 0, limit = 100;
  try {
    const j = await getJson(`https://api.cricapi.com/v1/currentMatches?apikey=${encodeURIComponent(env.CRICKET_KEY)}&offset=0`);
    if (j.status !== "success") throw new Error("cricket api status");
    used = +(j.info?.hitsToday || 0);
    limit = +(j.info?.hitsLimit || 100);
    data = cricketMatch(j.data || [], team.toLowerCase());
  } catch {
    data = { on: false };                       // never freeze a stale live score
  }
  await env.CAIDEN.put(key, JSON.stringify({ at: now, used, limit, data }));
  return data;
}

function cricketMatch(list, team) {
  const match = list.find((m) => m.matchStarted && !m.matchEnded && (m.teams || []).some((t) => t.trim().toLowerCase() === team));
  if (!match) return { on: false };
  const short = {};
  for (const ti of match.teamInfo || []) short[ti.name || ""] = (ti.shortname || (ti.name || "").slice(0, 3)).toUpperCase().slice(0, 6);
  for (const t of match.teams || []) if (!(t in short)) short[t] = t.slice(0, 3).toUpperCase();
  const scores = (match.score || []).map((sc) => {
    const inning = sc.inning || "";
    const name = Object.keys(short).find((t) => t && inning.startsWith(t)) || inning.split(" Inning")[0];
    return { t: short[name] || name.slice(0, 3).toUpperCase(), r: +sc.r || 0, w: +sc.w || 0, o: +sc.o || 0 };
  });
  const status = fold(match.status, 71);
  let need = 0, balls = 0, target = 0;
  const m = status.match(/need (\d+) runs? (?:in|from|off) (\d+) balls?/i);
  if (m) { need = +m[1]; balls = +m[2]; }
  const mt = (match.matchType || "").toLowerCase();
  if (scores.length >= 2 && ["t20", "odi", "t10"].includes(mt)) target = scores[scores.length - 2].r + 1;
  const batting = scores.length ? scores[scores.length - 1].t : short[match.teams[0]] || "";
  const other = Object.values(short).find((v) => v !== batting) || "";
  return { on: true, mt: mt.toUpperCase().slice(0, 6), in: scores.length, t1: batting, t2: other, st: status,
    sc: scores.slice(-2), tg: target, nd: need, bl: balls };
}

// ---------------------------------------------------------------- Spotify (PKCE, token kept in KV)
const SP_AUTH = "https://accounts.spotify.com/authorize";
const SP_TOKEN = "https://accounts.spotify.com/api/token";
const SP_SCOPES = "user-read-currently-playing user-read-playback-state";
let spTok = null;                               // cached per worker instance

const b64url = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const randomString = (n) => b64url(crypto.getRandomValues(new Uint8Array(n)));

async function spotifyLogin(env, url) {
  if (!env.ADMIN_KEY || url.searchParams.get("key") !== env.ADMIN_KEY) return new Response("Forbidden\n", { status: 403 });
  if (!env.SPOTIFY_CLIENT_ID || !env.CAIDEN) return new Response("Set SPOTIFY_CLIENT_ID and bind the CAIDEN KV namespace first.\n", { status: 500 });
  const verifier = randomString(64), state = randomString(16);
  const challenge = b64url(await crypto.subtle.digest("SHA-256", enc.encode(verifier)));
  await env.CAIDEN.put("sp_pkce", JSON.stringify({ verifier, state }), { expirationTtl: 600 });
  const q = new URLSearchParams({ client_id: env.SPOTIFY_CLIENT_ID, response_type: "code", redirect_uri: `https://${url.host}/spotify/callback`,
    scope: SP_SCOPES, code_challenge_method: "S256", code_challenge: challenge, state });
  return Response.redirect(`${SP_AUTH}?${q}`, 302);
}

async function spotifyCallback(env, url) {
  const p = env.CAIDEN && (await env.CAIDEN.get("sp_pkce", "json"));
  const code = url.searchParams.get("code");
  if (!p || !code || url.searchParams.get("state") !== p.state) return html("Spotify sign-in failed. Open the login link again.", 400);
  const r = await fetch(SP_TOKEN, { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ client_id: env.SPOTIFY_CLIENT_ID, grant_type: "authorization_code", code,
      redirect_uri: `https://${url.host}/spotify/callback`, code_verifier: p.verifier }) });
  if (!r.ok) return html("Spotify sign-in failed. Check the redirect URI in your Spotify app.", 400);
  await saveToken(env, await r.json());
  await env.CAIDEN.delete("sp_pkce");
  return html("Spotify connected. The clock shows what you play within a few seconds. You can close this tab.");
}

const html = (msg, status = 200) => new Response(`<!doctype html><meta name=viewport content="width=device-width"><h2 style="font-family:system-ui">CAIDEN relay</h2><p style="font-family:system-ui">${msg}</p>`,
  { status, headers: { "Content-Type": "text/html; charset=utf-8" } });

async function saveToken(env, t) {
  const old = spTok || (await env.CAIDEN.get("sp_tok", "json")) || {};
  const tok = { access_token: t.access_token, refresh_token: t.refresh_token || old.refresh_token,
    expires_at: Date.now() / 1000 + (+t.expires_in || 3600) - 60 };
  spTok = tok;
  await env.CAIDEN.put("sp_tok", JSON.stringify(tok));
  return tok;
}

async function accessToken(env, force) {
  if (!spTok) spTok = await env.CAIDEN.get("sp_tok", "json");
  if (!spTok || !spTok.refresh_token) return null;
  if (!force && Date.now() / 1000 < spTok.expires_at) return spTok.access_token;
  const latest = await env.CAIDEN.get("sp_tok", "json");          // another instance may have refreshed
  if (!force && latest && Date.now() / 1000 < latest.expires_at) { spTok = latest; return latest.access_token; }
  const r = await fetch(SP_TOKEN, { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ client_id: env.SPOTIFY_CLIENT_ID, grant_type: "refresh_token", refresh_token: (latest || spTok).refresh_token }) });
  if (!r.ok) return null;
  return (await saveToken(env, await r.json())).access_token;
}

async function nowPlaying(env) {
  if (!env.CAIDEN || !env.SPOTIFY_CLIENT_ID) return { on: false, x: 1 };   // x: Spotify not set up
  let j = null;
  for (const force of [false, true]) {
    const tok = await accessToken(env, force);
    if (!tok) return { on: false, x: 1 };
    const r = await fetch("https://api.spotify.com/v1/me/player/currently-playing?additional_types=track,episode",
      { headers: { Authorization: "Bearer " + tok } });
    if (r.status === 204) return { on: false };
    if (r.status === 401 && !force) continue;
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    j = await r.json();
    break;
  }
  const it = j && j.item;
  if (!it) return { on: false };
  const episode = it.type === "episode";
  const artist = episode ? (it.show || {}).name : (it.artists || []).map((a) => a.name).join(", ");
  const images = (episode ? it.images : (it.album || {}).images) || [];
  const img = [...images].sort((a, b) => (a.width || 0) - (b.width || 0)).find((m) => (m.width || 0) >= ART) || images[0];
  const id = ((img && img.url) || "").match(/i\.scdn\.co\/image\/([0-9a-f]{40})$/);
  return { on: true, pl: !!j.is_playing, ti: fold(it.name, 60) || "Now playing", ar: fold(artist, 60),
    p: +j.progress_ms || 0, d: +it.duration_ms || 0, id: id ? id[1] : "" };
}

// ---------------------------------------------------------------- album art -> RGB565
// The image is resized and re-encoded as PNG by wsrv.nl (a free image proxy), then decoded here:
// PNG needs only zlib, which Workers have built in, so no image library is needed.
async function inflate(bytes) {
  const ds = new DecompressionStream("deflate");
  const out = new Response(new Blob([bytes]).stream().pipeThrough(ds));
  return new Uint8Array(await out.arrayBuffer());
}

async function decodePng(buf) {
  const b = new Uint8Array(buf), dv = new DataView(buf);
  if (dv.getUint32(0) !== 0x89504e47) throw new Error("not png");
  let p = 8, w = 0, h = 0, depth = 0, type = 0, interlace = 0, palette = null;
  const idat = [];
  while (p < b.length) {
    const len = dv.getUint32(p), kind = String.fromCharCode(...b.subarray(p + 4, p + 8)), data = b.subarray(p + 8, p + 8 + len);
    if (kind === "IHDR") { w = dv.getUint32(p + 8); h = dv.getUint32(p + 12); depth = b[p + 16]; type = b[p + 17]; interlace = b[p + 20]; }
    else if (kind === "PLTE") palette = data;
    else if (kind === "IDAT") idat.push(data);
    else if (kind === "IEND") break;
    p += 12 + len;
  }
  if (depth !== 8 || interlace) throw new Error("unsupported png");
  const ch = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[type];
  if (!ch) throw new Error("unsupported png");
  const raw = await inflate(new Uint8Array(await new Blob(idat).arrayBuffer()));
  const stride = w * ch, px = new Uint8Array(w * h * 3), prev = new Uint8Array(stride), cur = new Uint8Array(stride);
  for (let y = 0, q = 0; y < h; y++) {
    const f = raw[q++];
    for (let x = 0; x < stride; x++, q++) {
      const a = x >= ch ? cur[x - ch] : 0, up = prev[x], c = x >= ch ? prev[x - ch] : 0;
      let v = raw[q];
      if (f === 1) v += a;
      else if (f === 2) v += up;
      else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      cur[x] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      const o = (y * w + x) * 3;
      if (type === 2 || type === 6) { px[o] = cur[x * ch]; px[o + 1] = cur[x * ch + 1]; px[o + 2] = cur[x * ch + 2]; }
      else if (type === 3) { const i = cur[x] * 3; px[o] = palette[i]; px[o + 1] = palette[i + 1]; px[o + 2] = palette[i + 2]; }
      else { px[o] = px[o + 1] = px[o + 2] = cur[x * ch]; }
    }
    prev.set(cur);
  }
  return { w, h, px };
}

// Two dominant UI colours: 4-bit buckets ranked by population x saturation x brightness.
function dominant(px) {
  const n = new Uint32Array(4096), sr = new Uint32Array(4096), sg = new Uint32Array(4096), sb = new Uint32Array(4096);
  for (let i = 0; i < px.length; i += 3) {
    const k = ((px[i] >> 4) << 8) | ((px[i + 1] >> 4) << 4) | (px[i + 2] >> 4);
    n[k]++; sr[k] += px[i]; sg[k] += px[i + 1]; sb[k] += px[i + 2];
  }
  const cands = [];
  for (let k = 0; k < 4096; k++) {
    if (!n[k]) continue;
    const c = [sr[k] / n[k], sg[k] / n[k], sb[k] / n[k]].map(Math.round);
    const mx = Math.max(...c), mn = Math.min(...c), sat = (mx - mn) / (mx || 1);
    cands.push({ s: n[k] * (0.25 + sat) * (0.3 + mx / 255), c });
  }
  cands.sort((a, b) => b.s - a.s);
  const c1 = cands[0].c;
  const c2 = (cands.slice(1).find((x) => x.c.reduce((t, v, i) => t + Math.abs(v - c1[i]), 0) > 120) || cands[Math.min(1, cands.length - 1)]).c;
  const hex = (c) => "#" + c.map((v) => v.toString(16).padStart(2, "0")).join("").toUpperCase();
  return [hex(c1), hex(c2)];
}

async function albumArt(id) {
  const src = `https://wsrv.nl/?url=${encodeURIComponent("i.scdn.co/image/" + id)}&w=${ART}&h=${ART}&fit=cover&output=png`;
  const r = await fetch(src, { headers: { "User-Agent": UA } });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  const { w, h, px } = await decodePng(await r.arrayBuffer());
  if (w !== ART || h !== ART) throw new Error("size");
  const out = new Uint8Array(ART * ART * 2);
  for (let i = 0; i < ART * ART; i++) {
    const v = ((px[3 * i] & 0xf8) << 8) | ((px[3 * i + 1] & 0xfc) << 3) | (px[3 * i + 2] >> 3);
    out[2 * i] = v & 255;
    out[2 * i + 1] = v >> 8;
  }
  const [c1, c2] = dominant(px);
  return new Response(out, { headers: { "Content-Type": "application/octet-stream", "X-C1": c1, "X-C2": c2, "Cache-Control": "public, max-age=86400" } });
}

// ---------------------------------------------------------------- HTTP entry
function hash(s) {                             // FNV-1a, 8 hex chars (the clock redraws when it changes)
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 0x01000193);
  return (h >>> 0).toString(16).padStart(8, "0");
}

export async function buildState(env, q) {
  const s = parseSettings(q);
  const [mk, nw, qt, cr] = await Promise.all([markets(s), news(s), quote(), cricket(env, s.team).catch(() => ({ on: false }))]);
  const body = { mk, nw, qt, cr };
  return { v: 2, t: Math.floor(Date.now() / 1000), h: hash(JSON.stringify(body)), ...body };
}

// Health report from the clock (sent with each /state.json, see BridgeService::fetchState), kept in
// KV "clock" and shown at /clock.json, so the clock can be checked without USB or its settings page.
// Written only when something changes or every 15 minutes: the free plan allows 1000 KV writes a day.
async function recordClock(env, q) {
  if (!env.CAIDEN || !q.get("fw")) return;
  const now = Math.floor(Date.now() / 1000);
  const num = k => { const v = parseInt(q.get(k), 10); return Number.isFinite(v) ? v : null; };
  const cur = { fw: q.get("fw").slice(0, 12), up: num("up"), hp: num("hp"), ss: num("ss"),
                mh: num("mh"), ma: num("ma"), mr: num("mr"),
                rr: (q.get("rr") || "").slice(0, 40), ue: (q.get("ue") || "").slice(0, 40), at: now };
  const old = await env.CAIDEN.get("clock", "json");
  if (old && old.fw === cur.fw && old.ue === cur.ue && old.rr === cur.rr && cur.up >= old.up && now - old.at < 900) return;
  if (old && (old.fw !== cur.fw || cur.up < old.up)) cur.prev = { fw: old.fw, up: old.up, at: old.at };   // restarted / updated
  else if (old && old.prev) cur.prev = old.prev;
  await env.CAIDEN.put("clock", JSON.stringify(cur));
}

// ---------------------------------------------------------------- Duino-Coin mining (clock 0.8.7+)
// /duco.json?u=<username>&r=<rig>&d=<YYYY-MM-DD, the clock's local date>
// Pool address (getPool), the account's balance, what the pool shows for this rig, DUCO price in INR
// and the Bitcoin network hash rate (for the clock's "lottery" line). "today" = balance minus the
// balance first seen on that date (one KV write per day). Prices and the pool are cached in memory.
const DUCO = "https://server.duinocoin.com";
let DUCO_CACHE = { at: 0, pool: null, usd: 0, inrRate: 0, btc: 0 };

async function ducoShared() {
  const now = Date.now();
  if (now - DUCO_CACHE.at < 10 * 60e3 && DUCO_CACHE.pool) return DUCO_CACHE;
  const [pool, api, fx, btc] = await Promise.allSettled([
    getJson(`${DUCO}/getPool`),
    getJson(`${DUCO}/api.json`),
    getJson("https://open.er-api.com/v6/latest/USD"),
    fetch("https://blockchain.info/q/hashrate", { headers: { "User-Agent": UA } }).then((r) => (r.ok ? r.text() : "")),
  ]);
  const ok = (p) => (p.status === "fulfilled" ? p.value : null);
  const c = { at: now, pool: ok(pool), usd: 0, inrRate: 0, btc: 0 };
  if (ok(api)) c.usd = Number(ok(api)["Duco price"]) || 0;
  if (ok(fx)) c.inrRate = Number((ok(fx).rates || {}).INR) || 0;
  const gh = parseFloat(ok(btc) || "");                     // GH/s
  if (gh > 0) c.btc = gh * 1e9;
  if (c.pool && c.pool.success) DUCO_CACHE = c;
  return c;
}

async function ducoInfo(env, q) {
  const user = (q.get("u") || "").trim();
  const rig = (q.get("r") || "caiden").trim();
  const day = /^\d{4}-\d{2}-\d{2}$/.test(q.get("d") || "") ? q.get("d") : new Date().toISOString().slice(0, 10);
  if (!/^[A-Za-z0-9_.\-]{1,32}$/.test(user)) return { ok: false, err: "bad username" };
  const [shared, acct] = await Promise.all([ducoShared(), getJson(`${DUCO}/users/${encodeURIComponent(user)}`).catch(() => null)]);
  const out = { ok: true };
  if (shared.pool && shared.pool.success) { out.ip = shared.pool.ip; out.port = shared.pool.port; out.name = String(shared.pool.name || "").slice(0, 23); }
  out.inr = shared.usd * shared.inrRate;
  out.btc = shared.btc;
  const r = acct && acct.result;
  if (r && r.balance) {
    const bal = Number(r.balance.balance) || 0;
    out.bal = bal;
    out.today = 0;
    if (env.CAIDEN) {
      const key = `duco_day:${user.toLowerCase()}`;
      const base = await env.CAIDEN.get(key, "json");
      if (base && base.d === day) out.today = Math.max(0, bal - base.b);
      else await env.CAIDEN.put(key, JSON.stringify({ d: day, b: bal }), { expirationTtl: 3 * 86400 });
    }
    const mine = (r.miners || []).filter((m) => m.identifier === rig);
    out.phr = mine.reduce((a, m) => a + (Number(m.hashrate) || 0), 0);
    out.pacc = mine.reduce((a, m) => a + (Number(m.accepted) || 0), 0);
    out.prej = mine.reduce((a, m) => a + (Number(m.rejected) || 0), 0);
  } else {
    out.err = "account not found";
  }
  return out;
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    try {
      switch (url.pathname) {
        case "/state.json":
          ctx.waitUntil(recordClock(env, url.searchParams).catch(() => {}));
          return json(await buildState(env, url.searchParams));
        case "/duco.json": return json(await ducoInfo(env, url.searchParams));
        case "/clock.json": {
          const c = env.CAIDEN ? await env.CAIDEN.get("clock", "json") : null;
          if (!c) return json({ seen: false });
          return json({ ...c, age: Math.floor(Date.now() / 1000) - c.at });
        }
        case "/sp.json": return json(await nowPlaying(env));
        case "/art.bin": {
          const id = url.searchParams.get("id") || "";
          if (!/^[0-9a-f]{40}$/.test(id)) return new Response("bad id\n", { status: 400 });
          return await albumArt(id);
        }
        // firmware update, published from the PC with firmware/tools/publish_update.py (KV fw_meta,
        // fw_bin). The clock checks the HMAC in fw_meta with its own key; the relay only stores it.
        case "/fw.json": {
          const meta = env.CAIDEN ? await env.CAIDEN.get("fw_meta") : null;
          return new Response(meta || "{}", { headers: { "Content-Type": "application/json", "Cache-Control": "no-store" } });
        }
        case "/fw.bin": {
          const bin = await fwImage(env, url.searchParams.get("v"));
          if (!bin) return new Response("No update\n", { status: 404 });
          // ?o=<offset>&n=<length>: one piece of the image. The clock (0.7.3+) downloads in small
          // pieces, because a long download fills its memory with network buffers and stalls.
          if (url.searchParams.has("o")) {
            const o = parseInt(url.searchParams.get("o"), 10);
            const n = Math.min(parseInt(url.searchParams.get("n"), 10) || 4096, 65536);
            if (!(o >= 0) || o >= bin.byteLength) return new Response("Bad range\n", { status: 416 });
            return new Response(bin.slice(o, o + n), { headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store" } });
          }
          return new Response(bin, { headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store" } });
        }
        case "/spotify/login": return await spotifyLogin(env, url);
        case "/spotify/callback": return await spotifyCallback(env, url);
        default:
          return new Response("CAIDEN relay is running. The clock reads /state.json.\n", { headers: { "Content-Type": "text/plain" } });
      }
    } catch (e) {
      return new Response(JSON.stringify({ ok: false, err: String((e && e.message) || e).slice(0, 80) }),
        { status: 502, headers: { "Content-Type": "application/json", "Cache-Control": "no-store" } });
    }
  },
};
