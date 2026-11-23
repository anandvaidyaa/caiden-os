"""
CAIDEN OS - Wi-Fi setup over the USB cable (no phone needed).

    python firmware/tools/usb_setup.py            (finds the clock's USB port)
    python firmware/tools/usb_setup.py COM7       (or name the port: COM7, /dev/ttyUSB0, ...)

Asks for your Wi-Fi name and password on this PC (the password is typed hidden and
never printed), sends them to the clock as one JSON line over USB, then waits until
the clock reports it is online.
"""
import getpass
import json
import sys
import time

import serial  # pyserial (installed with esptool)


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


port = sys.argv[1] if len(sys.argv) > 1 else find_port()
TZ = [("IST-5:30", "India"), ("UTC0", "UTC"), ("GMT0BST,M3.5.0/1,M10.5.0", "London"),
      ("CET-1CEST,M3.5.0,M10.5.0/3", "Berlin / Paris"), ("<+04>-4", "Dubai"), ("<+08>-8", "Singapore"),
      ("JST-9", "Tokyo"), ("AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney"), ("EST5EDT,M3.2.0,M11.1.0", "New York"),
      ("PST8PDT,M3.2.0,M11.1.0", "Los Angeles")]

print("CAIDEN OS - USB Wi-Fi setup")
ssid = input("Wi-Fi name (SSID): ").strip("\r\n")
password = getpass.getpass("Wi-Fi password (hidden, Enter for open network): ")
city = input("City (for weather): ").strip() or "London"
print("Time zone:")
for i, (_, name) in enumerate(TZ, 1):
    print("  %2d  %s" % (i, name))
pick = input("Number, or a POSIX TZ string [1]: ").strip() or "1"
tz = TZ[int(pick) - 1][0] if pick.isdigit() and 1 <= int(pick) <= len(TZ) else pick
fmt = input("Clock format 12 or 24 [12]: ").strip() or "12"
theme = input("Theme midnight / aurora / crystal / hypertech [midnight]: ").strip().lower() or "midnight"
if theme not in ("midnight", "aurora", "crystal", "hypertech"):
    theme = "midnight"

msg = json.dumps({"cmd": "setup", "ssid": ssid, "pass": password, "city": city,
                  "tz": tz, "h24": 1 if fmt == "24" else 0, "theme": theme}, ensure_ascii=False)
if len(msg.encode("utf-8")) > 290:
    sys.exit("Settings too long for one line (SSID/password/city). Please shorten the city name.")

s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.3
s.dtr = s.rts = False          # don't reset the clock when opening the port
s.open()
s.reset_input_buffer()
s.write((msg + "\n").encode("utf-8"))
del password, msg

print("Sent. Waiting for the clock...")
buf, deadline, saved = "", time.time() + 90, False
while time.time() < deadline:
    buf += s.read(512).decode("utf-8", "replace")
    while "\n" in buf:
        line, buf = buf.split("\n", 1)
        line = line.strip()
        if line.startswith("[setup]") or line.startswith("[net]") or line.startswith("[clock]") or line.startswith("[wx]"):
            print("  clock:", line)
        if "[setup] saved" in line:
            saved = True
        if "[setup] save failed" in line or "bad json" in line:
            sys.exit("The clock could not save the settings.")
        if line.startswith("[net] online"):
            print("Done - CAIDEN is on your Wi-Fi. Settings page: http://caiden.local")
            s.close()
            sys.exit(0)
        if "connect timeout" in line:
            print("The clock could not join that network (wrong password or 5 GHz-only network?).")
            s.close()
            sys.exit(1)
s.close()
print("Saved, but no 'online' message yet." if saved else "No reply from the clock - is the port right and the logger closed?")
