// Local firmware upload page on the panel, for test builds and offline updates:
// http://tft-panel-xxxx.local/update (Wi-Fi station mode only, not during the setup portal).
//
// Opening /update makes the panel show a 6-digit code for 5 minutes; the upload must carry it
// (?code=), so only someone in front of the panel can flash it. A wrong or expired code is
// refused before anything is written. The new image boots "pending verify" like a GitHub
// update (rollback if it never gets healthy). Run by the net task.
#pragma once

#include <Arduino.h>

namespace webupdate {

void loop(bool wifiUp, bool portalActive);   // starts/stops the server and serves requests

// The code to show on screen, 0 when none is active.
uint32_t activeCode();
void cancelCode();   // the user dismissed it on the panel

}  // namespace webupdate
