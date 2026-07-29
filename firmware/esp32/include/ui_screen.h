#pragma once

// Base class for every UI screen (set-target, running, a settings menu,
// whatever comes next). To add a new screen to the UI:
//   1. Subclass Screen, implement update().
//   2. Give it a static instance and wire navigation to it from whichever
//      screen(s) should be able to reach it (see ui.cpp).
// No other file needs to change — ScreenManager and every existing screen
// are agnostic to how many screens exist or what they show.

class ScreenManager;

class Screen {
public:
    virtual ~Screen() = default;

    // Called once, the moment this screen becomes the active screen via
    // ScreenManager::goTo()/push(). Reset any per-visit state here (e.g.
    // clear a result string left over from the previous visit).
    virtual void onEnter() {}

    // Called every ui_update() while this screen is active. Read input via
    // the ui_input_* functions, draw via the ui_display_* helpers, and
    // navigate by calling mgr.goTo()/push()/pop() — screens never reach
    // into each other directly.
    //
    // forceFull is true on the first update() after onEnter() (or after a
    // screen above this one on the stack was popped) — do a full redraw
    // then. Otherwise, only redraw the parts that actually changed, to
    // avoid TFT flicker.
    virtual void update(ScreenManager &mgr, bool forceFull) = 0;
};
