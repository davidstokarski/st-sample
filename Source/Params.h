#pragma once
#include <st_shell/st_shell.h>

namespace stsample
{

/** 16 pads, each mapped to one MIDI note, each with up to 4 sample layers (A–D) blended on an XY pad. */
constexpr int kNumPads = 16;
constexpr int kNumLayers = 4;
/** Choices of the polyphony parameter (sounding voices). The engine keeps a few spare voices so stolen ones fade. */
constexpr int kPolyphonyChoices[] = { 8, 16, 32, 64 };
constexpr int kMaxVoices = 64;
constexpr int kVoicePool = kMaxVoices + 12;
/** Default note of pad 0; the 16 pads cover the General MIDI drum map C1..D#2 (36..51). */
constexpr int kFirstDefaultNote = 36;

namespace ids
{
    inline const juce::String outputGain = "out_gain";
    inline const juce::String polyphony  = "polyphony";

    // Per-pad: "p<n>_<suffix>", n = 1..16
    inline const juce::StringArray padSuffixes { "note", "level", "pan", "vel", "x", "y", "mode", "choke", "mute", "solo" };
    inline juce::String pad (int p, const juce::String& suffix) { return "p" + juce::String (p + 1) + "_" + suffix; }

    // Per-layer: "p<n>_<a..d>_<suffix>"
    inline const juce::StringArray layerSuffixes { "gain", "pitch", "start", "length", "invert" };
    inline juce::String layer (int p, int l, const juce::String& suffix)
    {
        return "p" + juce::String (p + 1) + "_" + juce::String::charToString ((juce::juce_wchar) ('a' + l)) + "_" + suffix;
    }
}

/** "A".."D" */
inline juce::String layerLetter (int l) { return juce::String::charToString ((juce::juce_wchar) ('A' + l)); }
/** Identity colour of each layer (coral, gold, teal, sky). */
juce::Colour layerColour (int l);

/** Note names. Middle C (60) is "C3" by default (Logic, Ableton, Cubase, Studio One); some hosts call it C4 or C5. */
namespace notes
{
    int& middleCOctave() noexcept;
    juce::String name (int note);
    /** Parses "C3", "d#2", "Eb1", "60" → note number, or -1. */
    int parse (const juce::String&);
}

/** General MIDI drum name of a note (35..81), or empty. */
juce::String gmDrumName (int note);

/** XY blend: equal-power weights of the four corner layers (A top-left, B top-right, C bottom-left, D bottom-right).
    Layers that hold no sample get no weight and the others are rescaled so the blend keeps its loudness, up to a limit
    (+6 dB) so moving towards an empty corner still fades out smoothly. */
std::array<float, kNumLayers> blendWeights (float x, float y, unsigned loadedMask) noexcept;

/** Velocity (0..1) → gain, with a floor: floor 0 = fully velocity sensitive, 1 = always full level. */
inline float velocityGain (float velocity, float floor) noexcept
{
    return floor + (1.0f - floor) * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.5f);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace stsample
