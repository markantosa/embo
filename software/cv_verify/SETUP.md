# Pi Zero 2W Setup Guide (cv_verify)

Written for someone coming from MCU/ESP32 flashing, not Linux. The mental model
difference: an ESP32 has no OS — "flashing" writes your program directly into
its flash. A Pi is a full Linux computer — you flash an *operating system* onto
its SD card once, then you develop on it like any networked Linux machine,
copying/running code the same way you would on a server. You don't re-flash
the OS every time your Python changes.

This mirrors `../RPI_SETUP_GUIDE.md` (your friend's Pi 5 guide) — SSH, VS Code
Remote-SSH, and venv setup are identical steps. Only the parts that differ for
Zero 2W are called out explicitly below; where a step is unchanged, this guide
just points at the equivalent section in that doc instead of repeating it.

---

## What you need

- Raspberry Pi Zero 2 W + a good 5V/2.5A micro-USB power supply
- microSD card (32GB+) + USB card reader for your Windows PC
- **Zero-format CSI camera adapter cable** — the Zero 2W's camera connector is
  physically smaller than a Pi 5/4's. The Global Shutter Camera's stock ribbon
  will not plug in directly; you need the Zero-specific adapter cable (sold by
  the same vendors as the camera, look for "Camera Cable for Raspberry Pi Zero").
  **Order/confirm this before anything else** — it's the one item that blocks
  physically connecting the camera at all.
- OV9281-110 camera module (per the earlier discussion — confirm this is the
  unit you have in hand, not the Global Shutter Camera the old docs assumed;
  connector/driver notes below assume OV9281)
- A network your Pi and Windows PC are both on (WiFi)
- Windows PC with VS Code already installed

---

## 1. Flash the OS

Same tool as the Pi 5 guide (Raspberry Pi Imager), different choices:

1. Download **Raspberry Pi Imager** from raspberrypi.com.
2. Insert the microSD card.
3. Open Imager:
   - **Device:** Raspberry Pi Zero 2 W
   - **OS:** Raspberry Pi OS **Lite (64-bit)** — no desktop GUI. The Zero 2W has
     512MB RAM total; a desktop environment sitting idle in the background isn't
     worth the memory pressure when everything here happens over SSH anyway.
   - **Storage:** your SD card
4. Click the **gear icon (⚙) / "Edit Settings"** before writing:
   - **hostname** (e.g. `embo-cv`)
   - **username and password**
   - **WiFi** (SSID + password) — the Zero 2W only has WiFi, no Ethernet port,
     so this step isn't optional the way it might be on a 5
   - **Enable SSH** (password auth for now)
5. Write and wait for verification to finish.

---

## 2. First boot + SSH

Same as `../RPI_SETUP_GUIDE.md` §2–3:

```powershell
ping embo-cv.local
ssh username@embo-cv.local
```

The Zero 2W's quad-core A53 is noticeably slower to boot than a Pi 5 — give it
an extra minute on first boot before worrying it's stuck.

---

## 3. VS Code Remote-SSH

Identical to `../RPI_SETUP_GUIDE.md` §4 — install the Remote-SSH extension,
connect to `username@embo-cv.local`, optionally switch to key auth. No
Zero-specific differences here.

One thing worth knowing going in: VS Code's remote server process itself uses
a non-trivial slice of that 512MB RAM. If things feel sluggish, prefer doing
heavier one-off Python work (e.g. batch-testing detection.py once it exists)
via a plain SSH terminal rather than through VS Code's remote extensions, and
close file-heavy language-server features you don't need for Python editing.

---

## 4. Get the repo onto the Pi

```bash
git clone <your-repo-url> ~/EMBO
cd ~/EMBO/software/cv_verify
```

Same private-repo credential setup as the Pi 5 guide if needed.

---

## 5. Python environment

```bash
cd ~/EMBO/software/cv_verify
python3 -m venv .venv
source .venv/bin/activate
pip install -r ../requirements.txt   # shared with cv-pipeline for now — pyserial, opencv-python, numpy
```

Same "externally-managed-environment" note as the Pi 5 guide applies — always
activate the venv first.

---

## 6. Camera bring-up (OV9281-110)

Different from the Pi 5 guide's UVC (`/dev/video0`) instructions — this is a
CSI camera, so it goes through `libcamera`, not `v4l2` generic UVC drivers.

```bash
libcamera-hello --list-cameras
```

should list the OV9281 sensor. If it doesn't show up:

- [ ] Confirm the Zero-format adapter cable is seated correctly at **both** ends
  (camera module and Pi board) — this is the single most common CSI "camera not
  detected" cause, more often than a software/driver issue.
- [ ] Check `/boot/firmware/config.txt` has camera auto-detection enabled
  (`camera_auto_detect=1`, on by default on recent Raspberry Pi OS) — the OV9281
  isn't one of the "official" Pi camera modules, so on older OS images you may
  need an explicit `dtoverlay=ov9281` line instead of relying on auto-detect.
  Check `dmesg | grep -i ov9281` after boot to see whether the kernel found the
  sensor at all — that tells you whether this is a config.txt/driver problem or
  a physical connection problem.
- [ ] Confirm you're running a 64-bit OS with a recent `libcamera`/`rpicam-apps`
  package — `sudo apt update && sudo apt full-upgrade` if the image is old.

Once detected, capture a test frame to confirm end-to-end:

```bash
libcamera-still -o test.jpg --timeout 2000
```

Copy `test.jpg` back to Windows (VS Code's remote file explorer, or `scp`) and
eyeball it — this is your Layer-1 optical sanity check before writing any
Python capture code.

---

## 7. UART bring-up (for the ESP32 link)

This is the step where Zero 2W is actually **simpler** than the Pi 5 your
friend was fighting with (RP1-chip UART-mapping uncertainty, `SOFTWARE_TODO.md`
task 2) — the Zero 2W's UART behaves like the well-documented Pi 3/4 story.

The catch on *any* Pi with onboard WiFi/Bluetooth (Zero 2W included): the good
PL011 UART is wired to the Bluetooth chip by default, and GPIO14/15 only get
the second-rate mini-UART (`ttyS0`), whose baud clock is tied to the CPU
frequency — not stable enough for our 921600 baud link. Fix it:

```bash
sudo raspi-config
```
Interface Options → Serial Port → **No** to "login shell over serial", **Yes**
to "enable serial port hardware". Then, edit `/boot/firmware/config.txt` and add:

```
dtoverlay=disable-bt
```

```bash
sudo systemctl disable hciuart
sudo reboot
```

After reboot, confirm:

```bash
ls -l /dev/serial0    # should exist, symlinked to /dev/ttyAMA0
```

`/dev/ttyAMA0` is now the real PL011 UART on GPIO14/15 — matches
`cv_verify`'s planned `UART_PORT` and needs no per-Pi-model uncertainty caveat
the way the Pi 5 did.

**Wiring reminder** (from the earlier JST discussion): ESP32 GPIO47 (TX) →
Pi GPIO14 pin 8 is wrong — cross them. ESP32 TX → **Pi RXD (GPIO15, pin 10)**,
ESP32 RX → **Pi TXD (GPIO14, pin 8)**, GND → GND. Both sides 3.3V, no level
shifter.

---

## 8. Permissions

```bash
sudo usermod -aG video,dialout $USER
```
Log out and back in (or reboot) for this to take effect — needed before Python
can open the camera or `/dev/serial0` without `sudo`.

---

## Verification gate for this layer

Before writing any `cv_verify` Python code, confirm all of these independently:

- [ ] `libcamera-hello --list-cameras` shows the OV9281
- [ ] `libcamera-still` produces a viewable, correctly exposed test image
- [ ] `ls -l /dev/serial0` resolves to `/dev/ttyAMA0`
- [ ] From the Pi, `echo "SIZE 250 30" > /dev/serial0` (or a short pyserial
  script) results in the ESP32's BLE log showing `RPi: median=250 iqr=30 um`
  (`firmware/esp32/src/rpi_uart.cpp:36`) — this is the same integration gate
  called out in `TODO.md` Layer 1, and it's worth doing manually here before
  any Python capture/detection code exists, so a UART wiring problem and a
  software bug are never debugged at the same time.

---

## Common gotchas

- **`libcamera-hello` says "no cameras available"** → almost always the ribbon
  cable, not software. Reseat both ends, check the blue/silky side faces the
  right direction (varies by connector, check the cable's own orientation
  markings).
- **`/dev/serial0` missing after the raspi-config step** → you likely skipped
  the `dtoverlay=disable-bt` line or didn't reboot after adding it — WiFi/BT
  settings changes don't take effect until reboot.
- **UART sends nothing / firmware never logs a packet** → double check the
  TX/RX cross-over (a very common wiring mistake is connecting TX→TX,
  RX→RX instead of crossed) and that both ends actually share a GND.
- **Pi feels slow generally** → expected on 512MB RAM; avoid running VS Code
  Remote-SSH and a heavy Python process simultaneously if things stall, prefer
  a plain SSH terminal for running scripts.

---

## Where to go next

Once every box in this file's verification gate is checked, move to
`TODO.md` Layer 1 (request/reply UART link) — that's where `link.py` gets
written.
