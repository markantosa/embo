"""Serial link to the ESP32-S3. See firmware/esp32/src/rpi_uart.cpp for the
receiving side and SOFTWARE_TODO.md Layer 1 task 2 / Layer 4 task 9.

Protocol: a single line "SIZE <median_um> <iqr_um>\\n". Firmware's sscanf
parser silently discards anything that doesn't match this shape exactly —
that's intentional and lets send_status() below share the same link safely.
Do not add extra fields to a SIZE line without coordinating with firmware
first (see SOFTWARE_TODO.md task 9) — they'd be silently ignored, not
parsed, until rpi_uart.cpp is explicitly extended.
"""
import serial

from config import UART_PORT, UART_BAUD


class FirmwareLink:
    def __init__(self, port: str = UART_PORT, baud: int = UART_BAUD):
        self._ser = serial.Serial(port, baud, timeout=0)

    def send_size(self, median_um: int, iqr_um: int) -> None:
        """Send a SIZE packet. median_um must be > 0, iqr_um >= 0 to match
        firmware's parser — values outside that range are silently
        discarded on the firmware side, not just rejected here.
        """
        if median_um <= 0 or iqr_um < 0:
            raise ValueError(f"invalid size stats: median={median_um} iqr={iqr_um}")
        self._ser.write(f"SIZE {int(median_um)} {int(iqr_um)}\n".encode("ascii"))

    def send_status(self, message: str) -> None:
        """Free-form status/diagnostic string. Firmware discards any line
        that isn't a valid SIZE packet, so this is safe to use liberally —
        e.g. for reporting a bad camera frame or an unimplemented pipeline
        stage without crashing the link.
        """
        self._ser.write((message.strip() + "\n").encode("ascii"))

    def close(self):
        self._ser.close()
