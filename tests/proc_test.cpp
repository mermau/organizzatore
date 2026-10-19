// Prova del processore come lo usa una tastiera MIDI singola (canale 1): instradamento e comandi dei bassi.
#include "PluginProcessor.h"

struct Result { float db, centroid; };

static Result render (OrgProcessor& p, std::initializer_list<int> notes, double seconds = 1.0)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    std::vector<float> y;
    for (int b = 0; b < (int) (seconds * 48000 / 512); ++b)
    {
        buf.clear(); p.processBlock (buf, midi); midi.clear();
        y.insert (y.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
    }
    for (int n : notes) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    for (int b = 0; b < 600; ++b) { buf.clear(); p.processBlock (buf, midi); midi.clear(); }   // code esaurite
    // livello e "brillantezza" (centroide spettrale grezzo: rapporto derivata/segnale)
    double s = 0, d = 0;
    for (size_t i = 4800; i < y.size(); ++i) { s += (double) y[i] * y[i]; const double dv = y[i] - y[i - 1]; d += dv * dv; }
    const double c = std::sqrt (d / (s + 1e-30)) * 48000.0 / (2.0 * M_PI);
    return { (float) (10.0 * std::log10 (s / (double) (y.size() - 4800) + 1e-20)), (float) c };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    OrgProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto set = [&] (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto reset = [&] (const char* id) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->getDefaultValue()); };

    std::printf ("modo MIDI = %.0f (1 = split), split = %.0f\n", p.apvts.getRawParameterValue ("midi_route")->load(),
                 p.apvts.getRawParameterValue ("split")->load());
    auto r0 = render (p, { 36 });
    std::printf ("Do2 (36) sul canale 1, levetta MAN: %.1f dB, centroide %.0f Hz\n", r0.db, r0.centroid);
    struct T { const char* id; float a, b; };
    for (int mode : { 2, 1 })
    {
        set ("bass_mode", (float) mode);
        std::printf ("MODE %s\n", mode == 1 ? "PERC." : "SUST.");
        for (auto t : { T { "bass_decay", 0.0f, 1.0f }, T { "bass_range", 0.0f, 1.0f }, T { "bass_res", 0.0f, 1.0f },
                        T { "bass_volume", 0.1f, 1.0f }, T { "bass_foot", 0.0f, 2.0f } })
        {
            set (t.id, t.a); const auto a = render (p, { 36 });
            set (t.id, t.b); const auto b = render (p, { 36 });
            reset (t.id);
            std::printf ("  %-12s %.0f -> %.0f :  %6.1f dB / %5.0f Hz  ->  %6.1f dB / %5.0f Hz\n", t.id, t.a, t.b, a.db, a.centroid, b.db, b.centroid);
        }
    }
    reset ("bass_mode");
    // zone dello split con una KeyLab 49 (note 36…84): bassi 36–47, manuale inferiore 48–59, superiore 60–84
    auto shown = [&] (int m) { std::string s; for (int k = 0; k < 61; ++k) if (p.keyIsDown (m, k)) s += std::to_string (k) + " "; return s; };
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int n : { 36, 50, 55, 64 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    p.processBlock (buf, midi); midi.clear();
    std::printf ("note 36 50 55 64 -> tasti sup.: [%s] inf.: [%s]\n", shown (0).c_str(), shown (1).c_str());
    for (int n : { 36, 50, 55, 64 }) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    p.processBlock (buf, midi); midi.clear();
    std::printf ("dopo i note-off -> sup.: [%s] inf.: [%s]\n", shown (0).c_str(), shown (1).c_str());
    set ("bass_volume", 0.0f);
    const auto lm = render (p, { 52 });
    const auto um = render (p, { 72 });
    std::printf ("Mi3 (52) zona del manuale inferiore: %.1f dB;  Do5 (72) manuale superiore: %.1f dB\n", lm.db, um.db);
    return 0;
}
