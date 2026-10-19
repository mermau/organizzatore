#pragma once
#include "PluginProcessor.h"

namespace org::ui
{
struct Fonts
{
    static Fonts& get();
    juce::Font label (float h) const;    // PT Sans Narrow Bold: serigrafia del pannello
    juce::Font sans (float h) const;     // PT Sans Bold
    juce::Font slab (float h) const;     // Zilla Slab Bold: lettere del marchio
    juce::Font model (float h) const;    // Zilla Slab SemiBold: nome del modello
    juce::Typeface::Ptr narrowBold, sansBold, slabBold, slabSemi;
private:
    Fonts();
};

class LearnHost
{
public:
    virtual ~LearnHost() = default;
    virtual void showControlMenu (juce::Component& c, const juce::String& paramId) = 0;
    virtual bool isLearning (const juce::String& paramId) const = 0;
};

// Comando legato a un parametro, con clic destro per il MIDI Learn
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (LearnHost& h, juce::RangedAudioParameter& p);
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float value() const { return current; }                     // valore reale del parametro
    float norm() const { return param.convertTo0to1 (current); }
    void setReal (float v);
    bool learning() const { return host.isLearning (param.paramID); }

    std::function<void()> onDoubleClick;
    float wheelStep = 0.05f;          // passo della rotellina (normalizzato)
    bool dragVertical = true, dragInverted = false;
    float dragPixels = 160.0f;

protected:
    virtual void clicked (const juce::MouseEvent&) {}
    virtual bool draggable() const { return true; }
    LearnHost& host;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float current = 0.0f;
    float dragStart = 0.0f;
    bool dragging = false;
};

class Knob : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
};

class Pointer : public ParamControl   // selettori MODE e FOOTAGE
{
public:
    Pointer (LearnHost& h, juce::RangedAudioParameter& p, int n, float spanDeg) : ParamControl (h, p), positions (n), span (spanDeg)
    { dragPixels = 14.0f * (float) (n - 1); wheelStep = 1.0f / (float) (n - 1); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override;   // clic senza trascinare: scatto successivo
    void select (int i) { setReal ((float) juce::jlimit (0, positions - 1, i)); }
    float angleFor (int i) const { return -span * 0.5f + span * (float) i / (float) (positions - 1); }
    int positions; float span;
};

class Drawbar : public ParamControl   // linguetta che scorre in giù (0 … 8)
{
public:
    Drawbar (LearnHost& h, juce::RangedAudioParameter& p, juce::Colour c, juce::String txt)
        : ParamControl (h, p), colour (c), text (std::move (txt)) { dragInverted = true; dragPixels = 34.0f; wheelStep = 1.0f / 8.0f; }
    void paint (juce::Graphics&) override;
    static constexpr float tabH = 17.0f, travel = 28.0f;
    juce::Colour colour; juce::String text;
};

class Tab : public ParamControl   // linguette a bilanciere della percussione, pulsanti AUTO e ON/OFF
{
public:
    Tab (LearnHost& h, juce::RangedAudioParameter& p, juce::Colour c) : ParamControl (h, p), colour (c) {}
    void paint (juce::Graphics&) override;
    juce::Colour colour;
protected:
    bool draggable() const override { return false; }
    void clicked (const juce::MouseEvent&) override { setReal (value() > 0.5f ? 0.0f : 1.0f); }
};

class PresetButton : public ParamControl   // preselettori con LED (gruppo radio su un parametro a 4 scelte)
{
public:
    PresetButton (LearnHost& h, juce::RangedAudioParameter& p, int idx) : ParamControl (h, p), index (idx) {}
    void paint (juce::Graphics&) override;
    int index;
protected:
    bool draggable() const override { return false; }
    void clicked (const juce::MouseEvent&) override { setReal ((float) index); }
};

class ToggleLever : public ParamControl   // levetta MAN./PED.
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
protected:
    bool draggable() const override { return false; }
    void clicked (const juce::MouseEvent&) override { setReal (value() > 0.5f ? 0.0f : 1.0f); }
};

class BendLever : public ParamControl
{
public:
    BendLever (LearnHost& h, juce::RangedAudioParameter& p, OrgProcessor& pr) : ParamControl (h, p), proc (pr)
    { dragVertical = false; dragPixels = 70.0f; }
    void paint (juce::Graphics&) override;
    OrgProcessor& proc;
    float shown = 0.0f;
};

//==============================================================================
class Manual : public juce::Component
{
public:
    Manual (OrgProcessor& p, int m) : proc (p), manual (m) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
private:
    int keyAt (juce::Point<float>) const;
    juce::Rectangle<float> whiteRect (int whiteIndex) const;
    juce::Rectangle<float> blackRect (int key) const;
    OrgProcessor& proc;
    int manual;
    int held = -1;
    std::array<bool, t2::kKeys> shown {};
};

//==============================================================================
// Pannello disegnato in unità (W x H) ricavate dalla foto dell'organo e scalato con una trasformazione.
class Panel : public juce::Component, public LearnHost, private juce::Timer
{
public:
    static constexpr int W = 1170, H = 484;
    explicit Panel (OrgProcessor&);
    ~Panel() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void showControlMenu (juce::Component&, const juce::String& paramId) override;
    bool isLearning (const juce::String& paramId) const override;

private:
    struct KnobSpec { juce::String id; float x, y; const char* label; bool secScale; };
    void timerCallback() override;
    template <typename T, typename... Args> T* add (const juce::String& id, juce::Rectangle<float> r, const char* tip, Args&&... args);
    void showSettingsMenu();
    void repaintAllControls();
    float ky() const;

    OrgProcessor& proc;
    juce::OwnedArray<ParamControl> controls;
    std::vector<KnobSpec> knobSpecs;
    Pointer* modeSel = nullptr; Pointer* footSel = nullptr;
    Knob* rateKnob = nullptr;
    BendLever* lever = nullptr;
    Manual upper, lower;
    juce::Rectangle<float> logoArea, stickerArea, ledArea;
    bool led = false, modOn = true;
    juce::String learnId;
};
} // namespace org::ui

//==============================================================================
class OrgEditor : public juce::AudioProcessorEditor
{
public:
    explicit OrgEditor (OrgProcessor&);
    ~OrgEditor() override = default;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101010)); }
private:
    OrgProcessor& proc;
    org::ui::Panel panel;
    juce::TooltipWindow tooltips { this, 600 };
};
