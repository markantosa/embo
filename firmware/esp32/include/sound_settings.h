#pragma once

// Device-wide sound on/off setting (Settings > Sound). Session-only — not
// persisted to flash, so it resets to enabled on every power cycle. The
// only current sound is the boot chirp (ui.cpp), which plays before this
// screen is ever reachable, so toggling this has no observable effect on
// the current session — it's here for when future UI feedback tones are
// added, and for the setting to exist/display correctly per the menu
// design either way.
bool sound_is_enabled();
void sound_set_enabled(bool enabled);
