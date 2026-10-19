// Breve dimostrazione: accordi con percussione, manuale inferiore, basso, poi vibrato e tremolo.
// c++ -O2 -std=c++17 -Isrc tests/render_demo.cpp -o render_demo && ./render_demo demo.wav
#include "dsp/T2Engine.h"
#include <cstdio>
#include <string>

using namespace t2;

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "demo.wav";
    const int sr = 48000;
    Engine e; e.prepare (sr);
    Params p;
    for (auto& d : p.umDraw) d = 0;
    p.umDraw[0] = 8; p.umDraw[2] = 8; p.umDraw[4] = 4;          // 16' 8' 2⅔'
    p.perc[3] = true; p.percVol = 0.8f; p.percDecay = 0.25f;     // percussione 4'
    p.lmDraw[2] = 8; p.lmDraw[3] = 5;
    p.bassMode = 1; p.bassDecay = 0.45f; p.bassRange = 0.55f; p.bassRes = 0.35f; p.bassVolume = 0.3f;
    p.master = 0.8f;
    e.setParams (p);

    std::vector<float> y;
    auto run = [&] (double s) { std::vector<float> b ((size_t) (s * sr)); e.process (b.data(), (int) b.size()); y.insert (y.end(), b.begin(), b.end()); };
    // progressione: (basso, accordo inferiore, accordo superiore) in tasti dal Do2
    struct Step { int bass; int lo[3]; int up[3]; };
    const Step prog[4] = { { 9, { 21, 24, 28 }, { 33, 36, 40 } },     // La minore
                           { 5, { 21, 24, 29 }, { 33, 36, 41 } },     // Fa
                           { 7, { 19, 23, 26 }, { 31, 35, 38 } },     // Sol
                           { 4, { 20, 23, 28 }, { 32, 35, 40 } } };   // Mi
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass == 1) { p.vibrato = 0.6f; p.tremolo = 0.5f; p.perc[3] = false; p.umDraw[3] = 6; p.k1 = true; e.setParams (p); }
        for (const auto& s : prog)
        {
            e.keyOn (Lower, s.bass);
            for (int k : s.lo) e.keyOn (Lower, k);
            for (int k : s.up) e.keyOn (Upper, k);
            run (0.9);
            for (int k : s.up) e.keyOff (Upper, k);
            run (0.15);
            e.keyOn (Upper, s.up[2] + 3); run (0.35); e.keyOff (Upper, s.up[2] + 3);
            e.keyOff (Lower, s.bass);
            for (int k : s.lo) e.keyOff (Lower, k);
            run (0.1);
        }
    }
    run (1.0);

    FILE* f = std::fopen (out.c_str(), "wb");
    if (! f) return 1;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + (uint32_t) y.size() * 2); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 ((uint32_t) y.size() * 2);
    float pk = 0;
    for (float s : y) { pk = std::max (pk, std::abs (s)); const int v = (int) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f); w16 ((uint16_t) (int16_t) v); }
    std::fclose (f);
    std::printf ("%s: %.1f s, picco %.2f\n", out.c_str(), (double) y.size() / sr, pk);
    return 0;
}
