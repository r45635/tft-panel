#include "cover.h"

#include <esp_heap_caps.h>

#include <cstring>

#include "../net/psram_text.h"

#include "src/libs/tjpgd/tjpgd.h"   // LVGL's bundled TJpgDec (LV_USE_TJPGD), RGB888 output

namespace {

constexpr int MAX_SRC = 512;
constexpr int DARKEN = 90;        // /256 ≈ 35 % brightness
constexpr int BLUR_PASSES = 2;    // 3x3 box blur on the small source before scaling

void *psramAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

struct Rgb {   // decoded source, w x h x 3, PSRAM
    uint8_t *px = nullptr;
    int w = 0, h = 0;
    ~Rgb() {
        if (px) heap_caps_free(px);
    }
};

struct Decode {
    const uint8_t *data;
    size_t len, pos;
    Rgb *rgb;
};

size_t inFunc(JDEC *jd, uint8_t *buf, size_t n) {
    Decode *d = (Decode *)jd->device;
    if (n > d->len - d->pos) n = d->len - d->pos;
    if (buf) memcpy(buf, d->data + d->pos, n);
    d->pos += n;
    return n;
}

// LVGL's TJpgDec writes B, G, R (LVGL's RGB888 memory order), not R, G, B: swap here, or every
// real cover comes out with red and blue exchanged (orange covers turned blue).
int outFunc(JDEC *jd, void *bitmap, JRECT *r) {
    Decode *d = (Decode *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int bw = r->right - r->left + 1;
    for (int y = r->top; y <= r->bottom; y++) {
        uint8_t *dst = d->rgb->px + ((size_t)y * d->rgb->w + r->left) * 3;
        for (int x = 0; x < bw; x++, src += 3, dst += 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
    }
    return 1;
}

bool decode(const uint8_t *jpeg, size_t len, Rgb &rgb, String &error) {
    constexpr size_t POOL = 8192;
    void *pool = psramAlloc(POOL);
    if (!pool) {
        error = "out of memory (decoder)";
        return false;
    }
    Decode d{jpeg, len, 0, &rgb};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, inFunc, pool, POOL, &d);
    if (res == JDR_OK && (jd.width < 2 || jd.height < 2 || jd.width > MAX_SRC || jd.height > MAX_SRC)) {
        heap_caps_free(pool);
        error = "cover size " + String(jd.width) + "x" + String(jd.height) + " out of range";
        return false;
    }
    if (res == JDR_OK) {
        rgb.w = jd.width;
        rgb.h = jd.height;
        rgb.px = (uint8_t *)psramAlloc((size_t)rgb.w * rgb.h * 3);
        if (!rgb.px) {
            heap_caps_free(pool);
            error = "out of memory (cover)";
            return false;
        }
        res = jd_decomp(&jd, outFunc, 0);
    }
    heap_caps_free(pool);
    if (res != JDR_OK) {
        error = res == JDR_FMT3 ? "unsupported JPEG (progressive?)" : "JPEG decode failed (" + String((int)res) + ")";
        return false;
    }
    return true;
}

void boxBlur(Rgb &rgb) {
    const int w = rgb.w, h = rgb.h;
    uint8_t *tmp = (uint8_t *)psramAlloc((size_t)w * h * 3);
    if (!tmp) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                int sum = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        sum += rgb.px[((size_t)yy * w + xx) * 3 + c];
                        n++;
                    }
                tmp[((size_t)y * w + x) * 3 + c] = sum / n;
            }
    memcpy(rgb.px, tmp, (size_t)w * h * 3);
    heap_caps_free(tmp);
}

// Area-average copy fitting in box x box (aspect kept); a plain copy when already small.
bool downsample(const Rgb &src, int box, Rgb &out) {
    float s = std::max(1.0f, std::max((float)src.w / box, (float)src.h / box));
    out.w = std::max(1, (int)(src.w / s));
    out.h = std::max(1, (int)(src.h / s));
    out.px = (uint8_t *)psramAlloc((size_t)out.w * out.h * 3);
    if (!out.px) return false;
    for (int y = 0; y < out.h; y++) {
        int y0 = y * src.h / out.h, y1 = std::max(y0 + 1, (y + 1) * src.h / out.h);
        for (int x = 0; x < out.w; x++) {
            int x0 = x * src.w / out.w, x1 = std::max(x0 + 1, (x + 1) * src.w / out.w);
            int sum[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t *p = src.px + ((size_t)yy * src.w + xx) * 3;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    n++;
                }
            uint8_t *o = out.px + ((size_t)y * out.w + x) * 3;
            for (int c = 0; c < 3; c++) o[c] = sum[c] / n;
        }
    }
    return true;
}

std::shared_ptr<Picture> newPicture(int w, int h) {
    auto p = std::allocate_shared<Picture>(PsramAllocator<Picture>());   // the block too: 150 thumbnails
    p->pixels = (uint16_t *)psramAlloc((size_t)w * h * 2);
    if (!p->pixels) return nullptr;
    p->w = w;
    p->h = h;
    p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    p->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    p->dsc.header.w = w;
    p->dsc.header.h = h;
    p->dsc.header.stride = w * 2;
    p->dsc.data = (const uint8_t *)p->pixels;
    p->dsc.data_size = (uint32_t)w * h * 2;
    return p;
}

// Bilinear resample of the source window (srcX, srcY, s source px per output px) into
// `out`, scaled by `gain`/256, ordered-dithered to RGB565 (no banding in dark gradients).
void resample(const Rgb &src, Picture &out, float s, float offX, float offY, int gain) {
    auto coord = [](float f, int max, int &i0, int &i1, int &frac) {
        if (f < 0) f = 0;
        if (f > max - 1) f = max - 1;
        i0 = (int)f;
        i1 = i0 + 1 < max ? i0 + 1 : i0;
        frac = (int)((f - i0) * 256);
    };
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int sw = src.w, sh = src.h, ow = out.w;
    int *x0 = new int[ow], *x1 = new int[ow], *fx = new int[ow];
    for (int x = 0; x < ow; x++) coord(offX + (x + 0.5f) * s - 0.5f, sw, x0[x], x1[x], fx[x]);
    for (int y = 0; y < out.h; y++) {
        int y0, y1, fy;
        coord(offY + (y + 0.5f) * s - 0.5f, sh, y0, y1, fy);
        const uint8_t *r0 = src.px + (size_t)y0 * sw * 3, *r1 = src.px + (size_t)y1 * sw * 3;
        uint16_t *o = out.pixels + (size_t)y * ow;
        for (int x = 0; x < ow; x++) {
            int v[3];
            for (int c = 0; c < 3; c++) {
                int a = r0[x0[x] * 3 + c], b = r0[x1[x] * 3 + c];
                int cc = r1[x0[x] * 3 + c], dd = r1[x1[x] * 3 + c];
                int top = a * (256 - fx[x]) + b * fx[x];
                int bot = cc * (256 - fx[x]) + dd * fx[x];
                v[c] = ((top * (256 - fy) + bot * fy) >> 16) * gain / 256;   // 0..255
            }
            int t = bayer[y & 3][x & 3];
            int r = std::min(255, v[0] + (t >> 1)) >> 3;
            int g = std::min(255, v[1] + (t >> 2)) >> 2;
            int bl = std::min(255, v[2] + (t >> 1)) >> 3;
            o[x] = (uint16_t)((r << 11) | (g << 5) | bl);
        }
    }
    delete[] x0;
    delete[] x1;
    delete[] fx;
}

}  // namespace

Picture::~Picture() {
    if (pixels) heap_caps_free(pixels);
}

namespace cover {

namespace {
bool compose(Rgb &rgb, bool withBig, int thumbBox, bool withBackdrop, int outW, int outH, Cover &out, String &error);
}

bool build(const uint8_t *jpeg, size_t len, bool withBig, int thumbBox, bool withBackdrop, int outW, int outH,
           Cover &out, String &error) {
    out = Cover();
    Rgb rgb;
    if (!decode(jpeg, len, rgb, error)) return false;
    return compose(rgb, withBig, thumbBox, withBackdrop, outW, outH, out, error);
}

bool buildFromRgb(const uint8_t *rgb888, int w, int h, int thumbBox, bool withBackdrop, int outW, int outH,
                  Cover &out, String &error) {
    out = Cover();
    Rgb rgb;
    rgb.w = w;
    rgb.h = h;
    rgb.px = (uint8_t *)psramAlloc((size_t)w * h * 3);
    if (!rgb.px) {
        error = "out of memory (cover)";
        return false;
    }
    memcpy(rgb.px, rgb888, (size_t)w * h * 3);
    return compose(rgb, true, thumbBox, withBackdrop, outW, outH, out, error);
}

namespace {
bool compose(Rgb &rgb, bool withBig, int thumbBox, bool withBackdrop, int outW, int outH, Cover &out, String &error) {

    if (withBig) {
        auto big = newPicture(rgb.w, rgb.h);
        if (!big) {
            error = "out of memory (big cover)";
            return false;
        }
        resample(rgb, *big, 1.0f, 0, 0, 256);
        out.big = big;
    }

    // Thumbnail and backdrop from a small area-average copy: same look from any source size.
    Rgb small;
    if (!downsample(rgb, thumbBox, small)) {
        error = "out of memory (thumbnail)";
        return false;
    }
    auto thumb = newPicture(small.w, small.h);
    if (!thumb) {
        error = "out of memory (thumbnail)";
        return false;
    }
    resample(small, *thumb, 1.0f, 0, 0, 256);
    out.thumb = thumb;
    if (!withBackdrop) return true;

    for (int i = 0; i < BLUR_PASSES; i++) boxBlur(small);
    auto bd = newPicture(outW, outH);
    if (!bd) {
        error = "out of memory (backdrop)";
        return false;
    }
    // Scale to cover: crop the overflow, centred.
    float s = std::min((float)small.w / outW, (float)small.h / outH);
    resample(small, *bd, s, (small.w - outW * s) / 2, (small.h - outH * s) / 2, DARKEN);
    out.backdrop = bd;
    return true;
}
}  // namespace

}  // namespace cover
