#pragma once

#include "ui_screen.h"

// Settings > Sound. Mirrors uas_debug_toggle_screen.h's structure exactly.
class SoundToggleScreen : public Screen {
public:
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    void _draw();
};
