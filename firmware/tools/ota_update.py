#!/usr/bin/env python3
"""Wireless firmware update for the CAIDEN clock.

    python tools/ota_update.py --sha256 <expected image SHA-256>

Steps:
  1. Checks the image's SHA-256 against the value you pass (printed by the build, see the README).
  2. Uploads it to the clock with the clock's private update key, size and MD5.
  3. The clock stages the image in free flash, checks the MD5, and only then restarts into it.
     A failed or interrupted upload leaves the current firmware running.
  4. Waits for the clock to come back and confirms it is running exactly this image.

The update key is read once from the clock over USB ({"cmd":"otakey"}) and kept in
tools/ota_key.txt on this PC. Treat that file like a password; never share it. The key itself is
never sent over Wi-Fi: each upload signs a one-time nonce from the clock with it (HMAC-SHA256).
Use --fetch-key to (re)read it over USB.
"""
import argparse
import hashlib
import hmac
import http.client
import json
import os
import sys
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
KEY_FILE = os.path.join(HERE, "ota_key.txt")
DEFAULT_BIN = os.path.join(FW, ".pio", "build", "caiden", "firmware.bin")


def default_host():
    return "caiden.local"


def find_port():
    """The clock's USB port: the CH340 serial chip (VID 1A86), or the only serial port there is."""
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    ch340 = [p.device for p in ports if p.vid == 0x1A86]
    if len(ch340) == 1:
        return ch340[0]
    if len(ports) == 1:
        return ports[0].device
    names = ", ".join(p.device for p in ports) or "none"
    sys.exit("Can't tell which port is the clock (found: %s). Pass it with --port, e.g. --port COM7 or --port /dev/ttyUSB0." % names)


def fetch_key(port):
    import serial  # pyserial (installed with esptool)
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = s.rts = False                     # don't reset the clock
    s.open()
    try:
        s.reset_input_buffer()
        s.write(b'{"cmd":"otakey"}\n')
        buf, t0 = b"", time.time()
        while time.time() - t0 < 4:
            buf += s.read(512)
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith("[otakey] "):
                    key = line[9:].strip()
                    if len(key) == 32 and all(c in "0123456789abcdef" for c in key):
                        return key
    finally:
        s.close()
    sys.exit("Could not read the update key over USB. Is the clock on %s with firmware 0.2.0 or newer?" % port)


def load_key(args):
    if args.fetch_key or not os.path.exists(KEY_FILE):
        key = fetch_key(args.port or find_port())
        with open(KEY_FILE, "w", encoding="utf-8") as f:
            f.write(key + "\n")
        print("Update key saved to tools/ota_key.txt (private).")
        return key
    with open(KEY_FILE, encoding="utf-8") as f:
        return f.read().strip()


def get_state(host, timeout=5):
    c = http.client.HTTPConnection(host, 80, timeout=timeout)
    try:
        c.request("GET", "/api/state")
        r = c.getresponse()
        return json.loads(r.read()) if r.status == 200 else None
    finally:
        c.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sha256", help="expected SHA-256 of the image (safety check)")
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--host", default=default_host(), help="clock address (default caiden.local; use its IP if that name does not resolve)")
    ap.add_argument("--port", default=None, help="USB port, only used to read the update key (found automatically)")
    ap.add_argument("--fetch-key", action="store_true", help="re-read the update key over USB")
    args = ap.parse_args()
    if args.fetch_key and not args.sha256:       # only save the key (needed once, also for publish_update.py)
        load_key(args)
        return
    if not args.sha256:
        sys.exit("Pass --sha256 <expected SHA-256 of the image>.")
    if not args.host:
        sys.exit("No clock address: pass --host 192.168.x.x")

    data = open(args.bin, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    if sha != args.sha256.lower():
        sys.exit("SHA-256 mismatch: image is %s, expected %s. Not uploading." % (sha, args.sha256))
    if data[:1] != b"\xe9":
        sys.exit("Not an ESP8266 firmware image. Not uploading.")
    md5 = hashlib.md5(data).hexdigest()
    key = load_key(args)

    try:
        before = get_state(args.host)
    except OSError as e:
        sys.exit("Clock not reachable at %s (%s). Is this PC on the same Wi-Fi?" % (args.host, type(e).__name__))
    print("Clock %s is running %s. Uploading %d bytes..." % (args.host, before and before.get("fw"), len(data)))
    old_md5 = before.get("md5") if before else None
    if before and before.get("md5") == md5:
        print("The clock already runs this exact image. Nothing to do.")
        return

    c = http.client.HTTPConnection(args.host, 80, timeout=10)
    c.request("GET", "/api/update/nonce", headers={"X-Caiden": "1"})
    r = c.getresponse()
    body_ = r.read()
    c.close()
    if r.status != 200:
        sys.exit("Clock refused the update request (HTTP %d)." % r.status)
    nonce = json.loads(body_)["nonce"]
    auth = hmac.new(key.encode(), ("%s:%d:%s" % (nonce, len(data), md5)).encode(), hashlib.sha256).hexdigest()

    boundary = uuid.uuid4().hex
    head = ("--%s\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n" % boundary).encode()
    tail = ("\r\n--%s--\r\n" % boundary).encode()
    body = head + data + tail
    c = http.client.HTTPConnection(args.host, 80, timeout=60)
    t0 = time.time()
    c.putrequest("POST", "/api/update")
    for k, v in (("Content-Type", "multipart/form-data; boundary=" + boundary), ("Content-Length", str(len(body))),
                 ("X-Caiden", "1"), ("X-Ota-Auth", auth), ("X-Ota-Size", str(len(data))), ("X-Ota-Md5", md5)):
        c.putheader(k, v)
    c.endheaders()
    sent, step = 0, 4096
    while sent < len(body):
        c.send(body[sent:sent + step])
        sent += step
        pct = min(100, sent * 100 // len(body))
        print("\r  sent %3d%%" % pct, end="", flush=True)
    try:
        r = c.getresponse()
        reply = r.read().decode(errors="replace")
        status = r.status
    except OSError:                     # reply lost (seen 2026-11-22: 0.4.0 installed fine, reply timed out)
        status, reply = None, ""
    c.close()
    print("\r  sent 100%% in %.1f s" % (time.time() - t0))
    if status is None:
        print("No reply from the clock. Checking whether it installed the image (MD5)...")
    elif status != 200:
        sys.exit("Clock refused the update (HTTP %d): %s\nThe current firmware is still running." % (status, reply))
    else:
        print("Image verified by the clock. Waiting for it to restart...")
    time.sleep(6)
    deadline = time.time() + 90
    while time.time() < deadline:
        try:
            st = get_state(args.host, timeout=3)
            if st:
                if st.get("md5") == md5:
                    print("Done: the clock is running the new image (fw %s, MD5 verified)." % st.get("fw"))
                    return
                if status is None and st.get("md5") == old_md5:
                    sys.exit("The clock is still running its old firmware (fw %s); the update did not install. "
                             "Try again." % st.get("fw"))
                sys.exit("The clock came back but reports MD5 %s, not %s." % (st.get("md5"), md5))
        except OSError:
            pass
        time.sleep(3)
    if status == 200:   # 0.8.3+: /api/state needs the settings-page login, so the version can't be read here
        print("The clock verified the image and is restarting into it. Its settings page is password-locked, so check\n"
              "the version on the clock's System screen or at http://<your relay>/clock.json.")
        return
    sys.exit("The clock did not come back within 90 s. Check the screen; USB flashing still works as a fallback.")


if __name__ == "__main__":
    main()
