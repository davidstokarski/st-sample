#include "WaveDisplay.h"

namespace stsample
{

namespace
{
    constexpr float laneGap = 6.0f, labelHeight = 16.0f, handleGrab = 6.0f;
}

WaveDisplay::WaveDisplay (Services& s, int p) : services (s), proc (s.getProcessor()), pad (p)
{
    proc.getKit().addChangeListener (this);
    playheads.fill (-1.0f);
}

WaveDisplay::~WaveDisplay()
{
    proc.getKit().removeChangeListener (this);
}

juce::Rectangle<float> WaveDisplay::laneBounds (int layer) const
{
    const float h = ((float) getHeight() - laneGap * (kNumLayers - 1)) / (float) kNumLayers;
    return { 0.0f, (float) layer * (h + laneGap), (float) getWidth(), h };
}

juce::Rectangle<float> WaveDisplay::waveArea (int layer) const
{
    return laneBounds (layer).reduced (10.0f, 3.0f).withTrimmedTop (labelHeight - 2.0f);
}

float WaveDisplay::xForPosition (int layer, float pos01) const
{
    const auto a = waveArea (layer);
    return a.getX() + juce::jlimit (0.0f, 1.0f, pos01) * a.getWidth();
}

float WaveDisplay::positionForX (int layer, float x) const
{
    const auto a = waveArea (layer);
    return juce::jlimit (0.0f, 1.0f, (x - a.getX()) / juce::jmax (1.0f, a.getWidth()));
}

float WaveDisplay::startOf (int layer) { return proc.getLayerValue (pad, layer, "start") * 0.01f; }

float WaveDisplay::endOf (int layer)
{
    const float s = startOf (layer);
    return s + (1.0f - s) * proc.getLayerValue (pad, layer, "length") * 0.01f;
}

int WaveDisplay::laneAt (juce::Point<float> p) const
{
    for (int l = 0; l < kNumLayers; ++l)
        if (laneBounds (l).expanded (0.0f, laneGap * 0.5f).contains (p)) return l;
    return -1;
}

WaveDisplay::Hover WaveDisplay::hitTestHandles (juce::Point<float> p) const
{
    auto* self = const_cast<WaveDisplay*> (this);
    const int l = laneAt (p);
    if (l < 0 || proc.getKit().getLayer (pad, l).data == nullptr) return { l, Handle::none };
    const float xs = xForPosition (l, self->startOf (l)), xe = xForPosition (l, self->endOf (l));
    const float ds = std::abs (p.x - xs), de = std::abs (p.x - xe);
    if (de <= handleGrab && de <= ds) return { l, Handle::end };
    if (ds <= handleGrab) return { l, Handle::start };
    return { l, Handle::none };
}

const juce::Path& WaveDisplay::wavePath (int layer, const SampleData& d, juce::Rectangle<float> a)
{
    auto& c = cache[(size_t) layer];
    if (c.data == &d && c.w == (int) a.getWidth() && c.h == (int) a.getHeight()) return c.path;

    c.data = &d;
    c.w = (int) a.getWidth();
    c.h = (int) a.getHeight();
    c.path.clear();

    // symmetric min / max envelope, normalised to the sample's peak so quiet samples stay readable
    const int bins = (int) d.peakMax.size();
    const int cols = juce::jmax (2, (int) a.getWidth());
    const float norm = d.peak > 1.0e-6f ? 0.92f / d.peak : 1.0f;
    const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
    std::vector<float> top ((size_t) cols), bottom ((size_t) cols);
    for (int x = 0; x < cols; ++x)
    {
        const int b0 = x * bins / cols, b1 = juce::jmax (b0 + 1, (x + 1) * bins / cols);
        float lo = 0.0f, hi = 0.0f;
        for (int b = b0; b < juce::jmin (b1, bins); ++b)
        {
            lo = juce::jmin (lo, d.peakMin[(size_t) b]);
            hi = juce::jmax (hi, d.peakMax[(size_t) b]);
        }
        top[(size_t) x] = mid - juce::jmax (0.6f, hi * norm * half);
        bottom[(size_t) x] = mid - juce::jmin (-0.6f, lo * norm * half);
    }
    const float step = a.getWidth() / (float) (cols - 1);
    c.path.startNewSubPath (a.getX(), top[0]);
    for (int x = 1; x < cols; ++x) c.path.lineTo (a.getX() + (float) x * step, top[(size_t) x]);
    for (int x = cols - 1; x >= 0; --x) c.path.lineTo (a.getX() + (float) x * step, bottom[(size_t) x]);
    c.path.closeSubPath();
    return c.path;
}

void WaveDisplay::tick (double)
{
    bool changed = false;
    for (int l = 0; l < kNumLayers; ++l)
    {
        const float ph = proc.getEngine().getPlayhead (pad, l);
        if (std::abs (ph - playheads[(size_t) l]) > 1.0e-4f) { playheads[(size_t) l] = ph; changed = true; }
    }

    // start / length / gain / blend can change from automation, undo or the knobs below
    juce::int64 sig = 0;
    for (int l = 0; l < kNumLayers; ++l)
        sig = sig * 1000003 + (juce::int64) (startOf (l) * 9973.0f) * 31 + (juce::int64) (endOf (l) * 9973.0f);
    sig = sig * 131 + (juce::int64) (proc.getPadValue (pad, "x") * 997.0f) * 7 + (juce::int64) (proc.getPadValue (pad, "y") * 997.0f);
    if (sig != paramSignature) { paramSignature = sig; changed = true; }

    if (changed) repaint();
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto& kit = proc.getKit();
    const auto weights = blendWeights (proc.getPadValue (pad, "x"), proc.getPadValue (pad, "y"), proc.getEngine().getLoadedMask (pad));

    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto lane = laneBounds (l);
        const auto colour = layerColour (l);
        const auto& layer = kit.getLayer (pad, l);
        const bool hasData = layer.data != nullptr;
        style::paintWell (g, lane, 7.0f, colour);

        if (dropLane == l)
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (lane, 7.0f);
            g.setColour (colour.withAlpha (0.85f));
            g.drawRoundedRectangle (lane.reduced (0.75f), 7.0f, 1.5f);
        }

        // label: letter + name, length on the right
        auto label = lane.reduced (10.0f, 0.0f).withHeight (labelHeight + 2.0f).withTrimmedTop (3.0f);
        g.setFont (st::fonts::bold (11.0f));
        g.setColour (colour.withAlpha (layer.source.isEmpty() ? 0.45f : 0.95f));
        g.drawText (layerLetter (l), label.removeFromLeft (14.0f), juce::Justification::centredLeft, false);
        if (hasData)
        {
            const double secs = layer.data->getLengthSeconds() * (endOf (l) - startOf (l));
            g.setFont (st::fonts::medium (10.5f));
            g.setColour (t.textDim.withAlpha (0.7f));
            const auto len = st::params::formatMs ((float) (secs * 1000.0));
            g.drawText (len, label.removeFromRight (60.0f), juce::Justification::centredRight, false);
        }
        g.setFont (st::fonts::medium (11.0f));
        g.setColour (layer.status == Kit::Status::missing ? t.danger.withAlpha (0.9f) : t.textDim.withAlpha (layer.source.isEmpty() ? 0.0f : 0.85f));
        juce::String name = layer.name;
        if (layer.status == Kit::Status::missing) name << "  (" << layer.error << ")";
        else if (layer.status == Kit::Status::loading && ! hasData) name << "  (loading)";
        g.drawText (name, label, juce::Justification::centredLeft, true);

        auto area = waveArea (l);
        if (! hasData)
        {
            if (layer.source.isEmpty())
            {
                // empty lane: a quiet invitation, dashed like the ST Split "Add plug-in" row
                auto box = lane.reduced (10.0f, 9.0f);
                juce::Path outline;
                outline.addRoundedRectangle (box, 5.0f);
                juce::Path dashed;
                const float dash[] = { 4.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
                g.setColour (juce::Colours::white.withAlpha (hover.lane == l ? 0.16f : 0.08f));
                g.fillPath (dashed);
                g.setFont (st::fonts::medium (11.5f));
                g.setColour (hover.lane == l ? t.textDim : t.textFaint);
                g.drawText ("Drop a sample or click to browse", box, juce::Justification::centred, false);
            }
            continue;
        }

        const auto& d = *layer.data;
        const float w = weights[(size_t) l];
        const float xs = xForPosition (l, startOf (l)), xe = xForPosition (l, endOf (l));
        const auto& path = wavePath (l, d, area);

        // centre line
        st::paint::fadedLine (g, { area.getX(), area.getCentreY() }, { area.getRight(), area.getCentreY() },
                              colour.withAlpha (0.18f), 1.0f, 0.1f);

        // outside the region: dim; inside: the layer colour, brighter the more the XY blend uses it
        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (path);
        {
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<float> (xs, lane.getY(), juce::jmax (0.0f, xe - xs), lane.getHeight()).toNearestInt());
            const float a = 0.28f + 0.62f * juce::jlimit (0.0f, 1.0f, w);
            juce::ColourGradient fill (colour.withAlpha (a), 0.0f, area.getY(), colour.withAlpha (a * 0.7f), 0.0f, area.getBottom(), false);
            g.setGradientFill (fill);
            g.fillPath (path);
            // fade over the cut at the end (when shortened)
            if (endOf (l) < 0.9995f)
            {
                const double regionFrames = (endOf (l) - startOf (l)) * d.numFrames;
                const double fadeFrames = juce::jlimit (0.002 * d.sampleRate, juce::jmax (0.002 * d.sampleRate, 0.06 * d.sampleRate), regionFrames * 0.2);
                const float fx = xe - (float) (fadeFrames / d.numFrames) * area.getWidth();
                g.setGradientFill (juce::ColourGradient (t.bg0.withAlpha (0.0f), fx, 0.0f, t.bg0.withAlpha (0.75f), xe, 0.0f, false));
                g.fillRect (juce::Rectangle<float> (fx, lane.getY(), xe - fx, lane.getHeight()));
            }
            g.restoreState();
        }

        // handles: thin lines with a small grip at the top
        for (auto [x, which] : { std::pair { xs, Handle::start }, std::pair { xe, Handle::end } })
        {
            const bool hot = (hover.lane == l && hover.handle == which) || (drag.lane == l && drag.handle == which);
            const bool atEdge = which == Handle::start ? startOf (l) <= 0.0001f : endOf (l) >= 0.9999f;
            if (atEdge && ! hot && hover.lane != l) continue;
            st::paint::fadedLine (g, { x, area.getY() - 2.0f }, { x, area.getBottom() + 1.0f },
                                  juce::Colours::white.withAlpha (hot ? 0.85f : 0.45f), hot ? 1.5f : 1.0f, 0.12f);
            auto grip = juce::Rectangle<float> (9.0f, 7.0f).withCentre ({ x, area.getY() - 1.0f });
            juce::Path tab;
            tab.addTriangle (grip.getX(), grip.getY(), grip.getRight(), grip.getY(), grip.getCentreX(), grip.getBottom());
            g.setColour (juce::Colours::white.withAlpha (hot ? 0.95f : 0.6f));
            g.fillPath (tab);
        }

        // playhead
        const float ph = playheads[(size_t) l];
        if (ph >= 0.0f)
        {
            const float px = xForPosition (l, ph);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (px - 0.5f, area.getY(), 1.25f, area.getHeight()));
            g.setColour (colour.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (px - 3.0f, area.getY(), 6.0f, area.getHeight()));
        }

        // value readout while dragging a handle
        if (drag.lane == l && drag.handle != Handle::none)
        {
            const double ms = d.getLengthSeconds() * 1000.0 * (drag.handle == Handle::start ? startOf (l) : endOf (l));
            const auto txt = (drag.handle == Handle::start ? "Start " : "End ") + st::params::formatMs ((float) ms);
            g.setFont (st::fonts::medium (11.0f));
            const float tw = st::fonts::stringWidth (g.getCurrentFont(), txt) + 14.0f;
            const float hx = drag.handle == Handle::start ? xs : xe;
            auto box = juce::Rectangle<float> (tw, 18.0f).withCentre ({ juce::jlimit (area.getX() + tw * 0.5f, area.getRight() - tw * 0.5f, hx), area.getCentreY() });
            g.setColour (t.bg1.withAlpha (0.92f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (t.text);
            g.drawText (txt, box, juce::Justification::centred, false);
        }
    }
}

void WaveDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTestHandles (e.position);
    if (h.lane != hover.lane || h.handle != hover.handle)
    {
        hover = h;
        setMouseCursor (h.handle != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                        : (h.lane >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void WaveDisplay::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveDisplay::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitTestHandles (e.position);
    drag = {};
    if (h.lane < 0) return;

    if (e.mods.isPopupMenu())
    {
        services.showSampleMenu (pad, h.lane, *this);
        return;
    }
    const auto& layer = proc.getKit().getLayer (pad, h.lane);
    if (layer.source.isEmpty()) { services.browseSamples (pad, h.lane); return; }

    if (h.handle != Handle::none)
    {
        drag = h;
        auto& prm = proc.layerParam (pad, h.lane, h.handle == Handle::start ? "start" : "length");
        if (e.mods.isCommandDown())   // Cmd-click resets
        {
            proc.setPlain (prm, h.handle == Handle::start ? 0.0f : 100.0f);
            drag = {};
            return;
        }
        proc.undoManager.beginNewTransaction();
        prm.beginChangeGesture();
        return;
    }
    proc.auditionPad (pad, 0.85f, h.lane);
}

void WaveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.handle == Handle::none) return;
    const int l = drag.lane;
    float pos = positionForX (l, e.position.x);
    if (drag.handle == Handle::start)
    {
        const float end = endOf (l);
        pos = juce::jlimit (0.0f, juce::jmax (0.0f, end - 0.01f), pos);
        // keep the end where it is: length is relative to what follows the start
        auto& sp = proc.layerParam (pad, l, "start");
        auto& lp = proc.layerParam (pad, l, "length");
        sp.setValueNotifyingHost (sp.convertTo0to1 (pos * 100.0f));
        const float len = (end - pos) / juce::jmax (1.0e-4f, 1.0f - pos);
        lp.setValueNotifyingHost (lp.convertTo0to1 (juce::jlimit (1.0f, 100.0f, len * 100.0f)));
    }
    else
    {
        const float s = startOf (l);
        pos = juce::jlimit (s + 0.005f, 1.0f, pos);
        auto& lp = proc.layerParam (pad, l, "length");
        lp.setValueNotifyingHost (lp.convertTo0to1 (juce::jlimit (1.0f, 100.0f, (pos - s) / juce::jmax (1.0e-4f, 1.0f - s) * 100.0f)));
    }
    repaint();
}

void WaveDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag.handle != Handle::none)
        proc.layerParam (pad, drag.lane, drag.handle == Handle::start ? "start" : "length").endChangeGesture();
    drag = {};
    repaint();
}

void WaveDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTestHandles (e.position);
    if (h.handle == Handle::start) proc.setPlain (proc.layerParam (pad, h.lane, "start"), 0.0f);
    else if (h.handle == Handle::end) proc.setPlain (proc.layerParam (pad, h.lane, "length"), 100.0f);
}

bool WaveDisplay::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void WaveDisplay::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int l = laneAt ({ (float) x, (float) y });
    if (l != dropLane) { dropLane = l; repaint(); }
}

void WaveDisplay::fileDragExit (const juce::StringArray&)
{
    dropLane = -1;
    repaint();
}

void WaveDisplay::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int l = laneAt ({ (float) x, (float) y });
    dropLane = -1;
    repaint();
    if (l >= 0) services.dropFiles (pad, l, files);
}

} // namespace stsample
