#include "backdrop.h"

#include <esp_heap_caps.h>

#include <cstring>

#include "src/libs/tjpgd/tjpgd.h"   // LVGL's bundled TJpgDec (LV_USE_TJPGD), RGB888 output

namespace {

constexpr int MAX_SRC = 512;
constexpr int DARKEN = 90;        // /256 ≈ 35 % brightness
constexpr int BLUR_PASSES = 2;    // 3x3 box blur on the small source before scaling

struct Decode {
    const uint8_t *data;
    size_t len, pos;
    uint8_t *rgb;   // w x h x 3
    int w;
};

size_t inFunc(JDEC *jd, uint8_t *buf, size_t n) {
    Decode *d = (Decode *)jd->device;
    if (n > d->len - d->pos) n = d->len - d->pos;
    if (buf) memcpy(buf, d->data + d->pos, n);
    d->pos += n;
    return n;
}

int outFunc(JDEC *jd, void *bitmap, JRECT *r) {
    Decode *d = (Decode *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int bw = r->right - r->left + 1;
    for (int y = r->top; y <= r->bottom; y++) {
        memcpy(d->rgb + ((size_t)y * d->w + r->left) * 3, src, (size_t)bw * 3);
        src += bw * 3;
    }
    return 1;
}

void *psramAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void boxBlur(uint8_t *rgb, int w, int h) {
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
                        sum += rgb[((size_t)yy * w + xx) * 3 + c];
                        n++;
                    }
                tmp[((size_t)y * w + x) * 3 + c] = sum / n;
            }
    memcpy(rgb, tmp, (size_t)w * h * 3);
    heap_caps_free(tmp);
}

}  // namespace

Backdrop::~Backdrop() {
    if (pixels) heap_caps_free(pixels);
}

namespace backdrop {

std::shared_ptr<const Backdrop> build(const uint8_t *jpeg, size_t len, int outW, int outH, String &error) {
    constexpr size_t POOL = 8192;
    void *pool = psramAlloc(POOL);
    if (!pool) {
        error = "out of memory (decoder)";
        return nullptr;
    }
    Decode d{jpeg, len, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, inFunc, pool, POOL, &d);
    if (res != JDR_OK) {
        heap_caps_free(pool);
        error = res == JDR_FMT3 ? "unsupported JPEG (progressive?)" : "bad JPEG (" + String((int)res) + ")";
        return nullptr;
    }
    int sw = jd.width, sh = jd.height;
    if (sw < 2 || sh < 2 || sw > MAX_SRC || sh > MAX_SRC) {
        heap_caps_free(pool);
        error = "cover size " + String(sw) + "x" + String(sh) + " out of range";
        return nullptr;
    }
    d.w = sw;
    d.rgb = (uint8_t *)psramAlloc((size_t)sw * sh * 3);
    if (!d.rgb) {
        heap_caps_free(pool);
        error = "out of memory (cover)";
        return nullptr;
    }
    res = jd_decomp(&jd, outFunc, 0);
    heap_caps_free(pool);
    if (res != JDR_OK) {
        heap_caps_free(d.rgb);
        error = "JPEG decode failed (" + String((int)res) + ")";
        return nullptr;
    }
    for (int i = 0; i < BLUR_PASSES; i++) boxBlur(d.rgb, sw, sh);

    auto b = std::make_shared<Backdrop>();
    b->w = outW;
    b->h = outH;
    b->pixels = (uint16_t *)psramAlloc((size_t)outW * outH * 2);
    if (!b->pixels) {
        heap_caps_free(d.rgb);
        error = "out of memory (backdrop)";
        return nullptr;
    }

    // Scale to cover (crop the overflow, centred), bilinear in 8.8 fixed point.
    // s = source pixels per output pixel.
    float s = std::min((float)sw / outW, (float)sh / outH);
    float offX = (sw - outW * s) / 2, offY = (sh - outH * s) / 2;
    auto coord = [](float f, int max, int &i0, int &i1, int &frac) {
        if (f < 0) f = 0;
        if (f > max - 1) f = max - 1;
        i0 = (int)f;
        i1 = i0 + 1 < max ? i0 + 1 : i0;
        frac = (int)((f - i0) * 256);
    };
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    int *x0 = new int[outW], *x1 = new int[outW], *fx = new int[outW];
    for (int x = 0; x < outW; x++) coord(offX + (x + 0.5f) * s - 0.5f, sw, x0[x], x1[x], fx[x]);
    for (int y = 0; y < outH; y++) {
        int y0, y1, fy;
        coord(offY + (y + 0.5f) * s - 0.5f, sh, y0, y1, fy);
        const uint8_t *r0 = d.rgb + (size_t)y0 * sw * 3, *r1 = d.rgb + (size_t)y1 * sw * 3;
        uint16_t *out = b->pixels + (size_t)y * outW;
        for (int x = 0; x < outW; x++) {
            int v[3];
            for (int c = 0; c < 3; c++) {
                int a = r0[x0[x] * 3 + c], bb = r0[x1[x] * 3 + c];
                int cc = r1[x0[x] * 3 + c], dd = r1[x1[x] * 3 + c];
                int top = a * (256 - fx[x]) + bb * fx[x];
                int bot = cc * (256 - fx[x]) + dd * fx[x];
                int val = (top * (256 - fy) + bot * fy) >> 16;   // 0..255
                v[c] = val * DARKEN / 256;
            }
            // Ordered dither before truncating to 5/6/5 bits: no banding in the dark gradients.
            int t = bayer[y & 3][x & 3];
            int r = std::min(255, v[0] + (t >> 1)) >> 3;
            int g = std::min(255, v[1] + (t >> 2)) >> 2;
            int bl = std::min(255, v[2] + (t >> 1)) >> 3;
            out[x] = (uint16_t)((r << 11) | (g << 5) | bl);
        }
    }
    delete[] x0;
    delete[] x1;
    delete[] fx;
    heap_caps_free(d.rgb);

    b->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    b->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    b->dsc.header.w = outW;
    b->dsc.header.h = outH;
    b->dsc.header.stride = outW * 2;
    b->dsc.data = (const uint8_t *)b->pixels;
    b->dsc.data_size = (uint32_t)outW * outH * 2;
    return b;
}

}  // namespace backdrop
