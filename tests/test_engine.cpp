// Offline test: fake sprite catalog, PPM in -> PPM out + object string.
#include "../src/evolve.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace gdevo;
static Sprite make(int id, int kind) {
    Sprite s; s.id = id; s.n = 32; s.box = 30; s.alpha.assign(1024, 0); s.gray.assign(1024, 255);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        float u = (x + .5f) / 32 * 2 - 1, v = (y + .5f) / 32 * 2 - 1; bool in = false; int g = 255;
        switch (kind) {
            case 0: in = true; break;
            case 1: in = u*u + v*v <= 1; break;
            case 2: in = v >= -1 && v <= 1 && std::fabs(u) <= (v + 1) / 2; break;
            case 3: in = u*u + v*v <= 1 && u*u + v*v >= .35f; break;
            case 4: in = u*u + v*v <= 1 && v >= 0; break;
            case 5: in = true; g = (int)(140 + 115 * (u + 1) / 2); break;
        }
        s.alpha[y*32+x] = in ? 255 : 0; s.gray[y*32+x] = g;
    }
    return s;
}
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary); std::string m; int w, h, mx; f >> m >> w >> h >> mx; f.get();
    std::vector<unsigned char> raw(w*h*3); f.read((char*)raw.data(), raw.size());
    std::vector<float> t(w*h*3); for (size_t i = 0; i < t.size(); i++) t[i] = raw[i] / 255.f;
    std::vector<Sprite> cat; for (int k = 0; k < 6; k++) cat.push_back(make(100 + k, k));
    int N = atoi(argv[3]); float units = 600; Evolver ev(cat, w, h, t, w / units, 1);
    auto t0 = std::chrono::steady_clock::now();
    ev.addBackground();
    for (int tries = 0; (int)ev.shapes.size() < N && tries < N * 4; tries++) {
        ev.addShape(40, 80, ev.shapes.size() / (float)N);
        if (ev.shapes.size() % 200 == 0 && tries % 1 == 0) fprintf(stderr, "%zu mse=%.5f\n", ev.shapes.size(), ev.mse());
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%zu objects, mse=%.5f, %.1fs\n", ev.shapes.size(), ev.mse(), secs);
    std::ofstream o(argv[2], std::ios::binary); o << "P6\n" << w << " " << h << "\n255\n";
    for (float c : ev.canvas()) o.put((char)std::lround(std::min(1.f, std::max(0.f, c)) * 255));
    std::string s = ev.toObjectString(15, 15); printf("%s\n", s.substr(0, 330).c_str());
}
