#include "sound_settings.h"

static bool _soundEnabled = true;

bool sound_is_enabled() { return _soundEnabled; }
void sound_set_enabled(bool enabled) { _soundEnabled = enabled; }
