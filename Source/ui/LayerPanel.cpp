#include "LayerPanel.h"

namespace stsample
{

SampleSelector::SampleSelector (Services& s, int p, int l) : services (s), pad (p), layer (l)
{
    setTooltip ("Sample: click for the menu (open, same folder, built-in sounds), arrows or wheel step through the folder");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> SampleSelector::leftArrow() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (2.0f).removeFromLeft (20.0f);
}

juce::Rectangle<float> SampleSelector::rightArrow() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (2.0f).removeFromRight (40.0f).removeFromLeft (20.0f);
}

void SampleSelector::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool over = isMouseOver();
    const auto& l = services.getProcessor().getKit().getLayer (pad, layer);
    const bool empty = l.source.isEmpty();
    st::paint::greyButton (g, r, t.radius - 1.0f, over, false);
    auto face = r.withTrimmedBottom (2.0f);

    if (empty)
    {
        g.setColour (over ? t.text : t.text.withAlpha (0.75f));
        g.setFont (st::fonts::medium (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("+  Load sample")), face, juce::Justification::centred, false);
        return;
    }

    const auto mouse = getMouseXYRelative().toFloat();
    if (over)
    {
        for (auto [a, icon] : { std::pair { leftArrow(), st::icons::Icon::chevronLeft }, std::pair { rightArrow(), st::icons::Icon::chevronRight } })
            st::icons::draw (g, icon, a.withSizeKeepingCentre (12.0f, 12.0f), a.contains (mouse) ? t.text : t.text.withAlpha (0.5f), 1.6f);
    }
    st::icons::draw (g, st::icons::Icon::chevronDown, face.removeFromRight (20.0f).withSizeKeepingCentre (10.0f, 10.0f), t.text.withAlpha (0.6f), 1.6f);
    face.removeFromRight (20.0f);
    face.removeFromLeft (20.0f);
    g.setFont (st::fonts::medium (12.0f));
    g.setColour (l.status == Kit::Status::missing ? t.danger : t.text.withAlpha (0.92f));
    g.drawText (l.name, face, juce::Justification::centredLeft, true);
}

void SampleSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto& l = services.getProcessor().getKit().getLayer (pad, layer);
    if (l.source.isNotEmpty() && ! e.mods.isPopupMenu())
    {
        if (leftArrow().contains (e.position)) { services.stepSample (pad, layer, -1); return; }
        if (rightArrow().contains (e.position)) { services.stepSample (pad, layer, 1); return; }
    }
    if (l.source.isEmpty() && ! e.mods.isPopupMenu()) services.browseSamples (pad, layer);
    else services.showSampleMenu (pad, layer, *this);
}

void SampleSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (services.getProcessor().getKit().getLayer (pad, layer).source.isEmpty()) return;
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) >= 0.12f)
    {
        services.stepSample (pad, layer, wheelAccum > 0.0f ? -1 : 1);
        wheelAccum = 0.0f;
    }
}

//==============================================================================
LayerPanel::LayerPanel (Services& s, int p, int l)
    : services (s), proc (s.getProcessor()), pad (p), layer (l), selector (s, p, l)
{
    auto* um = &proc.undoManager;
    const auto colour = layerColour (layer);

    invert = std::make_unique<st::ToggleChip> (proc.layerParam (pad, layer, "invert"), um,
                                               juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), st::Theme::get().accent);
    invert->setGreyStyle (true);
    invert->setTooltip ("Phase invert this layer (fix cancellation between layers)");
    addAndMakeVisible (*invert);

    listen.onClick = [this] { proc.auditionPad (pad, 0.85f, layer); };
    addAndMakeVisible (listen);
    addAndMakeVisible (selector);

    gain = std::make_unique<st::Knob> (proc.layerParam (pad, layer, "gain"), um, "Gain");
    gain->setTooltip ("Level of this layer");
    pitch = std::make_unique<st::Knob> (proc.layerParam (pad, layer, "pitch"), um, "Pitch");
    pitch->setBipolar (true);
    pitch->setTooltip ("Pitch of this layer in semitones (plays the sample faster or slower)");
    length = std::make_unique<st::Knob> (proc.layerParam (pad, layer, "length"), um, "Length");
    length->setTooltip ("How much of the sample plays after its start (fades out at the cut); also the end handle on the waveform");
    length->valueToText = [this] (float pct)
    {
        const auto& d = proc.getKit().getLayer (pad, layer).data;
        if (d == nullptr) return juce::String (juce::roundToInt (pct)) + "%";
        const float start = proc.getLayerValue (pad, layer, "start") * 0.01f;
        return st::params::formatMs ((float) (d->getLengthSeconds() * 1000.0 * (1.0f - start) * pct * 0.01f));
    };
    for (auto* k : { gain.get(), pitch.get(), length.get() })
    {
        k->setSize (st::Knob::Size::small);
        k->setColour (colour);
        addAndMakeVisible (*k);
    }

    proc.getKit().addChangeListener (this);
    refresh();
}

LayerPanel::~LayerPanel()
{
    proc.getKit().removeChangeListener (this);
}

void LayerPanel::refresh()
{
    const bool loaded = proc.getKit().getLayer (pad, layer).source.isNotEmpty();
    for (auto* c : { (juce::Component*) gain.get(), (juce::Component*) pitch.get(), (juce::Component*) length.get(),
                     (juce::Component*) invert.get(), (juce::Component*) &listen })
    {
        c->setAlpha (loaded ? 1.0f : 0.35f);
        c->setEnabled (loaded);
    }
    selector.repaint();
    repaint();
}

void LayerPanel::resized()
{
    box = getLocalBounds().toFloat().withTrimmedTop ((float) edgeHeight).reduced (2.0f, 0.0f).withTrimmedBottom (5.0f);
    const int right = getWidth() - 14;
    listen.setBounds (right - 30, 0, 30, 24);
    invert->setBounds (right - 30 - 6 - 30, 0, 30, 24);

    auto inner = box.toNearestInt().reduced (12, 0).withTrimmedTop (22);
    selector.setBounds (inner.removeFromTop (26));
    inner.removeFromTop (8);
    auto knobs = inner.removeFromTop (76);
    const int w = knobs.getWidth() / 3;
    gain->setBounds (knobs.removeFromLeft (w).withSizeKeepingCentre (58, 76));
    pitch->setBounds (knobs.removeFromLeft (w).withSizeKeepingCentre (58, 76));
    length->setBounds (knobs.withSizeKeepingCentre (58, 76));
    info = inner.withTrimmedTop (2).removeFromTop (16);
}

void LayerPanel::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = layerColour (layer);
    const auto& l = proc.getKit().getLayer (pad, layer);
    const bool loaded = l.source.isNotEmpty();
    style::paintGlassBar (g, box, loaded ? colour : juce::Colour());
    if (dropping)
    {
        g.setColour (colour.withAlpha (0.85f));
        g.drawRoundedRectangle (box.reduced (0.75f), 8.0f, 1.5f);
    }

    // letter badge on the top edge
    style::paintLayerBadge (g, juce::Rectangle<float> (14.0f, 1.0f, 24.0f, 21.0f), layer, ! loaded);
    g.setFont (st::fonts::medium (11.0f));
    g.setColour (t.textDim.withAlpha (0.75f));
    g.drawText ("Layer " + layerLetter (layer), juce::Rectangle<float> (44.0f, (float) edgeHeight + 1.0f, 80.0f, 16.0f), juce::Justification::centredLeft, false);

    // sample info
    juce::String text;
    if (l.data != nullptr)
    {
        const auto& d = *l.data;
        text << st::params::formatMs ((float) (d.getLengthSeconds() * 1000.0)) << "   "
             << juce::String (d.sampleRate / 1000.0, d.sampleRate == std::round (d.sampleRate / 1000.0) * 1000.0 ? 0 : 1) << " kHz   "
             << (d.getNumChannels() > 1 ? "Stereo" : "Mono");
    }
    else if (l.status == Kit::Status::missing) text = l.error;
    else if (l.status == Kit::Status::loading) text = "Loading";
    else text = "Drop a sample here";
    g.setFont (st::fonts::regular (10.5f));
    g.setColour (l.status == Kit::Status::missing ? t.danger.withAlpha (0.85f) : t.textFaint.brighter (0.15f));
    g.drawText (text, info, juce::Justification::centred, true);
}

bool LayerPanel::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void LayerPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    services.dropFiles (pad, layer, files);
}

} // namespace stsample
