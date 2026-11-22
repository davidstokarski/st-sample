#pragma once
#include "Common.h"

namespace stsample
{

/** Sample selector (grey face): the sample's name, ‹ › on hover to step through its folder, click for the menu. */
class SampleSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    SampleSelector (Services&, int pad, int layer);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    Services& services;
    const int pad, layer;
    float wheelAccum = 0.0f;
    juce::Rectangle<float> leftArrow() const, rightArrow() const;
};

/** One layer of the selected pad (ST Sat band-bar style): a glass bar in the layer's colour with the letter, phase
    invert and listen on its top edge, the sample selector, and Gain / Pitch / Length knobs. An empty layer dims its
    knobs and offers "+ Load sample". Drop a file on it to load it. */
class LayerPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    LayerPanel (Services&, int pad, int layer);
    ~LayerPanel() override;

    static constexpr int edgeHeight = 12;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    Services& services;
    StSampleProcessor& proc;
    const int pad, layer;
    std::unique_ptr<st::ToggleChip> invert;
    GreyButton listen { "", "Listen: play this layer on its own", st::icons::Icon::headphones };
    SampleSelector selector;
    std::unique_ptr<st::Knob> gain, pitch, length;
    juce::Rectangle<float> box;
    juce::Rectangle<int> info;
    bool dropping = false;

    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
};

} // namespace stsample
