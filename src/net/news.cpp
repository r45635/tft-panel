#include "news.h"

#include "http_get.h"
#include "net_internal.h"
#include "settings.h"

namespace net::news {

using namespace internal;

namespace {

constexpr uint32_t REFRESH_MS = 15u * 60u * 1000u;
constexpr uint32_t RETRY_MS = 3u * 60u * 1000u;
constexpr size_t MAX_FEED_BYTES = 24 * 1024;   // enough for the head of any feed
constexpr int MAX_HEADLINES = 8;

uint32_t g_nextAt = 0;
String g_url;

// The handful of escapes feeds actually use, plus numeric ones: a headline full of &#8217; is
// worse than no headline at all.
String unescape(const String &in) {
    String out;
    out.reserve(in.length());
    for (unsigned i = 0; i < in.length();) {
        if (in[i] != '&') {
            out += in[i++];
            continue;
        }
        int end = in.indexOf(';', i);
        if (end < 0 || end - (int)i > 10) {
            out += in[i++];
            continue;
        }
        String ent = in.substring(i + 1, end);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos" || ent == "#39") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (ent.startsWith("#")) {
            long cp = ent.startsWith("#x") || ent.startsWith("#X") ? strtol(ent.c_str() + 2, nullptr, 16)
                                                                   : strtol(ent.c_str() + 1, nullptr, 10);
            // UTF-8, since the fonts carry Latin Extended-A and typographic punctuation.
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        } else {
            out += '&';
            out += ent;
        }
        i = end + 1;
    }
    out.trim();
    return out;
}

// Titles of the feed's items, in order. The first <title> belongs to the feed itself, so the
// scan starts at the first item or entry.
String headlines(const String &xml) {
    int from = xml.indexOf("<item");
    if (from < 0) from = xml.indexOf("<entry");
    if (from < 0) return String();

    String line;
    int found = 0;
    while (found < MAX_HEADLINES) {
        int open = xml.indexOf("<title", from);
        if (open < 0) break;
        int gt = xml.indexOf('>', open);
        if (gt < 0) break;
        int close = xml.indexOf("</title>", gt);
        if (close < 0) break;
        String t = xml.substring(gt + 1, close);
        from = close + 8;
        t.replace("<![CDATA[", "");
        t.replace("]]>", "");
        t = unescape(t);
        t.replace('\n', ' ');
        t.replace('\r', ' ');
        while (t.indexOf("  ") >= 0) t.replace("  ", " ");
        t.trim();
        if (!t.length()) continue;
        if (line.length()) line += "   ·   ";   // a middle dot between headlines
        line += t;
        found++;
    }
    return line;
}

}  // namespace

void reset() {
    g_nextAt = 0;
    g_url = "";
    publishNews("");
}

void tick(uint32_t now) {
    String url = settings::get().newsUrl;
    if (!url.length()) {
        if (g_url.length()) reset();
        return;
    }
    if (g_nextAt && (int32_t)(now - g_nextAt) < 0 && url == g_url) return;
    g_url = url;

    String xml;
    httpget::Result r = httpget::text(url, xml, MAX_FEED_BYTES);
    if (!r.ok()) {
        Serial.printf("[news] no feed: %s\n", r.message.c_str());
        g_nextAt = now + RETRY_MS;
        return;
    }
    String line = headlines(xml);
    if (!line.length()) {
        Serial.println("[news] nothing that looks like a headline in that feed");
        g_nextAt = now + RETRY_MS;
        return;
    }
    Serial.printf("[news] %u characters of headlines\n", (unsigned)line.length());
    publishNews(line);
    g_nextAt = now + REFRESH_MS;
}

}  // namespace net::news
