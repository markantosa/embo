#pragma once

#include "ui_screen.h"

// Persistent hardware-fault screen (e.g. homing failed at boot). There is
// no way back short of a reboot — ui.cpp routes here via goTo() and no
// screen ever navigates away from it.
class ErrorScreen : public Screen {
public:
    void setMessage(const char *msg);
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    char _msg[48] = "";
};
