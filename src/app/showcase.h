// Showcase content for screenshots, the user guide and promotion: an invented track, lyrics,
// zones and a generated cover, so no copyrighted lyrics or artwork and nothing personal (zone
// names, setup password) ever ends up in a picture. Fed through the normal UI path.
#pragma once

#include "../net/net.h"

namespace showcase {

// The invented "now playing" state, at `positionMs` (frozen: the UI keeps it still).
net::Snapshot make(int32_t positionMs);

// Same state as the setup portal would show it, with an invented network and password.
void fakePortal(net::Snapshot &s);

}  // namespace showcase
