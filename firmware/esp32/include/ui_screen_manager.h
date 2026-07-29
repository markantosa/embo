#pragma once

#include <stdint.h>
#include "ui_screen.h"

// Drives whichever Screen is currently active and lets screens navigate
// without knowing about each other. Two ways to move between screens:
//
//   goTo(next)  — replace the whole stack with `next`. Use this for moves
//                 with no "back" (e.g. set-target -> running: once a run
//                 starts there's nothing to return to).
//   push(next)  — remember the current screen and switch to `next`; a
//                 later pop() returns to exactly where you were, with its
//                 state untouched. Use this for anything that's
//                 conceptually "a dialog on top of" the current screen
//                 (e.g. the camera-verify screen, or a settings menu).
//
// A screen that was reached via push() doesn't need to know who pushed it
// — VerifyingScreen, for example, just calls pop() when it's done,
// whether it was opened from the set-target screen or the done screen.
class ScreenManager {
public:
    void begin(Screen *initial);
    void update();  // call every ui_update()

    void goTo(Screen *next);
    void push(Screen *next);
    void pop();  // no-op if there's nothing to pop back to

private:
    static constexpr uint8_t MAX_STACK = 4;  // plenty for foreseeable nesting (screen -> menu -> submenu)
    Screen  *_stack[MAX_STACK] = {};
    uint8_t  _depth            = 0;
    bool     _forceFull        = true;  // true for the update() right after any nav change
};
