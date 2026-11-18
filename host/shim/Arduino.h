// Host stand-in for <Arduino.h>: just enough of Arduino's String to compile the pure logic
// in src/ on a desktop compiler (native tests, simulator). Grow it as host code needs more.
#pragma once

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <utility>

class String {
public:
    String() = default;
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v) : s_(std::to_string(v)) {}
    String(unsigned int v) : s_(std::to_string(v)) {}
    String(long v) : s_(std::to_string(v)) {}
    String(unsigned long v) : s_(std::to_string(v)) {}
    String(long long v) : s_(std::to_string(v)) {}
    String(unsigned long long v) : s_(std::to_string(v)) {}

    unsigned int length() const { return (unsigned int)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator==(const char *o) const { return s_ == (o ? o : ""); }
    bool operator!=(const String &o) const { return !(*this == o); }
    bool operator!=(const char *o) const { return !(*this == o); }
    String &operator+=(const String &o) { s_ += o.s_; return *this; }
    String &operator+=(const char *o) { s_ += o ? o : ""; return *this; }
    String &operator+=(char c) { s_ += c; return *this; }
    friend String operator+(String a, const String &b) { return a += b; }
    friend String operator+(String a, const char *b) { return a += b; }
    friend String operator+(const char *a, const String &b) { return String(a) += b; }

    bool startsWith(const char *p) const { return p && s_.compare(0, strlen(p), p) == 0; }
    bool endsWith(const char *p) const {
        if (!p) return false;
        size_t n = strlen(p);
        return n <= s_.size() && s_.compare(s_.size() - n, n, p) == 0;
    }
    long toInt() const { return strtol(s_.c_str(), nullptr, 10); }

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

// The simulator advances this clock itself (sim/src/main.cpp), so pictures do not depend on timing.
uint32_t millis();

struct HostSerial {
    void begin(unsigned long) {}
    int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        int n = vfprintf(stderr, fmt, ap);
        va_end(ap);
        return n;
    }
    void print(const char *s) { fputs(s, stderr); }
    void print(const String &s) { print(s.c_str()); }
    void println(const char *s = "") { fprintf(stderr, "%s\n", s); }
    void println(const String &s) { println(s.c_str()); }
};
inline HostSerial Serial;
