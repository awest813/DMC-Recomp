#include "raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

#include "ps2/system.hpp"

namespace ps2::gs::raster {

namespace {

struct Setup {
    bool iip, tme, fge, abe, fst;
    std::uint32_t fbp, fbw, fpsm, fbmsk;
    std::uint32_t zbp, zpsm;
    bool zmsk;
    bool ate, date, datm, zte;
    int atst, afail, ztst;
    std::uint32_t aref;
    int a, b, c, d;
    std::uint32_t fix;
    bool pabe, colclamp, fba, dthe;
    std::uint64_t dimx;
    int scax0, scax1, scay0, scay1;
    int ofx, ofy;
    std::uint32_t tbp, tbw, tpsm, tw, th, cpsm, csa;
    bool tcc, bilinear;
    std::uint64_t tex1, miptbp[2];
    int tfx, wms, wmt;
    std::uint32_t minu, maxu, minv, maxv;
    std::uint32_t ta0, ta1;
    bool aem;
    std::uint32_t fogcol;
    int scanmsk;
    const std::uint32_t *clut;
};

struct Attr {
    float r, g, b, a, s, t, q, f;
    double z;
};

int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

std::uint32_t expand16(std::uint32_t c, const Setup &s) {
    const std::uint32_t r = (c & 0x1Fu) << 3, g = ((c >> 5) & 0x1Fu) << 3, b = ((c >> 10) & 0x1Fu) << 3;
    std::uint32_t a;
    if (c & 0x8000u) a = s.ta1;
    else a = (s.aem && !(c & 0x7FFFu)) ? 0u : s.ta0;
    return r | (g << 8) | (b << 16) | (a << 24);
}

std::uint32_t fetch(const Setup &s, int x, int y) {
    const auto wrap = [](int v, int size, int mode, std::uint32_t min, std::uint32_t max) {
        switch (mode) {
        case 0: return v & (size - 1);
        case 1: return std::clamp(v, 0, size - 1);
        case 2: return std::clamp(v, static_cast<int>(min), static_cast<int>(max));
        default: return static_cast<int>((static_cast<std::uint32_t>(v) & min) | max);
        }
    };
    x = wrap(x, 1 << s.tw, s.wms, s.minu, s.maxu);
    y = wrap(y, 1 << s.th, s.wmt, s.minv, s.maxv);
    const std::uint32_t raw = read_pixel(s.tpsm, s.tbp, s.tbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    switch (s.tpsm) {
    case PSMCT32: case PSMZ32: return raw;
    case PSMCT24: case PSMZ24: return (raw & 0xFFFFFFu) | (((s.aem && !(raw & 0xFFFFFFu)) ? 0u : s.ta0) << 24);
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return expand16(raw, s);
    case PSMT8: case PSMT8H: {
        const std::uint32_t e = s.clut[raw & 255u];
        return s.cpsm == PSMCT32 || s.cpsm == PSMCT24 ? e : expand16(e, s);
    }
    case PSMT4: case PSMT4HL: case PSMT4HH: {
        const std::uint32_t e = s.clut[((s.csa & 15u) * 16u + (raw & 15u)) & 511u];
        return s.cpsm == PSMCT32 || s.cpsm == PSMCT24 ? e : expand16(e, s);
    }
    default: return raw;
    }
}

std::uint32_t sample_level(const Setup &s, float u, float v) {
    if (!s.bilinear) return fetch(s, static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
    u -= 0.5f;
    v -= 0.5f;
    const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
    const float fx = u - static_cast<float>(x0), fy = v - static_cast<float>(y0);
    const std::uint32_t c00 = fetch(s, x0, y0), c10 = fetch(s, x0 + 1, y0), c01 = fetch(s, x0, y0 + 1), c11 = fetch(s, x0 + 1, y0 + 1);
    std::uint32_t out = 0;
    for (int ch = 0; ch < 32; ch += 8) {
        const float a = static_cast<float>((c00 >> ch) & 0xFFu), b = static_cast<float>((c10 >> ch) & 0xFFu);
        const float c = static_cast<float>((c01 >> ch) & 0xFFu), d = static_cast<float>((c11 >> ch) & 0xFFu);
        const float top = a + (b - a) * fx, bottom = c + (d - c) * fx;
        out |= static_cast<std::uint32_t>(std::clamp(top + (bottom - top) * fy + 0.5f, 0.0f, 255.0f)) << ch;
    }
    return out;
}

std::uint32_t sample(const Setup &s, float u, float v, float q) {
    const unsigned mxl = std::min<unsigned>((s.tex1 >> 2) & 7u, 6u);
    if (!mxl) return sample_level(s, u, v);
    const unsigned mmin = (s.tex1 >> 6) & 7u;
    const int bias = static_cast<int>(((s.tex1 >> 32) & 0xFFFu) ^ 0x800u) - 0x800;
    float lod = static_cast<float>(bias) / 16.0f;
    if (!(s.tex1 & 1u) && !s.fst)
        lod -= std::log2(std::max(std::abs(q), 1.0e-30f)) * static_cast<float>(1u << ((s.tex1 >> 19) & 3u));
    if (lod <= 0) return sample_level(s, u, v);
    Setup level = s;
    level.bilinear = mmin == 1 || mmin >= 4;
    if (mmin < 2) return sample_level(level, u, v);
    lod = std::min(lod, static_cast<float>(mxl));
    const bool trilinear = mmin & 1u;
    const unsigned first = static_cast<unsigned>(std::floor(lod + (trilinear ? 0.0f : 0.5f)));
    const auto at_level = [&](unsigned n) {
        Setup t = level;
        if (n) {
            const auto descriptor = s.miptbp[(n - 1) / 3] >> (((n - 1) % 3) * 20);
            t.tbp = descriptor & 0x3FFFu;
            t.tbw = (descriptor >> 14) & 0x3Fu;
            t.tw = s.tw > n ? s.tw - n : 0;
            t.th = s.th > n ? s.th - n : 0;
            t.minu >>= n; t.maxu >>= n; t.minv >>= n; t.maxv >>= n;
        }
        const float scale = static_cast<float>(1u << n);
        return sample_level(t, u / scale, v / scale);
    };
    const auto a = at_level(first);
    if (!trilinear || first == mxl) return a;
    const auto b = at_level(first + 1);
    const float fraction = lod - static_cast<float>(first);
    std::uint32_t out = 0;
    for (unsigned ch = 0; ch < 32; ch += 8) {
        const float av = (a >> ch) & 255u, bv = (b >> ch) & 255u;
        out |= static_cast<std::uint32_t>(av + (bv - av) * fraction + 0.5f) << ch;
    }
    return out;
}

std::uint32_t read_frame(const Setup &s, int x, int y) {
    const std::uint32_t raw = read_pixel(s.fpsm, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    switch (s.fpsm) {
    case PSMCT24: return raw | 0x80000000u;
    case PSMCT16: case PSMCT16S:
        return ((raw & 0x1Fu) << 3) | (((raw >> 5) & 0x1Fu) << 11) | (((raw >> 10) & 0x1Fu) << 19) | ((raw & 0x8000u) ? 0x80000000u : 0u);
    default: return raw;
    }
}

void write_frame(const Setup &s, int x, int y, std::uint32_t c, std::uint32_t mask) {
    switch (s.fpsm) {
    case PSMCT16: case PSMCT16S: {
        const std::uint32_t old = read_pixel(s.fpsm, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const auto pack = [](std::uint32_t v) {
            return ((v >> 3) & 0x1Fu) | (((v >> 11) & 0x1Fu) << 5) | (((v >> 19) & 0x1Fu) << 10) | ((v & 0x80000000u) ? 0x8000u : 0u);
        };
        const std::uint32_t m16 = pack(mask) | ((mask & 0x80000000u) ? 0x8000u : 0u);
        write_pixel(s.fpsm, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), (pack(c) & ~m16) | (old & m16));
        break;
    }
    case PSMCT24: {
        const std::uint32_t old = read_pixel(PSMCT32, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const std::uint32_t m = mask | 0xFF000000u;
        write_pixel(PSMCT32, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), (c & ~m) | (old & m));
        break;
    }
    default: {
        if (mask) {
            const std::uint32_t old = read_pixel(s.fpsm, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            c = (c & ~mask) | (old & mask);
        }
        write_pixel(s.fpsm, s.fbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), c);
        break;
    }
    }
}

std::uint32_t z_max(std::uint32_t psm) {
    switch (psm) {
    case PSMZ24: return 0xFFFFFFu;
    case PSMZ16: case PSMZ16S: return 0xFFFFu;
    default: return 0xFFFFFFFFu;
    }
}

void plot(const Setup &s, int x, int y, const Attr &at) {
    if (x < s.scax0 || x > s.scax1 || y < s.scay0 || y > s.scay1) return;
    if (s.scanmsk == 2 && !(y & 1)) return;
    if (s.scanmsk == 3 && (y & 1)) return;

    const std::uint32_t zmax = z_max(s.zpsm);
    const double zf = std::clamp(at.z, 0.0, static_cast<double>(zmax));
    const std::uint32_t z = static_cast<std::uint32_t>(zf);

    // The GS interpolates colors in fixed point, so a constant vertex color stays exact; float barycentrics can land a
    // hair under the integer (127.9999 for 128) and truncation would then fail alpha tests like ATST GEQUAL 128.
    constexpr float kExact = 1.0f / 256.0f;
    int r = static_cast<int>(at.r + kExact), g = static_cast<int>(at.g + kExact), b = static_cast<int>(at.b + kExact), a = static_cast<int>(at.a + kExact);
    if (s.tme) {
        float u, v;
        if (s.fst) {
            u = at.s / 16.0f;
            v = at.t / 16.0f;
        } else {
            const float q = at.q != 0.0f ? at.q : 1.0f;
            u = at.s / q * static_cast<float>(1 << s.tw);
            v = at.t / q * static_cast<float>(1 << s.th);
        }
        const std::uint32_t t = sample(s, u, v, at.q);
        const int tr = static_cast<int>(t & 0xFFu), tg = static_cast<int>((t >> 8) & 0xFFu), tb = static_cast<int>((t >> 16) & 0xFFu), ta = static_cast<int>(t >> 24);
        switch (s.tfx) {
        case 0:
            r = clamp255((tr * r) >> 7);
            g = clamp255((tg * g) >> 7);
            b = clamp255((tb * b) >> 7);
            a = s.tcc ? clamp255((ta * a) >> 7) : a;
            break;
        case 1:
            r = tr; g = tg; b = tb;
            a = s.tcc ? ta : a;
            break;
        case 2:
            r = clamp255(((tr * r) >> 7) + a);
            g = clamp255(((tg * g) >> 7) + a);
            b = clamp255(((tb * b) >> 7) + a);
            a = s.tcc ? clamp255(ta + a) : a;
            break;
        default:
            r = clamp255(((tr * r) >> 7) + a);
            g = clamp255(((tg * g) >> 7) + a);
            b = clamp255(((tb * b) >> 7) + a);
            a = s.tcc ? ta : a;
            break;
        }
    }
    static int trace_x = -1, trace_y = -1;
    static const bool tracing = [] {
        const char *spec = std::getenv("PS2_TRACE_PIXEL");
        return spec && std::sscanf(spec, "%d,%d", &trace_x, &trace_y) == 2;
    }();
    if (tracing && x == trace_x && y == trace_y) {
        const std::uint32_t old = s.zte ? read_pixel(s.zpsm, s.zbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) & zmax : 0;
        const float q = at.q != 0.0f ? at.q : 1.0f;
        const float tu = s.fst ? at.s / 16.0f : at.s / q * static_cast<float>(1 << s.tw), tv = s.fst ? at.t / 16.0f : at.t / q * static_cast<float>(1 << s.th);
        std::fprintf(stderr, "[pixel] frame %llu fbp %X fbw %u tme %d tbp %X tbw %u tpsm %X tw %u th %u wm %d/%d bil %d uv %.2f %.2f tfx %d -> rgba %d %d %d %d abe %d blend %d%d%d%d fix %u dst %08X z %u old %u zte %d ztst %d zmsk %d ate %d atst %d afail %d fbmsk %08X\n",
                     static_cast<unsigned long long>(frames_run()), s.fbp, s.fbw, s.tme, s.tbp, s.tbw, s.tpsm, s.tw, s.th, s.wms, s.wmt, s.bilinear ? 1 : 0, tu, tv, s.tfx,
                     r, g, b, a, s.abe, s.a, s.b, s.c, s.d, s.fix, read_frame(s, x, y), z, old, s.zte, s.ztst, s.zmsk ? 1 : 0, s.ate, s.atst, s.afail, s.fbmsk);
    }
    if (s.fge) {
        const int f = static_cast<int>(at.f);
        const int fr = static_cast<int>(s.fogcol & 0xFFu), fg = static_cast<int>((s.fogcol >> 8) & 0xFFu), fb = static_cast<int>((s.fogcol >> 16) & 0xFFu);
        r = (f * r + (255 - f) * fr) >> 8;
        g = (f * g + (255 - f) * fg) >> 8;
        b = (f * b + (255 - f) * fb) >> 8;
    }

    bool write_fb = true, write_z = !s.zmsk, write_alpha = true;
    if (s.ate) {
        bool pass;
        const std::uint32_t av = static_cast<std::uint32_t>(a);
        switch (s.atst) {
        case 0: pass = false; break;
        case 1: pass = true; break;
        case 2: pass = av < s.aref; break;
        case 3: pass = av <= s.aref; break;
        case 4: pass = av == s.aref; break;
        case 5: pass = av >= s.aref; break;
        case 6: pass = av > s.aref; break;
        default: pass = av != s.aref; break;
        }
        if (!pass) {
            switch (s.afail) {
            case 0: return;
            case 1: write_z = false; break;
            case 2: write_fb = false; break;
            default: write_z = false; write_alpha = false; break;
            }
        }
    }

    std::uint32_t dst = 0;
    bool have_dst = false;
    // PSMCT24 has no destination alpha bit; DATE is ignored for this format.
    if (s.date && s.fpsm != PSMCT24) {
        dst = read_frame(s, x, y);
        have_dst = true;
        const bool bit = (dst & 0x80000000u) != 0;
        if (bit != s.datm) return;
    }

    if (s.zte) {
        const std::uint32_t old = read_pixel(s.zpsm, s.zbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) & zmax;
        bool pass;
        switch (s.ztst) {
        case 0: pass = false; break;
        case 1: pass = true; break;
        case 2: pass = z >= old; break;
        default: pass = z > old; break;
        }
        if (!pass) return;
    }

    if (write_fb) {
        if (s.abe && (!s.pabe || (a & 0x80))) {
            if (!have_dst) dst = read_frame(s, x, y);
            const int cd[3] = {static_cast<int>(dst & 0xFFu), static_cast<int>((dst >> 8) & 0xFFu), static_cast<int>((dst >> 16) & 0xFFu)};
            const int cs[3] = {r, g, b};
            const int ad = static_cast<int>(dst >> 24);
            const int cc = s.c == 0 ? a : s.c == 1 ? ad : static_cast<int>(s.fix);
            int out[3];
            for (int i = 0; i < 3; ++i) {
                const int av = s.a == 0 ? cs[i] : s.a == 1 ? cd[i] : 0;
                const int bv = s.b == 0 ? cs[i] : s.b == 1 ? cd[i] : 0;
                const int dv = s.d == 0 ? cs[i] : s.d == 1 ? cd[i] : 0;
                const int v = (((av - bv) * cc) >> 7) + dv;
                out[i] = v;
            }
            r = out[0];
            g = out[1];
            b = out[2];
        }
        if (s.dthe && (s.fpsm == PSMCT16 || s.fpsm == PSMCT16S)) {
            // DIMX entries are signed 3-bit values in 4-bit slots, indexed in native GS coordinates.
            const unsigned shift = static_cast<unsigned>(((y & 3) * 4 + (x & 3)) * 4);
            const int dither = (static_cast<int>((s.dimx >> shift) & 7u) ^ 4) - 4;
            r += dither;
            g += dither;
            b += dither;
        }
        r = s.colclamp ? clamp255(r) : (r & 0xFF);
        g = s.colclamp ? clamp255(g) : (g & 0xFF);
        b = s.colclamp ? clamp255(b) : (b & 0xFF);
        std::uint32_t out_a = static_cast<std::uint32_t>(a & 0xFF);
        if (s.fba) out_a |= 0x80u;
        std::uint32_t mask = s.fbmsk;
        if (!write_alpha) mask |= 0xFF000000u;
        write_frame(s, x, y, static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) | (out_a << 24), mask);
    }
    if (write_z && s.zte) {
        if (s.zpsm == PSMZ24) {
            write_pixel(PSMZ24, s.zbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), z);
        } else {
            write_pixel(s.zpsm, s.zbp, s.fbw, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), z);
        }
    }
}

Setup make_setup(const State &st) {
    Setup s{};
    std::uint64_t attr = st.prim;
    if (!(st.prmodecont & 1u)) attr = (st.prim & 7u) | (st.prmode & ~7ull);
    s.iip = attr & 0x8u;
    s.tme = attr & 0x10u;
    s.fge = attr & 0x20u;
    s.abe = attr & 0x40u;
    s.fst = attr & 0x100u;
    const Context &c = st.ctx[(attr >> 9) & 1u];
    s.fbp = (static_cast<std::uint32_t>(c.frame) & 0x1FFu) * 32u;
    s.fbw = static_cast<std::uint32_t>(c.frame >> 16) & 0x3Fu;
    s.fpsm = static_cast<std::uint32_t>(c.frame >> 24) & 0x3Fu;
    s.fbmsk = static_cast<std::uint32_t>(c.frame >> 32);
    s.zbp = (static_cast<std::uint32_t>(c.zbuf) & 0x1FFu) * 32u;
    s.zpsm = 0x30u | (static_cast<std::uint32_t>(c.zbuf >> 24) & 0xFu);
    s.zmsk = (c.zbuf >> 32) & 1u;
    s.ate = c.test & 1u;
    s.atst = static_cast<int>(c.test >> 1) & 7;
    s.aref = static_cast<std::uint32_t>(c.test >> 4) & 0xFFu;
    s.afail = static_cast<int>(c.test >> 12) & 3;
    s.date = (c.test >> 14) & 1u;
    s.datm = (c.test >> 15) & 1u;
    s.zte = (c.test >> 16) & 1u;
    s.ztst = static_cast<int>(c.test >> 17) & 3;
    s.a = static_cast<int>(c.alpha) & 3;
    s.b = static_cast<int>(c.alpha >> 2) & 3;
    s.c = static_cast<int>(c.alpha >> 4) & 3;
    s.d = static_cast<int>(c.alpha >> 6) & 3;
    s.fix = static_cast<std::uint32_t>(c.alpha >> 32) & 0xFFu;
    s.pabe = st.pabe & 1u;
    s.colclamp = st.colclamp & 1u;
    s.dthe = st.dthe & 1u;
    s.dimx = st.dimx;
    s.fba = c.fba & 1u;
    s.scax0 = static_cast<int>(c.scissor & 0x7FFu);
    s.scax1 = static_cast<int>((c.scissor >> 16) & 0x7FFu);
    s.scay0 = static_cast<int>((c.scissor >> 32) & 0x7FFu);
    s.scay1 = static_cast<int>((c.scissor >> 48) & 0x7FFu);
    s.ofx = static_cast<int>(c.xyoffset & 0xFFFFu);
    s.ofy = static_cast<int>((c.xyoffset >> 32) & 0xFFFFu);
    s.tbp = static_cast<std::uint32_t>(c.tex0) & 0x3FFFu;
    s.tbw = static_cast<std::uint32_t>(c.tex0 >> 14) & 0x3Fu;
    s.tpsm = static_cast<std::uint32_t>(c.tex0 >> 20) & 0x3Fu;
    s.tw = std::min<std::uint32_t>(static_cast<std::uint32_t>(c.tex0 >> 26) & 0xFu, 10u);
    s.th = std::min<std::uint32_t>(static_cast<std::uint32_t>(c.tex0 >> 30) & 0xFu, 10u);
    s.tcc = (c.tex0 >> 34) & 1u;
    s.tfx = static_cast<int>(c.tex0 >> 35) & 3;
    s.cpsm = static_cast<std::uint32_t>(c.tex0 >> 51) & 0xFu;
    s.csa = static_cast<std::uint32_t>(c.tex0 >> 56) & 0x1Fu;
    s.bilinear = (c.tex1 >> 5) & 1u;
    s.tex1 = c.tex1;
    s.miptbp[0] = c.miptbp1;
    s.miptbp[1] = c.miptbp2;
    s.wms = static_cast<int>(c.clamp) & 3;
    s.wmt = static_cast<int>(c.clamp >> 2) & 3;
    s.minu = static_cast<std::uint32_t>(c.clamp >> 4) & 0x3FFu;
    s.maxu = static_cast<std::uint32_t>(c.clamp >> 14) & 0x3FFu;
    s.minv = static_cast<std::uint32_t>(c.clamp >> 24) & 0x3FFu;
    s.maxv = static_cast<std::uint32_t>(c.clamp >> 34) & 0x3FFu;
    s.ta0 = static_cast<std::uint32_t>(st.texa) & 0xFFu;
    s.aem = (st.texa >> 15) & 1u;
    s.ta1 = static_cast<std::uint32_t>(st.texa >> 32) & 0xFFu;
    s.fogcol = static_cast<std::uint32_t>(st.fogcol) & 0xFFFFFFu;
    s.scanmsk = static_cast<int>(st.scanmsk) & 3;
    s.clut = st.clut;
    return s;
}

Attr attr_of(const Setup &s, const Vertex &v) {
    Attr a;
    a.r = v.r;
    a.g = v.g;
    a.b = v.b;
    a.a = v.a;
    if (s.fst) {
        a.s = static_cast<float>(v.u);
        a.t = static_cast<float>(v.v);
        a.q = 1.0f;
    } else {
        a.s = v.s;
        a.t = v.t;
        a.q = v.q;
    }
    a.f = v.f;
    a.z = static_cast<double>(v.z);
    return a;
}

Attr lerp3(const Attr &a, const Attr &b, const Attr &c, float wa, float wb, float wc) {
    Attr r;
    r.r = a.r * wa + b.r * wb + c.r * wc;
    r.g = a.g * wa + b.g * wb + c.g * wc;
    r.b = a.b * wa + b.b * wb + c.b * wc;
    r.a = a.a * wa + b.a * wb + c.a * wc;
    r.s = a.s * wa + b.s * wb + c.s * wc;
    r.t = a.t * wa + b.t * wb + c.t * wc;
    r.q = a.q * wa + b.q * wb + c.q * wc;
    r.f = a.f * wa + b.f * wb + c.f * wc;
    r.z = a.z * wa + b.z * wb + c.z * wc;
    return r;
}

void triangle(const Setup &s, const Vertex *in) {
    // Interpolate from a fixed vertex order: games draw the same triangle again in another order for
    // extra passes with ZTST GEQUAL, and order-dependent float rounding would fail scattered pixels.
    Vertex sorted[3] = {in[0], in[1], in[2]};
    const auto before = [](const Vertex &a, const Vertex &b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
    if (before(sorted[1], sorted[0])) std::swap(sorted[0], sorted[1]);
    if (before(sorted[2], sorted[1])) std::swap(sorted[1], sorted[2]);
    if (before(sorted[1], sorted[0])) std::swap(sorted[0], sorted[1]);
    const Vertex *v = sorted;
    const float x0 = (static_cast<float>(v[0].x) - static_cast<float>(s.ofx)) / 16.0f, y0 = (static_cast<float>(v[0].y) - static_cast<float>(s.ofy)) / 16.0f;
    const float x1 = (static_cast<float>(v[1].x) - static_cast<float>(s.ofx)) / 16.0f, y1 = (static_cast<float>(v[1].y) - static_cast<float>(s.ofy)) / 16.0f;
    const float x2 = (static_cast<float>(v[2].x) - static_cast<float>(s.ofx)) / 16.0f, y2 = (static_cast<float>(v[2].y) - static_cast<float>(s.ofy)) / 16.0f;
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0.0f) return;
    Attr a0 = attr_of(s, v[0]), a1 = attr_of(s, v[1]), a2 = attr_of(s, v[2]);
    if (!s.iip) {
        const Attr flat = attr_of(s, in[2]);
        a0.r = a1.r = a2.r = flat.r;
        a0.g = a1.g = a2.g = flat.g;
        a0.b = a1.b = a2.b = flat.b;
        a0.a = a1.a = a2.a = flat.a;
    }
    const int minx = std::max(s.scax0, static_cast<int>(std::ceil(std::min({x0, x1, x2}))));
    const int maxx = std::min(s.scax1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))) - 1);
    const int miny = std::max(s.scay0, static_cast<int>(std::ceil(std::min({y0, y1, y2}))));
    const int maxy = std::min(s.scay1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))) - 1);
    if (minx > maxx || miny > maxy) return;
    const float inv = 1.0f / area;
    const float sg = area > 0.0f ? 1.0f : -1.0f;
    const auto edge_in = [](float e, float gx, float gy) { return e > 0.0f || (e == 0.0f && (gx > 0.0f || (gx == 0.0f && gy > 0.0f))); };
    for (int y = miny; y <= maxy; ++y) {
        const float py = static_cast<float>(y);
        for (int x = minx; x <= maxx; ++x) {
            const float px = static_cast<float>(x);
            const float e0 = (x1 - px) * (y2 - py) - (x2 - px) * (y1 - py);
            const float e1 = (x2 - px) * (y0 - py) - (x0 - px) * (y2 - py);
            const float e2 = (x0 - px) * (y1 - py) - (x1 - px) * (y0 - py);
            if (!edge_in(e0 * sg, (y1 - y2) * sg, (x2 - x1) * sg) || !edge_in(e1 * sg, (y2 - y0) * sg, (x0 - x2) * sg) ||
                !edge_in(e2 * sg, (y0 - y1) * sg, (x1 - x0) * sg))
                continue;
            const float w0 = e0 * inv;
            const float w1 = e1 * inv;
            const float w2 = 1.0f - w0 - w1;
            plot(s, x, y, lerp3(a0, a1, a2, w0, w1, w2));
        }
    }
}

void sprite(const Setup &s, const Vertex *v) {
    const Vertex &p = v[0], &q = v[1];
    const float x0 = (static_cast<float>(p.x) - static_cast<float>(s.ofx)) / 16.0f, y0 = (static_cast<float>(p.y) - static_cast<float>(s.ofy)) / 16.0f;
    const float x1 = (static_cast<float>(q.x) - static_cast<float>(s.ofx)) / 16.0f, y1 = (static_cast<float>(q.y) - static_cast<float>(s.ofy)) / 16.0f;
    const Attr a = attr_of(s, p), b = attr_of(s, q);
    const float left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
    const int minx = std::max(s.scax0, static_cast<int>(std::ceil(left)));
    const int maxx = std::min(s.scax1, static_cast<int>(std::ceil(right)) - 1);
    const int miny = std::max(s.scay0, static_cast<int>(std::ceil(top)));
    const int maxy = std::min(s.scay1, static_cast<int>(std::ceil(bottom)) - 1);
    if (minx > maxx || miny > maxy) return;
    const float dx = x1 - x0, dy = y1 - y0;
    Attr at = b;
    for (int y = miny; y <= maxy; ++y) {
        const float ty = dy != 0.0f ? (static_cast<float>(y) - y0) / dy : 0.0f;
        at.t = a.t + (b.t - a.t) * ty;
        for (int x = minx; x <= maxx; ++x) {
            const float tx = dx != 0.0f ? (static_cast<float>(x) - x0) / dx : 0.0f;
            at.s = a.s + (b.s - a.s) * tx;
            at.q = s.fst ? 1.0f : b.q;
            plot(s, x, y, at);
        }
    }
}

void line(const Setup &s, const Vertex *v) {
    const float x0 = (static_cast<float>(v[0].x) - static_cast<float>(s.ofx)) / 16.0f, y0 = (static_cast<float>(v[0].y) - static_cast<float>(s.ofy)) / 16.0f;
    const float x1 = (static_cast<float>(v[1].x) - static_cast<float>(s.ofx)) / 16.0f, y1 = (static_cast<float>(v[1].y) - static_cast<float>(s.ofy)) / 16.0f;
    Attr a = attr_of(s, v[0]), b = attr_of(s, v[1]);
    if (!s.iip) a = Attr{b.r, b.g, b.b, b.a, a.s, a.t, a.q, a.f, a.z};
    const int steps = static_cast<int>(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)));
    for (int i = 0; i < std::max(steps, 1); ++i) {
        const float t = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0.0f;
        plot(s, static_cast<int>(std::lround(x0 + (x1 - x0) * t)), static_cast<int>(std::lround(y0 + (y1 - y0) * t)),
             lerp3(a, b, b, 1.0f - t, t, 0.0f));
    }
}

}

void reset() {}

void draw(const State &st, const Vertex *v, int count) {
    const Setup s = make_setup(st);
    static const char *texsum = std::getenv("PS2_TRACE_TEXSUM");
    if (texsum) {
        struct Use { std::uint64_t n; int x0, y0, x1, y1; };
        static std::map<std::tuple<int, int, int, int, int, int, int, int>, Use> *uses = nullptr;
        static const std::uint64_t frame = std::strtoull(texsum, nullptr, 10);
        if (!uses) {
            uses = new std::map<std::tuple<int, int, int, int, int, int, int, int>, Use>;
            std::atexit([] {
                for (const auto &[k, u] : *uses)
                    std::fprintf(stderr, "[tex] tme %d tbp %X tbw %d tpsm %X tw %d th %d cpsm %X fbp %X prims %llu box %d,%d-%d,%d\n", std::get<0>(k),
                                 std::get<1>(k), std::get<2>(k), std::get<3>(k), std::get<4>(k), std::get<5>(k), std::get<6>(k), std::get<7>(k),
                                 static_cast<unsigned long long>(u.n), u.x0, u.y0, u.x1, u.y1);
            });
        }
        if (frames_run() == frame) {
            Use &u = (*uses)[{s.tme, static_cast<int>(s.tbp), static_cast<int>(s.tbw), static_cast<int>(s.tpsm), static_cast<int>(s.tw),
                              static_cast<int>(s.th), static_cast<int>(s.cpsm), static_cast<int>(s.fbp)}];
            if (!u.n) u = {0, 99999, 99999, -99999, -99999};
            ++u.n;
            for (int i = 0; i < count; ++i) {
                const int x = (static_cast<int>(v[i].x) - static_cast<int>(s.ofx)) / 16, y = (static_cast<int>(v[i].y) - static_cast<int>(s.ofy)) / 16;
                u.x0 = std::min(u.x0, x); u.y0 = std::min(u.y0, y); u.x1 = std::max(u.x1, x); u.y1 = std::max(u.y1, y);
            }
        }
    }
    static const char *skip = std::getenv("PS2_SKIP_TBP");
    if (skip && s.tme && s.tbp == std::strtoul(skip, nullptr, 16)) return;
    static const char *trace = std::getenv("PS2_TRACE_PRIM");
    if (trace) {
        static const bool by_frame = trace[0] == 'f';
        static std::uint64_t n = 0, start = std::strtoull(trace + (by_frame ? 1 : 0), nullptr, 10);
        static const char *only = std::getenv("PS2_TRACE_PRIM_BP");
        static const char *type = std::getenv("PS2_TRACE_PRIM_TYPE");
        const bool wanted = (!only || s.fbp == std::strtoul(only, nullptr, 16) || (s.tme && s.tbp == std::strtoul(only, nullptr, 16))) &&
                            (!type || (st.prim & 7u) == std::strtoul(type, nullptr, 10));
        if (wanted && (by_frame ? frames_run() == start && ++n <= 1000000 : ++n >= start && n < start + 1000)) {
            std::fprintf(stderr,
                         "[prim] %llu type %u count %d fbp %X fbw %u fpsm %X tme %d tbp %X tbw %u tpsm %X tw %u th %u tcc %d tfx %d cpsm %X csa %u\n",
                         static_cast<unsigned long long>(n), static_cast<unsigned>(st.prim & 7u), count, s.fbp, s.fbw, s.fpsm, s.tme,
                         s.tbp, s.tbw, s.tpsm, s.tw, s.th, s.tcc, s.tfx, s.cpsm, s.csa);
            std::fprintf(stderr,
                         "[prim] %llu abe %d fst %d zte %d ztst %d afail %d fbmsk %08X scissor %d %d %d %d ofs %d %d\n",
                         static_cast<unsigned long long>(n), s.abe, s.fst, s.zte, s.ztst, s.afail, s.fbmsk,
                         s.scax0, s.scax1, s.scay0, s.scay1, s.ofx, s.ofy);
            const Context &context = st.ctx[(st.prmodecont & 1u ? st.prim : st.prmode) >> 9 & 1u];
            std::fprintf(stderr, "[prim] %llu tex1 %016llX mip1 %016llX mip2 %016llX bilinear %d\n",
                         static_cast<unsigned long long>(n), static_cast<unsigned long long>(context.tex1),
                         static_cast<unsigned long long>(context.miptbp1), static_cast<unsigned long long>(context.miptbp2), s.bilinear);
            for (int i = 0; i < count; ++i)
                std::fprintf(stderr, "[prim] %llu v%d %08X %08X z %08X st %.6f %.6f q %.6f rgba %02X%02X%02X%02X uv %04X %04X\n",
                             static_cast<unsigned long long>(n), i, v[i].x, v[i].y, v[i].z, v[i].s, v[i].t, v[i].q,
                             v[i].r, v[i].g, v[i].b, v[i].a, v[i].u, v[i].v);
        }
    }
    switch (st.prim & 7u) {
    case 0: {
        const Attr a = attr_of(s, v[0]);
        plot(s, static_cast<int>((static_cast<float>(v[0].x) - static_cast<float>(s.ofx)) / 16.0f),
             static_cast<int>((static_cast<float>(v[0].y) - static_cast<float>(s.ofy)) / 16.0f), a);
        break;
    }
    case 1: case 2: line(s, v); break;
    case 3: case 4: case 5: if (count >= 3) triangle(s, v); break;
    case 6: sprite(s, v); break;
    default: break;
    }
}

}
