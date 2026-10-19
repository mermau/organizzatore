// Motore audio dell'emulazione del Crumar Organizer T2.
// Ricavato dal manuale di servizio T1-T2 e dagli schemi elettrici del T2 (numeri di scheda P-xxx e DWG).
// Nessuna dipendenza da JUCE: viene compilato anche dai test.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace t2
{
constexpr int kKeys   = 61;   // tastiere da 61 tasti, Do–Do
constexpr int kFeet   = 9;    // 16' 5⅓' 8' 4' 2⅔' 2' 1⅗' 1⅓' 1'
constexpr int kQ      = 97;   // 96 frequenze + il Do più acuto (Do• del MK50240)
constexpr int kBands  = 7;    // filtri F1…F7 della scheda P-509
constexpr int kPedals = 18;   // pedaliera opzionale da 18 note (Do–Fa)
constexpr int kBassKeys = 24; // le prime due ottave del manuale inferiore

constexpr double kMasterHz = 2.00024e6;   // oscillatore principale P-489 (DWG 2)
// divisori del generatore d'ottava MOSTEK 50240 per l'ottava più acuta (Do8 … Si8 a 2,00024 MHz)
constexpr int kDiv[12] = { 478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253 };
// piedi in semitoni rispetto all'8': mutazioni temperate, come in ogni organo a divisori
constexpr int kFootOffset[kFeet] = { -12, 7, 0, 12, 19, 24, 28, 31, 36 };

enum { Upper = 0, Lower = 1 };

struct Params
{
    float umDraw[kFeet] { 8, 8, 8, 0, 0, 0, 0, 0, 0 };   // 0…8
    float lmDraw[kFeet] { 0, 0, 8, 4, 0, 0, 0, 0, 0 };
    int   umPreset = 0, lmPreset = 0;                    // 0 = DRAWBARS, 1…3 = preselettori 2…4
    bool  perc[kFeet] {};
    bool  k1 = false, k2 = false;                        // effetti "click" K1, K2
    float percVol = 0.75f, percDecay = 0.4f;             // cursori 0…1
    float lmVolume = 0.7f, master = 0.7f;                // manopole 0…1 (scala 0…10)
    float rate = 0.44f, vibrato = 0.0f, tremolo = 0.0f;
    bool  modOn = true;
    int   bassMode = 2;                                  // 0 OFF, 1 PERC, 2 SUST, 3 OFF
    int   bassFoot = 0;                                  // 0 = 16', 1 = 8', 2 = 4'
    float bassDecay = 0.3f, bassRange = 0.5f, bassRes = 0.2f, bassVolume = 0.5f;
    bool  bassPedal = false;                             // false = MAN., true = PED.
    float bend = 0.0f;                                   // leva PITCH BENDING −1…+1
    bool  sustOn = false, sustAuto = false;
    float sustLength = 0.4f;
    float expression = 1.0f;                             // pedale del volume
    float tuneCents = 0.0f;                              // PITCH sul retro
};

//==============================================================================
// Filtro lineare in spazio di stato, discretizzato con la trasformata bilineare (trapezi).
template <std::size_t N>
struct StateSpace
{
    double ad[N][N] {}, bd[N] {}, c[N] {};
    double x[N] {};
    double uPrev = 0.0;

    void design (const double a[N][N], const double b[N], const double cc[N], double h)
    {
        double m[N][2 * N];
        for (std::size_t i = 0; i < N; ++i)
            for (std::size_t j = 0; j < 2 * N; ++j)
                m[i][j] = j < N ? (i == j ? 1.0 : 0.0) - 0.5 * h * a[i][j] : (j - N == i ? 1.0 : 0.0);
        // inversione di (I − hA/2) con Gauss-Jordan
        for (std::size_t col = 0; col < N; ++col)
        {
            std::size_t piv = col;
            for (std::size_t r = col + 1; r < N; ++r) if (std::abs (m[r][col]) > std::abs (m[piv][col])) piv = r;
            for (std::size_t j = 0; j < 2 * N; ++j) std::swap (m[col][j], m[piv][j]);
            const double d = m[col][col];
            for (std::size_t j = 0; j < 2 * N; ++j) m[col][j] /= d;
            for (std::size_t r = 0; r < N; ++r)
                if (r != col)
                {
                    const double f = m[r][col];
                    for (std::size_t j = 0; j < 2 * N; ++j) m[r][j] -= f * m[col][j];
                }
        }
        for (std::size_t i = 0; i < N; ++i)
        {
            for (std::size_t j = 0; j < N; ++j)
            {
                double s = 0.0;
                for (std::size_t k = 0; k < N; ++k) s += m[i][N + k] * ((k == j ? 1.0 : 0.0) + 0.5 * h * a[k][j]);
                ad[i][j] = s;
            }
            double s = 0.0;
            for (std::size_t k = 0; k < N; ++k) s += m[i][N + k] * 0.5 * h * b[k];
            bd[i] = s;
            c[i] = cc[i];
        }
    }
    void reset() { std::fill (x, x + N, 0.0); uPrev = 0.0; }
    inline double tick (double u)
    {
        const double uu = u + uPrev;
        uPrev = u;
        double nx[N];
        for (std::size_t i = 0; i < N; ++i)
        {
            double s = bd[i] * uu;
            for (std::size_t j = 0; j < N; ++j) s += ad[i][j] * x[j];
            nx[i] = s;
        }
        double y = 0.0;
        for (std::size_t i = 0; i < N; ++i) { x[i] = nx[i]; y += c[i] * nx[i]; }
        return y;
    }
};

//==============================================================================
// Un filtro d'ottava della scheda P-509 (DWG 11): stadio d'ingresso a transimpedenza (Rf ‖ 220 pF)
// che raccoglie le correnti degli interruttori TDA 0470 D, cella RC 47 k, poi filtro a reazione
// multipla con il trimmer di taratura Rt e il condensatore di reazione C3.
struct BandSpec { double rf1, c1, c2, c3, rt, g; };

inline void bandMatrices (const BandSpec& s, double a[4][4], double b[4], double c[4])
{
    const double cf = 220e-12, r1 = 47e3, r2 = 47e3, rfb = 47e3;
    for (int i = 0; i < 4; ++i) { b[i] = c[i] = 0.0; for (int j = 0; j < 4; ++j) a[i][j] = 0.0; }
    // stati: Vs (uscita stadio 1), X, Y, Vo
    a[0][0] = -1.0 / (s.rf1 * cf);                b[0] = 1.0 / cf;           // corrente → tensione
    a[1][0] = 1.0 / (r1 * s.c1); a[1][1] = -(1.0 / r1 + 1.0 / r2) / s.c1; a[1][2] = 1.0 / (r2 * s.c1);
    a[2][1] = 1.0 / (r2 * s.c2); a[2][2] = -(1.0 / r2 + 1.0 / rfb + 1.0 / s.rt) / s.c2; a[2][3] = 1.0 / (rfb * s.c2);
    a[3][2] = -1.0 / (s.rt * s.c3);
    c[3] = 1.0;
}

// Valori dei componenti. F4, F5 e F7 come disegnati, con il trimmer posto (come prescrive la
// procedura di taratura del manuale) sulla massima ampiezza al Sol dell'ottava. Per F1, F2, F3 e F6
// i condensatori disegnati non permettono la taratura: si usa la rete di F4 scalata d'ottava.
inline BandSpec bandSpec (int band)   // 0 = F1 (acuti) … 6 = F7 (bassi)
{
    const double rf1[kBands] = { 12e3, 15e3, 18e3, 22e3, 33e3, 47e3, 68e3 };
    const double g[kBands]   = { 6271.9, 3135.96, 1567.98, 783.99, 392.0, 196.0, 98.0 };
    BandSpec s { rf1[band], 22e-9, 33e-9, 470e-12, 50711.0, g[band] };
    if (band == 4) s.rt = 173116.0;
    else if (band == 6) { s.c1 = 150e-9; s.c2 = 220e-9; s.c3 = 4.7e-9; s.rt = 46644.0; }
    else if (band != 3)
    {
        const double k = 783.99 / g[band];
        s.c1 *= k; s.c2 *= k; s.c3 *= k;
    }
    return s;
}

inline int bandOf (int q)   // indice di frequenza (Do1 = 0) → filtro
{
    const int o = q / 12;   // ottava: 0 = Do1…Si1
    return o <= 1 ? 6 : std::max (0, 7 - o);
}

//==============================================================================
// Accoppiamento d'uscita della P-509: 470 nF – nodo (8,2 kΩ a massa, cortocircuitato dal transistor
// durante il GATE PERCUSSIONE) – 470 nF – ingresso 2,7 kΩ + 47 Ω del CA3080 sulla P-493.
struct Coupling
{
    StateSpace<2> on, off;   // muto / aperto
    void design (double h)
    {
        for (int k = 0; k < 2; ++k)
        {
            const double c = 470e-9, rl = 2747.0, rn = k == 0 ? 8.2e3 : 15.0;
            // stati: Vc1, Vc2; ingresso Vsum. Vn = Vsum − Vc1, Vout = Vn − Vc2
            const double a[2][2] = { { -(1.0 / rn + 1.0 / rl) / c, -1.0 / (rl * c) },
                                     { -1.0 / (rl * c), -1.0 / (rl * c) } };
            const double b[2] = { (1.0 / rn + 1.0 / rl) / c, 1.0 / (rl * c) };
            const double cc[2] = { 0, 0 };
            (k == 0 ? off : on).design (a, b, cc, h);
        }
    }
    void reset() { on.reset(); off.reset(); }
    inline double tick (double vsum, bool mute)
    {
        auto& s = mute ? on : off;
        auto& o = mute ? off : on;
        s.tick (vsum);
        // gli stati sono gli stessi condensatori: si tengono allineati
        o.x[0] = s.x[0]; o.x[1] = s.x[1]; o.uPrev = s.uPrev;
        return vsum - s.x[0] - s.x[1];
    }
};

//==============================================================================
class Engine
{
public:
    Engine()
    {
        for (int m = 0; m < 2; ++m)
            for (auto& k : keys[m]) k = {};
        // la fase iniziale dei due generatori MK50240 è quella dell'accensione
        uint32_t seed = 0x2C9E1F37u;
        for (int m = 0; m < 2; ++m)
            for (auto& ph : phase[m]) { seed = seed * 1664525u + 1013904223u; ph = (double) (seed >> 8) / 16777216.0; }
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double h = 1.0 / fs;
        for (int b = 0; b < kBands; ++b)
        {
            double a[4][4], bb[4], c[4];
            bandMatrices (bandSpec (b), a, bb, c);
            for (int m = 0; m < 2; ++m) band[m][b].design (a, bb, c, h);
        }
        for (int m = 0; m < 2; ++m) coupling[m].design (h);
        ctrlDt = (double) kCtrl / fs;
        dcR = 1.0 - 2.0 * 3.14159265 * 5.0 / fs;
        for (int q = 0; q < kQ; ++q) { kCls[q] = q % 12; kMul[q] = (double) (1 << (q / 12)); kBand[q] = bandOf (q); }
        for (int n = 0; n < 12; ++n) kIncBase[n] = 1.0 / ((double) kDiv[n] * 128.0);
        reset();
    }

    void reset()
    {
        for (int m = 0; m < 2; ++m)
        {
            for (auto& f : band[m]) f.reset();
            coupling[m].reset();
            for (auto& k : keys[m]) k = {};
            std::fill (std::begin (amp[m]), std::end (amp[m]), 0.0f);
            std::fill (std::begin (ampStep[m]), std::end (ampStep[m]), 0.0f);
            gateT[m] = 0.0; prevDetect[m] = false;
        }
        pedals.fill (false);
        percEnv = 0.0; clickT = -1.0; clickLp = 0.0;
        bassV1 = bassV2 = 0.0; bassHp = bassHpX = 0.0; bassEnv = 0.0; bassTp = 0.0;
        dcX = dcY = 0.0; ctrlCount = 0; lfo = 0.0; lfoDir = 1.0;
        bendSm = p.bend;
    }

    void setParams (const Params& np) { p = np; }

    // manuale 0 = superiore, 1 = inferiore; tasto 0…60 (Do2 … Do7)
    void keyOn (int manual, int key)  { if (key >= 0 && key < kKeys) keys[manual][(size_t) key].pressed = true; }
    void keyOff (int manual, int key) { if (key >= 0 && key < kKeys) keys[manual][(size_t) key].pressed = false; }
    void pedalOn (int k)  { if (k >= 0 && k < kPedals) pedals[(size_t) k] = true; }
    void pedalOff (int k) { if (k >= 0 && k < kPedals) pedals[(size_t) k] = false; }
    void allNotesOff()
    {
        for (int m = 0; m < 2; ++m) for (auto& k : keys[m]) k.pressed = false;
        pedals.fill (false);
    }
    void setSustainPedal (bool down) { sustainPedal = down; }

    bool lfoLed() const { return ledOn; }
    float lfoValue() const { return (float) lfo; }

    // frequenza (Hz) dell'indice q con l'oscillatore principale a fm
    static double noteHz (int q, double fm = kMasterHz)
    {
        const int n = q % 12, o = q / 12;
        return fm / ((double) kDiv[n] * std::pow (2.0, 7 - o));
    }
    static int footQ (int key, int foot)
    {
        int q = key + 12 + kFootOffset[foot];
        while (q > kQ - 1) q -= 12;   // ripiegamento sull'ultima ottava del generatore
        return q;
    }

    void process (float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ctrlCount == 0) control();
            ctrlCount = (ctrlCount + 1) % kCtrl;
            out[i] = (float) tick();
        }
    }

private:
    static constexpr int kCtrl = 16;   // campioni per passo di controllo

    struct Key { bool pressed = false; double v = 0.0; bool active = false; };

    //------------------------------------------------------------------ modulazione (P-489)
    double lfoHz() const
    {
        // integratore 470n‖470n e trigger di Schmitt 10k/68k: periodo = 12,16 ms · (1 + Rrate/10k)
        const double x = std::clamp ((double) p.rate, 0.0, 1.0);
        const double rpot = 1.0e6 * (std::pow (10.0, 2.0 * (1.0 - x)) - 1.0) / 99.0;   // 1 MΩ logaritmico
        return 1.0 / (0.012163 * (1.0 + rpot / 10.0e3));
    }

    static double audioTaper (double x) { x = std::clamp (x, 0.0, 1.0); return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }
    // potenziometro lineare caricato da rl (Thevenin del cursore)
    static double loadedPot (double x, double rpot, double rl)
    {
        x = std::clamp (x, 0.0, 1.0);
        return x * rl / (rl + x * (1.0 - x) * rpot);
    }

    //------------------------------------------------------------------ passo di controllo
    void control()
    {
        const double dt = ctrlDt;
        // ---------- livelli dei piedi (Vbc + 4 V): DWG 6 / DWG 10
        double levelU[kFeet], levelL[kFeet];
        presetLevels (p.umPreset, true, p.umDraw, levelU);
        presetLevels (p.lmPreset, false, p.lmDraw, levelL);

        // ---------- rilevamento tasti, GATE PERCUSSIONE (≈10 ms), percussione, click
        const bool sust = p.sustOn || sustainPedal;
        for (int m = 0; m < 2; ++m)
        {
            bool any = false;
            for (int k = 0; k < kKeys; ++k)
                if (keys[m][(size_t) k].pressed && ! (m == Lower && ! p.bassPedal && k < kBassKeys)) any = true;
            detect[m] = any;
            if (any && ! prevDetect[m])
            {
                gateT[m] = kGate;
                if (m == Upper) { percEnv = 1.0; clickT = -kGate; }
            }
            prevDetect[m] = any;
            if (gateT[m] > 0.0) gateT[m] -= dt;
        }
        // percussione: il 2,2 µF è tenuto scarico durante il gate e si ricarica con (33k + DECAY) · 2,2 µF
        if (gateT[Upper] <= 0.0)
        {
            const double tau = (33e3 + 220e3 * std::clamp ((double) p.percDecay, 0.0, 1.0)) * 2.2e-6;
            percEnv *= std::exp (-dt / tau);
        }
        const double percL = sust ? 0.0 : 2.5 * std::clamp ((double) p.percVol, 0.0, 1.0) * percEnv;
        double footU[kFeet], footL[kFeet];
        for (int f = 0; f < kFeet; ++f)
        {
            footU[f] = 1.5 * levelU[f] + (p.perc[f] ? percL : 0.0);
            footL[f] = 4.0 * levelL[f];
        }

        // ---------- linee dei tasti: P-525 (moduli di sustain, manuale superiore) e P-487 (inferiore)
        const double sTau = 0.25 * std::pow (80.0, std::clamp ((double) p.sustLength, 0.0, 1.0));
        const bool autoCut = sust && p.sustAuto && detect[Upper];
        for (int m = 0; m < 2; ++m)
        {
            const double tauA = m == Upper ? 1.5e-3 : 0.47e-3;
            const double aA = 1.0 - std::exp (-dt / tauA);
            double tauR = m == Upper ? 0.1 : 0.047, target = -15.0;
            if (m == Upper && sust) { tauR = autoCut ? 3.0e-3 : sTau; target = autoCut ? -15.0 : 0.0; }
            const double aR = 1.0 - std::exp (-dt / tauR);
            for (int k = 0; k < kKeys; ++k)
            {
                auto& key = keys[m][(size_t) k];
                if (key.pressed) { key.v += (kPressV - key.v) * aA; key.active = true; }
                else if (key.active)
                {
                    key.v += (target - key.v) * aR;
                    if (key.v < 0.6) { key.active = false; key.v = 0.0; }
                }
            }
        }

        // ---------- ampiezze per ciascuna delle 97 frequenze
        for (int m = 0; m < 2; ++m)
        {
            float tgt[kQ] {};
            const double* foot = m == Upper ? footU : footL;
            for (int k = 0; k < kKeys; ++k)
            {
                const auto& key = keys[m][(size_t) k];
                if (! key.active) continue;
                if (m == Lower && ! p.bassPedal && k < kBassKeys) continue;   // barra -6 V delle due ottave basse staccata
                const double drop = 0.6875 * (kPressV - key.v);
                for (int f = 0; f < kFeet; ++f)
                {
                    const double a = foot[f] - kThreshold - drop;
                    if (a > 0.0) tgt[footQ (k, f)] += (float) a;
                }
            }
            nActive[m] = 0;
            bool bandIn[kBands] {};
            for (int q = 0; q < kQ; ++q)
            {
                ampStep[m][q] = (tgt[q] - amp[m][q]) / (float) kCtrl;
                if (tgt[q] > 0.0f || amp[m][q] > 0.0f) { activeQ[m][nActive[m]++] = q; bandIn[kBand[q]] = true; }
            }
            // un filtro senza ingresso e con gli stati spenti non viene calcolato
            for (int b = 0; b < kBands; ++b)
            {
                double e = 0.0;
                for (double v : band[m][b].x) e = std::max (e, std::abs (v));
                bandAwake[m][b] = bandIn[b] || e > 1e-9;
                if (! bandAwake[m][b]) band[m][b].reset();
            }
        }

        // ---------- sezione bassi: M 147 (P-499), commutazioni (P-502), pedaliera/manuale (P-514)
        int lowest = -1;
        if (p.bassPedal) { for (int k = 0; k < kPedals; ++k) if (pedals[(size_t) k]) { lowest = k; break; } }
        else for (int k = 0; k < kBassKeys; ++k) if (keys[Lower][(size_t) k].pressed) { lowest = k; break; }
        const bool ts = lowest >= 0;
        if (ts && (! prevTs || lowest < bassKey)) bassTp = 3.0e-3;   // impulso TP del tasto prioritario
        if (ts) bassKey = lowest;
        prevTs = ts;
        if (bassTp > 0.0) bassTp -= dt;
        const bool bassOn = p.bassMode == 1 || p.bassMode == 2;
        const bool gate = bassOn && (p.bassMode == 2 ? ts : bassTp > 0.0);
        if (gate) bassEnv += (13.6 - bassEnv) * (1.0 - std::exp (-dt / 0.3e-3));
        else
        {
            const double tau = 10e-6 * (10e3 + 470e3 * audioTaper (p.bassDecay));
            bassEnv *= std::exp (-dt / tau);
        }

        // ---------- leva del pitch bending lisciata e grandezze lente
        bendSm += (p.bend - bendSm) * (1.0 - std::exp (-dt / 0.004));
        lfoStep = 4.0 * lfoHz() / fs;
        tuneRatio = std::pow (2.0, p.tuneCents / 1200.0) * (1.0 + 0.1225 * bendSm);
        lmGain = audioTaper (p.lmVolume);   // LOWER MANUAL: 100 kΩ logaritmico
        bassGain = 0.47 * loadedPot (p.bassVolume, 100e3, 100e3);
        outGain = audioTaper (p.master) * std::clamp ((double) p.expression, 0.0, 1.0) * kOutScale;
    }

    void presetLevels (int preset, bool upper, const float* draw, double* lv) const
    {
        if (preset <= 0) { for (int f = 0; f < kFeet; ++f) lv[f] = std::clamp ((double) draw[f], 0.0, 8.0) / 8.0; return; }
        // 1 = interruttore 4016 aperto (piede presente), 0.5 = aperto con 100 kΩ in parallelo, 0 = chiuso
        static const double up[3][kFeet] = {
            { 1, 1,   1, 0, 0,   0,   0,   0,   0   },   // PR2: 16' 5⅓' 8'
            { 1, 0,   0, 0, 0,   0.5, 0,   0,   0   },   // PR3: 16' 2'
            { 1, 0.5, 1, 1, 0.5, 1,   0.5, 0.5, 0.5 } }; // PR4: pieno
        // manuale inferiore (P-527): la tabella non è pubblicata, ricostruita dagli interruttori U1…U8
        static const double lo[3][kFeet] = {
            { 0, 0, 1, 0.5, 0, 0,   0, 0,   0   },
            { 0, 0, 1, 1,   1, 0,   0, 0,   0   },
            { 0, 0, 1, 0.5, 0, 0.5, 0, 0.5, 0.5 } };
        const auto& t = upper ? up[preset - 1] : lo[preset - 1];
        for (int f = 0; f < kFeet; ++f) lv[f] = t[f];
    }

    //------------------------------------------------------------------ campione
    double tick()
    {
        const double dt = 1.0 / fs;
        // ---------- LFO a triangolo (±1,9 V) e quadra del trigger di Schmitt
        double tri = 0.0;
        if (p.modOn)
        {
            lfo += lfoStep * lfoDir;
            if (lfo > 1.0) { lfo = 2.0 - lfo; lfoDir = -1.0; }
            else if (lfo < -1.0) { lfo = -2.0 - lfo; lfoDir = 1.0; }
            tri = lfo;
            ledOn = lfoDir > 0.0;
        }
        else ledOn = false;

        // ---------- oscillatore principale (punto A): PITCH, PITCH BENDING (1K5/1K8), VIBRATO (560k)
        const double ratio = tuneRatio * (1.0 + 0.0423 * (double) p.vibrato * tri);
        const double fm = kMasterHz * ratio;

        // ---------- generatori dei due manuali e banchi di filtri
        double vsum[2];
        for (int m = 0; m < 2; ++m)
        {
            double in[kBands] {};
            for (int n = 0; n < 12; ++n)
            {
                phaseInc[m][n] = fm * kIncBase[n] * dt;
                phase[m][n] += phaseInc[m][n];
                if (phase[m][n] >= 1.0) phase[m][n] -= 1.0;
            }
            for (int i = 0; i < nActive[m]; ++i)
            {
                const int q = activeQ[m][i];
                const float a = amp[m][q];
                amp[m][q] = std::max (0.0f, a + ampStep[m][q]);
                if (a <= 0.0f) continue;
                const int cls = kCls[q];
                const double mul = kMul[q];
                double ph = phase[m][cls] * mul;
                ph -= std::floor (ph);
                const double d = phaseInc[m][cls] * mul;
                // onda quadra 0/1 del divisore, con correzione polyBLEP sui fronti
                double s = ph < 0.5 ? 1.0 : 0.0;
                s += 0.5 * blep (ph, d);
                double ph2 = ph + 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                s -= 0.5 * blep (ph2, d);
                in[kBand[q]] += (double) a * s;
            }
            double sum = 0.0;
            for (int b = 0; b < kBands; ++b)
                if (bandAwake[m][b]) sum += band[m][b].tick (in[b] / 103.0e3);    // corrente del TDA 0470 D: (Vbc + 3,9 V) / 103 kΩ
            vsum[m] = sum * 0.33;                             // sommatore d'uscita 33k / 100k
        }

        // ---------- accoppiamento e GATE PERCUSSIONE sull'uscita dei filtri
        const bool sust = p.sustOn || sustainPedal;
        const double vu = coupling[Upper].tick (vsum[Upper], gateT[Upper] > 0.0 && ! sust);
        const double vl = coupling[Lower].tick (vsum[Lower], gateT[Lower] > 0.0);

        // ---------- P-493: CA3080 del tremolo (ingressi 2k7 su 47 Ω), basso e click al 741
        const double vd = (vu + vl * lmGain) * (47.0 / 2747.0);
        const double iabc = 200e-6 * (1.0 + 0.48 * (double) p.tremolo * tri);
        const double iota = iabc * std::tanh (vd / 0.052);
        double vmain = iota * 47e3;

        vmain += bassGain * bassTick (fm, dt);

        // click K1/K2: impulso PIK brevissimo alla fine del gate, 470k/120 pF, 2M2 sul sommatore
        if (clickT > -1.0)
        {
            clickT += dt;
            double pulse = 0.0;
            if (clickT >= 0.0 && clickT < kPik)
                pulse = (p.k1 && ! p.k2) ? 1.0 : ((p.k1 || p.k2) ? 47.0 / 147.0 : 0.0);
            if (clickT > kPik + 0.002) clickT = -1.0;
            clickLp += (pulse * 15.0 * (47e3 / 2.67e6) - clickLp) * (1.0 - std::exp (-dt / 46e-6));
        }
        else clickLp *= 0.99;
        vmain += clickLp;

        // MASTER (logaritmico) e pedale d'espressione, poi l'ingresso dell'amplificatore (accoppiato in AC)
        const double y = vmain * outGain;
        const double hp = y - dcX + dcR * dcY; dcX = y; dcY = hp;
        return hp;
    }

    // sezione bassi: dente di sega a gradini dal M 147, filtro a due CA3080 (P-491)
    double bassTick (double fm, double dt)
    {
        const bool on = p.bassMode == 1 || p.bassMode == 2;
        // frequenza di base: bit più basso del 16' (ottava 1) o un'ottava sopra per i tasti della seconda
        const int key = bassKey;
        const int n = key % 12, oct = key / 12;
        const double inc = fm / 4.0 / ((double) kDiv[n] * 32.0) * (double) (1 << oct) * dt;
        bassPh += inc; if (bassPh >= 1.0) bassPh -= 1.0;

        static const double g[4] = { 1.0 / 22e3, 1.0 / 39e3, 1.0 / 82e3, 1.0 / 150e3 };
        static const double gfoot[3] = { 1.0 / 1.2e3, 1.0 / 2.7e3, 1.0 / 6.8e3 };
        const int foot = std::clamp (p.bassFoot, 0, 2);
        double num = 0.0;
        for (int k = foot; k < 4; ++k)
        {
            double ph = bassPh * (double) (1 << k); ph -= std::floor (ph);
            const double d = inc * (double) (1 << k);
            double s = ph < 0.5 ? 1.0 : 0.0;
            s += 0.5 * blep (ph, d);
            double ph2 = ph + 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
            s -= 0.5 * blep (ph2, d);
            num += g[k] * s;
        }
        const double gtot = 2.0 * (g[0] + g[1] + g[2] + g[3]) + gfoot[foot] + 1.0 / 10e3;
        const double vin = on ? 15.0 * num / gtot : 0.0;   // BASS ON/OFF a massa sul selettore MODE

        // 470 nF + 15 kΩ verso l'ingresso: passa-alto a 22 Hz
        const double rc = 470e-9 * 15.22e3;
        const double a = rc / (rc + dt);
        bassHp = a * (bassHp + vin - bassHpX); bassHpX = vin;

        // corrente di polarizzazione degli OTA: inviluppo · RANGE / 3k9
        const double kr = (470.0 + 10e3 * std::clamp ((double) p.bassRange, 0.0, 1.0)) / 49470.0;
        const double iabc = std::max (0.0, bassEnv * kr / 3900.0);

        // RESONANCE: 100 kΩ dall'uscita a massa, cursore → 1 kΩ → pin 2 del primo OTA (100 Ω a massa)
        const double r = std::clamp ((double) p.bassRes, 0.0, 1.0);
        const double rs = 1e3 + r * (1.0 - r) * 100e3;
        const double gn = 1.0 / 100.0 + 2.0 / 100e3 + 1.0 / rs;
        const double b11 = (1.0 / 100e3) / gn, b12 = (r / rs) / gn;
        const double ai = 220.0 / 15220.0, c2 = 220.0 / 220220.0, d2 = 100.0 / 100100.0;
        const double cap = 1.5e-9, vt2 = 0.052;

        const double u = bassHp;
        const double vd1 = ai * u - b11 * bassV1 - b12 * bassV2;
        const double vd2 = c2 * bassV1 - d2 * bassV2;
        auto geff = [&] (double vd) { return std::abs (vd) < 1e-6 ? iabc / vt2 : iabc * std::tanh (vd / vt2) / vd; };
        const double k1 = geff (vd1) / cap * dt * 0.5, k2 = geff (vd2) / cap * dt * 0.5;
        // trapezi sul sistema lineare (guadagni congelati nel passo)
        const double f1 = ai * (u + bassUPrev);
        const double m11 = 1.0 + k1 * b11, m12 = k1 * b12, m21 = -k2 * c2, m22 = 1.0 + k2 * d2;
        const double r1 = bassV1 + k1 * (f1 - b11 * bassV1 - b12 * bassV2);
        const double r2 = bassV2 + k2 * (c2 * bassV1 - d2 * bassV2);
        const double det = m11 * m22 - m12 * m21;
        double nv1 = (r1 * m22 - m12 * r2) / det;
        double nv2 = (m11 * r2 - m21 * r1) / det;
        bassUPrev = u;
        // limiti delle uscite (JFET inseguitori tra ±15 V)
        bassV1 = 13.0 * std::tanh (nv1 / 13.0) * 0.99999;
        bassV2 = 13.0 * std::tanh (nv2 / 13.0) * 0.99999;
        return bassV2;
    }

    static inline double blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    static constexpr double kPressV = 6.5;       // tensione della barra a tasto premuto
    static constexpr double kThreshold = 0.1;    // soglia di conduzione (≈ −3,9 V di Vbc)
    static constexpr double kGate = 0.010;       // GATE PERCUSSIONE ≈ 10 ms
    // impulso PIK: NAND tra il segnale e la sua copia ritardata da 22 kΩ e dal condensatore segnato "1µ5"
    // (letto come 1,5 nF: con 1,5 µF durerebbe 23 ms e darebbe un "tump", non un click) → circa 25 µs
    static constexpr double kPik = 25e-6;
    static constexpr double kOutScale = 0.6;     // volt → digitale

    Params p;
    double fs = 48000.0, ctrlDt = kCtrl / 48000.0;
    int ctrlCount = 0;
    std::array<Key, kKeys> keys[2];
    std::array<bool, kPedals> pedals {};
    bool sustainPedal = false;
    bool detect[2] {}, prevDetect[2] {};
    double gateT[2] {};
    double percEnv = 0.0, clickT = -1.0, clickLp = 0.0;

    double phase[2][12] {}, phaseInc[2][12] {};
    float amp[2][kQ] {}, ampStep[2][kQ] {};
    int activeQ[2][kQ] {}, nActive[2] {};
    StateSpace<4> band[2][kBands];
    Coupling coupling[2];

    double lfo = 0.0, lfoDir = 1.0; bool ledOn = false;
    double bendSm = 0.0;

    int bassKey = 0; bool prevTs = false; double bassTp = 0.0, bassEnv = 0.0, bassPh = 0.0;
    double bassHp = 0.0, bassHpX = 0.0, bassV1 = 0.0, bassV2 = 0.0, bassUPrev = 0.0;
    double dcX = 0.0, dcY = 0.0, dcR = 0.9997;
    double lfoStep = 0.0, tuneRatio = 1.0, lmGain = 0.0, bassGain = 0.0, outGain = 0.0;
    int kCls[kQ] {}, kBand[kQ] {};
    bool bandAwake[2][kBands] {};
    double kMul[kQ] {}, kIncBase[12] {};
};
} // namespace t2
