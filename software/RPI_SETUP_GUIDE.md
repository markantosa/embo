# Raspberry Pi Setup & Development Guide (EMBO CV Pipeline)

A from-zero guide to getting a Raspberry Pi 5 running the EMBO CV pipeline, developed entirely from VS Code on Windows over SSH — no separate monitor/keyboard for the Pi, no copying files back and forth by hand.

If you've only done ESP32 development before, the mental model is different: the ESP32 has no OS, and "flashing" writes your compiled program directly into it. The Pi is a full Linux computer — you flash an *operating system* onto its SD card once, then develop on it like any other Linux machine over the network.

---

## What you need

- Raspberry Pi 5 + official power supply (5V/5A USB-C — an ordinary phone charger is usually not enough)
- microSD card (32GB+, A2-rated recommended) + a USB card reader for your Windows PC
- The USB 2.0 microscope camera (see `README.md`)
- A network your Pi and your Windows PC are both on (WiFi is fine)
- Windows PC with VS Code already installed

---

## 1. Flash the OS

"Flashing" here means writing a full Linux OS image onto the SD card — a one-time step, not something you repeat when your code changes.

1. Download **Raspberry Pi Imager** from raspberrypi.com on your Windows PC.
2. Insert the microSD card into your card reader.
3. Open Imager:
   - **Device:** Raspberry Pi 5
   - **OS:** Raspberry Pi OS (64-bit) — the standard Debian-based option is fine, no need for Lite unless you want to save space (Lite has no desktop GUI, which is fine since we're doing everything over SSH anyway)
   - **Storage:** your SD card
4. Click the **gear icon (⚙) / "Edit Settings"** before writing — this is the important part. It lets you pre-configure the Pi so it's usable immediately on first boot, with no monitor ever needed:
   - Set a **hostname** (e.g. `embo-pi` — you'll use this to find it on the network)
   - Set a **username and password**
   - Configure **WiFi** (SSID + password)
   - **Enable SSH** — tick "Enable SSH", use password authentication for now (you can switch to SSH keys later, see §4)
5. Write. This takes a few minutes and verifies the write afterward.

---

## 2. First boot

1. Insert the microSD card into the Pi, connect power.
2. First boot takes a minute or two longer than normal (resizing the filesystem, applying your Imager settings).
3. From a Windows PowerShell window, check the Pi is reachable:
   ```powershell
   ping embo-pi.local
   ```
   Replace `embo-pi` with whatever hostname you set. If this doesn't resolve, find its IP address from your router's connected-devices list instead.

---

## 3. Connect over SSH (quick sanity check)

Before bringing in VS Code, confirm plain SSH works from PowerShell:

```powershell
ssh username@embo-pi.local
```

Accept the host key prompt (first connection only), enter your password. You should land at a Linux shell prompt on the Pi. Type `exit` to disconnect. If this works, VS Code Remote-SSH will too — it uses the same connection underneath.

---

## 4. Set up VS Code Remote-SSH

This is what lets you keep working entirely inside VS Code on Windows, with the Pi's filesystem and terminal available directly in the editor.

1. In VS Code, install the **Remote - SSH** extension (publisher: Microsoft, extension ID `ms-vscode-remote.remote-ssh`).
2. Press `Ctrl+Shift+P` → **Remote-SSH: Connect to Host...** → **Add New SSH Host** → enter `username@embo-pi.local`.
3. Connect. VS Code opens a new window connected to the Pi — the bottom-left corner will show the hostname, confirming you're remote.
4. **Optional but recommended — switch to SSH key auth** so you're not typing a password every time:
   ```powershell
   ssh-keygen -t ed25519            # on Windows, if you don't already have a key
   ssh-copy-id username@embo-pi.local   # or manually append your .pub key to ~/.ssh/authorized_keys on the Pi
   ```
   After this, `ssh embo-pi.local` and VS Code Remote-SSH both connect without a password prompt.

From here on, **everything below happens inside that VS Code remote window** — open a terminal with `` Ctrl+` `` and it's a real shell running on the Pi, not Windows.

---

## 5. Get the EMBO repo onto the Pi

The Pi needs its own clone of the repo — your Windows checkout and the Pi's checkout are two separate copies that you keep in sync via git, the same as you would between any two machines.

```bash
git clone <your-repo-url> ~/EMBO
cd ~/EMBO/software/cv-pipeline
```

If the repo is private, you'll need to set up a GitHub credential on the Pi too (a personal access token, or an SSH key added to your GitHub account — same idea as §4's key setup, just for GitHub instead of SSH login).

---

## 6. Python environment

Raspberry Pi OS (Debian Bookworm and later) blocks `pip install` directly into the system Python — this is intentional (Debian calls it an "externally managed environment") and you'll hit an error if you try. Use a virtual environment instead:

```bash
cd ~/EMBO/software/cv-pipeline
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

`source .venv/bin/activate` needs to be run again every time you open a new terminal (VS Code will usually offer to do this automatically once it detects the `.venv` folder — accept it, or select the interpreter manually via `Ctrl+Shift+P` → **Python: Select Interpreter**).

---

## 7. Hardware bring-up checks

Do these before trying to run `main.py` against real hardware — they isolate "is the hardware even visible to Linux" from "does the pipeline code work."

**Camera:**
```bash
sudo apt install v4l-utils   # one-time, provides v4l2-ctl
ls /dev/video*               # the USB microscope camera should show up as /dev/video0 (or similar)
v4l2-ctl --list-formats-ext  # lists actual supported resolutions/fps — check against config.py
```

**UART (for the ESP32 link):** the Pi's primary UART is disabled by default and its pins are normally used for a serial console login, which conflicts with our use as a data link. Free it up:
```bash
sudo raspi-config
```
Navigate to **Interface Options → Serial Port** → answer **No** to "login shell over serial", **Yes** to "enable serial port hardware". Reboot after. Then confirm the device exists:
```bash
ls -l /dev/serial0   # should exist, usually a symlink to /dev/ttyAMA0 (Pi 5 uses the RP1 chip — mapping may differ from older Pis, confirm this actually matches config.py's UART_PORT)
```

**Permissions:** your user needs to be in the right groups to access the camera and serial port without `sudo`:
```bash
sudo usermod -aG video,dialout $USER
```
Log out and back in (or reboot) for group changes to take effect.

---

## 8. Run it

```bash
cd ~/EMBO/software/cv-pipeline
source .venv/bin/activate
python main.py
```

Right now (per `SOFTWARE_TODO.md`), this exercises camera capture + the UART link to the ESP32 — you should see the firmware's BLE log show `"RPi: median=X iqr=Y um"` disappear and instead get a `"CV: detection/sizing not implemented yet"` status line, since `detection.py`/`sizing.py` are still stubs. That's expected — it's proof Layer 1 (camera + UART) works, which is the current milestone.

Stop with `Ctrl+C`.

---

## Common gotchas

- **`pip install` fails with "externally-managed-environment"** → you forgot to activate the venv (§6). Always `source .venv/bin/activate` first.
- **`/dev/video0` doesn't appear** → try a different USB port (some Pi 5 USB3 ports draw more power than others; a cheap UVC camera occasionally needs a powered hub). Check `dmesg | tail` after plugging in for kernel-level errors.
- **`/dev/serial0` permission denied** → you're not in the `dialout` group yet, or haven't logged out/in since adding yourself (§7).
- **VS Code Remote-SSH "could not establish connection"** → usually the Pi's hostname isn't resolving. Try connecting with the raw IP address instead of `embo-pi.local` as a fallback.
- **Pi feels slow over WiFi for file operations** → normal for the first index/extension-install after connecting; subsequent sessions are much faster since VS Code caches its server on the Pi.

---

## Where to go next

Once hardware bring-up (this guide) is done, actual pipeline development tasks — camera calibration, dye/lighting trials, the segmentation model, sizing math — are tracked in [`../SOFTWARE_TODO.md`](../SOFTWARE_TODO.md), not here. This guide only covers "how do I get onto the Pi and run code," not "what code to write."
