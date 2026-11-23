"""
Send CAIDEN OS serial commands and print the clock's log lines.

    python -I tools/serial_cmd.py [--port PORT] [--reset] [--wait SECONDS] '{"cmd":"status"}' ...

Finds the clock's USB port (or use --port), opens it without toggling reset (unless --reset), sends each JSON command,
waits, and prints log lines (those starting with '['). HTTP request logs are hidden.
"""
import sys
import time

import serial


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


args = sys.argv[1:]
reset = "--reset" in args
wait = 6.0
if "--wait" in args:
    i = args.index("--wait"); wait = float(args[i + 1]); del args[i:i + 2]
port = None
if "--port" in args:
    i = args.index("--port"); port = args[i + 1]; del args[i:i + 2]
cmds = [a for a in args if a.startswith("{")]

s = serial.Serial()
s.port, s.baudrate, s.timeout = port or find_port(), 115200, 0.2
s.dtr = s.rts = False
s.open()
if reset:
    s.rts = True; time.sleep(0.15); s.rts = False
buf = b""
t_end = time.time() + (wait if reset else 0.5)
while time.time() < t_end:
    buf += s.read(4096)
for c in cmds:
    s.write((c + "\n").encode())
    t_end = time.time() + wait
    while time.time() < t_end:
        buf += s.read(4096)
s.close()
for line in buf.decode("utf-8", "replace").splitlines():
    line = line.strip()
    if line.startswith("[") and not line.startswith("[http]"):
        print(line)
