#include "lyrics.h"

#include <algorithm>

namespace lyrics {
namespace {

// Parses one "[mm:ss]" / "[mm:ss.x]" / "[mm:ss.xx]" / "[mm:ss.xxx]" tag starting at s[i] == '['.
// Returns the time in ms and moves i past ']', or -1 (i untouched) if it is not a time tag.
int32_t parseStamp(const String &s, int &i) {
    int j = i + 1;
    int n = s.length();
    auto digits = [&](int &v) {
        int start = j;
        v = 0;
        while (j < n && isdigit((unsigned char)s[j])) v = v * 10 + (s[j++] - '0');
        return j - start;
    };
    int mm, ss;
    if (digits(mm) == 0 || j >= n || s[j] != ':') return -1;
    j++;
    if (digits(ss) == 0) return -1;
    int frac = 0, fracDigits = 0;
    if (j < n && (s[j] == '.' || s[j] == ':')) {
        j++;
        int start = j;
        while (j < n && isdigit((unsigned char)s[j])) {
            if (j - start < 3) {
                frac = frac * 10 + (s[j] - '0');
                fracDigits++;
            }
            j++;
        }
    }
    if (j >= n || s[j] != ']') return -1;
    while (fracDigits < 3) {
        frac *= 10;
        fracDigits++;
    }
    i = j + 1;
    return (int32_t)mm * 60000 + (int32_t)ss * 1000 + frac;
}

}  // namespace

Lyrics parse(const String &text) {
    Lyrics out;
    std::vector<Line> plain;
    int pos = 0;
    int n = text.length();
    while (pos <= n) {
        int eol = text.indexOf('\n', pos);
        if (eol < 0) eol = n;
        String raw = text.substring(pos, eol);
        raw.replace("\r", "");
        pos = eol + 1;

        int i = 0;
        std::vector<int32_t> stamps;
        bool metadata = false;
        while (i < (int)raw.length() && raw[i] == '[') {
            int32_t t = parseStamp(raw, i);
            if (t < 0) {
                // [ar:...], [ti:...], [offset:...]: metadata, not lyrics.
                metadata = raw.indexOf(']') > 0 && raw.indexOf(':') > 0 && stamps.empty();
                break;
            }
            stamps.push_back(t);
        }
        if (metadata) continue;
        String body = raw.substring(i);
        body.trim();
        if (!stamps.empty()) {
            for (int32_t t : stamps) out.lines.push_back({t, body});
        } else if (body.length() || !plain.empty()) {
            plain.push_back({-1, body});
        }
        if (eol == n) break;
    }

    if (!out.lines.empty()) {
        out.synced = true;
        std::stable_sort(out.lines.begin(), out.lines.end(),
                         [](const Line &a, const Line &b) { return a.timeMs < b.timeMs; });
    } else {
        while (!plain.empty() && plain.back().text.length() == 0) plain.pop_back();
        out.lines = std::move(plain);
    }
    return out;
}

int currentIndex(const Lyrics &l, int32_t t) {
    if (!l.synced || l.lines.empty() || t < l.lines.front().timeMs) return -1;
    // Last line whose time <= t.
    auto it = std::upper_bound(l.lines.begin(), l.lines.end(), t,
                               [](int32_t v, const Line &line) { return v < line.timeMs; });
    return (int)(it - l.lines.begin()) - 1;
}

}  // namespace lyrics
