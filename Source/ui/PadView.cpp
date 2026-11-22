#include "PadView.h"

namespace stsample
{

NotePill::NotePill (StSampleProcessor& p, int padIndex) : proc (p), pad (padIndex), param (p.padParam (padIndex, "note"))
{
    setTooltip ("MIDI note that plays this pad: click for the list, drag or scroll to change");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void NotePill::setNote (int n)
{
    n = juce::jlimit (0, 127, n);
    if (n != proc.getPadNote (pad))
        param.setValueNotifyingHost (param.convertTo0to1 ((float) n));
}

void NotePill::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    st::paint::greyButton (g, r, t.radius - 1.0f, isMouseOver() || dragging, false);
    auto face = r.withTrimmedBottom (2.0f);
    st::icons::draw (g, st::icons::Icon::chevronDown, face.removeFromRight (18.0f).withSizeKeepingCentre (9.0f, 9.0f), t.text.withAlpha (0.6f), 1.6f);
    g.setColour (t.text);
    g.setFont (st::fonts::semibold (13.0f));
    g.drawText (notes::name (proc.getPadNote (pad)), face.withTrimmedLeft (6.0f), juce::Justification::centred, false);
}

void NotePill::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    noteAtStart = proc.getPadNote (pad);
    if (! e.mods.isPopupMenu()) return;   // left click: menu on mouse up (unless dragged)
}

void NotePill::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && std::abs (e.getDistanceFromDragStartY()) > 3)
    {
        dragging = true;
        proc.undoManager.beginNewTransaction();
        param.beginChangeGesture();
    }
    if (dragging)
    {
        setNote (noteAtStart - e.getDistanceFromDragStartY() / 6);
        repaint();
    }
}

void NotePill::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        param.endChangeGesture();
        repaint();
        return;
    }
    if (! getLocalBounds().contains (e.getPosition())) return;

    // note menu: one submenu per octave, mapped notes show their pad
    juce::PopupMenu m;
    const int current = proc.getPadNote (pad);
    for (int oct = 0; oct < 11; ++oct)
    {
        juce::PopupMenu sub;
        bool hasCurrent = false;
        for (int n = oct * 12; n < juce::jmin (128, oct * 12 + 12); ++n)
        {
            juce::String label = notes::name (n);
            const int other = proc.findPadForNote (n);
            if (other >= 0 && other != pad) label << "    " << proc.getKit().getPadName (other);
            else if (auto gm = gmDrumName (n); gm.isNotEmpty()) label << "    (" << gm << ")";
            sub.addItem (n + 1, label, true, n == current);
            hasCurrent |= n == current;
        }
        m.addSubMenu (notes::name (oct * 12) + " - " + notes::name (juce::jmin (127, oct * 12 + 11)), sub, true, nullptr, hasCurrent);
    }
    st::showMenu (m, this, [safe = juce::Component::SafePointer<NotePill> (this)] (int r)
    {
        if (safe != nullptr && r > 0) { safe->proc.setPadNote (safe->pad, r - 1); safe->repaint(); }
    });
}

void NotePill::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) >= 0.12f)
    {
        proc.setPadNote (pad, proc.getPadNote (pad) + (wheelAccum > 0.0f ? 1 : -1));
        wheelAccum = 0.0f;
        repaint();
    }
}

//==============================================================================
PadView::PadView (Services& s, int p)
    : services (s), proc (s.getProcessor()), pad (p), note (proc, p), wave (s, p), xy (s, p)
{
    auto* um = &proc.undoManager;
    const auto& t = st::Theme::get();

    addAndMakeVisible (note);
    learn.setClickingTogglesState (false);
    learn.setOnColour (t.accent);
    learn.setTooltip ("MIDI learn: play a key on your controller to map this pad to it");
    learn.onClick = [this]
    {
        proc.setLearnPad (proc.getLearnPad() == pad ? -1 : pad);
        tick (0.0);
    };
    addAndMakeVisible (learn);

    mode = std::make_unique<st::Dropdown> (proc.padParam (pad, "mode"), um);
    mode->setGreyStyle (true);
    mode->setTooltip ("One-shot plays the whole sample; Gate stops it (with a short fade) when the key is released");
    choke = std::make_unique<st::Dropdown> (proc.padParam (pad, "choke"), um);
    choke->setGreyStyle (true);
    choke->buildMenu = [] (int current)
    {
        juce::PopupMenu m;
        m.addItem (1, "No choke group", true, current == 0);
        m.addSeparator();
        for (int i = 1; i <= 8; ++i) m.addItem (i + 1, "Choke group " + juce::String (i), true, current == i);
        return m;
    };
    choke->setTooltip ("Choke group: a hit stops every pad in the same group (open / closed hi-hat)");
    addAndMakeVisible (*mode);
    addAndMakeVisible (*choke);

    mute = std::make_unique<st::ToggleChip> (proc.padParam (pad, "mute"), um, "M", t.textDim);
    solo = std::make_unique<st::ToggleChip> (proc.padParam (pad, "solo"), um, "S", t.warning);
    mute->setGreyStyle (true);
    solo->setGreyStyle (true);
    mute->setTooltip ("Mute this pad");
    solo->setTooltip ("Solo this pad");
    addAndMakeVisible (*mute);
    addAndMakeVisible (*solo);
    play.onClick = [this] { proc.auditionPad (pad, 0.85f); };
    addAndMakeVisible (play);

    addAndMakeVisible (wave);
    addAndMakeVisible (xy);

    level = std::make_unique<st::Knob> (proc.padParam (pad, "level"), um, "Level");
    level->setLook (st::Knob::Look::ringed);
    level->setColour (t.accent);
    level->setRimParameter (&proc.padParam (pad, "pan"), "Pan");
    level->setTooltip ("Pad level; drag the rim for pan");
    level->setSize (st::Knob::Size::medium);
    velocity = std::make_unique<st::Knob> (proc.padParam (pad, "vel"), um, "Velocity");
    velocity->setSize (st::Knob::Size::small);
    velocity->setTooltip ("Velocity floor: how loud the softest hit plays (100% ignores velocity)");
    velocity->valueToText = [] (float v) { return "Floor " + juce::String (juce::roundToInt (v)) + "%"; };
    addAndMakeVisible (*level);
    addAndMakeVisible (*velocity);

    for (int l = 0; l < kNumLayers; ++l)
    {
        layers[(size_t) l] = std::make_unique<LayerPanel> (s, pad, l);
        addAndMakeVisible (*layers[(size_t) l]);
    }

    proc.getKit().addChangeListener (this);
    shownName = proc.getKit().getPadName (pad);
}

PadView::~PadView()
{
    proc.getKit().removeChangeListener (this);
    if (proc.getLearnPad() == pad) proc.setLearnPad (-1);
}

void PadView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    tick (0.0);
}

void PadView::tick (double dt)
{
    wave.tick (dt);
    xy.tick (dt);

    const bool l = proc.getLearnPad() == pad;
    if (l != learning)
    {
        learning = l;
        learn.setToggleState (l, juce::dontSendNotification);
        learn.setButtonText (l ? "Play a key" : "Learn");
        resized();
    }
    const auto n = proc.getKit().getPadName (pad);
    const int noteNow = proc.getPadNote (pad);
    if (n != shownName || noteNow != shownNote)
    {
        shownName = n;
        shownNote = noteNow;
        resized();
        repaint();
        note.repaint();
    }
}

void PadView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (34);

    // right side of the header: play, solo, mute, choke, mode
    auto right = header.withTrimmedBottom (4);
    play.setBounds (right.removeFromRight (32));
    right.removeFromRight (6);
    solo->setBounds (right.removeFromRight (30));
    right.removeFromRight (4);
    mute->setBounds (right.removeFromRight (30));
    right.removeFromRight (12);
    choke->setBounds (right.removeFromRight (110));
    right.removeFromRight (6);
    mode->setBounds (right.removeFromRight (100));

    // left: name, note, learn
    juce::Font nameFont = st::fonts::semibold (19.0f);
    const int nameW = juce::jlimit (60, 300, (int) st::fonts::stringWidth (nameFont, shownName) + 6);
    nameArea = header.withWidth (nameW).withTrimmedBottom (4);
    note.setBounds (nameArea.getRight() + 12, 3, 58, 26);
    learn.setBounds (note.getRight() + 6, 5, learning ? 84 : 58, 22);
    gmArea = { learn.getRight() + 10, 0, juce::jmax (0, mode->getX() - learn.getRight() - 20), 30 };

    r.removeFromTop (10);
    auto display = r.removeFromTop (260);
    wave.setBounds (display.removeFromLeft (414));
    display.removeFromLeft (12);
    xy.setBounds (display.removeFromLeft (252).withSizeKeepingCentre (252, 252));
    display.removeFromLeft (4);
    level->setBounds (display.removeFromTop (128).withSizeKeepingCentre (100, 120));
    velocity->setBounds (display.removeFromTop (110).withSizeKeepingCentre (84, 96));

    r.removeFromTop (14);
    const int gap = 10;
    const int w = (r.getWidth() - gap * (kNumLayers - 1)) / kNumLayers;
    for (int l = 0; l < kNumLayers; ++l)
    {
        layers[(size_t) l]->setBounds (r.removeFromLeft (w).withHeight (190));
        r.removeFromLeft (gap);
    }
}

void PadView::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    g.setFont (st::fonts::semibold (19.0f));
    g.setColour (t.text);
    g.drawText (shownName, nameArea, juce::Justification::centredLeft, true);

    // what the note is in General MIDI, as a hint
    const auto gm = gmDrumName (proc.getPadNote (pad));
    if (gm.isNotEmpty() && gm != shownName && gmArea.getWidth() > 40)
    {
        g.setFont (st::fonts::regular (11.5f));
        g.setColour (t.textFaint.brighter (0.2f));
        g.drawText ("GM " + gm, gmArea, juce::Justification::centredLeft, true);
    }
}

void PadView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && nameArea.contains (e.getPosition()))
        services.showPadMenu (pad, *this);
}

void PadView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (nameArea.contains (e.getPosition())) services.renamePad (pad);
}

} // namespace stsample
