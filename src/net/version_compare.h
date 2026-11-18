// OTA release tag comparison. Header-only and free of Arduino types so it can be unit-tested
// on the host.
#pragma once

#include <stdio.h>
#include <string.h>

namespace ota {

// "v1.2.3[-anything]" → {1,2,3}, dev = has a suffix. false if not a version.
inline bool parseVersion(const char *v, int out[3], bool &dev) {
    int n = sscanf(v, "v%d.%d.%d", &out[0], &out[1], &out[2]);
    dev = strchr(v, '-') != nullptr;
    return n == 3;
}

// Newer release than what runs? A dev build (suffix) counts as newer than its base tag.
inline bool isNewer(const char *tag, const char *running) {
    int a[3], b[3];
    bool devA, devB;
    if (!parseVersion(tag, a, devA) || devA) return false;   // only plain release tags
    if (!parseVersion(running, b, devB)) return true;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;   // same base: the release is not newer (even if running a dev build of it)
}

}  // namespace ota
