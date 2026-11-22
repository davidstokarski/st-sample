#pragma once
#include "../Params.h"

/** ST Sample's identity inside the ST family (same structure as ST Sat / ST EQ / ST DC / ST Split: full-window
    backdrop, glass bars with grey buttons on their top edge, matte knobs, footer tray). Its own part: an ink-violet
    near-black, a violet accent, and the four layer colours (coral, gold, teal, sky) as the only strong hues. */
namespace stsample::style
{
    /** Recolours the shared ST tokens. Called once from the processor constructor. */
    void applyTheme();

    /** Window backdrop: violet haze at the top fading to near-black, vignette. Static (rendered once per scale). */
    void paintBackdrop (juce::Graphics&, juce::Rectangle<float> area);

    /** Footer tray: dark, rounded top corners, faded top edge light. */
    void paintTray (juce::Graphics&, juce::Rectangle<float> area);

    /** Translucent glass bar: soft shadow, glassPanel fill, hairline, a top edge light brightest in the middle
        (in `edge` when given, e.g. the layer's colour). */
    void paintGlassBar (juce::Graphics&, juce::Rectangle<float> box, juce::Colour edge = {});

    /** Recessed display well (wave lanes, XY pad): dark fill, inner shadow at the top, faint hairline. */
    void paintWell (juce::Graphics&, juce::Rectangle<float> box, float radius, juce::Colour tint = {});

    /** Small coloured letter badge ("A") used for layers. */
    void paintLayerBadge (juce::Graphics&, juce::Rectangle<float> box, int layer, bool dim = false);
}
