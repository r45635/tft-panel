// A line of headlines sliding under the idle clock, from any RSS or Atom feed.
//
// Feeds are XML, and a parser for it would cost more than this is worth, so the titles are
// picked out by scanning: everything before the first item is the feed's own name and is
// skipped, then each title is taken, un-escaped and joined into one line. A feed that changes
// shape shows fewer headlines; it never shows markup.
#pragma once

#include <Arduino.h>

namespace net::news {

void tick(uint32_t now);   // net task: refreshes on its own schedule
void reset();              // the feed changed: fetch it again now

}  // namespace net::news
