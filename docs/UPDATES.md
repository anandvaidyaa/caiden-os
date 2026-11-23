# Installing and updating CAIDEN OS

## First install (USB, once)

The first install has to go over the USB cable, because the stock firmware can't install other firmware.
After that, every update can come over Wi-Fi.

1. **Back up the stock firmware** first: [BACKUP_AND_RESTORE.md](BACKUP_AND_RESTORE.md).
2. **Build** (needs [PlatformIO](https://platformio.org/install/cli)):
   ```
   cd firmware
   python -I tools/gen_portal.py
   pio run -e caiden
   ```
   The image is `firmware/.pio/build/caiden/firmware.bin`.
3. **Flash** (replace `PORT`, see the backup guide for how to find it):
   ```
   python -m esptool --port PORT --baud 115200 write-flash 0x0 .pio/build/caiden/firmware.bin
   ```
   Use 115200 baud; faster speeds are unreliable on this clock. Then follow "Set up Wi-Fi" in the main README.

## Updates from anywhere (cloud relay)

Once the clock runs CAIDEN OS and knows your [cloud relay](../relay/README.md), new versions travel through
the relay. The clock and your PC don't need to be on the same Wi-Fi.

**One-time: get your clock's update key.** Each clock makes its own secret update key. With the clock on USB,
from `firmware/`:
```
python -I tools/ota_update.py --fetch-key
```
This saves it in `firmware/tools/ota_key.txt`. Treat that file like a password; `.gitignore` keeps it out
of git. You need the key to sign updates, so the clock only installs images made on your PC.

**Each update:**
1. Raise the version in `firmware/platformio.ini` (`-DCAIDEN_FW_VERSION=\"x.y.z\"`). The clock only installs
   newer versions.
2. Build (step 2 above) and note the SHA-256 of `firmware.bin`
   (`certutil -hashfile .pio\build\caiden\firmware.bin SHA256`, or `shasum -a 256 ...`).
3. Publish to your relay (needs the Wrangler login from the relay guide):
   ```
   python -I tools/publish_update.py --sha256 <SHA-256>
   ```
4. On the clock's settings page, open **System**, press **Check for update**, then **Install CAIDEN OS x.y.z**.
   The clock downloads the image (about 40 seconds), checks its signature and MD5, and restarts.
   Nothing installs until you press Install.

A failed or interrupted download changes nothing: the new image only replaces the running one after every
byte arrived and the checksum matched.

**Check the result from anywhere:** `http://<your relay>/clock.json` shows the version the clock runs, its
uptime, free memory, Wi-Fi signal, why it last restarted, and the last update error, if any.

## Updates on your home network (no relay)

`tools/ota_update.py` sends an image straight to the clock over your Wi-Fi:
```
python -I tools/ota_update.py --sha256 <SHA-256>              (uses caiden.local)
python -I tools/ota_update.py --sha256 <SHA-256> --host 192.168.1.50
```
It signs each upload with the same update key (fetched over USB the first time).

## If an update goes wrong

Flash over USB as in the first install; that always works. To go back to the stock firmware, see
[BACKUP_AND_RESTORE.md](BACKUP_AND_RESTORE.md).

## Settings-page password

The settings page (`http://caiden.local`, or the clock's IP address) is locked with a password the clock
makes itself.

- **Where to find it:** the clock's **System** screen, **Password** tile: 10 lowercase letters and digits, with
  no look-alikes such as 0/o or 1/l. On the lock page, **Show it on the clock** switches the clock to that screen.
- **Logging in:** each phone or computer enters it once; the browser then keeps a login token.
- **New password:** System tab, **Password**, **Make a new password**. Every other browser is logged out.
- **Wrong guesses:** 5 wrong tries lock login for 1 minute, doubling up to 16 minutes.
- **Limits:** anyone who can see the System screen can read the password, and the page uses plain HTTP, so
  the password crosses your Wi-Fi unencrypted when you log in.

## Troubleshooting with USB

The clock prints a log on its USB serial port (115200 baud). From `firmware/`:
```
python -I tools/serial_cmd.py '{"cmd":"status"}'
```
`tools/serial_cmd.py` finds the clock's port by itself; pass `--port COM7` (or `/dev/ttyUSB0`) if you have
several serial devices.
