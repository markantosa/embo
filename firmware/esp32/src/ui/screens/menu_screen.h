#pragma once

#include <stdint.h>
#include "ui_screen.h"

// Generic, data-driven list menu. To add a new menu anywhere in the UI:
//
//   void myAction(ScreenManager &mgr) { ...; mgr.pop(); }
//   static const MenuItem kMyItems[] = {
//       { "Do the thing", myAction },
//   };
//   static MenuScreen myMenu("My Menu", kMyItems, 1);
//
// then mgr.push(&myMenu) from wherever it should open. No new Screen
// subclass needed for a simple list — see bench_diagnostics_menu.cpp for a worked
// example wired to real backend calls. Don't add a "Back" item — BTN1 is
// already wired as Back for every MenuScreen (see update() below), so an
// explicit row for it is redundant.
//
// An item can optionally be conditionally greyed out/unselectable — add a
// third field, a bool()-returning function: { "Label", myAction, myGate }.
// See ui.cpp's kTargetTypeItems for a real example (Size/Viscosity gated
// on which agent was selected earlier). Leaving it off (the normal case)
// means always-enabled, unchanged from before this existed.

class ScreenManager;

struct MenuItem {
    const char *label;
    void (*onSelect)(ScreenManager &mgr);  // called when this item is confirmed
    // Optional — nullptr (the default for every existing menu item
    // throughout this codebase, via ordinary aggregate init leaving it
    // unset) means "always enabled." When set, an item this returns
    // false for is drawn greyed out and refuses to confirm — see
    // MenuScreen::update()/_draw(). The item still exists in the list and
    // can still be navigated to with the encoder, it just can't be
    // selected.
    bool (*isEnabled)() = nullptr;
};

class MenuScreen : public Screen {
public:
    // subtitle is optional (nullptr = none) — small text drawn just under
    // the title, e.g. Settings uses it for the firmware version. Every
    // other menu leaves this at the default and shows nothing extra.
    MenuScreen(const char *title, const MenuItem *items, uint8_t count, const char *subtitle = nullptr);

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    const char *_title;
    const MenuItem *_items;
    uint8_t _count;
    const char *_subtitle;
    uint8_t _selected = 0;

    void _draw(bool forceFull);
};
