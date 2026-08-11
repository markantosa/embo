"""Request/reply serial link to the ESP32-S3 — the Layer 1 piece that
replaces ../cv-pipeline/uart_link.py's continuous-stream model.

Protocol (unchanged wire format, see firmware/esp32/src/rpi_uart.cpp and
firmware/esp32/include/rpi_uart.h):
  ESP32 -> Pi:  "CAPTURE\n"                          (request, no args)
  Pi -> ESP32:  "SIZE <median_um> <iqr_um>\n"         (reply)
             or "IMG <width> <height>\n" + <width*height> raw greyscale
                bytes (no trailing delimiter — length is already known
                from the header) — see PROTOTYPE note below.
             or any other line, treated as a free-form status/diagnostic —
                firmware's sscanf parser silently discards anything that
                isn't a valid SIZE/IMG line, so status strings are always
                safe to send.

Firmware gives up and marks the request TIMED_OUT if no SIZE/IMG reply
arrives within RPI_CAPTURE_TIMEOUT_MS (firmware/esp32/include/config.h,
currently 8000ms) — see CAPTURE_TIMEOUT_S in config.py. Capture + inference
must fit inside that budget; this module does not enforce it, main.py does
(the link itself just sends/receives lines and bytes).

PROTOTYPE — IMG protocol: built alongside
testing/CV_Verify_UART_Prototype/esp32 (a throwaway copy of firmware/, not
the real firmware) to prove out end-to-end photo transfer + on-device
display ahead of detection.py/sizing.py existing. See that copy's
rpi_uart.h for the receiving side and the full rationale (raw greyscale,
not JPEG — no PSRAM to assume on this board). If/when this protocol gets
adopted into the real firmware, this module doesn't need to change either
way — the wire format is identical.
"""
import serial

from config import UART_PORT, UART_BAUD, CAPTURE_COMMAND


class FirmwareLink:
    def __init__(self, port: str = UART_PORT, baud: int = UART_BAUD):
        # timeout=0.1: readline() below returns (possibly empty) at least
        # every 100ms rather than blocking forever, so main.py's loop stays
        # responsive to Ctrl+C / other housekeeping between polls.
        self._ser = serial.Serial(port, baud, timeout=0.1)

    def wait_for_capture_request(self) -> bool:
        """Poll for one line from the ESP32. Returns True if it was exactly
        "CAPTURE", False otherwise (including on a read timeout with no
        data, or any unrecognised line, which is expected — the ESP32 may
        be silent most of the time between verification requests).
        """
        raw = self._ser.readline()
        if not raw:
            return False
        line = raw.decode("ascii", errors="replace").strip()
        return line == CAPTURE_COMMAND

    def send_size(self, median_um: int, iqr_um: int) -> None:
        """Send a SIZE packet. median_um must be > 0, iqr_um >= 0 to match
        firmware's parser — values outside that range are silently
        discarded on the firmware side, not just rejected here.
        """
        if median_um <= 0 or iqr_um < 0:
            raise ValueError(f"invalid size stats: median={median_um} iqr={iqr_um}")
        self._ser.write(f"SIZE {int(median_um)} {int(iqr_um)}\n".encode("ascii"))

    def send_image(self, width: int, height: int, pixels: bytes) -> None:
        """Send a raw greyscale image: "IMG <width> <height>\\n" followed by
        exactly width*height raw bytes (1 byte/pixel, row-major). Must
        match firmware's RPI_IMG_MAX_W/H (testing/CV_Verify_UART_Prototype's
        rpi_uart.h) — this function doesn't know that constant, so callers
        (main.py) are responsible for downscaling to a size the receiving
        firmware build actually allocated a buffer for. No trailing
        newline after the binary payload — firmware knows the exact byte
        count from the header and reads exactly that many bytes.
        """
        if len(pixels) != width * height:
            raise ValueError(f"pixels length {len(pixels)} != width*height ({width}*{height})")
        self._ser.write(f"IMG {int(width)} {int(height)}\n".encode("ascii"))
        self._ser.write(pixels)

    def send_status(self, message: str) -> None:
        """Free-form status/diagnostic string. Firmware discards any line
        that isn't a valid SIZE packet, so this is safe to use liberally —
        e.g. reporting a bad frame or an unimplemented pipeline stage
        without crashing the link, and safe to send while idle between
        CAPTURE requests too.
        """
        self._ser.write((message.strip() + "\n").encode("ascii"))

    def close(self):
        self._ser.close()
