#include "Footer.h"

namespace stsample
{

FooterBar::FooterBar (StSampleProcessor& p)
    : proc (p),
      voices (p.param (ids::polyphony), &p.undoManager),
      output (p.param (ids::outputGain), &p.undoManager, "Out")
{
    voices.setFlatStyle (true);
    voices.setCaption ("Voices");
    voices.setTooltip ("Polyphony: how many hits can ring at once (the oldest fades out to make room)");
    output.setFlatStyle (true);
    output.setTooltip ("Output level (drag)");

    noteNames.setDropdownArrow (true);
    noteNames.onClick = [this]
    {
        juce::PopupMenu m;
        for (int o : { 3, 4, 5 })
            m.addItem (o, "Middle C = C" + juce::String (o) + (o == 3 ? "  (Logic, Ableton, Cubase)" : o == 4 ? "  (Bitwig, Reaper)" : "  (FL Studio)"),
                       true, notes::middleCOctave() == o);
        st::showMenu (m, &noteNames, [safe = juce::Component::SafePointer<FooterBar> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            notes::middleCOctave() = r;
            safe->proc.uiState.setProperty ("middleC", r, nullptr);
            safe->refreshNoteNames();
            if (safe->onNoteNamesChanged) safe->onNoteNamesChanged();
        });
    };
    refreshNoteNames();

    for (juce::Component* c : { (juce::Component*) &noteNames, (juce::Component*) &voices, (juce::Component*) &output })
        addAndMakeVisible (c);
    startTimerHz (15);
}

void FooterBar::refreshNoteNames()
{
    noteNames.setButtonText ("C" + juce::String (notes::middleCOctave()));
    noteNames.setDimmedText ("Middle C  ");
}

void FooterBar::timerCallback()
{
    const auto c = proc.getMidiCounter();
    const int v = proc.getEngine().getNumActiveVoices();
    if (c != shownCounter || v != shownVoices)
    {
        shownCounter = c;
        shownVoices = v;
        repaint (midiArea);
    }
}

void FooterBar::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    style::paintTray (g, r);
    for (float x : dividers)
        st::paint::fadedLine (g, { x + 0.5f, r.getY() + 6.0f }, { x + 0.5f, r.getBottom() - 6.0f }, juce::Colours::white.withAlpha (0.10f), 1.0f, 0.5f);

    auto a = midiArea.toFloat();
    g.setFont (st::fonts::medium (13.0f));
    auto pair = [&] (const juce::String& label, const juce::String& value, float width)
    {
        auto cell = a.removeFromLeft (width);
        g.setColour (t.textDim.withAlpha (0.7f));
        const float w = st::fonts::stringWidth (g.getCurrentFont(), label);
        g.drawText (label, cell.removeFromLeft (w), juce::Justification::centredLeft, false);
        g.setColour (t.text.withAlpha (0.9f));
        g.drawText (value, cell, juce::Justification::centredLeft, false);
    };
    const int n = proc.getLastNote();
    pair ("MIDI  ", n < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))
                          : notes::name (n) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (proc.getLastVelocity()), 150.0f);
    pair ("Playing  ", juce::String (juce::jmax (0, shownVoices)), 110.0f);
}

void FooterBar::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    dividers.clear();
    auto divide = [this, &r] { dividers.push_back ((float) r.getX() + 10.0f); r.removeFromLeft (20); };

    noteNames.setBounds (r.removeFromLeft (128));
    divide();
    voices.setBounds (r.removeFromLeft (110));
    divide();
    midiArea = r.removeFromLeft (270);

    output.setBounds (r.removeFromRight (96));
    dividers.push_back ((float) output.getX() - 12.0f);
}

} // namespace stsample
