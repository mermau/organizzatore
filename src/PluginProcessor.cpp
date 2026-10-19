#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace org;

OrgProcessor::OrgProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Uscita", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ORGANIZZATORE", createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            raw[rp->paramID.toStdString()] = apvts.getRawParameterValue (rp->paramID);
    for (int m = 0; m < 2; ++m)
        for (int k = 0; k < t2::kKeys; ++k) { guiHeld[m][(size_t) k].store (false); shownDown[m][(size_t) k].store (false); }
    for (auto& ch : noteTarget) ch.fill (-1);
    resetDefaultMapping();
}

int OrgProcessor::paramIndexOf (const juce::String& id) const
{
    const auto& ps = getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
            if (rp->paramID == id) return i;
    return -1;
}

// Assegnazioni predefinite pensate per la Arturia KeyLab (modalità User): i 9 fader sui drawbar,
// le 9 manopole sulla modulazione, sulla percussione e sui bassi. Tutto il resto con il MIDI Learn.
void OrgProcessor::resetDefaultMapping (bool lower)
{
    fadersLower = lower;
    for (auto& c : ccMap) c.store (-1);
    const int faders[9]   = { 73, 75, 79, 72, 80, 81, 82, 83, 85 };
    const int encoders[9] = { 74, 71, 76, 77, 93, 18, 19, 16, 17 };
    const juce::String encIds[9] = { pid::rate, pid::vibrato, pid::tremolo, pid::percVol, pid::percDecay,
                                     pid::bassDecay, pid::bassRange, pid::bassRes, pid::bassVolume };
    for (int i = 0; i < 9; ++i)
    {
        ccMap[(size_t) faders[i]].store (paramIndexOf (lower ? pid::lmDraw (i) : pid::umDraw (i)));
        ccMap[(size_t) encoders[i]].store (paramIndexOf (encIds[i]));
    }
    ccMap[1].store  (paramIndexOf (pid::vibrato));     // rotella di modulazione
    ccMap[7].store  (paramIndexOf (pid::master));
    ccMap[11].store (paramIndexOf (pid::expression));  // pedale d'espressione
}

int OrgProcessor::ccForParam (int paramIndex) const
{
    for (int cc = 0; cc < 128; ++cc) if (ccMap[(size_t) cc].load() == paramIndex) return cc;
    return -1;
}

void OrgProcessor::clearMapping (int paramIndex)
{
    for (auto& c : ccMap) if (c.load() == paramIndex) c.store (-1);
}

bool OrgProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void OrgProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    for (size_t st = 0; st < 2; ++st)
    {
        overs[st] = std::make_unique<juce::dsp::Oversampling<float>> (
            1, st + 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        overs[st]->initProcessing ((size_t) samplesPerBlock);
    }
    mono.setSize (1, samplesPerBlock);
    readParams();
    engine.setParams (ep);
    selectFactor (factorFor (rawv (pid::quality) > 0.5f));
    setLatencySamples (oversampler != nullptr ? (int) std::round (oversampler->getLatencyInSamples()) : 0);
}

// Normale: il motore gira alla frequenza dell'host (almeno 44,1 kHz); alta: ~88–96 kHz o più
int OrgProcessor::factorFor (bool high) const
{
    if (hostRate < 40000.0) return high ? 4 : 2;
    if (hostRate <= 50000.0) return high ? 2 : 1;
    return 1;
}

void OrgProcessor::selectFactor (int factor)
{
    osFactor = factor;
    oversampler = factor == 4 ? overs[1].get() : (factor == 2 ? overs[0].get() : nullptr);
    if (oversampler) oversampler->reset();
    engine.prepare (hostRate * factor);
    for (int m = 0; m < 2; ++m) engineDown[m].fill (false);
}

void OrgProcessor::readParams()
{
    for (int f = 0; f < 9; ++f)
    {
        ep.umDraw[f] = rawv (pid::umDraw (f));
        ep.lmDraw[f] = rawv (pid::lmDraw (f));
        ep.perc[f] = rawv (pid::perc (f)) > 0.5f;
    }
    ep.umPreset = (int) rawv (pid::umPreset);
    ep.lmPreset = (int) rawv (pid::lmPreset);
    ep.k1 = rawv (pid::k1) > 0.5f;
    ep.k2 = rawv (pid::k2) > 0.5f;
    ep.percVol = rawv (pid::percVol) / 8.0f;
    ep.percDecay = rawv (pid::percDecay) / 8.0f;
    ep.lmVolume = rawv (pid::lmVolume);
    ep.master = rawv (pid::master);
    ep.rate = rawv (pid::rate);
    ep.vibrato = rawv (pid::vibrato);
    ep.tremolo = rawv (pid::tremolo);
    ep.modOn = rawv (pid::modOn) > 0.5f;
    ep.bassMode = (int) rawv (pid::bassMode);
    ep.bassFoot = (int) rawv (pid::bassFoot);
    ep.bassDecay = rawv (pid::bassDecay);
    ep.bassRange = rawv (pid::bassRange);
    ep.bassRes = rawv (pid::bassRes);
    ep.bassVolume = rawv (pid::bassVolume);
    ep.bassPedal = rawv (pid::bassPedal) > 0.5f;
    ep.bend = juce::jlimit (-1.0f, 1.0f, rawv (pid::bend) + wheel.load());
    ep.sustOn = rawv (pid::susOn) > 0.5f;
    ep.sustAuto = rawv (pid::susAuto) > 0.5f;
    ep.sustLength = rawv (pid::susLength);
    ep.expression = rawv (pid::expression);
    ep.tuneCents = rawv (pid::tune);
}

// Canali: 1 (e ogni altro) = manuale superiore, 2 = inferiore, 3 = pedaliera.
// Split (una sola tastiera, tre zone):
//   dallo split in su            → manuale superiore (Do2 = primo tasto)
//   dallo split dei bassi allo split → manuale inferiore, a partire dal suo Do4 (tasto 24): le due ottave più
//                                      basse del manuale inferiore, sull'originale, sono quelle dei bassi
//   sotto lo split dei bassi     → bassi: prime ottave del manuale inferiore (MAN.) o pedaliera (PED.)
// Il canale 3 resta sempre la pedaliera.
void OrgProcessor::routeNote (int channel, int note, bool on)
{
    const int ch = juce::jlimit (1, 16, channel) - 1;
    auto fold = [] (int k, int n) { while (k < 0) k += 12; while (k >= n) k -= 12; return k; };
    int target, key;
    if (on)
    {
        if (ch == 2) { target = 2; key = fold (note - 36, t2::kPedals); }
        else if ((int) rawv (pid::midiMode) == 1)
        {
            const int sp = (int) rawv (pid::split);
            const int bs = juce::jmin ((int) rawv (pid::bassSplit), sp);
            if (note >= sp)      { target = 0; key = fold (note - 36, t2::kKeys); }
            else if (note >= bs) { target = 1; key = fold (note - bs + 24, t2::kKeys); }
            else if (rawv (pid::bassPedal) > 0.5f) { target = 2; key = fold (note - 36, t2::kPedals); }
            else                 { target = 1; key = fold (note - 36, t2::kBassKeys); }
        }
        else { target = ch == 1 ? 1 : 0; key = fold (note - 36, t2::kKeys); }
        noteTarget[ch][(size_t) note] = target * 128 + key;
    }
    else
    {
        const int packed = noteTarget[ch][(size_t) note];
        noteTarget[ch][(size_t) note] = -1;
        if (packed < 0) return;
        target = packed / 128; key = packed % 128;
    }
    if (target == 2)
    {
        pedalCount[(size_t) key] = juce::jmax (0, pedalCount[(size_t) key] + (on ? 1 : -1));
        if (pedalCount[(size_t) key] > 0) engine.pedalOn (key); else engine.pedalOff (key);
        return;
    }
    midiCount[target][(size_t) key] = juce::jmax (0, midiCount[target][(size_t) key] + (on ? 1 : -1));
}

void OrgProcessor::syncKeys()
{
    for (int m = 0; m < 2; ++m)
        for (int k = 0; k < t2::kKeys; ++k)
        {
            const bool down = midiCount[m][(size_t) k] > 0 || guiHeld[m][(size_t) k].load();
            if (down != engineDown[m][(size_t) k])
            {
                engineDown[m][(size_t) k] = down;
                if (down) engine.keyOn (m, k); else engine.keyOff (m, k);
            }
            shownDown[m][(size_t) k].store (down);
        }
}

void OrgProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())       routeNote (m.getChannel(), m.getNoteNumber(), true);
    else if (m.isNoteOff()) routeNote (m.getChannel(), m.getNoteNumber(), false);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& c : midiCount) c.fill (0);
        pedalCount.fill (0);
        for (auto& ch : noteTarget) ch.fill (-1);
        engine.allNotesOff();
        for (int m2 = 0; m2 < 2; ++m2) engineDown[m2].fill (false);
    }
    else if (m.isPitchWheel())
        wheel.store ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        const int learn = learnIndex.load();
        if (learn >= 0 && cc != 64)
        {
            clearMapping (learn);
            ccMap[(size_t) cc].store (learn);
            learnIndex.store (-1);
        }
        const int idx = ccMap[(size_t) cc].load();
        if (idx >= 0)
        {
            if (auto* p = getParameters()[idx])
                p->setValueNotifyingHost ((float) val / 127.0f);
        }
        else if (cc == 64)
        {
            sustainPedal = val >= 64;               // pedale di sustain = interruttore SUSTAIN ON
            engine.setSustainPedal (sustainPedal);
        }
    }
}

void OrgProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if (const int want = factorFor (rawv (pid::quality) > 0.5f); want != osFactor)
        selectFactor (want);
    if (mono.getNumSamples() < n) mono.setSize (1, n, false, false, true);
    mono.clear();

    juce::dsp::AudioBlock<float> monoBlock (mono.getArrayOfWritePointers(), 1, (size_t) n);
    float* hi = mono.getWritePointer (0);
    if (oversampler)
        hi = oversampler->processSamplesUp (monoBlock).getChannelPointer (0);

    readParams();
    engine.setParams (ep);
    syncKeys();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.process (hi + pos * osFactor, (at - pos) * osFactor);
            pos = at;
        }
        handleMidi (meta.getMessage());
        readParams();
        engine.setParams (ep);
        syncKeys();
    }
    if (pos < n) engine.process (hi + pos * osFactor, (n - pos) * osFactor);

    if (oversampler) oversampler->processSamplesDown (monoBlock);
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono, 0, 0, n);
    led.store (engine.lfoLed());
}

void OrgProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    auto map = state.getOrCreateChildWithName ("MIDIMAP", nullptr);
    map.removeAllProperties (nullptr);
    map.setProperty ("fadersLower", fadersLower, nullptr);
    for (int cc = 0; cc < 128; ++cc)
    {
        const int idx = ccMap[(size_t) cc].load();
        if (idx >= 0)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (getParameters()[idx]))
                map.setProperty ("cc" + juce::String (cc), rp->paramID, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void OrgProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        auto map = state.getChildWithName ("MIDIMAP");
        if (map.isValid())
        {
            for (auto& c : ccMap) c.store (-1);
            fadersLower = (bool) map.getProperty ("fadersLower", false);
            for (int i = 0; i < map.getNumProperties(); ++i)
            {
                const auto name = map.getPropertyName (i).toString();
                if (! name.startsWith ("cc")) continue;
                const int cc = name.substring (2).getIntValue();
                if (cc >= 0 && cc < 128)
                    ccMap[(size_t) cc].store (paramIndexOf (map.getProperty (name).toString()));
            }
            state.removeChild (map, nullptr);
        }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* OrgProcessor::createEditor() { return new OrgEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrgProcessor(); }
