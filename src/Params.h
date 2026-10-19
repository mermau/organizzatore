#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace org::pid
{
inline juce::String umDraw (int f) { return "um_db" + juce::String (f); }
inline juce::String lmDraw (int f) { return "lm_db" + juce::String (f); }
inline juce::String perc (int f)   { return "perc" + juce::String (f); }
inline constexpr const char* umPreset   = "um_preset";
inline constexpr const char* lmPreset   = "lm_preset";
inline constexpr const char* k1         = "perc_k1";
inline constexpr const char* k2         = "perc_k2";
inline constexpr const char* percVol    = "perc_vol";
inline constexpr const char* percDecay  = "perc_decay";
inline constexpr const char* lmVolume   = "lm_volume";
inline constexpr const char* master     = "master";
inline constexpr const char* rate       = "mod_rate";
inline constexpr const char* vibrato    = "vibrato";
inline constexpr const char* tremolo    = "tremolo";
inline constexpr const char* modOn      = "mod_on";
inline constexpr const char* bassMode   = "bass_mode";
inline constexpr const char* bassFoot   = "bass_foot";
inline constexpr const char* bassDecay  = "bass_decay";
inline constexpr const char* bassRange  = "bass_range";
inline constexpr const char* bassRes    = "bass_res";
inline constexpr const char* bassVolume = "bass_volume";
inline constexpr const char* bassPedal  = "bass_ped";
inline constexpr const char* bend       = "bend";
inline constexpr const char* susOn      = "sus_on";
inline constexpr const char* susAuto    = "sus_auto";
inline constexpr const char* susLength  = "sus_length";
inline constexpr const char* expression = "expression";
inline constexpr const char* tune       = "tune";
inline constexpr const char* midiMode   = "midi_mode";
inline constexpr const char* split      = "split";
inline constexpr const char* quality    = "quality";
}

namespace org
{
inline const char* const footNames[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const int v = 1;
    // manopole: valore 0…1, scala del pannello 0…10
    auto knob = [&] (const String& id, const String& name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (x * 10.0f, 1); })));
    };
    // drawbar e cursori: 0…8
    auto bar = [&] (const String& id, const String& name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, NormalisableRange<float> (0.0f, 8.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (x, 1); })));
    };
    auto toggle = [&] (const String& id, const String& name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, v }, name, def)); };
    auto choice = [&] (const String& id, const String& name, StringArray items, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, v }, name, items, def)); };

    const float umDef[9] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    const float lmDef[9] = { 0, 0, 8, 6, 0, 0, 0, 0, 0 };
    for (int f = 0; f < 9; ++f) bar (pid::umDraw (f), String ("Superiore - Drawbar ") + footNames[f], umDef[f]);
    for (int f = 0; f < 9; ++f) bar (pid::lmDraw (f), String ("Inferiore - Drawbar ") + footNames[f], lmDef[f]);
    const StringArray presets { "Drawbars", "2", "3", "4" };
    choice (pid::umPreset, "Superiore - Preselettore", presets, 0);
    choice (pid::lmPreset, "Inferiore - Preselettore", presets, 0);
    for (int f = 0; f < 9; ++f) toggle (pid::perc (f), String ("Percussione ") + footNames[f], false);
    toggle (pid::k1, "Percussione K1 (click)", false);
    toggle (pid::k2, "Percussione K2 (click)", false);
    bar    (pid::percVol,   "Percussione - Volume", 6.0f);
    bar    (pid::percDecay, "Percussione - Decay", 3.0f);

    knob   (pid::lmVolume, "Volume - Lower manual", 0.85f);
    knob   (pid::master,   "Volume - Master", 0.75f);
    knob   (pid::rate,     "Modulation - Rate", 0.44f);
    knob   (pid::vibrato,  "Modulation - Vibrato", 0.0f);
    knob   (pid::tremolo,  "Modulation - Tremolo", 0.0f);
    toggle (pid::modOn,    "Modulation - On/Off", true);

    choice (pid::bassMode, "Bass - Mode", { "Off", "Perc.", "Sust.", "Off " }, 2);
    choice (pid::bassFoot, "Bass - Footage", { "16'", "8'", "4'" }, 0);
    knob   (pid::bassDecay,  "Bass - Decay", 0.35f);
    knob   (pid::bassRange,  "Bass - Range", 0.5f);
    knob   (pid::bassRes,    "Bass - Resonance", 0.2f);
    knob   (pid::bassVolume, "Bass - Volume", 0.35f);
    toggle (pid::bassPedal,  "Bass - Man./Ped. (on = pedaliera)", false);

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { pid::bend, v }, "Pitch bending", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    toggle (pid::susOn,     "Sustain - On/Off", false);
    toggle (pid::susAuto,   "Sustain - Auto", false);
    knob   (pid::susLength, "Sustain - Length", 0.4f);

    knob   (pid::expression, "Pedale d'espressione", 1.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { pid::tune, v }, "Pitch (accordatura, cent)", NormalisableRange<float> (-100.0f, 100.0f), 0.0f));
    choice (pid::midiMode, "Tastiera MIDI", { "Canali (1 sup., 2 inf., 3 pedaliera)", "Split su un canale" }, 1);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { pid::split, v }, "Punto di split", 37, 95, 60));
    choice (pid::quality, "Qualita", { "Normale", "Alta (sovracampionamento)" }, 0);
    return { p.begin(), p.end() };
}
}
