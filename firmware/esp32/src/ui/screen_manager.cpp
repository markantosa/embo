#include "ui_screen_manager.h"

void ScreenManager::begin(Screen *initial) {
    _depth = 0;
    _stack[_depth++] = initial;
    _forceFull = true;
    initial->onEnter();
}

void ScreenManager::update() {
    if (_depth == 0) return;
    Screen *current = _stack[_depth - 1];
    bool full = _forceFull;
    _forceFull = false;
    current->update(*this, full);
}

void ScreenManager::goTo(Screen *next) {
    _depth = 0;
    _stack[_depth++] = next;
    _forceFull = true;
    next->onEnter();
}

void ScreenManager::push(Screen *next) {
    if (_depth >= MAX_STACK) return;  // stack exhausted — ignore rather than corrupt it
    _stack[_depth++] = next;
    _forceFull = true;
    next->onEnter();
}

void ScreenManager::pop() {
    if (_depth <= 1) return;  // never pop the root screen
    _depth--;
    _forceFull = true;  // force a redraw of whoever we returned to — its own state is untouched
}
