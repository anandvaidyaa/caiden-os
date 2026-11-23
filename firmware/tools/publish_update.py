#!/usr/bin/env python3
"""Publish a firmware image to the CAIDEN cloud relay, so the clock can update on any Wi-Fi.

    python -I tools/publish_update.py --sha256 <expected image SHA-256>

Steps:
  1. Checks the built image (.pio/build/caiden/firmware.bin) against the SHA-256 you pass and that
     it contains the version in platformio.ini.
  2. Signs "caiden-fw:<version>:<size>:<md5>" with the clock's update key (tools/ota_key.txt,
     HMAC-SHA256). The key never leaves this PC; the relay stores only the image and the signature.
  3. Uploads the image (KV key fw_bin), then its description (fw_meta) with Wrangler
     (relay/wrangler.toml, `npx wrangler login` once).

Nothing is installed by this: the clock's settings page shows "Install CAIDEN OS <version>", and the
update runs only when you press it. The clock refuses images that are not newer than what it runs,
not signed with its key, or whose MD5 does not match.
"""
import argparse
import hashlib
import hmac
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
RELAY = os.path.join(os.path.dirname(FW), "relay")
BIN = os.path.join(FW, ".pio", "build", "caiden", "firmware.bin")
KEY_FILE = os.path.join(HERE, "ota_key.txt")


def wrangler(*args):
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        sys.exit("npx not found (install Node.js)")
    r = subprocess.run([npx, "--yes", "wrangler@4", *args], cwd=RELAY, capture_output=True, text=True)
    if r.returncode:
        sys.exit("wrangler failed:\n" + (r.stderr or r.stdout)[-800:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sha256", required=True)
    a = ap.parse_args()

    data = open(BIN, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    if sha != a.sha256.lower():
        sys.exit(f"SHA-256 mismatch: image is {sha}")
    ini = open(os.path.join(FW, "platformio.ini"), encoding="utf-8").read()
    m = re.search(r'CAIDEN_FW_VERSION=\\"([0-9.]+)\\"', ini)
    if not m:
        sys.exit("version not found in platformio.ini")
    ver = m.group(1)
    if b"OS %s" % ver.encode() not in data and ver.encode() not in data:
        sys.exit(f"image does not contain version {ver}: rebuild first")
    key = open(KEY_FILE, encoding="ascii").read().strip()
    if not re.fullmatch(r"[0-9a-f]{32}", key):
        sys.exit("tools/ota_key.txt is not a valid key (run ota_update.py --fetch-key over USB)")

    md5 = hashlib.md5(data).hexdigest()
    msg = f"caiden-fw:{ver}:{len(data)}:{md5}".encode()
    mac = hmac.new(key.encode(), msg, hashlib.sha256).hexdigest()
    meta = json.dumps({"v": ver, "size": len(data), "md5": md5, "mac": mac}, separators=(",", ":"))

    print(f"Publishing CAIDEN OS {ver} ({len(data)} B, sha256 {sha[:12]}...)")
    wrangler("kv", "key", "put", "fw_bin", "--path", BIN, "--binding", "CAIDEN", "--remote")
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        f.write(meta)
    try:
        wrangler("kv", "key", "put", "fw_meta", "--path", f.name, "--binding", "CAIDEN", "--remote")
    finally:
        os.remove(f.name)
    print("Done. The clock sees it within 6 hours, or at once with 'Check for update' on its settings page.")


if __name__ == "__main__":
    main()
