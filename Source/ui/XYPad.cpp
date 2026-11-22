#include "XYPad.h"

namespace stsample
{

XYPad::XYPad (Services& s, int p)
    : services (s), proc (s.getProcessor()), pad (p),
      px (proc.padParam (p, "x")), py (proc.padParam (p, "y"))
{
    proc.getKit().addChangeListener (this);
    setTooltip ("XY blend: drag to mix the four layers (Shift = fine, double-click centres). Drop a sample on a corner to load it");
}

XYPad::~XYPad()
{
    proc.getKit().removeChangeListener (this);
}

juce::Rectangle<float> XYPad::field() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

int XYPad::cellAt (juce::Point<float> p) const
{
    const auto f = field();
    if (! f.contains (p)) return -1;
    const int cx = p.x >= f.getCentreX() ? 1 : 0, cy = p.y >= f.getCentreY() ? 1 : 0;
    return cy * 2 + cx;
}

juce::Point<float> XYPad::valueAt (juce::Point<float> p) const
{
    const auto f = field().reduced (10.0f);
    return { juce::jlimit (0.0f, 1.0f, (p.x - f.getX()) / f.getWidth()), juce::jlimit (0.0f, 1.0f, (p.y - f.getY()) / f.getHeight()) };
}

void XYPad::setValue (juce::Point<float> v)
{
    px.setValueNotifyingHost (px.convertTo0to1 (juce::jlimit (0.0f, 1.0f, v.x)));
    py.setValueNotifyingHost (py.convertTo0to1 (juce::jlimit (0.0f, 1.0f, v.y)));
}

void XYPad::tick (double)
{
    const float x = proc.getPadValue (pad, "x"), y = proc.getPadValue (pad, "y");
    if (std::abs (x - shownX) > 1.0e-5f || std::abs (y - shownY) > 1.0e-5f)
    {
        shownX = x;
        shownY = y;
        repaint();
    }
}

void XYPad::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto& kit = proc.getKit();
    const auto f = field();
    const float x = proc.getPadValue (pad, "x"), y = proc.getPadValue (pad, "y");
    const unsigned mask = proc.getEngine().getLoadedMask (pad);
    const auto weights = blendWeights (x, y, mask);

    style::paintWell (g, f, 10.0f);

    // corner cells, lit by their weight
    const float gap = 3.0f;
    const auto inner = f.reduced (6.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        const int cx = l % 2, cy = l / 2;
        auto cell = juce::Rectangle<float> (inner.getX() + (float) cx * (inner.getWidth() + gap) * 0.5f,
                                            inner.getY() + (float) cy * (inner.getHeight() + gap) * 0.5f,
                                            (inner.getWidth() - gap) * 0.5f, (inner.getHeight() - gap) * 0.5f);
        const auto colour = layerColour (l);
        const bool loaded = ((mask >> l) & 1u) != 0;
        const float w = weights[(size_t) l];

        g.setColour (loaded ? colour.withAlpha (0.05f + 0.16f * w) : juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (cell, 6.0f);
        if (loaded && w > 0.01f)
        {
            // light pooling from the outer corner
            const auto corner = juce::Point<float> (cx == 0 ? cell.getX() : cell.getRight(), cy == 0 ? cell.getY() : cell.getBottom());
            juce::ColourGradient pool (colour.withAlpha (0.28f * w), corner.x, corner.y, colour.withAlpha (0.0f),
                                       corner.x + cell.getWidth() * (cx == 0 ? 1.1f : -1.1f), corner.y, true);
            g.setGradientFill (pool);
            g.fillRoundedRectangle (cell, 6.0f);
        }
        if (dropCell == l)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.drawRoundedRectangle (cell.reduced (0.75f), 6.0f, 1.5f);
        }

        // letter + name + weight
        g.setFont (st::fonts::semibold (24.0f));
        g.setColour (loaded ? colour.withAlpha (0.35f + 0.6f * w) : t.textFaint.withAlpha (0.5f));
        g.drawText (layerLetter (l), cell.withTrimmedBottom (cell.getHeight() * 0.18f), juce::Justification::centred, false);
        g.setFont (st::fonts::medium (10.5f));
        const auto& layer = kit.getLayer (pad, l);
        g.setColour (loaded ? t.textDim.withAlpha (0.55f + 0.4f * w) : t.textFaint.withAlpha (0.6f));
        const auto sub = cell.withTrimmedTop (cell.getHeight() * 0.6f).reduced (6.0f, 0.0f).withHeight (14.0f);
        g.drawText (layer.source.isEmpty() ? juce::String ("Empty") : layer.name, sub, juce::Justification::centred, true);
        if (loaded && (dragging || hovering))
        {
            g.setColour (colour.withAlpha (0.9f));
            g.drawText (juce::String (juce::roundToInt (w * w * 100.0f)) + "%", sub.translated (0.0f, 13.0f), juce::Justification::centred, false);
        }
    }

    // crosshair through the puck (faded ends) and the puck itself
    const auto area = f.reduced (10.0f);
    const juce::Point<float> c { area.getX() + x * area.getWidth(), area.getY() + y * area.getHeight() };
    const auto line = juce::Colours::white.withAlpha (dragging ? 0.3f : 0.16f);
    st::paint::fadedLine (g, { f.getX() + 4.0f, c.y }, { f.getRight() - 4.0f, c.y }, line, 1.0f, 0.3f);
    st::paint::fadedLine (g, { c.x, f.getY() + 4.0f }, { c.x, f.getBottom() - 4.0f }, line, 1.0f, 0.3f);

    const float r = dragging ? 9.0f : 8.0f;
    g.setColour (t.accent.withAlpha (dragging || hovering ? 0.28f : 0.16f));
    g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c.translated (0.0f, 1.5f)));
    g.setGradientFill (juce::ColourGradient (t.knobLight, c.x, c.y - r, t.knobDark, c.x, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (c));
}

void XYPad::mouseMove (const juce::MouseEvent&)
{
    if (! hovering) { hovering = true; repaint(); }
}

void XYPad::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const int cell = cellAt (e.position);
        if (cell >= 0) services.showSampleMenu (pad, cell, *this);
        return;
    }
    if (e.mods.isCommandDown()) { proc.undoManager.beginNewTransaction(); px.beginChangeGesture(); py.beginChangeGesture();
                                  setValue ({ 0.5f, 0.5f }); px.endChangeGesture(); py.endChangeGesture(); return; }

    proc.undoManager.beginNewTransaction();
    px.beginChangeGesture();
    py.beginChangeGesture();
    dragging = true;
    dragStart = e.position;
    // Shift: relative (fine) movement; otherwise the puck jumps to the click
    if (! e.mods.isShiftDown()) setValue (valueAt (e.position));
    valueAtStart = { proc.getPadValue (pad, "x"), proc.getPadValue (pad, "y") };
    repaint();
}

void XYPad::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    if (e.mods.isShiftDown())
    {
        const auto a = field().reduced (10.0f);
        const auto d = (e.position - dragStart) * 0.2f;
        setValue ({ valueAtStart.x + d.x / a.getWidth(), valueAtStart.y + d.y / a.getHeight() });
    }
    else
    {
        setValue (valueAt (e.position));
        valueAtStart = { proc.getPadValue (pad, "x"), proc.getPadValue (pad, "y") };
        dragStart = e.position;
    }
}

void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    px.endChangeGesture();
    py.endChangeGesture();
    repaint();
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    proc.undoManager.beginNewTransaction();
    proc.setPlain (px, 0.5f);
    proc.setPlain (py, 0.5f);
}

bool XYPad::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void XYPad::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int c = cellAt ({ (float) x, (float) y });
    if (c != dropCell) { dropCell = c; repaint(); }
}

void XYPad::fileDragExit (const juce::StringArray&)
{
    dropCell = -1;
    repaint();
}

void XYPad::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int c = cellAt ({ (float) x, (float) y });
    dropCell = -1;
    repaint();
    if (c >= 0) services.dropFiles (pad, c, files);
}

} // namespace stsample
