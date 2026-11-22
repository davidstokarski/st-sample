#include "Style.h"

namespace stsample::style
{

void applyTheme()
{
    // Near-black with an ink-violet cast: neutral enough that the four layer colours read true.
    auto& t = st::Theme::get();
    t.bg0 = juce::Colour (0xff09080c);
    t.bg1 = juce::Colour (0xff0f0e14);
    t.bg2 = juce::Colour (0xff16141c);
    t.bg3 = juce::Colour (0xff1f1c27);
    t.bg4 = juce::Colour (0xff2a2633);
    t.stroke = juce::Colour (0xff2a2632);
    t.strokeSoft = juce::Colour (0xff19171f);
    t.highlight = juce::Colour (0x12ffffff);
    t.displayTop = juce::Colour (0xff1b1626);    // violet haze at the top of the window
    t.displayGlow = juce::Colour (0xff34284d);
    t.glassPanel = juce::Colour (0xb81b1823);
    t.knobLight = juce::Colour (0xffb9b7bf);
    t.knobDark = juce::Colour (0xff6a6772);
    t.buttonTop = juce::Colour (0xff57535f);
    t.buttonBottom = juce::Colour (0xff3e3b46);
    t.text = juce::Colour (0xffedebf2);
    t.textDim = juce::Colour (0xffa8a3b3);
    t.textFaint = juce::Colour (0xff6b6677);
    t.accent = juce::Colour (0xff9479ff);        // violet
    t.accent2 = juce::Colour (0xffd08cff);       // orchid
    t.spectrum = juce::Colour (0xffc6c0d6);
    t.curve = juce::Colour (0xffece8f5);
    t.danger = juce::Colour (0xffe5554a);
    t.warning = juce::Colour (0xffe9c35c);
}

void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::ColourGradient base (t.displayTop, r.getCentreX(), r.getY(), t.bg0, r.getCentreX(), r.getBottom(), false);
    base.addColour (0.42, t.displayTop.interpolatedWith (t.bg0, 0.55f));
    base.addColour (0.78, t.bg1.interpolatedWith (t.bg0, 0.5f));
    g.setGradientFill (base);
    g.fillRect (r);

    auto pool = [&] (juce::Point<float> c, float radius, juce::Colour col, float alpha)
    {
        juce::ColourGradient rg (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + radius, c.y, true);
        g.setGradientFill (rg);
        g.fillRect (r);
    };
    pool ({ r.getCentreX() + r.getWidth() * 0.1f, r.getY() - r.getHeight() * 0.12f }, r.getWidth() * 0.62f, t.displayGlow, 0.5f);
    pool ({ r.getX() + r.getWidth() * 0.08f, r.getY() + r.getHeight() * 0.55f }, r.getWidth() * 0.3f, t.accent, 0.045f);
    pool ({ r.getRight() - r.getWidth() * 0.12f, r.getY() + r.getHeight() * 0.38f }, r.getWidth() * 0.32f, t.accent2, 0.04f);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getY() + r.getHeight() * 0.42f,
                                   juce::Colours::black.withAlpha (0.42f), r.getX() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.42f, true);
    g.setGradientFill (vignette);
    g.fillRect (r);
}

void paintTray (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::Path bar;
    bar.addRoundedRectangle (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, r.getHeight() + 20.0f, 16.0f, 16.0f, true, true, false, false);
    g.setGradientFill (juce::ColourGradient (t.bg2.withAlpha (0.92f), 0.0f, r.getY(), t.bg0.withAlpha (0.96f), 0.0f, r.getBottom(), false));
    g.fillPath (bar);
    st::paint::fadedLine (g, { r.getX() + 24.0f, r.getY() + 0.5f }, { r.getRight() - 24.0f, r.getY() + 0.5f },
                          juce::Colours::white.withAlpha (0.10f), 1.0f, 0.18f);
}

void paintGlassBar (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour edge)
{
    const auto& t = st::Theme::get();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (box.translated (0.0f, 3.0f).expanded (2.0f), 9.0f);
    g.setColour (t.glassPanel);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    const auto light = edge.isTransparent() ? juce::Colours::white.withAlpha (0.13f) : edge.withAlpha (0.55f);
    juce::ColourGradient top (light.withAlpha (0.0f), box.getX(), 0.0f, light.withAlpha (0.0f), box.getRight(), 0.0f, false);
    top.addColour (0.25, light);
    top.addColour (0.75, light);
    g.setGradientFill (top);
    g.fillRect (box.getX() + 8.0f, box.getY() + 0.5f, box.getWidth() - 16.0f, 1.0f);
}

void paintWell (juce::Graphics& g, juce::Rectangle<float> box, float radius, juce::Colour tint)
{
    const auto& t = st::Theme::get();
    auto fill = t.bg0.withAlpha (0.62f);
    if (! tint.isTransparent()) fill = fill.interpolatedWith (tint, 0.05f);
    g.setColour (fill);
    g.fillRoundedRectangle (box, radius);

    // inner shadow from the top edge, kept inside the well
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (box, radius);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, box.getY(),
                                             juce::Colours::transparentBlack, 0.0f, box.getY() + 10.0f, false));
    g.fillRect (box.withHeight (10.0f));
    g.restoreState();

    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);
}

void paintLayerBadge (juce::Graphics& g, juce::Rectangle<float> box, int layer, bool dim)
{
    const auto c = layerColour (layer);
    const float rad = box.getHeight() * 0.3f;
    g.setColour (c.withAlpha (dim ? 0.12f : 0.2f));
    g.fillRoundedRectangle (box, rad);
    g.setColour (c.withAlpha (dim ? 0.35f : 0.8f));
    g.drawRoundedRectangle (box.reduced (0.5f), rad, 1.0f);
    g.setColour (dim ? c.withAlpha (0.5f) : c.brighter (0.25f));
    g.setFont (st::fonts::bold (box.getHeight() * 0.62f));
    g.drawText (layerLetter (layer), box, juce::Justification::centred, false);
}

} // namespace stsample::style
