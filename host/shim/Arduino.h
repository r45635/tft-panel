// Host stand-in for <Arduino.h>: just enough of Arduino's String to compile the pure logic
// in src/ on a desktop compiler (native tests, simulator). Grow it as host code needs more.
#pragma once

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <string>

class String {
public:
    String() = default;
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}

    unsigned int length() const { return (unsigned int)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator==(const char *o) const { return s_ == o; }

    int indexOf(char c, unsigned int from = 0) const {
        size_t p = s_.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    String substring(unsigned int from) const { return substring(from, length()); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) std::swap(from, to);
        if (from >= s_.size()) return String();
        return String(s_.substr(from, to - from));
    }
    void replace(const char *find, const char *with) {
        size_t n = strlen(find);
        if (!n) return;
        for (size_t p = s_.find(find); p != std::string::npos; p = s_.find(find, p + strlen(with)))
            s_.replace(p, n, with);
    }
    void trim() {
        size_t b = 0, e = s_.size();
        while (b < e && isspace((unsigned char)s_[b])) b++;
        while (e > b && isspace((unsigned char)s_[e - 1])) e--;
        s_ = s_.substr(b, e - b);
    }

private:
    std::string s_;
};
