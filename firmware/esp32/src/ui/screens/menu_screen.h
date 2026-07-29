#pragma once

#include <stdint.h>
#include "ui_screen.h"

// Generic, data-driven list menu. To add a new menu anywhere in the UI:
//
//   void myAction(ScreenManager &mgr) { ...; mgr.pop(); }
//   static const MenuItem kMyItems[] = {
//       { "Do the thing", myAction },
//       { "Back",         [](ScreenManager &mgr) { mgr.pop(); } },
//   };
//   static MenuScreen myMenu("My Menu", kMyItems, 2);
//
// then mgr.push(&myMenu) from wherever it should open. No new Screen
// subclass needed for a simple list — see settings_menu.cpp for a worked
// example wired to real backend calls.

class ScreenManager;

struct MenuItem {
    const char *label;
    void (*onSelect)(ScreenManager &mgr);  // called when this item is confirmed
};

class MenuScreen : public Screen {
public:
    MenuScreen(const char *title, const MenuItem *items, uint8_t count);

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    const char *_title;
    const MenuItem *_items;
    uint8_t _count;
    uint8_t _selected = 0;

    void _draw(bool forceFull);
};
