CV VERIFY — FILES TO PORT INTO MAIN FIRMWARE (firmware/esp32)
================================================================
Generated 2026-08-12. Ports the camera-image-display + IMG UART protocol
from the prototype (testing/CV_Verify_UART_Prototype/esp32) into the real
firmware (firmware/esp32), while KEEPING the real firmware's existing
IN SPEC / OUT OF SPEC logic that the prototype never had.

DROP-IN FILES (safe to overwrite directly — pure additions vs. current
firmware/esp32, confirmed by diff, nothing removed):

  include/rpi_uart.h        -> firmware/esp32/include/rpi_uart.h
  src/rpi_uart.cpp          -> firmware/esp32/src/rpi_uart.cpp
  include/ui_display.h      -> firmware/esp32/include/ui_display.h
  src/ui/ui_display.cpp     -> firmware/esp32/src/ui/ui_display.cpp

What these add:
  - rpi_uart.h/.cpp: "IMG <w> <h>" binary wire protocol (receives the
    actual captured/enhanced image over UART) + rpi_get_last_image*()
    getters. ALSO includes a real bugfix: sets the UART RX ring buffer to
    RPI_IMG_MAX_W*RPI_IMG_MAX_H+256 bytes before begin() — the default
    256-byte buffer caused visibly corrupted/shifted images during the
    ~16KB image transfer at 921600 baud (dropped bytes desync every
    subsequent pixel). Worth keeping even if the image feature itself is
    deferred.
  - ui_display.h/.cpp: adds ui_display_draw_grayscale_image() — draws a
    raw 8-bit greyscale buffer 1:1, converting to RGB565 pixel-by-pixel
    with no intermediate buffer (safe on a board with likely no PSRAM).

MERGED FILE (do NOT blind-copy — combines the prototype's image/spread-bar
display with the real firmware's existing spec-check, which the prototype
had dropped):

  src/ui/screens/verifying_screen.h    -> firmware/esp32/src/ui/screens/verifying_screen.h
  src/ui/screens/verifying_screen.cpp  -> firmware/esp32/src/ui/screens/verifying_screen.cpp

What changed vs. the CURRENT real firmware/esp32/src/ui/screens/verifying_screen.cpp:
  - Title moved from y=50 to y=20 to make room for the image (starts y=45).
  - Result state now draws: captured image (left) + median/IQR spread bar
    (right) + IN SPEC/OUT OF SPEC line below (unchanged logic: abs(median
    - scheduler_get_target_um()) <= TARGET_TOLERANCE_UM), + footer.
  - Added a "No size data (model not trained yet)" placeholder for when
    a capture completes via image-arrival alone but no real SIZE line
    came in yet (i.e. sizing.py is still a stub on the RPi side) — this
    is exactly what makes the eventual real model swap-in need NO
    firmware change/reflash: once the RPi starts sending real
    "SIZE <median> <iqr>" lines, this same code path renders the real
    result and spec check automatically.
  - Everything else (calib_breakage_add_point() call, TIMED_OUT handling,
    knob press to pop()) is unchanged from current firmware behavior.

NOT INCLUDED HERE — apply by hand, one-line each:

  1. ui.cpp.diff.txt (in this folder) — wires the "Camera feature" menu
     item to VerifyingScreen instead of the current stub/placeholder
     screen. This is a diff against the FULL ui.cpp, not a drop-in file,
     because firmware/esp32/src/ui.cpp is large and may have moved on
     since this diff was generated — apply the two hunks by hand rather
     than trusting line numbers.

  2. config.h — the prototype changed RPI_CAPTURE_TIMEOUT_MS from 8000 to
     16000. DELIBERATELY NOT carried over here. Per project decision,
     that value should only be raised once a real trained model has been
     benchmarked for actual inference time on the Pi Zero 2W — not
     copied over just because the prototype used a bigger number. Leave
     firmware/esp32/include/config.h's RPI_CAPTURE_TIMEOUT_MS at 8000
     until that benchmark exists.

BUILD NOTE: none of this has been compiled (no PlatformIO run in this
session) — build/flash-test before relying on it.
