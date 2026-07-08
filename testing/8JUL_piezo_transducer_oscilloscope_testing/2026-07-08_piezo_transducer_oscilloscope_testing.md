# Piezo Electric Transducers Test with Oscilloscope — 8 July 2026

Preliminary test of signal transmission and attenuation between two piezoelectric transducers, sandwiching a finger between them, using a signal generator and oscilloscope.

## Goal

Testing attenuation and transmission of signal between transducers with an oscilloscope.

## Setup

- Transducer electrodes soldered to short multicore wires
- One transducer (TX) connected to the signal generator; the other (RX) connected to Channel 1 of the oscilloscope
- Both transducers positioned sandwiching a finger between them

| | |
|:---:|:---:|
| ![Oscilloscope lines connection to transducers](../../assets/Piezo%20Prelim%20Test__Osciloscope%20Lines%20Connection%20to%20Transducers.jpg) | ![Finger between TX and RX transducers](../../assets/Piezo%20Prelim%20Test_finger%20between%20TX%20RX.jpg) |
| *Oscilloscope lines connection to transducers* | *Finger positioned between TX and RX* |

## Results

Upon turning on the wave generator, a clear 1MHz signal is received through the finger with a readable attenuation.

| | |
|:---:|:---:|
| ![Input signal, 1MHz Pk-Pk 4.76V](../../assets/Piezo%20Prelim%20Test_Input%20SIgnal%20(1MHz%20Pk-Pk%204.76V).jpg) | ![Received signal, 1MHz Pk-Pk 211mV](../../assets/Piezo%20Prelim%20Test_Received%20SIgnal%20(1MHz%20Pk-Pk%20211mV).jpg) |
| *Input signal: 1MHz, Pk-Pk 4.76V* | *Received signal: 1MHz, Pk-Pk 211mV* |

- Input signal: 1MHz, Pk-Pk 4.76V
- Received signal: 1MHz, Pk-Pk 211mV
- Attenuation: roughly 4.76V → 211mV Pk-Pk across the finger (~23x, ~27dB)

## Observations

- A clear, readable 1MHz signal transmits through a finger sandwiched between the two transducers
- Attenuation is significant but the signal remains clearly readable on the oscilloscope, confirming basic through-tissue transmission is feasible at 1MHz
