// Which server the panel asks for lyrics, and what it says when there is none: the one decision
// of lyrics_source.cpp, kept free of Arduino and the network so a host test can hold it to its
// rules (test/test_lyrics_ladder).
//
// The rules, in order - a ladder, not a switch:
//   "none"                 lyrics are off; nothing is asked and the screen says so
//   a companion was found  it wins, whatever else is set: it has the listener's own files, the
//                          corrections and the timing offset. A companion announcing api=get
//                          (v0.7.1+) is asked LRCLIB's /api/get, which goes through the player's
//                          chain; an older one is asked /api/lrclib/lookup
//   a fallback server set  asked in LRCLIB's shape (/api/get) when no companion is on the network
//   nothing                no lyrics, and the screen says what is missing and where it is typed
//
// Templated on the string type: Arduino's String on the board (std::string would be the only
// use of it in the image, and the CrowPanel has no room for a second string class), std::string
// in the host test. Only what both offer is used: construction from a literal, ==, length(),
// [], += of a literal, a string and a char.
#pragma once

namespace net::ladder {

template <class S>
struct Choice {
    enum class Kind { Off, Companion, Server, None };
    Kind kind = Kind::None;
    S base;                // server base URL, no trailing slash (Companion, Server)
    bool getApi = false;   // Companion: ask /api/get rather than /api/lrclib/lookup
    S why;                 // Off, None: the sentence for the screen
};

template <class S>
S stripSlashes(const S &in) {
    unsigned n = in.length();
    while (n && in[n - 1] == '/') n--;
    S out;
    for (unsigned i = 0; i < n; i++) out += in[i];
    return out;
}

// `setting` is the panel's lyrics setting ("" = nothing typed, "none" = off, else a URL);
// `companionUrl` is empty when no companion is on the network; `companionApi` is the `api` TXT
// key of the one found ("" on companions before 0.7.1); `host` is this panel's own name
// (tft-panel-xxxx), used to tell the listener where the answer is typed.
template <class S>
Choice<S> choose(const S &setting, const S &companionUrl, const S &companionApi, const S &host) {
    Choice<S> c;
    if (setting == "none") {
        c.kind = Choice<S>::Kind::Off;
        c.why = "Lyrics are turned off for this panel";
        return c;
    }
    if (companionUrl.length()) {
        c.kind = Choice<S>::Kind::Companion;
        c.base = stripSlashes(companionUrl);
        c.getApi = companionApi == "get";
        return c;
    }
    if (setting.length()) {
        c.kind = Choice<S>::Kind::Server;
        c.base = stripSlashes(setting);
        return c;
    }
    c.kind = Choice<S>::Kind::None;
    // Said in the order the listener can act on: what is missing, then where to fix it. The
    // address is this panel's own page, which is where both answers are typed.
    c.why = "No lyrics source\n\nNo companion is announcing itself on your network, and no fallback "
            "lyrics server is set.\nSet one at http://";
    c.why += host;
    c.why += ".local";
    return c;
}

}  // namespace net::ladder
