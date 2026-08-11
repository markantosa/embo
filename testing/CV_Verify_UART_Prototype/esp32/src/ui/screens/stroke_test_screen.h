#pragma once

#include "ui_screen.h"
#include "config.h"

// Settings > Motion > Test Both Motors. Encoder rotation picks how many
// strokes to run (STROKE_TEST_COUNT_MIN..MAX, config.h) before starting —
// short press confirms and runs the test; long press or BTN1 cancels back
// to the Motion menu without running anything. See _runTest() for what a
// "stroke" means here.
class StrokeTestScreen : public Screen {
public:
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    int _strokeCount = STROKE_TEST_COUNT_DEFAULT;

    void _draw(bool forceFull);
    void _runTest(ScreenManager &mgr);
};
