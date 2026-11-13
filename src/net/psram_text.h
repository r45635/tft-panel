// Text and containers kept in PSRAM. On the ESP32-S3 every malloc under 4 KB goes to internal RAM
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), which the CrowPanel's Wi-Fi needs: a library page of a
// few hundred items held as Arduino Strings would take tens of kilobytes of it.
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <memory>

// Allocator for STL containers (and std::allocate_shared) that places them in PSRAM.
template <class T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() = default;
    template <class U>
    PsramAllocator(const PsramAllocator<U> &) {}
    T *allocate(size_t n) {
        void *p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = malloc(n * sizeof(T));   // PSRAM full: better internal RAM than a crash
        if (!p) abort();   // out of all memory
        return (T *)p;
    }
    void deallocate(T *p, size_t) { heap_caps_free(p); }
    template <class U>
    bool operator==(const PsramAllocator<U> &) const { return true; }
    template <class U>
    bool operator!=(const PsramAllocator<U> &) const { return false; }
};

// A small immutable string whose characters live in PSRAM. Copying duplicates it (no shared
// control block, which would itself land in internal RAM).
class PStr {
public:
    PStr() = default;
    explicit PStr(const char *s) { set(s, s ? strlen(s) : 0); }
    explicit PStr(const String &s) { set(s.c_str(), s.length()); }
    PStr(const PStr &o) { set(o.c_str(), o.n_); }
    PStr(PStr &&o) noexcept : p_(o.p_), n_(o.n_) {
        o.p_ = nullptr;
        o.n_ = 0;
    }
    PStr &operator=(const PStr &o) {
        if (this != &o) {
            PStr t(o);
            swap(t);
        }
        return *this;
    }
    PStr &operator=(PStr &&o) noexcept {
        swap(o);
        return *this;
    }
    PStr &operator=(const char *s) { return *this = PStr(s); }
    PStr &operator=(const String &s) { return *this = PStr(s); }
    ~PStr() {
        if (p_) heap_caps_free(p_);
    }

    const char *c_str() const { return p_ ? p_ : ""; }
    size_t length() const { return n_; }
    bool startsWith(const char *s) const { return strncmp(c_str(), s, strlen(s)) == 0; }
    operator String() const { return String(c_str()); }

    bool operator==(const char *s) const { return strcmp(c_str(), s ? s : "") == 0; }
    bool operator==(const PStr &o) const { return n_ == o.n_ && strcmp(c_str(), o.c_str()) == 0; }
    bool operator==(const String &s) const { return n_ == s.length() && strcmp(c_str(), s.c_str()) == 0; }
    bool operator!=(const char *s) const { return !(*this == s); }

private:
    void set(const char *s, size_t n) {
        if (!n) return;
        p_ = (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p_) p_ = (char *)malloc(n + 1);
        if (!p_) return;
        memcpy(p_, s, n);
        p_[n] = 0;
        n_ = n;
    }
    void swap(PStr &o) noexcept {
        std::swap(p_, o.p_);
        std::swap(n_, o.n_);
    }
    char *p_ = nullptr;
    size_t n_ = 0;
};

inline bool operator==(const String &a, const PStr &b) { return b == a; }
inline String operator+(const char *a, const PStr &b) { return String(a) + b.c_str(); }
inline String operator+(const String &a, const PStr &b) { return a + b.c_str(); }
inline String operator+(const PStr &a, const char *b) { return String(a.c_str()) + b; }
