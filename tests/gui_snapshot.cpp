// Rende l'interfaccia in PNG senza schermo (xvfb-run) e prova il processore: note, MIDI Learn, stato.
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "snapshot.png";
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    OrgProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);    // superiore
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 10);
    midi.addEvent (juce::MidiMessage::noteOn (2, 55, (juce::uint8) 100), 20);   // inferiore
    midi.addEvent (juce::MidiMessage::noteOn (2, 36, (juce::uint8) 100), 30);   // basso (prime due ottave)
    float peak = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        buf.clear();
        proc.processBlock (buf, midi);
        midi.clear();
        peak = juce::jmax (peak, buf.getMagnitude (0, 0, 512));
    }
    std::printf ("picco uscita processore: %.3f\n", peak);
    std::printf ("tasti mostrati: sup. Do4=%d Mi4=%d, inf. Sol3=%d\n", proc.keyIsDown (0, 24), proc.keyIsDown (0, 28), proc.keyIsDown (1, 19));

    // MIDI Learn: CC 20 sulla risonanza dei bassi; fader KeyLab predefinito sul drawbar 8'
    proc.startLearn (proc.paramIndexOf ("bass_res"));
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 100), 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 79, 127), 1);
    proc.processBlock (buf, midi); midi.clear();
    std::printf ("CC appreso per bass_res: %d (valore %.3f); CC79 -> drawbar 8' sup. = %.2f\n",
                 proc.ccForParam (proc.paramIndexOf ("bass_res")), proc.apvts.getRawParameterValue ("bass_res")->load(),
                 proc.apvts.getRawParameterValue ("um_db2")->load());

    juce::MemoryBlock mb; proc.getStateInformation (mb);
    OrgProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
    std::printf ("dopo ricarica: CC per bass_res = %d\n", p2.ccForParam (p2.paramIndexOf ("bass_res")));

    // qualche comando in posizione "suonata" per l'istantanea
    auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("perc3", 1.0f); set ("perc_k1", 1.0f); set ("um_db3", 4.0f); set ("lm_db5", 3.0f); set ("vibrato", 0.4f);
    proc.guiKey (0, 31, true);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize ((int) (1170 * scale), (int) (484 * scale));
    proc.processBlock (buf, midi);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("salvato %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
