#pragma once

#include <stdint.h>
#include "ui_screen.h"

// Settings > Motion > Jog Motor 1 / Jog Motor 2. Drives one motor
// independently of the mixing scheduler/homing — deliberately NOT gated on
// motors_is_homed(), same as the BLE console's own MOVE command, since this
// exists specifically to move a motor when homing isn't trusted/working
// yet. Still gated on !scheduler_is_running() — a real mix must never be
// interrupted by a bench jog. One instance per motor, set via setMotor()
// once at boot (see ui.cpp) — this screen doesn't know which motor it
// drives until then.
//
// Moves CONTINUOUSLY at a fixed speed (MOTOR_JOG_HZ, config.h) once
// started — turning the knob picks a direction and the motor keeps running
// until explicitly stopped (short press) or the screen is exited (hold /
// BTN1), not a timed burst.
class JogMotorScreen : public Screen {
public:
    void setMotor(uint8_t motor) { _motor = motor; }

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    uint8_t _motor   = 1;
    bool    _active  = false;
    bool    _lastFwd = true;

    void _draw(bool forceFull);
    void _startContinuous(bool forward);
    void _stop();
};
