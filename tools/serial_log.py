"""
Read serial output from the timer and write to a rolling log file.
Claude reads serial_log.txt with the Read tool while this runs in background.

Usage:
    python tools\serial_log.py          # default COM4, 115200
    python tools\serial_log.py COM5     # specify port
"""
import sys
import os
import datetime

PORT     = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD     = 115200
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE  = os.path.join(TOOLS_DIR, "serial_log.txt")
MAX_LINES = 500

def log(msg):
    ts    = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    entry = f"[{ts}] {msg}"
    print(entry, flush=True)
    lines.append(entry)
    if len(lines) > MAX_LINES:
        del lines[0]
    with open(LOG_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

lines = []

try:
    import serial
except ImportError:
    log("ERROR: pyserial not installed — run: pip install pyserial")
    sys.exit(1)

log(f"Serial logger started: {PORT} @ {BAUD} → {LOG_FILE}")

try:
    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").rstrip()
            log(text)
except KeyboardInterrupt:
    log("Stopped by user.")
except serial.SerialException as e:
    log(f"ERROR: {e}")
    sys.exit(1)
