// Geode-free evolution engine. Shapes are real GD sprites (from a catalog the
// mod renders in-game). Greedy: each new object goes on top of the stack, so
// the creation order IS the z order.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gdevo {

struct Sprite {
    int id = 0;
    int n = 0;                   // catalog image is n x n (square box)
    float box = 30.f;            // box side in GD units at scale 1
    std::vector<uint8_t> alpha;  // n*n
    std::vector<uint8_t> gray;   // n*n, un-premultiplied luminance
};

struct Shape {
    int spr = 0;
    float cx = 0, cy = 0;        // image px, y down
    float sx = 1, sy = 1;        // GD object scale
    float rot = 0;               // radians, clockwise
    int hue = 0;                 // -180..180
    float sat = 0, val = 1;      // 0..1
};

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 1) : s(seed * 2685821657736338717ULL + 88172645463325252ULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uni() { return (next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uni(); }
    int below(int n) { return (int)(next() % (uint64_t)n); }
    float gauss() {
        float u = std::max(1e-7f, uni()), v = uni();
        return std::sqrt(-2.f * std::log(u)) * std::cos(6.2831853f * v);
    }
};

inline void rgb2hsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    v = mx; s = mx <= 0 ? 0 : d / mx;
    if (d <= 1e-6f) h = 0;
    else if (mx == r) h = std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60; if (h < 0) h += 360;
}
inline void hsv2rgb(float h, float s, float v, float* o) {
    h = std::fmod(h, 360.f); if (h < 0) h += 360;
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2.f) - 1)), m = v - c, r, g, b;
    switch ((int)(h / 60)) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x;
    }
    o[0] = r + m; o[1] = g + m; o[2] = b + m;
}

class Evolver {
public:
    std::vector<Shape> shapes;

    // target: w*h*3 floats 0..1. unitPx = image pixels per GD unit.
    Evolver(const std::vector<Sprite>& cat, int w, int h, std::vector<float> target,
            float unitPx, uint64_t seed = 1)
        : cat_(cat), W(w), H(h), T(std::move(target)), unitPx_(unitPx), rng_(seed) {
        cur_.assign((size_t)W * H * 3, 0.f);
        err_.resize((size_t)W * H);
        for (size_t i = 0; i < err_.size(); i++) err_[i] = pix(i);
    }

    const std::vector<float>& canvas() const { return cur_; }

    float mse() const {
        double s = 0; for (float e : err_) s += e;
        return (float)(s / (double)err_.size() / 3.0);
    }

    // First object: the most "solid" sprite stretched over the whole image.
    bool addBackground() {
        int best = -1; double bf = -1;
        for (size_t i = 0; i < cat_.size(); i++) {
            double f = 0; for (uint8_t a : cat_[i].alpha) f += a;
            f *= cat_[i].gray.empty() ? 0 : 1;
            if (f > bf) { bf = f; best = (int)i; }
        }
        if (best < 0) return false;
        Shape s; s.spr = best; s.cx = W / 2.f; s.cy = H / 2.f;
        float need = std::max(W, H) * 1.6f / (cat_[best].box * unitPx_);
        s.sx = s.sy = need;
        float d; if (!evaluate(s, d)) return false;
        commit(s);
        return true;
    }

    // Try `cands` random objects, hill-climb the best `climbs` times, keep it
    // if it lowers the error. progress 0..1 shrinks sizes over time.
    bool addShape(int cands, int climbs, float progress) {
        Shape best; float bd = 1e30f; bool have = false;
        for (int i = 0; i < cands; i++) {
            Shape s = randomShape(progress); float d;
            if (evaluate(s, d) && d < bd) { bd = d; best = s; have = true; }
        }
        if (!have) return false;
        for (int j = 0; j < climbs; j++) {
            Shape m = mutate(best, progress); float d;
            if (evaluate(m, d) && d < bd) { bd = d; best = m; }
        }
        if (bd >= 0) return false;
        commit(best);
        return true;
    }

    // GD level objects. White channel (1011) + HSV: hue shift, ADDITIVE
    // saturation (checkbox on), brightness multiplier.
    std::string toObjectString(float ox, float oy) const {
        std::string out; char buf[400];
        static const int layers[7] = {-3, -1, 1, 3, 5, 7, 9}; // B4..T3
        int n = (int)shapes.size(), per = std::max(1, (n + 6) / 7);
        for (int i = 0; i < n; i++) {
            const Shape& s = shapes[i]; const Sprite& sp = cat_[s.spr];
            int layer = layers[std::min(6, i / per)], z = i % per;
            std::snprintf(buf, sizeof buf,
                "1,%d,2,%.2f,3,%.2f,6,%.2f,128,%.3f,129,%.3f,21,1011,22,1011,41,1,42,1,"
                "43,%da%.2fa%.2fa1a0,44,%da%.2fa%.2fa1a0,24,%d,25,%d;",
                sp.id, ox + s.cx / unitPx_, oy + (H - s.cy) / unitPx_, std::fmod(s.rot * 57.29578f + 360.f, 360.f),
                s.sx, s.sy, s.hue, s.sat, s.val, s.hue, s.sat, s.val, layer, z);
            out += buf;
        }
        return out;
    }

private:
    const std::vector<Sprite>& cat_;
    int W, H;
    std::vector<float> T, cur_, err_;
    float unitPx_;
    Rng rng_;

    float pix(size_t i) const {
        float d0 = T[i*3] - cur_[i*3], d1 = T[i*3+1] - cur_[i*3+1], d2 = T[i*3+2] - cur_[i*3+2];
        return d0*d0 + d1*d1 + d2*d2;
    }

    struct Box { int x0, x1, y0, y1; };
    bool bounds(const Shape& s, Box& b) const {
        const Sprite& sp = cat_[s.spr];
        float wx = sp.box * s.sx * unitPx_, wy = sp.box * s.sy * unitPx_;
        if (wx < 1.5f || wy < 1.5f) return false;
        float r = 0.5f * std::hypot(wx, wy);
        b.x0 = std::max(0, (int)std::floor(s.cx - r)); b.x1 = std::min(W, (int)std::ceil(s.cx + r) + 1);
        b.y0 = std::max(0, (int)std::floor(s.cy - r)); b.y1 = std::min(H, (int)std::ceil(s.cy + r) + 1);
        return b.x1 > b.x0 && b.y1 > b.y0;
    }

    template <class F> void forPixels(const Shape& s, const Box& b, F f) const {
        const Sprite& sp = cat_[s.spr];
        float wx = sp.box * s.sx * unitPx_, wy = sp.box * s.sy * unitPx_;
        float c = std::cos(s.rot), sn = std::sin(s.rot);
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                float dx = x + 0.5f - s.cx, dy = y + 0.5f - s.cy;
                float u = dx * c + dy * sn, v = -dx * sn + dy * c;
                float tx = u / wx + 0.5f, ty = v / wy + 0.5f;
                if (tx < 0 || tx >= 1 || ty < 0 || ty >= 1) continue;
                int idx = (int)(ty * sp.n) * sp.n + (int)(tx * sp.n);
                float a = sp.alpha[idx] * (1.f / 255.f);
                if (a < 0.03f) continue;
                f(y * W + x, a, sp.gray[idx] * (1.f / 255.f));
            }
    }

    // Best colour is solved exactly (least squares), then snapped to what GD
    // can store (integer hue, 0.01 steps).
    bool solve(Shape& s, float* cq) const {
        Box b; if (!bounds(s, b)) return false;
        double num[3] = {0, 0, 0}, den = 0; int cnt = 0;
        forPixels(s, b, [&](int i, float a, float g) {
            float ag = a * g; den += (double)ag * ag; cnt++;
            for (int c = 0; c < 3; c++) num[c] += ag * (T[i*3+c] - (1 - a) * cur_[i*3+c]);
        });
        if (cnt < 3 || den < 1e-6) return false;
        float C[3];
        for (int c = 0; c < 3; c++) C[c] = (float)std::min(1.0, std::max(0.0, num[c] / den));
        float h, sa, v; rgb2hsv(C[0], C[1], C[2], h, sa, v);
        int hi = (int)std::lround(h) % 360;
        s.hue = hi > 180 ? hi - 360 : hi;
        s.sat = std::round(sa * 100.f) / 100.f; s.val = std::round(v * 100.f) / 100.f;
        hsv2rgb((float)hi, s.sat, s.val, cq);
        return true;
    }

    bool evaluate(Shape& s, float& delta) const {
        float cq[3]; if (!solve(s, cq)) return false;
        Box b{}; bounds(s, b);
        double d = 0;
        forPixels(s, b, [&](int i, float a, float g) {
            for (int c = 0; c < 3; c++) {
                float nw = (1 - a) * cur_[i*3+c] + a * g * cq[c];
                float e1 = T[i*3+c] - nw, e0 = T[i*3+c] - cur_[i*3+c];
                d += e1 * e1 - e0 * e0;
            }
        });
        delta = (float)d; return true;
    }

    void commit(Shape s) {
        float cq[3]; if (!solve(s, cq)) return;
        Box b{}; bounds(s, b);
        forPixels(s, b, [&](int i, float a, float g) {
            for (int c = 0; c < 3; c++) cur_[i*3+c] = (1 - a) * cur_[i*3+c] + a * g * cq[c];
            err_[i] = pix(i);
        });
        shapes.push_back(s);
    }

    Shape randomShape(float p) {
        Shape s; s.spr = rng_.below((int)cat_.size());
        // aim at where the picture is still wrong
        int bx = 0, by = 0; float be = -1;
        for (int k = 0; k < 8; k++) {
            int x = rng_.below(W), y = rng_.below(H); float e = err_[(size_t)y * W + x];
            if (e > be) { be = e; bx = x; by = y; }
        }
        s.cx = bx + rng_.uni(); s.cy = by + rng_.uni();
        float mx = (float)std::max(W, H);
        float hi = mx * (0.6f * (1 - p) * (1 - p) + 0.05f), lo = std::max(2.f, mx * 0.015f);
        float px = std::exp(rng_.range(std::log(lo), std::log(std::max(lo + 1, hi))));
        const Sprite& sp = cat_[s.spr];
        s.sx = px / (sp.box * unitPx_);
        s.sy = s.sx * std::exp(rng_.gauss() * 0.4f);
        s.rot = rng_.range(0.f, 6.2831853f);
        return s;
    }

    Shape mutate(Shape s, float p) {
        float mx = (float)std::max(W, H);
        float step = std::max(1.f, mx * 0.1f * (1 - p));
        switch (rng_.below(7)) {
            case 0: case 1: s.cx += rng_.gauss() * step; s.cy += rng_.gauss() * step; break;
            case 2: s.sx *= std::exp(rng_.gauss() * 0.15f); break;
            case 3: s.sy *= std::exp(rng_.gauss() * 0.15f); break;
            case 4: { float f = std::exp(rng_.gauss() * 0.15f); s.sx *= f; s.sy *= f; } break;
            case 5: s.rot += rng_.gauss() * 0.35f; break;
            default: if (rng_.uni() < 0.5f) {  // swap to another object, keep footprint
                const Sprite& o = cat_[s.spr];
                s.spr = rng_.below((int)cat_.size());
                float k = o.box / cat_[s.spr].box; s.sx *= k; s.sy *= k;
            }
        }
        return s;
    }
};

}  // namespace gdevo
