#include "Keyboard.h"

namespace stsample
{

Keyboard::Keyboard (Services& s) : services (s), proc (s.getProcessor())
{
    lowest = juce::jlimit (0, 48, (int) proc.uiState.getProperty ("keyboardLowest", 12));
    for (int p = 0; p < kNumPads; ++p) seenHits[(size_t) p] = proc.getEngine().getHitCount (p);
    setTooltip ("Click a key to play it, drag a pad here to map it, drop samples on a key to load them");
}

Keyboard::~Keyboard() = default;

bool Keyboard::isBlack (int note) noexcept
{
    const int k = note % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

juce::Rectangle<float> Keyboard::keysArea() const
{
    return getLocalBounds().toFloat().reduced ((float) arrowWidth + 4.0f, 0.0f);
}

juce::Rectangle<float> Keyboard::keyBounds (int note) const
{
    const auto a = keysArea();
    const float ww = a.getWidth() / (float) (octaves * 7);
    static const int whiteIndex[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const int rel = note - lowest;
    const int oct = rel / 12, k = rel % 12;
    const float x = a.getX() + ((float) (oct * 7 + whiteIndex[k])) * ww;
    if (! isBlack (note)) return { x, a.getY(), ww, a.getHeight() };
    const float bw = ww * 0.62f;
    return { x + ww - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.6f };
}

int Keyboard::keyAt (juce::Point<float> p) const
{
    if (! keysArea().contains (p)) return -1;
    for (int n = lowest; n < juce::jmin (128, lowest + octaves * 12); ++n)
        if (isBlack (n) && keyBounds (n).contains (p)) return n;
    for (int n = lowest; n < juce::jmin (128, lowest + octaves * 12); ++n)
        if (! isBlack (n) && keyBounds (n).contains (p)) return n;
    return -1;
}

int Keyboard::arrowAt (juce::Point<float> p) const
{
    if (p.x < (float) arrowWidth + 2.0f) return -1;
    if (p.x > (float) getWidth() - (float) arrowWidth - 2.0f) return 1;
    return 0;
}

void Keyboard::shift (int delta)
{
    const int next = juce::jlimit (0, 48, lowest + delta * 12);
    if (next == lowest) return;
    lowest = next;
    proc.uiState.setProperty ("keyboardLowest", lowest, nullptr);
    repaint();
}

void Keyboard::showNote (int note)
{
    while (note < lowest && lowest > 0) shift (-1);
    while (note >= lowest + octaves * 12 && lowest + octaves * 12 < 128) shift (1);
}

void Keyboard::tick (double dt)
{
    bool changed = false;
    for (int p = 0; p < kNumPads; ++p)
    {
        const auto h = proc.getEngine().getHitCount (p);
        if (h != seenHits[(size_t) p])
        {
            seenHits[(size_t) p] = h;
            flash[(size_t) proc.getPadNote (p)] = 1.0f;
            changed = true;
        }
    }
    for (int n = 0; n < 128; ++n)
    {
        const bool d = proc.getEngine().isNoteDown (n) || n == playingKey;
        if (d != down[(size_t) n]) { down[(size_t) n] = d; changed = true; }
        auto& f = flash[(size_t) n];
        if (f > 0.0f) { f = juce::jmax (0.0f, f - (float) dt * 4.0f); changed = true; }
    }
    juce::int64 sig = services.getSelectedPad();
    for (int p = 0; p < kNumPads; ++p) sig = sig * 131 + proc.getPadNote (p);
    if (sig != mapSignature) { mapSignature = sig; changed = true; }
    if (changed) repaint();
}

void Keyboard::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int selectedNote = proc.getPadNote (services.getSelectedPad());
    std::array<int, 128> padOn;
    padOn.fill (-1);
    for (int p = kNumPads - 1; p >= 0; --p)
        padOn[(size_t) proc.getPadNote (p)] = p;

    // arrows
    for (int side : { -1, 1 })
    {
        auto a = side < 0 ? getLocalBounds().toFloat().removeFromLeft ((float) arrowWidth)
                          : getLocalBounds().toFloat().removeFromRight ((float) arrowWidth);
        const bool can = side < 0 ? lowest > 0 : lowest + octaves * 12 < 128;
        st::icons::draw (g, side < 0 ? st::icons::Icon::chevronLeft : st::icons::Icon::chevronRight,
                         a.withSizeKeepingCentre (12.0f, 12.0f),
                         t.text.withAlpha (! can ? 0.15f : (hoverArrow == side ? 0.9f : 0.45f)), 1.6f);
    }

    const auto area = keysArea();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (area.expanded (1.0f, 1.0f), 5.0f);

    auto drawKey = [&] (int n)
    {
        const auto k = keyBounds (n);
        const bool black = isBlack (n);
        const int p = padOn[(size_t) n];
        const bool mapped = p >= 0, sel = n == selectedNote, isDown = down[(size_t) n];
        const float f = flash[(size_t) n];
        auto r = black ? k : k.reduced (0.5f, 0.0f);

        juce::Colour top = black ? juce::Colour (0xff17151c) : t.knobLight.withAlpha (0.20f);
        juce::Colour bottom = black ? juce::Colour (0xff0c0b10) : t.knobLight.withAlpha (0.13f);
        if (mapped)
        {
            const float amt = sel ? 0.8f : 0.34f;
            top = top.interpolatedWith (t.accent, amt);
            bottom = bottom.interpolatedWith (t.accent.darker (0.3f), amt);
        }
        if (n == hoverKey || n == dropKey) { top = top.brighter (0.25f); bottom = bottom.brighter (0.2f); }
        if (isDown || f > 0.0f)
        {
            const float a = isDown ? 0.7f : 0.6f * f;
            top = top.interpolatedWith (t.accent2, a);
            bottom = bottom.interpolatedWith (t.accent2.darker (0.2f), a);
        }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, black ? 2.0f : 3.0f);
        if (black)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (r.reduced (2.0f, 0.0f).withHeight (1.0f).translated (0.0f, r.getHeight() - 3.0f));
        }
        if (n == dropKey)
        {
            g.setColour (t.accent2);
            g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.5f);
        }

        // labels: C of every octave, and the mapped pad's initial on its key
        if (! black && n % 12 == 0)
        {
            g.setFont (st::fonts::medium (9.5f));
            g.setColour (mapped ? t.text.withAlpha (0.9f) : t.textDim.withAlpha (0.6f));
            g.drawText (notes::name (n), r.withTrimmedTop (r.getHeight() - 15.0f), juce::Justification::centred, false);
        }
    };

    for (int n = lowest; n < juce::jmin (128, lowest + octaves * 12); ++n) if (! isBlack (n)) drawKey (n);
    for (int n = lowest; n < juce::jmin (128, lowest + octaves * 12); ++n) if (isBlack (n)) drawKey (n);

    // hovered key: its note and pad
    const int info = dropKey >= 0 ? dropKey : hoverKey;
    if (info >= 0)
    {
        const int p = padOn[(size_t) info];
        auto txt = notes::name (info);
        if (p >= 0) txt << "  " << proc.getKit().getPadName (p);
        g.setFont (st::fonts::medium (11.0f));
        const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 14.0f;
        const auto k = keyBounds (info);
        auto box = juce::Rectangle<float> (w, 18.0f).withCentre ({ juce::jlimit (area.getX() + w * 0.5f, area.getRight() - w * 0.5f, k.getCentreX()), area.getY() + 11.0f });
        g.setColour (t.bg1.withAlpha (0.94f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (t.text);
        g.drawText (txt, box, juce::Justification::centred, false);
    }
}

void Keyboard::mouseMove (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position), a = arrowAt (e.position);
    if (k != hoverKey || a != hoverArrow) { hoverKey = k; hoverArrow = a; repaint(); }
}

void Keyboard::mouseExit (const juce::MouseEvent&)
{
    hoverKey = -1;
    hoverArrow = 0;
    repaint();
}

void Keyboard::startNote (int note, float y)
{
    stopNote();
    if (note < 0) return;
    const auto a = keysArea();
    const float rel = juce::jlimit (0.0f, 1.0f, (y - a.getY()) / juce::jmax (1.0f, (isBlack (note) ? a.getHeight() * 0.6f : a.getHeight())));
    proc.auditionNote (note, 0.35f + 0.65f * rel, true);
    playingKey = note;
    const int p = proc.findPadForNote (note);
    if (p >= 0) services.selectPad (p);
}

void Keyboard::stopNote()
{
    if (playingKey >= 0) proc.auditionNote (playingKey, 0.0f, false);
    playingKey = -1;
}

void Keyboard::mouseDown (const juce::MouseEvent& e)
{
    if (const int a = arrowAt (e.position); a != 0) { shift (a); return; }
    const int k = keyAt (e.position);
    if (k < 0) return;

    const int sel = services.getSelectedPad();
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem ("Map " + proc.getKit().getPadName (sel) + " to " + notes::name (k), proc.getPadNote (sel) != k,
                   false, [this, sel, k] { proc.setPadNote (sel, k); });
        bool header = false;
        for (int p = 0; p < kNumPads; ++p)
            if (proc.getPadNote (p) == k && p != sel)
            {
                if (! header) { m.addSeparator(); header = true; }
                m.addItem ("Select " + proc.getKit().getPadName (p), [this, p] { services.selectPad (p); });
            }
        st::showMenu (m, this, nullptr, true);
        return;
    }

    // MIDI learn by clicking a key
    if (const int lp = proc.getLearnPad(); lp >= 0)
    {
        proc.setPadNote (lp, k);
        proc.setLearnPad (-1);
        proc.auditionNote (k, 0.8f, true);
        proc.auditionNote (k, 0.0f, false);
        return;
    }
    startNote (k, e.position.y);
}

void Keyboard::mouseDrag (const juce::MouseEvent& e)
{
    if (playingKey < 0) return;
    const int k = keyAt (e.position);
    if (k >= 0 && k != playingKey) startNote (k, e.position.y);
}

void Keyboard::mouseUp (const juce::MouseEvent&)
{
    stopNote();
}

//==============================================================================
bool Keyboard::isInterestedInDragSource (const SourceDetails& d) { return padFromDragDescription (d.description) >= 0; }

void Keyboard::itemDragMove (const SourceDetails& d)
{
    const int k = keyAt (d.localPosition.toFloat());
    if (k != dropKey) { dropKey = k; repaint(); }
}

void Keyboard::itemDragExit (const SourceDetails&)
{
    dropKey = -1;
    repaint();
}

void Keyboard::itemDropped (const SourceDetails& d)
{
    const int k = keyAt (d.localPosition.toFloat());
    const int p = padFromDragDescription (d.description);
    dropKey = -1;
    repaint();
    if (k >= 0 && p >= 0)
    {
        proc.setPadNote (p, k);
        services.selectPad (p);
    }
}

bool Keyboard::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void Keyboard::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int k = keyAt ({ (float) x, (float) y });
    if (k != dropKey) { dropKey = k; repaint(); }
}

void Keyboard::fileDragExit (const juce::StringArray&)
{
    dropKey = -1;
    repaint();
}

void Keyboard::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int k = keyAt ({ (float) x, (float) y });
    dropKey = -1;
    repaint();
    if (k < 0) return;

    // the pad on this key, or the first empty pad (mapped here)
    int p = proc.findPadForNote (k);
    if (p < 0)
    {
        for (int i = 0; i < kNumPads && p < 0; ++i)
            if (proc.getKit().isPadEmpty (i)) p = i;
        if (p < 0) return;
        proc.setPadNote (p, k);
    }
    services.selectPad (p);
    services.dropFiles (p, files.size() > 1 ? 0 : -1, files);
}

} // namespace stsample
