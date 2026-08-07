import serial
import threading
import queue
import csv
import time
from datetime import datetime

# ----------------------------
# Configuration
# ----------------------------
PORT = "/dev/cu.usbmodem1101"          # Change to your port
BAUD = 115200
TIMEOUT = 0.01

CSV_FILE = datetime.now().strftime("serial_log_%Y%m%d_%H%M%S.csv")

QUEUE_SIZE = 10000
WRITE_BATCH = 100
FLUSH_INTERVAL = 1.0   # seconds

# ----------------------------
# Shared Queue
# ----------------------------
data_queue = queue.Queue(maxsize=QUEUE_SIZE)
running = True


def serial_reader():
    global running

    ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)

    while running:
        line = ser.readline()

        if line:
            timestamp_ns = time.perf_counter_ns()

            try:
                text = line.decode("utf-8").strip()
            except UnicodeDecodeError:
                text = line.decode("utf-8", errors="replace").strip()

            try:
                data_queue.put_nowait((timestamp_ns, text))
            except queue.Full:
                print("WARNING: Queue Full! Data Lost.")

    ser.close()


def csv_writer():
    global running

    packets = 0
    last_flush = time.time()
    start = time.perf_counter()

    with open(CSV_FILE, "w", newline="", buffering=1024 * 1024) as f:

        writer = csv.writer(f)
        writer.writerow([
            "packet",
            "timestamp_ns",
            "timestamp_us",
            "serial_data"
        ])

        buffer = []

        while running or not data_queue.empty():

            try:
                item = data_queue.get(timeout=0.1)

                packets += 1

                timestamp_ns, text = item

                buffer.append([
                    packets,
                    timestamp_ns,
                    timestamp_ns / 1000,
                    text
                ])

                if len(buffer) >= WRITE_BATCH:
                    writer.writerows(buffer)
                    buffer.clear()

                now = time.time()

                if now - last_flush >= FLUSH_INTERVAL:
                    if buffer:
                        writer.writerows(buffer)
                        buffer.clear()

                    f.flush()
                    last_flush = now

            except queue.Empty:
                pass

        if buffer:
            writer.writerows(buffer)

        f.flush()

    elapsed = time.perf_counter() - start

    print("\nFinished.")
    print(f"Packets Logged : {packets}")
    print(f"Elapsed Time   : {elapsed:.2f} s")
    print(f"Average Rate   : {packets/elapsed:.2f} packets/s")


reader = threading.Thread(target=serial_reader)
writer = threading.Thread(target=csv_writer)

reader.start()
writer.start()

print("Logging... Press Ctrl+C to stop.")

try:
    while True:
        time.sleep(1)

except KeyboardInterrupt:
    print("\nStopping...")
    running = False

reader.join()
writer.join()

print(f"Saved to {CSV_FILE}")