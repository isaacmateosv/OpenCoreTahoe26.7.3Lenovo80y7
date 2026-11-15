#!/usr/bin/env python3 -I
"""Per-interval rates from the patched RMII2C's "RMII2C Stats" (IORegistry).
Usage: rmistat.py [seconds] — the driver republishes its counters every 2 s while
the trackpad line is active, and on sleep/wake/recovery events."""
import plistlib, subprocess, sys, time

def stats():
    out = subprocess.run(["ioreg", "-a", "-r", "-c", "RMII2C", "-d", "1"],
                         capture_output=True).stdout
    if not out:
        return None
    nodes = plistlib.loads(out)
    for n in nodes:
        s = n.get("RMII2C Stats")
        if s:
            return s, n.get("Interrupt mode", "?")
    return None

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
first = stats()
if not first:
    print("patched RMII2C not found (stock driver loaded?)")
    sys.exit(1)
a, mode = first
time.sleep(secs)
b, mode = stats()
keys = ["Interrupts", "Reads", "Attention reports", "Empty reads", "Other reports",
        "Read failures", "Recoveries", "Polling fallbacks", "Sleeps", "Wakes"]
print(f"mode: {b.get('Mode')}  |  Interrupt mode: {mode}")
print(f"{'counter':<22}{'total':>10}{'per sec':>10}")
for k in keys:
    d = b.get(k, 0) - a.get(k, 0)
    print(f"{k:<22}{b.get(k, 0):>10}{d / secs:>10.1f}")
for k in ["Peak attention per second", "Peak reads per second", "Max input length",
          "Last empty read", "Last other report ID", "Last recovery", "HID command failures",
          "Pad re-arms", "Pad re-arm failures", "Stuck after wake ms", "Polling interval ms",
          "HID wMaxInputLength", "HID wCommandRegister", "GPIO pin"]:
    if k in b:
        print(f"{k:<26} {b.get(k)!r}")
for k in sorted(x for x in b if x.startswith("Pad ") and x[4:5].isdigit()):
    print(f"{k:<26} {b[k]}")
