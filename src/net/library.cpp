#include "library.h"

#include <esp_heap_caps.h>

#include <algorithm>

#include "../app/cover.h"
#include "net_internal.h"

namespace net::lib {
namespace {

using namespace net::internal;

constexpr int PAGE = 50;                // items per /browse/load
constexpr size_t THUMB_CACHE = 150;     // thumbnails kept, by image key (PSRAM)
constexpr uint32_t TIMEOUT_MS = 10000;  // Roon can be slow on a big library

// One level as shown: its title and where it sits in Roon's stack for this hierarchy.
struct Level {
    String title;
    int roonLevel = 0;
    int loaded = 0;   // items read from Roon so far (before hiding empty search categories)
};

std::shared_ptr<LibraryPage> g_page;   // working copy; published as a const copy
String g_hier;
std::vector<Level> g_levels;
int g_roon = 0;          // Roon's current level in g_hier (deeper than the shown one after actions)
bool g_open = false;     // the library screen is open: thumbnails load
uint32_t g_nextId = 1;
std::vector<std::pair<String, std::shared_ptr<const Picture>>> g_cache;   // back = most recent
std::vector<String> g_failedThumbs;

const String &sessionKey() {
    static String key = net::hostName();   // the panel's own browse stack in Roon
    return key;
}

void publish() {
    g_page->path.clear();
    for (const Level &l : g_levels) g_page->path.push_back(l.title);
    publishLibrary(std::make_shared<const LibraryPage>(*g_page));
}

void setMessage(const String &m, bool error) {
    g_page->message = m;
    g_page->error = error;
    g_page->messageAt = millis();
    if (m.length()) Serial.printf("[lib] %s%s\n", error ? "ERROR: " : "", m.c_str());
}

String errorText(const HttpResult &r) {
    if (r.code == 401) return "the companion needs its access token";
    if (r.code == 503) return "Roon is not connected to the companion";
    return r.message.length() ? r.message : "HTTP " + String(r.code);
}

std::shared_ptr<const Picture> cached(const String &key) {
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
        if (it->first == key) {
            auto pic = it->second;
            g_cache.erase(it);
            g_cache.emplace_back(key, pic);   // most recent last
            return pic;
        }
    return nullptr;
}

HttpResult post(const char *path, JsonDocument &body, JsonDocument &resp) {
    body["hierarchy"] = g_hier;
    body["multi_session_key"] = sessionKey();
    return request(path, &body, resp, TIMEOUT_MS);
}

LibItem readItem(JsonObjectConst it, const String &listImage) {
    LibItem i;
    i.title = it["title"] | "";
    i.subtitle = it["subtitle"] | "";
    i.itemKey = it["item_key"] | "";
    i.hint = it["hint"] | "";
    i.imageKey = it["image_key"] | "";
    if (!i.imageKey.length()) i.imageKey = listImage;   // tracks of an album: the album's cover
    i.prompt = !it["input_prompt"].isNull();
    i.thumb = i.imageKey.length() ? cached(i.imageKey) : nullptr;
    return i;
}

// Items [offset, offset + PAGE) of the top level: replaces the list at 0, appends after.
bool loadTop(int offset) {
    Level &lv = g_levels.back();
    JsonDocument body(psram()), resp(psram());
    body["level"] = lv.roonLevel;
    body["offset"] = offset;
    body["count"] = PAGE;
    HttpResult r = post("/api/roon/browse/load", body, resp);
    if (r.code != 200) {
        setMessage("Could not load the list: " + errorText(r), true);
        return false;
    }
    if (offset == 0) {
        g_page->items.clear();
        g_page->id = g_nextId++;
        lv.loaded = 0;
    }
    g_page->total = resp["list"]["count"] | 0;
    String listImage = resp["list"]["image_key"] | "";
    JsonArrayConst items = resp["items"].as<JsonArrayConst>();
    bool searchRoot = g_hier == "search" && g_levels.size() == 1;
    for (JsonObjectConst it : items) {
        lv.loaded++;
        LibItem i = readItem(it, listImage);
        if (searchRoot && i.subtitle.startsWith("0 ")) continue;   // empty result category
        g_page->items.push_back(std::move(i));
    }
    return true;
}

// Roon's stack may be deeper than what is shown (after an action menu): pop back to it first.
bool syncTo() {
    int shown = g_levels.back().roonLevel;
    if (g_roon <= shown) return true;
    JsonDocument body(psram()), resp(psram());
    body["pop_levels"] = g_roon - shown;
    HttpResult r = post("/api/roon/browse", body, resp);
    if (r.code != 200) {
        setMessage("Could not go back: " + errorText(r), true);
        return false;
    }
    g_roon = resp["list"]["level"] | shown;
    return true;
}

void openRoot(const String &hier, const String &query) {
    g_hier = hier;
    g_levels.clear();
    g_page = std::make_shared<LibraryPage>();
    g_page->hierarchy = hier;
    g_page->query = query;
    g_page->loading = true;
    publish();
    JsonDocument body(psram()), resp(psram());
    body["pop_all"] = true;
    if (hier == "search") body["input"] = query;
    String zone = followedZoneId();
    if (zone.length()) body["zone_or_output_id"] = zone;
    HttpResult r = post("/api/roon/browse", body, resp);
    if (r.code != 200) {
        setMessage((hier == "search" ? "Search failed: " : "Could not open the library: ") + errorText(r), true);
    } else {
        g_roon = resp["list"]["level"] | 0;
        String title = hier == "search" ? "Results for \"" + query + "\"" : String(resp["list"]["title"] | "Library");
        g_levels.push_back({title, g_roon, 0});
        if (loadTop(0) && g_page->items.empty()) setMessage(hier == "search" ? "Nothing found" : "Empty", false);
    }
    g_page->loading = false;
    publish();
    Serial.printf("[lib] %s \"%s\": %u items, Roon level %d\n", hier.c_str(), query.c_str(),
                  (unsigned)g_page->items.size(), g_roon);
}

// Roon no longer knows an item key (the list changed under us): start this hierarchy again.
void restart(const char *why) {
    Serial.printf("[lib] %s: reloading\n", why);
    String hier = g_hier, query = g_page->query;
    openRoot(hier, query);
    setMessage("The list changed: reloaded", false);
    publish();
}

void openActions(int index);

void drill(int index) {
    if (index < 0 || index >= (int)g_page->items.size()) return;
    LibItem it = g_page->items[index];
    if (it.hint == "action_list" || it.hint == "action") return openActions(index);
    if (it.hint != "list") return;   // a section header
    if (it.prompt) {
        setMessage("Use the Search tab to search", false);
        return publish();
    }
    if (!syncTo()) return publish();
    g_page->loading = true;
    publish();
    JsonDocument body(psram()), resp(psram());
    body["item_key"] = it.itemKey;
    String zone = followedZoneId();
    if (zone.length()) body["zone_or_output_id"] = zone;
    HttpResult r = post("/api/roon/browse", body, resp);
    g_page->loading = false;
    if (r.code == 502) return restart("stale item key");
    if (r.code != 200) {
        setMessage("Could not open \"" + it.title + "\": " + errorText(r), true);
        return publish();
    }
    String action = resp["action"] | "";
    if (action == "list") {
        g_roon = resp["list"]["level"] | (g_roon + 1);
        g_levels.push_back({String(resp["list"]["title"] | it.title.c_str()), g_roon, 0});
        loadTop(0);
        // Roon puts an album behind a level holding only that album: open it at once (Back
        // pops both, see back()).
        if (g_page->items.size() == 1 && g_page->items[0].hint == "list" && g_page->items[0].title == it.title) {
            JsonDocument b2(psram()), r2(psram());
            b2["item_key"] = g_page->items[0].itemKey;
            if (zone.length()) b2["zone_or_output_id"] = zone;
            HttpResult rr = post("/api/roon/browse", b2, r2);
            if (rr.code == 200 && String(r2["action"] | "") == "list") {
                g_roon = r2["list"]["level"] | (g_roon + 1);
                g_levels.back().roonLevel = g_roon;
                loadTop(0);
            }
        }
    } else if (action == "message") {
        setMessage(resp["message"] | "", resp["is_error"] | false);
    }
    publish();
}

void back() {
    if (g_levels.size() <= 1 || !syncTo()) return publish();
    JsonDocument body(psram()), resp(psram());
    // One shown level can be several in Roon (an album opened through its single-item level).
    body["pop_levels"] = std::max(1, g_levels.back().roonLevel - g_levels[g_levels.size() - 2].roonLevel);
    HttpResult r = post("/api/roon/browse", body, resp);
    if (r.code != 200) {
        setMessage("Could not go back: " + errorText(r), true);
        return publish();
    }
    g_levels.pop_back();
    g_roon = resp["list"]["level"] | g_levels.back().roonLevel;
    g_page->actions.clear();
    g_page->actionsFor = "";
    loadTop(0);
    publish();
}

void more() {
    if (g_levels.empty() || g_levels.back().loaded >= g_page->total) return;
    if (!syncTo()) return publish();
    loadTop(g_levels.back().loaded);
    publish();
}

// The actions Roon offers for an item (Play Now, Add Next, Queue, Start Radio…), sometimes one
// level deeper behind a single "action_list" entry.
void openActions(int index) {
    if (index < 0 || index >= (int)g_page->items.size()) return;
    LibItem it = g_page->items[index];
    String zone = followedZoneId();
    if (!zone.length()) {
        setMessage("Choose a zone first (controls bar)", true);
        return publish();
    }
    if (!syncTo()) return publish();
    String key = it.itemKey;
    for (int depth = 0; depth < 2; depth++) {
        JsonDocument body(psram()), resp(psram());
        body["item_key"] = key;
        body["zone_or_output_id"] = zone;
        HttpResult r = post("/api/roon/browse", body, resp);
        if (r.code == 502) return restart("stale item key");
        if (r.code != 200 || String(resp["action"] | "") != "list") {
            setMessage(r.code != 200 ? "No actions for \"" + it.title + "\": " + errorText(r)
                                     : String(resp["message"] | "No actions"),
                       r.code != 200);
            return publish();
        }
        g_roon = resp["list"]["level"] | (g_roon + 1);
        JsonDocument lbody(psram()), lresp(psram());
        lbody["level"] = g_roon;
        lbody["offset"] = 0;
        lbody["count"] = 20;
        r = post("/api/roon/browse/load", lbody, lresp);
        if (r.code != 200) {
            setMessage("No actions: " + errorText(r), true);
            return publish();
        }
        std::vector<LibItem> actions, lists;
        for (JsonObjectConst a : lresp["items"].as<JsonArrayConst>()) {
            LibItem i = readItem(a, "");
            if (i.hint == "action") actions.push_back(std::move(i));
            else if (i.hint == "action_list") lists.push_back(std::move(i));
        }
        if (!actions.empty()) {
            g_page->actions = std::move(actions);
            g_page->actionsFor = it.title;
            return publish();
        }
        if (lists.size() != 1) break;
        key = lists[0].itemKey;   // one more level
    }
    setMessage("Roon offers no action for \"" + it.title + "\"", false);
    publish();
}

void runAction(int index) {
    if (index < 0 || index >= (int)g_page->actions.size()) return;
    LibItem a = g_page->actions[index];
    String zone = followedZoneId();
    String what = g_page->actionsFor;
    g_page->actions.clear();
    g_page->actionsFor = "";
    if (!zone.length()) {
        setMessage("Choose a zone first (controls bar)", true);
        return publish();
    }
    JsonDocument body(psram()), resp(psram());
    body["item_key"] = a.itemKey;
    body["zone_or_output_id"] = zone;
    HttpResult r = post("/api/roon/browse", body, resp);
    if (r.code == 502) return restart("stale action key");
    if (r.code != 200) setMessage(a.title + " failed: " + errorText(r), true);
    else if (String(resp["action"] | "") == "message" && (resp["is_error"] | false))
        setMessage(resp["message"] | "Roon refused it", true);
    else setMessage(a.title + ": " + what, false);
    publish();
}

}  // namespace

void run(LibOp op, int index, const String &text) {
    if (op == LibOp::Open) {
        g_open = true;
        return;
    }
    if (op == LibOp::Close) {
        g_open = false;
        return;
    }
    if (!baseUrl().length()) return;
    if (op == LibOp::Search) return openRoot("search", text);
    if (op == LibOp::Browse) return openRoot("browse", "");
    if (!g_page || g_levels.empty()) return;
    switch (op) {
        case LibOp::Drill: drill(index); break;
        case LibOp::Back: back(); break;
        case LibOp::More: more(); break;
        case LibOp::Actions: openActions(index); break;
        case LibOp::Action: runAction(index); break;
        case LibOp::CloseActions:
            g_page->actions.clear();
            g_page->actionsFor = "";
            publish();
            break;
        default: break;
    }
}

void tick() {
    if (!g_open || !g_page || !baseUrl().length()) return;
    String key;
    for (const LibItem &i : g_page->items)
        if (!i.thumb && i.imageKey.length() &&
            std::find(g_failedThumbs.begin(), g_failedThumbs.end(), i.imageKey) == g_failedThumbs.end()) {
            key = i.imageKey;
            break;
        }
    if (!key.length()) return;
    std::shared_ptr<const Picture> pic = cached(key);
    if (!pic) {
        uint8_t *jpeg = nullptr;
        size_t len = 0;
        String q = "/api/roon/image/" + key + "?w=" + COVER_THUMB_PX + "&h=" + COVER_THUMB_PX;
        HttpResult r = getBytes(baseUrl(), q, jpeg, len, 64 * 1024, 4000);
        cover::Cover c;
        String why;
        if (r.code == 200 && jpeg && cover::build(jpeg, len, false, COVER_THUMB_PX, false, 0, 0, c, why) && c.thumb) {
            pic = c.thumb;
            g_cache.emplace_back(key, pic);
            if (g_cache.size() > THUMB_CACHE) g_cache.erase(g_cache.begin());
        } else {
            g_failedThumbs.push_back(key);
            if (g_failedThumbs.size() > 50) g_failedThumbs.erase(g_failedThumbs.begin());
        }
        if (jpeg) heap_caps_free(jpeg);
    }
    if (!pic) return;
    for (LibItem &i : g_page->items)
        if (i.imageKey == key) i.thumb = pic;
    publish();
}

}  // namespace net::lib
