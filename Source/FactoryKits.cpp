#include "FactoryKits.h"
#include "Kit.h"

namespace stsample
{

namespace
{
    /** Small fluent helper: one pad of a factory kit. */
    struct PadDef
    {
        int pad;
        juce::String name;
        juce::StringArray layers;   // factory sound names, "" = empty
        std::vector<std::pair<juce::String, float>> values;   // suffix → plain value ("x", "a_pitch", ...)
    };

    struct KitDef
    {
        juce::String name;
        std::vector<PadDef> pads;
    };

    void add (st::PresetManager& presets, const KitDef& kit)
    {
        std::vector<std::pair<juce::String, float>> values;
        for (auto& p : kit.pads)
            for (auto& [suffix, v] : p.values)
            {
                const bool layerParam = suffix.length() > 2 && suffix[1] == '_' && suffix[0] >= 'a' && suffix[0] <= 'd';
                values.push_back ({ layerParam ? ids::layer (p.pad, suffix[0] - 'a', suffix.substring (2)) : ids::pad (p.pad, suffix), v });
            }

        presets.addFactoryPreset (kit.name, "Kits", values, [kit] (juce::ValueTree& state)
        {
            juce::ValueTree k (Kit::kitType);
            for (auto& p : kit.pads)
            {
                juce::StringArray srcs;
                for (auto& l : p.layers) srcs.add (l.isEmpty() ? juce::String() : sources::factory (l));
                k.appendChild (Kit::buildPadTree (p.pad, p.name, srcs), nullptr);
            }
            state.appendChild (k, nullptr);
        });
    }
}

void registerFactoryKits (st::PresetManager& presets)
{
    // General MIDI layout from C1 (36): kick, side stick, snare, clap, snare 2, toms, hats, crash, ride.
    add (presets, { "ST Kit", {
        { 0, "Kick", { "Kick Deep", "Kick Punch", "Kick Click" }, { { "x", 0.32f }, { "y", 0.3f }, { "c_gain", -4.0f } } },
        { 1, "Side Stick", { "Rim" }, { { "level", -4.0f } } },
        { 2, "Snare", { "Snare Crack", "Snare Body", "Snare Rattle" }, { { "x", 0.4f }, { "y", 0.36f } } },
        { 3, "Clap", { "Clap" }, { { "level", -2.0f }, { "pan", 6.0f } } },
        { 4, "Snare 2", { "Snare Body", "Clap" }, { { "a_pitch", 2.0f }, { "x", 0.3f }, { "y", 0.0f } } },
        { 5, "Low Floor Tom", { "Tom Low" }, { { "a_pitch", -2.0f }, { "pan", -30.0f } } },
        { 6, "Closed Hat", { "Closed Hat" }, { { "choke", 1.0f }, { "level", -5.0f }, { "pan", 18.0f } } },
        { 7, "High Floor Tom", { "Tom Low" }, { { "a_pitch", 1.0f }, { "pan", -22.0f } } },
        { 8, "Pedal Hat", { "Pedal Hat" }, { { "choke", 1.0f }, { "level", -7.0f }, { "pan", 18.0f } } },
        { 9, "Low Tom", { "Tom Mid" }, { { "a_pitch", -1.0f }, { "pan", -12.0f } } },
        { 10, "Open Hat", { "Open Hat" }, { { "choke", 1.0f }, { "level", -6.0f }, { "pan", 18.0f } } },
        { 11, "Low-Mid Tom", { "Tom Mid" }, { { "a_pitch", 1.5f }, { "pan", -4.0f } } },
        { 12, "Hi-Mid Tom", { "Tom High" }, { { "pan", 6.0f } } },
        { 13, "Crash", { "Crash" }, { { "level", -6.0f }, { "pan", -26.0f } } },
        { 14, "High Tom", { "Tom High" }, { { "a_pitch", 2.5f }, { "pan", 14.0f } } },
        { 15, "Ride", { "Ride" }, { { "level", -7.0f }, { "pan", 28.0f } } },
    } });

    // Four layers per hit: move the XY dot to morph between weights of kick and snare.
    add (presets, { "Layer Lab", {
        { 0, "Kick Morph", { "Kick Deep", "Kick Punch", "Kick Sub", "Kick Click" },
          { { "x", 0.25f }, { "y", 0.25f }, { "d_gain", -3.0f }, { "c_length", 70.0f } } },
        { 1, "Rim", { "Rim" }, { { "level", -4.0f } } },
        { 2, "Snare Morph", { "Snare Crack", "Snare Body", "Snare Rattle", "Clap" },
          { { "x", 0.38f }, { "y", 0.42f }, { "b_pitch", -1.0f } } },
        { 3, "Clap Stack", { "Clap", "Snare Rattle" }, { { "x", 0.2f }, { "y", 0.0f }, { "b_length", 45.0f } } },
        { 6, "Closed Hat", { "Closed Hat", "Shaker" }, { { "choke", 1.0f }, { "level", -5.0f }, { "x", 0.25f }, { "y", 0.0f } } },
        { 8, "Pedal Hat", { "Pedal Hat" }, { { "choke", 1.0f }, { "level", -7.0f } } },
        { 10, "Open Hat", { "Open Hat" }, { { "choke", 1.0f }, { "level", -6.0f }, { "a_length", 60.0f } } },
        { 13, "Crash", { "Crash" }, { { "level", -6.0f } } },
        { 15, "Ride", { "Ride" }, { { "level", -7.0f } } },
    } });

    // Long sub kick, clap, cowbell and pitched-down toms.
    add (presets, { "Boom Kit", {
        { 0, "Boom", { "Kick Sub", "Kick Click" }, { { "x", 0.12f }, { "y", 0.0f }, { "b_gain", -6.0f } } },
        { 1, "Rim", { "Rim" }, { { "a_pitch", -2.0f }, { "level", -5.0f } } },
        { 2, "Snare", { "Snare Body", "Snare Rattle" }, { { "a_pitch", -3.0f }, { "x", 0.35f }, { "y", 0.0f } } },
        { 3, "Clap", { "Clap" }, { { "a_pitch", -1.0f } } },
        { 5, "Tom Low", { "Tom Low" }, { { "a_pitch", -5.0f } } },
        { 6, "Closed Hat", { "Closed Hat" }, { { "choke", 1.0f }, { "level", -6.0f }, { "a_pitch", -2.0f } } },
        { 7, "Tom Mid", { "Tom Mid" }, { { "a_pitch", -5.0f } } },
        { 9, "Tom High", { "Tom High" }, { { "a_pitch", -5.0f } } },
        { 10, "Open Hat", { "Open Hat" }, { { "choke", 1.0f }, { "level", -7.0f }, { "a_pitch", -2.0f }, { "a_length", 45.0f } } },
        { 11, "Shaker", { "Shaker" }, { { "level", -8.0f } } },
        { 12, "Cowbell", { "Cowbell" }, { { "level", -8.0f } } },
        { 13, "Crash", { "Crash" }, { { "level", -7.0f }, { "a_pitch", -3.0f } } },
    } });
}

} // namespace stsample
