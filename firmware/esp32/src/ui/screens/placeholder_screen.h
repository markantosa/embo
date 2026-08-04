#pragma once

#include "ui_screen.h"

// Reusable screen for anything that's just "a message, maybe a touch Back
// button, maybe a confirm action" — the insert-syringe prompt, the
// camera-mount reminder, and the "feature developing" stubs (Presets,
// Camera feature) are all just different configure()/setConfirmTarget()
// calls on this one class. Add a new one the same way before reaching for
// a bespoke Screen subclass.
class PlaceholderScreen : public Screen {
public:
    // title may be "" to skip the title line. showBack draws a touch "Back"
    // button that calls mgr.pop().
    void configure(const char *title, const char *message, bool showBack);

    // If set, a knob short-press navigates to `target` (goTo if viaPush is
    // false, push if true) instead of doing nothing. Leave unset (nullptr)
    // for a screen with no forward action (e.g. a stub with only Back).
    void setConfirmTarget(Screen *target, bool viaPush);

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    const char *_title   = "";
    const char *_message = "";
    bool _showBack        = true;
    Screen *_confirmTarget = nullptr;
    bool _confirmPushes    = false;

    void _draw();
};
