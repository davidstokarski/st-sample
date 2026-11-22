#include "PadList.h"

namespace stsample
{

PadList::PadList (Services& s) : services (s), proc (s.getProcessor())
{
    for (int p = 0; p < kNumPads; ++p) seenHits[(size_t) p] = proc.getEngine().getHitCount (p);
    proc.getKit().addChangeListener (this);
    setTooltip ("Pads: click to select, Option-click to play, double-click to rename, drag onto the keyboard to map, "
                "drop samples to load them");
}

PadList::~PadList()
{
    proc.getKit().removeChangeListener (this);
}

juce::Rectangle<int> PadList::rowBounds (int pad) const
{
    return { 0, titleHeight + pad * rowHeight, getWidth(), rowHeight };
}

int PadList::rowAt (juce::Point<int> p) const
{
    if (p.y < titleHeight || p.x < 0 || p.x >= getWidth()) return -1;
    const int r = (p.y - titleHeight) / rowHeight;
    return juce::isPositiveAndBelow (r, kNumPads) ? r : -1;
}

void PadList::setSelected (int pad)
{
    if (pad != selected) { selected = pad; repaint(); }
}

void PadList::tick (double dt)
{
    // notes, mute and solo can change from automation / undo: repaint when they do
    juce::int64 sig = 0;
    for (int p = 0; p < kNumPads; ++p)
        sig = sig * 131 + proc.getPadNote (p) * 4 + (proc.getPadValue (p, "mute") >= 0.5f ? 1 : 0) + (proc.getPadValue (p, "solo") >= 0.5f ? 2 : 0);
    if (sig != paramSignature) { paramSignature = sig; repaint(); }

    // hit lights flash on every trigger (MIDI or audition) and fade
    for (int p = 0; p < kNumPads; ++p)
    {
        const auto h = proc.getEngine().getHitCount (p);
        auto& f = flash[(size_t) p];
        if (h != seenHits[(size_t) p])
        {
            seenHits[(size_t) p] = h;
            f = juce::jlimit (0.35f, 1.0f, 0.35f + 0.65f * proc.getEngine().getLastVelocity (p));
            repaint (rowBounds (p));
        }
        else if (f > 0.0f)
        {
            f = juce::jmax (0.0f, f - (float) dt * 3.2f);
            repaint (rowBounds (p).removeFromLeft (22));
        }
    }
}

void PadList::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto& kit = proc.getKit();

    st::paint::caption (g, "Pads", getLocalBounds().removeFromTop (titleHeight).toFloat().withTrimmedLeft (14.0f).withTrimmedBottom (6.0f),
                        t.textDim.withAlpha (0.8f), juce::Justification::bottomLeft, 10.5f);

    bool anySolo = false;
    for (int p = 0; p < kNumPads; ++p) anySolo |= proc.getPadValue (p, "solo") >= 0.5f;

    for (int p = 0; p < kNumPads; ++p)
    {
        auto r = rowBounds (p).toFloat();
        const bool sel = p == selected, hov = p == hovered, empty = kit.isPadEmpty (p);
        const bool muted = proc.getPadValue (p, "mute") >= 0.5f, soloed = proc.getPadValue (p, "solo") >= 0.5f;
        const bool silent = muted || (anySolo && ! soloed);

        // selection: lit, not boxed — a soft violet wash with an accent bar
        auto row = r.reduced (2.0f, 1.5f);
        if (sel)
        {
            juce::ColourGradient wash (t.accent.withAlpha (0.20f), row.getX(), 0.0f, t.accent.withAlpha (0.03f), row.getRight(), 0.0f, false);
            g.setGradientFill (wash);
            g.fillRoundedRectangle (row, 6.0f);
            g.setColour (t.accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, row.getHeight() - 12.0f).withPosition (row.getX(), row.getY() + 6.0f), 1.5f);
        }
        else if (hov || p == dropRow)
        {
            g.setColour (juce::Colours::white.withAlpha (p == dropRow ? 0.08f : 0.035f));
            g.fillRoundedRectangle (row, 6.0f);
        }
        if (p == dropRow)
        {
            g.setColour (t.accent.withAlpha (0.8f));
            g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f);
        }

        // hit light
        const auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ row.getX() + 14.0f, row.getCentreY() });
        const float f = flash[(size_t) p];
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (led.expanded (1.0f));
        g.setColour ((empty ? t.textFaint : t.textDim).withAlpha (0.25f).interpolatedWith (t.accent2, f));
        g.fillEllipse (led);
        if (f > 0.0f)
        {
            g.setColour (t.accent2.withAlpha (0.35f * f));
            g.fillEllipse (led.expanded (4.0f * f));
        }

        // note
        auto text = row.withTrimmedLeft (26.0f).reduced (0.0f, 2.0f);
        g.setFont (st::fonts::medium (11.5f));
        g.setColour (sel ? t.text.withAlpha (0.85f) : t.textDim.withAlpha (empty ? 0.45f : 0.75f));
        g.drawText (notes::name (proc.getPadNote (p)), text.removeFromLeft (34.0f), juce::Justification::centredLeft, false);

        // layer dots (right)
        auto dots = text.removeFromRight (38.0f);
        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto d = juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ dots.getX() + 6.0f + 8.0f * (float) l, dots.getCentreY() });
            const bool loaded = kit.getLayer (p, l).source.isNotEmpty();
            g.setColour (loaded ? layerColour (l).withAlpha (silent ? 0.35f : 0.9f) : juce::Colours::white.withAlpha (0.08f));
            g.fillEllipse (d);
        }

        // mute / solo tags
        if (muted || soloed)
        {
            auto tag = text.removeFromRight (16.0f);
            g.setFont (st::fonts::bold (10.0f));
            g.setColour (soloed ? t.warning : t.textDim);
            g.drawText (soloed ? "S" : "M", tag, juce::Justification::centred, false);
        }

        // name
        g.setFont (sel ? st::fonts::semibold (13.0f) : st::fonts::medium (13.0f));
        g.setColour (empty ? t.textFaint : (silent ? t.textDim.withAlpha (0.6f) : (sel ? t.text : t.text.withAlpha (0.86f))));
        g.drawText (empty ? juce::String ("Empty") : kit.getPadName (p), text.withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
    }
}

void PadList::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r != hovered) { hovered = r; repaint(); }
}

void PadList::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0) { hovered = -1; repaint(); }
}

void PadList::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    const int r = rowAt (e.getPosition());
    if (r < 0) return;
    if (e.mods.isPopupMenu())
    {
        services.selectPad (r);
        services.showPadMenu (r, *this);
        return;
    }
    services.selectPad (r);
    if (e.mods.isAltDown() || e.getNumberOfClicks() == 1 && e.mods.isCommandDown())
        proc.auditionPad (r);
}

void PadList::mouseDrag (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getMouseDownPosition());
    if (r < 0 || dragStarted || e.getDistanceFromDragStart() < 6 || e.mods.isPopupMenu()) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        juce::Image img (juce::Image::ARGB, 120, 26, true);
        {
            juce::Graphics g (img);
            const auto& t = st::Theme::get();
            g.setColour (t.bg3.withAlpha (0.92f));
            g.fillRoundedRectangle (0.0f, 0.0f, 120.0f, 26.0f, 6.0f);
            g.setColour (t.accent);
            g.drawRoundedRectangle (0.5f, 0.5f, 119.0f, 25.0f, 6.0f, 1.0f);
            g.setColour (t.text);
            g.setFont (st::fonts::medium (12.0f));
            g.drawText (proc.getKit().getPadName (r), juce::Rectangle<int> (8, 0, 104, 26), juce::Justification::centredLeft, true);
        }
        container->startDragging (padDragDescription (r), this, juce::ScaledImage (img), true);
    }
}

void PadList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r >= 0 && ! e.mods.isAltDown()) services.renamePad (r);
}

bool PadList::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void PadList::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int r = rowAt ({ x, y });
    if (r != dropRow) { dropRow = r; repaint(); }
}

void PadList::fileDragExit (const juce::StringArray&)
{
    dropRow = -1;
    repaint();
}

void PadList::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropRow = -1;
    const int r = rowAt ({ x, y });
    repaint();
    if (r < 0) return;
    services.selectPad (r);
    // several files dropped on a pad fill it from layer A; one file goes into the first free layer
    services.dropFiles (r, files.size() > 1 ? 0 : -1, files);
}

} // namespace stsample
