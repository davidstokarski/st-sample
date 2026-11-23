// Use-case videos of the real editor (website), on the shell's video kit (st_shell/testing/st_video.h): a groove
// plays the kit over MIDI while a scripted pointer works the UI.
//   StSamplePluginTests --usecases <dir> [scene ...]     (--video renders the looping demo clip)
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/PadList.h"
#include "ui/XYPad.h"
#include <st_shell/testing/st_video.h>

using st::video::Director;
using st::video::Music;
using st::video::Scene;

namespace
{
/** 16ths at 100 BPM: kick, snare, closed and open hat (C1, D1, F#1, A#1). */
void groove (juce::MidiBuffer& midi, int64_t start, int numSamples)
{
    constexpr int64_t step = (int64_t) (st::video::sampleRate * 60.0 / 100.0 / 4.0);
    static const char* kick  = "x.....x...x.....x.....x..x......";
    static const char* snare = "....x.......x.......x.......x..x";
    static const char* hat   = "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.";
    static const char* open  = "..............x...............x.";
    for (int64_t s = ((start + step - 1) / step) * step; s < start + numSamples; s += step)
    {
        const int i = (int) ((s / step) % 32);
        const int off = (int) (s - start);
        auto hit = [&] (int note, int vel) { midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), off); };
        if (kick[i] == 'x') hit (36, 120);
        if (snare[i] == 'x') hit (38, i == 31 ? 70 : 115);
        if (hat[i] == 'x' && open[i] != 'x') hit (42, i % 4 == 0 ? 100 : 70);
        if (open[i] == 'x') hit (46, 95);
    }
}

void loadKit (juce::AudioProcessor& ap, const juce::String& name, int pad)
{
    auto& p = dynamic_cast<StSampleProcessor&> (ap);
    for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
        if (p.presets.getPresets()[(size_t) i].name == name) p.presets.loadPreset (i);
    p.getKit().waitUntilLoaded();
    p.uiState.setProperty ("selectedPad", pad, nullptr);
}

juce::Point<float> padRow (Director& d, int pad)
{
    auto* list = d.findType<stsample::PadList>();
    if (list == nullptr) return d.ptr.pos;
    return d.pointIn (*list, { (float) list->getWidth() * 0.35f,
                               (float) stsample::PadList::titleHeight + ((float) pad + 0.5f) * (float) stsample::PadList::rowHeight });
}

juce::Point<float> layerKnob (Director& d, int pad, int layer, const char* suffix) { return d.controlCentre (stsample::ids::layer (pad, layer, suffix)); }

std::vector<Scene> scenes()
{
    std::vector<Scene> v;

    // 1. A kick built from layers: blend them on the XY pad, then pitch and trim the layers
    v.push_back ({ "layer-kick", 15.0, [] (juce::AudioProcessor& p, Music&) { loadKit (p, "Layer Lab", 0); }, [] (Director& dir)
    {
        dir.wait (1.2);
        dir.move ([&dir] { return dir.centreOf (dir.findType<stsample::XYPad>()); }, 1.0);
        dir.dragBy ({ 46.0f, -34.0f }, 1.6);                                      // toward the punchy layer
        dir.dragBy ({ -70.0f, 50.0f }, 1.8);                                      // and back toward the deep one
        dir.move ([&dir] { return layerKnob (dir, 0, 1, "pitch"); }, 1.0);
        dir.dragBy ({ 0.0f, -40.0f }, 1.0);                                       // layer B up a few semitones
        dir.move ([&dir] { return layerKnob (dir, 0, 0, "length"); }, 1.0);
        dir.dragBy ({ 0.0f, 45.0f }, 1.0);                                        // shorter tail on the deep layer
        dir.wait (2.4);
    }, groove });

    // 2. Tune the kit while it plays: a deeper, shorter snare, a tighter open hat
    v.push_back ({ "tune-kit", 15.0, [] (juce::AudioProcessor& p, Music&) { loadKit (p, "ST Kit", 0); }, [] (Director& dir)
    {
        dir.wait (1.2);
        dir.move ([&dir] { return padRow (dir, 2); }, 1.0);
        dir.click();                                                              // Snare
        dir.wait (0.6);
        dir.move ([&dir] { return layerKnob (dir, 2, 0, "pitch"); }, 1.0);
        dir.dragBy ({ 0.0f, 35.0f }, 1.1);                                        // down a couple of semitones
        dir.move ([&dir] { return layerKnob (dir, 2, 0, "length"); }, 0.9);
        dir.dragBy ({ 0.0f, 40.0f }, 1.0);                                        // shorter
        dir.move ([&dir] { return padRow (dir, 10); }, 1.1);
        dir.click();                                                              // Open Hat
        dir.wait (0.5);
        dir.move ([&dir] { return layerKnob (dir, 10, 0, "length"); }, 1.0);
        dir.dragBy ({ 0.0f, 60.0f }, 1.1);                                        // tighter
        dir.wait (2.2);
    }, groove });
    return v;
}
}

int runUseCaseVideos (int argc, char** argv);
int runUseCaseVideos (int argc, char** argv)
{
    return st::video::run (argc, argv, "st-sample", [] { return std::make_unique<StSampleProcessor>(); }, scenes(), "--usecases");
}
