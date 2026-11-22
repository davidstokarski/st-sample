#pragma once
#include "Common.h"

namespace stsample
{

/** The 16 pads, one row each (ST DC's drum-part list style): hit light, mapped note, name, layer dots,
    mute / solo tags. Click selects (and auditions with Option), double-click renames, right-click opens the pad
    menu, drag a row onto the keyboard to map it, drop audio files on a row to load them into that pad. */
class PadList : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient,
                private juce::ChangeListener
{
public:
    explicit PadList (Services&);
    ~PadList() override;

    void tick (double dt);
    void setSelected (int pad);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    static constexpr int titleHeight = 28, rowHeight = 30;

private:
    Services& services;
    StSampleProcessor& proc;
    int selected = 0, hovered = -1, dropRow = -1;
    std::array<float, kNumPads> flash {};
    std::array<juce::uint32, kNumPads> seenHits {};
    bool dragStarted = false;
    juce::int64 paramSignature = 0;

    juce::Rectangle<int> rowBounds (int pad) const;
    int rowAt (juce::Point<int>) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

} // namespace stsample
