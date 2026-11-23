# Back up and restore the stock firmware

Do this **before** you install CAIDEN OS. A full backup lets you put the clock back exactly as it came:
bootloader, firmware, files, settings and radio calibration.

> ⚠️ **The backup holds your Wi-Fi password** (and an OpenWeatherMap key) in plain text. Keep it on your own
> computer. Never upload it, commit it or attach it anywhere. This repository's `.gitignore` already
> excludes the `backups/` folder.

You need Python 3 and esptool:

```
python -m pip install esptool
```

## 1. Find the clock's port

Plug the clock in with a USB-C data cable. The port is the **USB-SERIAL CH340** device:

- **Windows:** Device Manager, Ports (COM & LPT), for example `COM5`.
- **macOS:** `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`.
- **Linux:** `/dev/ttyUSB0`. Add yourself to the `dialout` group if access is denied.

Below, replace `PORT` with yours.

## 2. Read the flash twice

Use 115200 baud: faster speeds gave corrupt reads on this clock. `--after no-reset` keeps the stock firmware
from running between the reads (it rewrites its settings sectors at every boot).

```
mkdir backups
python -m esptool --port PORT --baud 115200 --after no-reset read-flash 0 0x400000 backups/caiden_stock_backup.bin
python -m esptool --port PORT --baud 115200 read-flash 0 0x400000 backups/caiden_stock_backup_verify.bin
```

Each read takes about 6 minutes.

## 3. Check the two reads match

Windows:
```
certutil -hashfile backups\caiden_stock_backup.bin SHA256
certutil -hashfile backups\caiden_stock_backup_verify.bin SHA256
```
macOS / Linux:
```
shasum -a 256 backups/caiden_stock_backup*.bin
```

The two hashes must be identical, and each file exactly 4,194,304 bytes. Write the hash down (for example in
`backups/SHA256.txt`). If the hashes differ, read again: the second read may have caught the clock booting.

## Restore

1. Check the backup is intact: its SHA-256 must match the one you wrote down.
2. Write all 4 MB back:
   ```
   python -m esptool --port PORT --baud 115200 write-flash 0x0 backups/caiden_stock_backup.bin
   ```
   Don't run `erase-flash` first; a full image doesn't need it.
3. Unplug and plug the clock back in. It starts the stock "Smart Weather Clock" firmware with its old settings.

## If the clock seems bricked

The ESP8266's ROM bootloader can't be overwritten, so the clock can always be recovered.

1. Unplug the clock and plug it back in, then run the restore (or the CAIDEN OS install) again. esptool puts
   the chip into its bootloader through the CH340's DTR/RTS lines.
2. If esptool still can't connect, try another USB cable (some carry power only) or another port.
