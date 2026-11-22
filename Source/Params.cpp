#include "Params.h"

namespace stsample
{

juce::Colour layerColour (int l)
{
    static const juce::Colour colours[kNumLayers] = {
        juce::Colour (0xffff8a5c),   // A coral
        juce::Colour (0xfff0c24f),   // B gold
        juce::Colour (0xff3fd0ae),   // C teal
        juce::Colour (0xff5ba8ff),   // D sky
    };
    return colours[juce::jlimit (0, kNumLayers - 1, l)];
}

namespace notes
{
    int& middleCOctave() noexcept { static int o = 3; return o; }

    juce::String name (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = juce::jlimit (0, 127, note);
        return juce::String (names[note % 12]) + juce::String (note / 12 + middleCOctave() - 5);
    }

    int parse (const juce::String& text)
    {
        auto t = text.trim().toLowerCase().removeCharacters (" ");
        if (t.isEmpty()) return -1;
        if (juce::CharacterFunctions::isDigit (t[0]))
        {
            const int n = t.getIntValue();
            return juce::isPositiveAndBelow (n, 128) ? n : -1;
        }
        if (t[0] < 'a' || t[0] > 'g') return -1;
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        int semis = base[t[0] - 'a'];
        int i = 1;
        if (t[i] == '#') { ++semis; ++i; }
        else if (t[i] == 'b') { --semis; ++i; }
        const auto octaveText = t.substring (i);
        if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789")) return -1;
        const int n = (octaveText.getIntValue() - middleCOctave() + 5) * 12 + semis;
        return juce::isPositiveAndBelow (n, 128) ? n : -1;
    }
}

juce::String gmDrumName (int note)
{
    static const char* names[] = {
        "Kick 2", "Kick", "Side Stick", "Snare", "Clap", "Snare 2", "Low Floor Tom", "Closed Hat", "High Floor Tom",
        "Pedal Hat", "Low Tom", "Open Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash", "High Tom", "Ride", "China",
        "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap", "Ride 2", "Hi Bongo", "Low Bongo",
        "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale", "High Agogo", "Low Agogo",
        "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "Hi Wood Block",
        "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle" };
    const int i = note - 35;
    return juce::isPositiveAndBelow (i, (int) std::size (names)) ? juce::String (names[i]) : juce::String();
}

std::array<float, kNumLayers> blendWeights (float x, float y, unsigned loadedMask) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);
    const float b[kNumLayers] = { (1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y };

    float sum = 0.0f;
    for (int l = 0; l < kNumLayers; ++l)
        if ((loadedMask >> l) & 1u) sum += b[l];

    std::array<float, kNumLayers> w {};
    if (sum <= 1.0e-9f) return w;
    const float norm = 1.0f / std::sqrt (juce::jmax (sum, 0.25f));
    for (int l = 0; l < kNumLayers; ++l)
        w[(size_t) l] = ((loadedMask >> l) & 1u) ? std::sqrt (b[l]) * norm : 0.0f;
    return w;
}

namespace
{
    juce::AudioParameterFloatAttributes levelAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= -59.95f ? juce::String ("-inf dB") : st::params::formatDb (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return juce::jmax (-60.0f, st::params::parseDb (s)); })
            .withLabel ("dB");
    }

    juce::AudioParameterFloatAttributes semitoneAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const bool whole = std::abs (v - std::round (v)) < 0.005f;
                auto s = juce::String (v, whole ? 0 : 2);
                if (s == "-0") s = "0";
                return (v > 0.004f ? "+" : "") + s + " st";
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.retainCharacters ("-+.0123456789").getFloatValue(); })
            .withLabel ("st");
    }

    juce::AudioParameterFloatAttributes panAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const int i = juce::roundToInt (v);
                return i == 0 ? juce::String ("C") : (i < 0 ? "L " : "R ") + juce::String (std::abs (i));
            })
            .withValueFromStringFunction ([] (const juce::String& s)
            {
                auto t = s.trim().toLowerCase();
                const float v = std::abs (t.retainCharacters (".0123456789").getFloatValue());
                if (t.startsWith ("l")) return -v;
                if (t.startsWith ("r")) return v;
                return t.retainCharacters ("-+.0123456789").getFloatValue();
            });
    }

    juce::AudioParameterFloatAttributes blendAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.retainCharacters ("-.0123456789").getFloatValue() * 0.01f; });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace st::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (makeFloat (ids::outputGain, "Output", linear (-36.0f, 12.0f, 0.01f), 0.0f, dbAttributes()));
    juce::StringArray poly;
    for (int v : kPolyphonyChoices) poly.add (juce::String (v));
    global->addChild (makeChoice (ids::polyphony, "Voices", poly, 2));
    layout.add (std::move (global));

    juce::StringArray chokes { "No choke" };
    for (int i = 1; i <= 8; ++i) chokes.add ("Choke " + juce::String (i));

    for (int p = 0; p < kNumPads; ++p)
    {
        const auto n = "Pad " + juce::String (p + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("pad" + juce::String (p + 1), n, "|");

        group->addChild (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ids::pad (p, "note"), 1 }, n + " Note", 0, 127, kFirstDefaultNote + p,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int v, int) { return notes::name (v); })
                .withValueFromStringFunction ([] (const juce::String& s) { return juce::jmax (0, notes::parse (s)); })));
        group->addChild (makeFloat (ids::pad (p, "level"), n + " Level", skewed (-60.0f, 12.0f, -12.0f, 0.01f), 0.0f, levelAttributes()));
        group->addChild (makeFloat (ids::pad (p, "pan"), n + " Pan", linear (-100.0f, 100.0f, 1.0f), 0.0f, panAttributes()));
        group->addChild (makeFloat (ids::pad (p, "vel"), n + " Velocity Floor", linear (0.0f, 100.0f, 0.1f), 20.0f, percentAttributes()));
        group->addChild (makeFloat (ids::pad (p, "x"), n + " Blend X", linear (0.0f, 1.0f), 0.5f, blendAttributes()));
        group->addChild (makeFloat (ids::pad (p, "y"), n + " Blend Y", linear (0.0f, 1.0f), 0.5f, blendAttributes()));
        group->addChild (makeChoice (ids::pad (p, "mode"), n + " Trigger", { "One-shot", "Gate" }, 0));
        group->addChild (makeChoice (ids::pad (p, "choke"), n + " Choke Group", chokes, 0));
        group->addChild (makeBool (ids::pad (p, "mute"), n + " Mute", false));
        group->addChild (makeBool (ids::pad (p, "solo"), n + " Solo", false));

        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto ln = n + " " + layerLetter (l);
            group->addChild (makeFloat (ids::layer (p, l, "gain"), ln + " Gain", skewed (-60.0f, 12.0f, -12.0f, 0.01f), 0.0f, levelAttributes()));
            group->addChild (makeFloat (ids::layer (p, l, "pitch"), ln + " Pitch", linear (-24.0f, 24.0f, 0.01f), 0.0f, semitoneAttributes()));
            group->addChild (makeFloat (ids::layer (p, l, "start"), ln + " Start", linear (0.0f, 100.0f, 0.01f), 0.0f, percentAttributes()));
            group->addChild (makeFloat (ids::layer (p, l, "length"), ln + " Length", linear (1.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
            group->addChild (makeBool (ids::layer (p, l, "invert"), ln + " Phase Invert", false));
        }
        layout.add (std::move (group));
    }
    return layout;
}

} // namespace stsample
