// The wall clock stands still at the moment the showcase clock shows: Saturday 7 November 2026,
// 21:47 in the firmware's default time zone (src/app sets TZ). Replaces the C library's time()
// for this program, so a picture does not depend on when or where it is taken.
#include <time.h>

time_t time(time_t *out) {
    const time_t showcase = 1794106020;
    if (out) *out = showcase;
    return showcase;
}
