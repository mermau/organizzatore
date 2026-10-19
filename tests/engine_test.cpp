// Test del motore: intonazione, livelli lungo la tastiera, percussione, bassi, stabilità e CPU.
// c++ -O2 -std=c++17 -Isrc tests/engine_test.cpp -o engine_test && ./engine_test [cartella_wav]
#include "dsp/T2Engine.h"
#include <chrono>
#include <cstdio>
#include <complex>
#include <string>

using namespace t2;

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + (uint32_t) x.size() * 2); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 ((uint32_t) x.size() * 2);
    for (float s : x) { const int v = (int) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f); w16 ((uint16_t) (int16_t) v); }
    std::fclose (f);
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}
static double peak (const std::vector<float>& x) { double m = 0; for (float v : x) m = std::max (m, (double) std::abs (v)); return m; }
static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// ampiezza di una componente (Goertzel con finestra di Hann)
static double tone (const std::vector<float>& x, size_t a, size_t n, double f, double sr)
{
    std::complex<double> acc = 0; double wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) (n - 1));
        acc += w * (double) x[a + i] * std::polar (1.0, -2 * M_PI * f * (double) i / sr);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

struct Render
{
    Engine e; Params p; double sr;
    explicit Render (double s = 96000.0) : sr (s) { e.prepare (sr); e.setParams (p); }
    std::vector<float> run (double seconds)
    {
        std::vector<float> y ((size_t) (seconds * sr));
        e.setParams (p);
        e.process (y.data(), (int) y.size());
        return y;
    }
};

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "";
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "ok" : "ERRORE", what); if (! ok) ++fails; };

    // ------------------------------------------------------------ intonazione del generatore
    std::printf ("Intonazione MK50240 a 2,00024 MHz (scarto dal temperamento equabile, La4 = 440 Hz):\n");
    double worst = 0;
    for (int q = 0; q < kQ; ++q)
    {
        const double ideal = 440.0 * std::pow (2.0, (q - 45) / 12.0);
        const double c = 1200.0 * std::log2 (Engine::noteHz (q) / ideal);
        worst = std::max (worst, std::abs (c));
        if (q % 12 == 0 || q == 45) std::printf ("   q=%2d  %8.2f Hz  %+5.2f cent\n", q, Engine::noteHz (q), c);
    }
    check (worst < 3.0, "tutte le note entro 3 cent");

    // ------------------------------------------------------------ livello dell'8' lungo la tastiera
    std::printf ("\nLivello di un 8' (drawbar 8) lungo il manuale superiore, fondamentale e 3a armonica:\n");
    double lmin = 1e9, lmax = -1e9;
    for (int key = 0; key < kKeys; key += 6)
    {
        Render r; for (auto& d : r.p.umDraw) d = 0; r.p.umDraw[2] = 8;
        r.e.setParams (r.p); r.e.keyOn (Upper, key);
        auto y = r.run (0.6);
        const double f = Engine::noteHz (key + 12);
        const size_t n = (size_t) (0.3 * r.sr);
        const double h1 = tone (y, (size_t) (0.25 * r.sr), n, f, r.sr), h3 = tone (y, (size_t) (0.25 * r.sr), n, 3 * f, r.sr);
        lmin = std::min (lmin, db (h1)); lmax = std::max (lmax, db (h1));
        std::printf ("   tasto %2d %7.1f Hz  fond. %6.1f dB   3a %6.1f dB\n", key, f, db (h1), db (h3) - db (h1));
    }
    check (lmax - lmin < 12.0, "livello dell'8' uniforme entro 12 dB su tutta la tastiera");

    // ------------------------------------------------------------ registrazione piena e livelli
    {
        Render r; for (auto& d : r.p.umDraw) d = 8;
        r.e.setParams (r.p);
        for (int k : { 12, 16, 19, 24 }) r.e.keyOn (Upper, k);
        auto y = r.run (1.0);
        std::printf ("\nAccordo con tutti i drawbar a 8: picco %.3f, RMS %.1f dBFS\n", peak (y), db (rms (y, 20000, y.size())));
        check (peak (y) < 1.0, "accordo pieno senza superare 0 dBFS");
        if (! dir.empty()) writeWav (dir + "/accordo_pieno.wav", y, 96000);
    }
    {
        Render r; r.e.keyOn (Upper, 24); auto y = r.run (0.5);
        const double g0 = rms (y, 0, (size_t) (0.009 * r.sr)), g1 = rms (y, (size_t) (0.05 * r.sr), (size_t) (0.4 * r.sr));
        std::printf ("\n888 su Do4: RMS %.1f dBFS; nei primi 9 ms (gate) %.1f dBFS\n", db (g1), db (g0));
        check (g0 < g1 * 0.2, "il GATE PERCUSSIONE ammutolisce i primi 10 ms");
        if (! dir.empty()) writeWav (dir + "/888_do4.wav", y, 96000);
    }

    // ------------------------------------------------------------ percussione
    {
        Render r; for (auto& d : r.p.umDraw) d = 0; r.p.umDraw[2] = 8; r.p.perc[3] = true; r.p.percVol = 1.0f; r.p.percDecay = 0.3f;
        r.e.setParams (r.p); r.e.keyOn (Upper, 24);
        auto y = r.run (1.5);
        const double tau = (33e3 + 220e3 * 0.3) * 2.2e-6;
        const double a = tone (y, (size_t) (0.02 * r.sr), 4096, Engine::noteHz (48), r.sr);
        const double b = tone (y, (size_t) ((0.02 + tau) * r.sr), 4096, Engine::noteHz (48), r.sr);
        std::printf ("\nPercussione 4' (tau %.0f ms): dopo tau il 4' scende di %.1f dB (atteso ≈ −8,7)\n", tau * 1000, db (b) - db (a));
        check (db (b) - db (a) < -5.0 && db (b) - db (a) > -12.0, "decadimento della percussione");
        // secondo tasto in legato: nessun nuovo attacco (singolo trigger)
        r.e.keyOn (Upper, 28);
        auto y2 = r.run (0.3);
        const double c = tone (y2, 0, 4096, Engine::noteHz (52), r.sr);
        std::printf ("   legato: 4' della seconda nota %.1f dB rispetto al primo attacco\n", db (c) - db (a));
        check (db (c) - db (a) < -10.0, "percussione a trigger singolo (come l'originale)");
        if (! dir.empty()) { y.insert (y.end(), y2.begin(), y2.end()); writeWav (dir + "/percussione_4.wav", y, 96000); }
    }

    // ------------------------------------------------------------ sustain
    {
        Render r; r.p.sustOn = true; r.p.sustLength = 0.6f; r.e.setParams (r.p);
        r.e.keyOn (Upper, 24); auto y = r.run (0.3); r.e.keyOff (Upper, 24); auto y2 = r.run (3.0);
        size_t end = 0; for (size_t i = 0; i < y2.size(); ++i) if (std::abs (y2[i]) > 1e-4) end = i;
        std::printf ("\nSustain (lunghezza 6): la nota si spegne dopo %.2f s\n", (double) end / r.sr);
        check (end > (size_t) (0.5 * r.sr), "il sustain prolunga la nota");
        Render s; s.e.keyOn (Upper, 24); s.run (0.3); s.e.keyOff (Upper, 24); auto z = s.run (0.5);
        size_t e2 = 0; for (size_t i = 0; i < z.size(); ++i) if (std::abs (z[i]) > 1e-4) e2 = i;
        std::printf ("   senza sustain: rilascio in %.1f ms\n", (double) e2 / s.sr * 1000.0);
        if (! dir.empty()) { y.insert (y.end(), y2.begin(), y2.end()); writeWav (dir + "/sustain.wav", y, 96000); }
    }

    // ------------------------------------------------------------ bassi
    std::printf ("\nBassi (manuale inferiore, Do2 = tasto 0):\n");
    for (int mode : { 1, 2 })
        for (float res : { 0.0f, 0.6f, 1.0f })
        {
            Render r; r.p.bassMode = mode; r.p.bassRes = res; r.p.bassRange = 0.6f; r.p.bassDecay = 0.4f;
            for (auto& d : r.p.lmDraw) d = 0; for (auto& d : r.p.umDraw) d = 0;
            r.e.setParams (r.p); r.e.keyOn (Lower, 0);
            auto y = r.run (1.0);
            std::printf ("   %s ris %.1f: picco %.3f, RMS 0,1-0,3 s %.1f dBFS, 0,7-1 s %.1f dBFS\n", mode == 1 ? "PERC" : "SUST", res,
                         peak (y), db (rms (y, (size_t) (0.1 * r.sr), (size_t) (0.3 * r.sr))), db (rms (y, (size_t) (0.7 * r.sr), y.size())));
            if (! dir.empty()) writeWav (dir + "/basso_" + (mode == 1 ? "perc" : "sust") + "_ris" + std::to_string ((int) (res * 10)) + ".wav", y, 96000);
            check (std::isfinite (peak (y)) && peak (y) < 1.0, "basso stabile");
        }

    // ------------------------------------------------------------ vibrato e tremolo
    {
        Render r; r.p.vibrato = 1.0f; r.p.tremolo = 1.0f; r.p.rate = 0.44f;
        r.e.setParams (r.p); r.e.keyOn (Upper, 33); r.e.keyOn (Lower, 40);
        auto y = r.run (2.0);
        check (std::isfinite (peak (y)), "vibrato e tremolo stabili");
        if (! dir.empty()) writeWav (dir + "/vibrato_tremolo.wav", y, 96000);
    }

    // ------------------------------------------------------------ CPU nel caso peggiore
    {
        Render r (96000.0); for (auto& d : r.p.umDraw) d = 8; for (auto& d : r.p.lmDraw) d = 8;
        r.p.bassPedal = true; r.e.setParams (r.p);
        for (int k = 0; k < kKeys; k += 2) { r.e.keyOn (Upper, k); r.e.keyOn (Lower, k); }
        r.e.pedalOn (0);
        std::vector<float> y (96000);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) r.e.process (y.data(), (int) y.size());
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 5.0;
        std::printf ("\nCPU con 31+31 tasti e tutti i piedi a 96 kHz: %.1f %% di un core\n", sec * 100.0);
        Render q (96000.0); q.e.keyOn (Upper, 24); q.e.keyOn (Upper, 28); q.e.keyOn (Lower, 30);
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) q.e.process (y.data(), (int) y.size());
        const double s2 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count() / 5.0;
        std::printf ("CPU con un accordo tipico (888 + 2 note inferiori): %.1f %% di un core\n", s2 * 100.0);
    }

    std::printf ("\n%s\n", fails == 0 ? "TUTTI I TEST SUPERATI" : "ALCUNI TEST FALLITI");
    return fails == 0 ? 0 : 1;
}
