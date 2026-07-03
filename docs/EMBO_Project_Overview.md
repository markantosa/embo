# EMBO — Embolization Particle Sizing System

## Project Overview

**Last updated:** June 2026
**Status:** Active development, built by a small student/hobbyist engineering team spanning mechanical and electrical/software disciplines.

---

## What we are building

We are building a device called EMBO. It is an intelligent machine that helps doctors prepare medicine more consistently and safely before a type of procedure called embolization.

The device automates a preparation step that is currently done entirely by hand, with no quality checks, and produces different results depending on who does it and how experienced they are. Our goal is to make that step reliable, repeatable, and objective — regardless of who operates the device.

---

## What is embolization?

Embolization is a minimally invasive procedure used in interventional radiology. A doctor threads a thin catheter (tube) through a blood vessel and injects tiny particles to deliberately block blood flow to a targeted area. This is used to treat conditions like:

- Uterine fibroids (non-cancerous growths in the womb)
- Bleeding in the gut or after trauma
- Tumours — cutting off blood supply starves them
- Enlarged prostate (prostate artery embolization)

The particles used to block the vessels are called embolic agents. They are often made from gelatin foam — the same general material as gelatin desserts, but in medical grade sponge form. Common brand names include Gelfoam and Lyostypt.

---

## The problem we are solving

Before the procedure, a doctor or technician prepares the embolic agent by hand. The current process looks like this:

1. Cut a piece of gelatin foam into small chunks by hand
2. Place the chunks in a syringe with saline solution
3. Connect two syringes via a stopcock (valve)
4. Push the mixture back and forth between the syringes repeatedly until it reaches a "pudding consistency"
5. Inject into the patient

The problem is step 4. There is no measurement, no standard, and no objective quality check. Two doctors doing this side by side will produce slurries with completely different particle sizes. Particle size matters enormously — studies show that the wrong particle size can cause tissue death in the wrong place, or fail to block the target area effectively.

Nobody has ever built a device to measure and control particle size during this preparation step. We are building the first.

---

## How our device works

EMBO has three main jobs:

### 1. Automate the mixing

Two stepper motors drive the syringe plungers back and forth automatically, replacing the manual hand-pumping. The speed, stroke length, and number of strokes are all controlled precisely.

### 2. Measure the particle size

While mixing is happening, sensors measure the size of the particles forming in the slurry. We use two methods:

**a) Camera + Computer Vision**
A small camera looks through the syringe. AI software analyses each image and measures every individual particle, calculating statistics like the median size and spread (IQR — interquartile range).

**b) Ultrasound**
A pair of sensors clamp onto the outside of the syringe and fire sound waves through it. Larger particles absorb more sound energy. By measuring how much energy gets through, we can track whether particles are getting smaller as mixing continues.

### 3. Stop automatically at the right point

The device feeds the measurements into a control algorithm (PID loop) that decides when the particle size is in the correct range. The target size is adjustable per procedure (50–1000 micrometres, roughly 1 to 14 times the width of a human hair), since the ideal size depends on which blood vessels are being treated — see [The clinical context](#the-clinical-context) below. The doctor sets it on the touchscreen before starting; a typical default is around 300µm. When the measured size reaches the target, the device stops and alerts the doctor.

---

## The hardware — what the device is made of

### Mechanical

- A metal frame the size of a large iPad, designed to sit on a trolley in an interventional radiology suite
- 3D printed components for holding and guiding the syringes
- SLA (resin) printed outer cover
- Two stepper motors with lead screws that push and pull the syringe plungers
- Mechanical limit switches that stop the motors if they travel too far

### Electrical — two custom circuit boards

- **Main board (4-layer PCB)** — carries the ESP32-S3 microcontroller (the brain of the device), TMC2209 motor driver chips, the custom ultrasound signal chain (AD9833 signal generator, OPA2354 amplifiers, BAT54 envelope detector), and power regulation from a standard 24V supply
- **Display breakout board (2-layer PCB)** — a smaller board carrying the touchscreen, rotary encoder, buttons, and buzzer, connected to the main board via ribbon cable so the UI can sit wherever is ergonomic on the enclosure independent of the main board's placement

### Sensing

- Raspberry Pi 5 — a small single-board computer that runs the camera and AI software
- An off-the-shelf USB 2.0 microscope camera plugged into the Raspberry Pi
- Diffused LED panel for backlighting the syringe so particles show up clearly in camera images
- Two 1MHz ultrasound transducers (like miniature speakers/microphones for sound waves) that clamp onto the syringe

### Software

- Computer vision pipeline on the Raspberry Pi using OpenCV and a YOLOv8 AI model trained specifically on images of gelatin foam particles
- PID control loop on the ESP32-S3 that uses the sensor readings to decide how many more mixing strokes to do
- Bluetooth on the ESP32-S3 for wireless debugging

---

## The team structure

The project is divided into two sub-teams:

**Mechanical team**
Designs and builds the physical device — the frame, the motor mounts, the syringe holders, the enclosure, and all 3D printed parts. They decide how everything fits together physically.

**Electrical & software team**
Designs and builds both custom circuit boards (schematic, PCB layout, component sourcing, board assembly, and testing) and writes all the code that runs on them — the ESP32-S3 firmware, the computer vision pipeline on the Raspberry Pi, and the touchscreen user interface. Keeping hardware and firmware under one team keeps pinout and peripheral decisions tightly coupled to the code that has to use them.

Both teams have regular joint sessions because many decisions affect more than one team — for example, the motor choice affects both the mechanical mounting and the electrical driver circuit.

---

## The timeline

The project runs on a 13-week engineering timeline:

| Week | Milestone |
|---|---|
| 5 | System Requirements Review |
| 7 | Recess week (used for building, not lectures) |
| 9 | System Design Review (midpoint milestone) |
| 13 | Final Exhibition (public demonstration) |

The custom circuit boards were the biggest early bottleneck: the main board schematic had to be finished by end of Week 7 so it could be sent to the PCB manufacturer and arrive back in time for testing before the System Design Review.

---

## Key technical terms you will hear

**PID loop**
A standard control algorithm used in engineering. It stands for Proportional-Integral-Derivative. Ours uses the measured particle size to decide how many more mixing strokes to do. Think of it like a thermostat — the temperature is the particle size, and the heating element is the motor.

**IQR (Interquartile Range)**
A statistical measure of spread. Instead of just knowing the average particle size, IQR tells us how consistent the sizes are. A small IQR means most particles are close to the target size. A large IQR means there are lots of very small and very large particles mixed in.

**Micrometres (µm)**
One micrometre is one millionth of a metre — about 1/70th the width of a human hair. Our adjustable target range of 50–1000µm spans from barely visible dust to specks clearly visible to the naked eye.

**UVC (USB Video Class)**
The standard protocol most USB webcams and microscope cameras use, letting the Raspberry Pi read frames without a manufacturer-specific driver. Our camera connects this way over USB 2.0, rather than the ribbon-cable CSI connection used by dedicated Raspberry Pi camera modules.

**GPIO**
General Purpose Input/Output. These are the programmable pins on a microcontroller that connect to external components. Our ESP32-S3 module breaks out 36 GPIOs; 28 are assigned in this design, leaving 8 spare.

**SPI**
A standard way for chips to communicate with each other over four wires. Our touchscreen, signal generator, and amplifiers all use SPI to talk to the main microcontroller.

**UART**
Another communication standard, simpler than SPI. The Raspberry Pi talks to the ESP32-S3 over UART, and the motor drivers also use UART to report motor load data.

**StallGuard**
A clever feature built into the TMC2209 motor driver chip. It measures how hard the motor is working by reading the back-EMF (electrical signal generated by the motor coil). When the syringe is full of thick, unmixed slurry, the motor works harder — StallGuard detects this and we use it as a proxy for slurry viscosity (thickness).

**Back-EMF**
When a motor spins, it generates its own small voltage (electromotive force) in the opposite direction. By measuring this, the motor driver can estimate how much resistance the motor is experiencing — without any additional sensors.

**Envelope detector**
A circuit that extracts the amplitude (volume) of a high-frequency signal. We use one to convert the 1MHz ultrasound signal into a simple DC voltage that the microcontroller can read. Think of it like a volume meter.

---

## The clinical context

Why does particle size matter so much?

Published research shows that:

- In liver embolization (animal study), particles of 200–500µm caused 36% tissue death in surrounding liver — far more damage than larger particles
- In uterine fibroid treatment, smaller 200µm particles produced better outcomes than 500µm in human patients
- In adenomyosis (a similar condition), particle size made no significant difference to patient outcomes

These apparently contradictory results mean there is no single universal target. Particle size matters — but how much it matters depends on which blood vessels are being treated and what the patient has. Our device allows the right size to be set per procedure.

Currently, nobody in the world has a device that measures or controls particle size during gelatin foam preparation. This is a genuine gap. Gelfoam (made by Pfizer) has been used for embolization since the 1970s and is the most common temporary embolic agent globally, yet the preparation method has not changed since then.

---

## Skills and technologies applied

EMBO was built end-to-end by a small team across two disciplines. A rough breakdown of what was actually done, technology by technology:

### Mechanical

- CAD design (frame, syringe holders, enclosure) in SolidWorks/Fusion 360
- FDM 3D printing for functional/structural parts, SLA (resin) printing for the outer cover
- Mechanical assembly, fabrication, and iteration against real fit/clearance constraints (motor + driver module stack height, enclosure clearance, etc.)

### Electrical & software

- Full custom PCB design in KiCad across two boards: a 4-layer main board and a 2-layer display breakout board — schematic capture, layout, component sourcing, DRC, and manufacturing hand-off to JLCPCB
- Analog signal chain design (DDS-driven ultrasound transmit/receive path, op-amp gain stages, envelope detection) alongside digital/power electronics (buck + LDO regulation, reverse-polarity and overcurrent protection)
- SMD hand assembly and hardware bring-up/verification
- Embedded C++/Arduino firmware on the ESP32-S3: motor control, closed-loop PID, sensor fusion across UART, SPI, and ADC peripherals, and a touchscreen UI
- Python computer vision pipeline on a Raspberry Pi 5, using OpenCV and a YOLOv8 model trained on gelatin foam particle imagery
- Wireless (BLE) debug tooling for field diagnostics without a wired connection

### Cross-discipline

- Coordinating hardware and firmware decisions that span team boundaries — e.g. motor selection affecting both mechanical mounting and electrical driver choice, or PCB pinout changes requiring firmware updates
- Working against fixed external milestones (see [The timeline](#the-timeline)) with a physical, working device as the deliverable rather than a slide deck

---

## Further reading

If you want to go deeper into any aspect of the project:

**Clinical background**
Yamagami et al., "The Size of Gelatin Sponge Particles," CardioVascular and Interventional Radiology, 2006 — the key paper showing how preparation method affects particle size distribution

**Particle size and outcomes**
- Yamamoto et al., CVIR 1997 — liver necrosis vs particle size
- PMC8670118 — hemorrhoidal embolization outcomes

**Control model**
The mixing process follows first-order breakage kinetics. Our particle size model is:

```
D(N) = D_min + (D_0 - D_min) × e^(-kN)
```

Where N is stroke count and k is a shear constant. This was derived from pharmaceutical milling literature and adapted to our discrete stroke-based process.
