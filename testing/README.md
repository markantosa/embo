# Files used for Experimental Testing

# syringe_force_baseline
Firmware for testing baseline syringe force. Based on ESP32 Platform, uses a loadcell and HX711 loadcell amplifier. 
(still a work in progress)

# 3JUL_preliminary_microscope_testing
Write-up of the first hands-on microscope + backlighting test on Lyostypt and dental hemostatic foam. See [`3JUL_preliminary_microscope_testing/2026-07-03_preliminary_microscope_testing.md`](3JUL_preliminary_microscope_testing/2026-07-03_preliminary_microscope_testing.md) — this test is the origin of `docs/EMBO_UAS_CV_Technical_Advisory.txt`.

# 3JUL_stopcock_aperture_testing
Follow-up test moving the camera to the stopcock aperture (per the advisory's Problem B fix). Confirms overlap is avoided, but continuous lighting (smartphone backlight and the camera's built-in LED, both tested) causes significant motion blur — see [`3JUL_stopcock_aperture_testing/2026-07-03_stopcock_aperture_testing.md`](3JUL_stopcock_aperture_testing/2026-07-03_stopcock_aperture_testing.md). Confirms strobed illumination is now required, not optional, for `software/SOFTWARE_TODO.md` task 12.