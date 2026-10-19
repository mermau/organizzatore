#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/T2Engine.h"
#include "Params.h"

class OrgProcessor : public juce::AudioProcessor
{
public:
    OrgProcessor();
    ~OrgProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- tastiere a schermo ---------------------------------------------------
    void guiKey (int manual, int key, bool down) { if (key >= 0 && key < t2::kKeys) guiHeld[manual][(size_t) key].store (down); }
    bool keyIsDown (int manual, int key) const { return shownDown[manual][(size_t) key].load(); }
    float wheelBend() const { return wheel.load(); }

    // --- MIDI Learn -----------------------------------------------------------
    void startLearn (int paramIndex) { learnIndex.store (paramIndex); }
    void cancelLearn() { learnIndex.store (-1); }
    int  learningIndex() const { return learnIndex.load(); }
    int  ccForParam (int paramIndex) const;
    void clearMapping (int paramIndex);
    void resetDefaultMapping (bool fadersOnLower = false);
    bool fadersOnLower() const { return fadersLower; }
    int  paramIndexOf (const juce::String& id) const;

    bool lfoLed() const { return led.load(); }

private:
    void readParams();
    void handleMidi (const juce::MidiMessage&);
    void routeNote (int channel, int note, bool on);
    void syncKeys();
    int factorFor (bool high) const;
    void selectFactor (int factor);

    t2::Engine engine;
    t2::Params ep;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> overs;
    juce::dsp::Oversampling<float>* oversampler = nullptr;
    double hostRate = 48000.0;
    int osFactor = 1;
    juce::AudioBuffer<float> mono;

    // tasti: note MIDI tenute (contatore, più note possono cadere sullo stesso tasto) + clic a schermo
    std::array<int, t2::kKeys> midiCount[2] {};
    std::array<int, t2::kPedals> pedalCount {};
    std::array<std::atomic<bool>, t2::kKeys> guiHeld[2];
    std::array<std::atomic<bool>, t2::kKeys> shownDown[2];
    std::array<bool, t2::kKeys> engineDown[2] {};
    std::array<int, 128> noteTarget[16];   // dove è finita ogni nota (per il note-off): destinazione·128 + tasto, −1
    std::atomic<float> wheel { 0.0f };
    bool sustainPedal = false;

    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnIndex { -1 };
    std::atomic<bool> led { false };
    bool fadersLower = false;

    std::unordered_map<std::string, std::atomic<float>*> raw;
    float rawv (const juce::String& id) const { return raw.at (id.toStdString())->load(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrgProcessor)
};
