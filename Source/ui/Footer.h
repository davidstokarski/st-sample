#pragma once
#include "Common.h"

namespace stsample
{

/** Bottom tray: note naming and polyphony on the left, the last MIDI note and active voices in the middle,
    output level on the right. */
class FooterBar : public juce::Component, private juce::Timer
{
public:
    explicit FooterBar (StSampleProcessor&);
    std::function<void()> onNoteNamesChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSampleProcessor& proc;
    st::FlatButton noteNames { "", "How note names are numbered: middle C (MIDI 60) is C3 in Logic, Ableton and Cubase, C4 in some other hosts" };
    st::Dropdown voices;
    st::NumberBox output;
    std::vector<float> dividers;
    juce::Rectangle<int> midiArea;
    juce::uint32 shownCounter = 0;
    int shownVoices = -1;

    void refreshNoteNames();
    void timerCallback() override;
};

} // namespace stsample
