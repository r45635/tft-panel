// Music library browsing through the companion (boards with HAS_LIBRARY). Net task only.
//
// Contract (companion src/api/roonRoutes.js): POST /api/roon/browse and /browse/load only, always
// with the panel's own multi_session_key, so it never moves the desktop player's browse stack;
// never GET /search or POST /play-item (they use Roon's shared default session). Every step on an
// item carries the followed zone (zone_or_output_id), so actions play on the panel's zone.
#pragma once

#include "net.h"

namespace net::lib {

void run(LibOp op, int index, const String &text);   // a queued UI command
void tick();                                          // loads the next thumbnail while open

}  // namespace net::lib
