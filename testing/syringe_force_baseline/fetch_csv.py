#!/usr/bin/env python3
"""
Load cell CSV receiver
Listens on serial, saves each test run to a timestamped CSV file.

Usage:
    pip install pyserial
    python receive.py                        # auto-detects port
    python receive.py --port COM3            # Windows
    python receive.py --port /dev/ttyUSB0   # Linux
    python receive.py --port /dev/cu.usbmodem1101  # macOS
"""

import serial
import serial.tools.list_ports
import argparse
import os
from datetime import datetime

BAUD = 115200
OUTPUT_DIR = "test_results"

# ── Argument parsing ──────────────────────────────────────────
parser = argparse.ArgumentParser()
parser.add_argument("--port", help="Serial port (auto-detected if omitted)")
args = parser.parse_args()

# ── Auto-detect port ──────────────────────────────────────────
def find_port():
    ports = list(serial.tools.list_ports.comports())
    # Prefer ESP32 / Silicon Labs / CH340 devices
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ["cp210", "ch340", "esp32", "usb serial", "uart"]):
            return p.device
    # Fall back to first available port
    if ports:
        return ports[0].device
    return None

port = args.port or find_port()
if not port:
    print("No serial port found. Plug in the ESP32 or pass --port explicitly.")
    exit(1)

print(f"Connecting to {port} at {BAUD} baud...")
ser = serial.Serial(port, BAUD, timeout=1)
print("Connected. Waiting for test...\n")

os.makedirs(OUTPUT_DIR, exist_ok=True)

# ── Main loop ─────────────────────────────────────────────────
recording   = False
csv_file    = None
csv_writer  = None
test_count  = 0

try:
    while True:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()

        if not line:
            continue

        # ── Pass-through comment/info lines to terminal ──────
        if line.startswith("#"):
            print(line)
            continue

        # ── Test start ────────────────────────────────────────
        if line == "TEST_START":
            test_count += 1
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename  = os.path.join(OUTPUT_DIR, f"test_{timestamp}.csv")
            csv_file  = open(filename, "w", newline="")
            recording = True
            print(f"\n[TEST {test_count}] Recording → {filename}")
            continue

        # ── CSV header row ────────────────────────────────────
        if line == "ms,grams" and recording:
            csv_file.write("ms,grams\n")
            continue

        # ── Test end ──────────────────────────────────────────
        if line.startswith("TEST_END"):
            # TEST_END,peak,<value>
            parts = line.split(",")
            peak = parts[2] if len(parts) >= 3 else "?"
            if csv_file:
                csv_file.close()
                csv_file = None
            recording = False
            print(f"[TEST {test_count}] Done. Peak: {peak} g  →  saved to {filename}")
            continue

        # ── Data row ──────────────────────────────────────────
        if recording and "," in line:
            print(f"  {line}")
            csv_file.write(line + "\n")
            csv_file.flush()  # ensure data hits disk in real time

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    if csv_file:
        csv_file.close()
    ser.close()