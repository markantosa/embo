OSCILLOSCOPE VS. PCB CROSS-CHECK -- STROKE 40, 26x GAIN
=========================================================================

PURPOSE
-------
Verify that the PCB's ADC reading at stroke 40 is explained by the actual
raw receive signal (measured independently on the oscilloscope) passing
through the board's known RX amplifier gain -- i.e., confirm the two
measurement methods agree once amplifier saturation is accounted for.

Checked at THREE frequency points instead of one, so the conclusion
doesn't rest on a single reading.

METHOD NOTE: the oscilloscope value used below is the raw peak-to-peak
reading straight off channel 3 (max - min of the captured waveform) --
NOT an FFT-extracted amplitude. This matches how the reading was
actually taken on the bench.

DATA SOURCE
-----------
26 gain/cross check with oscilloscope at 40th stroke/ (TX side.csv, RX side.csv)
26 gain/40th stroke/uas_averaged_summary_*.csv

CALCULATION USED AT EVERY POINT
--------------------------------
    RX amplitude          = RX Vpp / 2
    predicted PCB voltage = baseline + gain x RX amplitude

  where baseline = 1.4498 V (off-resonance level, this stroke's sweep)
        gain     = 26x

=========================================================================
POINT 1 -- 1.00 MHz (peak)
=========================================================================
  TX Vpp (raw max-min)        2.0101 V
  RX Vpp (raw max-min)        0.2109 V
  RX amplitude (Vpp / 2)      105.46 mV

  predicted = 1.4498 + 26 x 0.10546 = 4.1919 V
  ADC/amplifier ceiling       ~3.1 V   <-- predicted EXCEEDS this

  Actual PCB reading @ 1.00MHz, stroke 40:        3.1399 V
  Sweep peak (whole curve, stroke 40):            3.1420 V @ 1.02MHz

  RESULT: predicted (4.19V) exceeds the ceiling, so hard clipping is
  expected. Actual reading (3.14V) sits right at the clipped ceiling,
  not at the linear prediction. MATCH (clipping confirmed).

=========================================================================
POINT 2 -- 0.96 MHz (near-peak flank)
=========================================================================
  TX Vpp (raw max-min)        [ fill in from scope ]
  RX Vpp (raw max-min)        [ fill in from scope ]
  RX amplitude (Vpp / 2)      [ = RX Vpp / 2 ]

  predicted = 1.4498 + 26 x [RX amplitude] = [ calculate once filled in ]

  Actual PCB reading @ 0.96MHz, stroke 40:        3.0350 V
  (already close to the ~3.05V clipping-warning threshold at this
  stroke count -- worth checking whether predicted also exceeds ~3.1V)

  RESULT: [ pending oscilloscope RX/TX capture at 0.96MHz ]

=========================================================================
POINT 3 -- 0.89 MHz (safe flank)
=========================================================================
  TX Vpp (raw max-min)        [ fill in from scope ]
  RX Vpp (raw max-min)        [ fill in from scope ]
  RX amplitude (Vpp / 2)      [ = RX Vpp / 2 ]

  predicted = 1.4498 + 26 x [RX amplitude] = [ calculate once filled in ]

  Actual PCB reading @ 0.89MHz, stroke 40:        1.9133 V
  (well clear of the ADC ceiling -- this is the frequency expected to
  still be in the amplifier's clean, linear operating region)

  RESULT: [ pending oscilloscope RX/TX capture at 0.89MHz ]

=========================================================================
HOW TO COMPLETE POINTS 2 AND 3
=========================================================================
For each frequency (0.96MHz and 0.89MHz), set the AD9833 to that
frequency, capture channel 3 (RX side) on the scope, and read the
peak-to-peak voltage directly off the display (or save the waveform
CSV, same as "RX side.csv"). Report just the Vpp number (in mV or V)
for each, and this file can be filled in and the match/mismatch
verdict completed the same way as Point 1.

=========================================================================
OVERALL VERDICT (so far)
=========================================================================
Point 1 (1.00MHz) confirms the measurement chain model: the oscilloscope
independently shows the amplifier is being driven well past what the
3.1V ADC ceiling can represent, and the PCB's actual reading lands
exactly at that ceiling rather than at the (impossible) linear
prediction. This is strong evidence the PCB and oscilloscope are
measuring the same real signal consistently -- the PCB just can't
display values above its ceiling.

Points 2 and 3 will show whether this same match holds at frequencies
where the PCB is NOT yet clipping (0.89MHz) and right at the edge of
clipping (0.96MHz) -- confirming the model isn't just "everything
reads the ceiling," but genuinely tracks the real signal amplitude
below that ceiling too.

IMPLICATION SO FAR: at stroke 40, the 1.0MHz peak remains unusable
under 26x gain, as expected. Flank frequencies (0.89MHz, 0.96MHz)
should be used instead for any stroke count this high -- consistent
with earlier recommendations in this dataset.
