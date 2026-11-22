#pragma once
#include "Common.h"

namespace stsample
{

/** XY blend between the pad's four layers (A top-left, B top-right, C bottom-left, D bottom-right). Each corner cell
    glows in its layer's colour as much as the blend uses it; empty corners stay dark. Drag the puck (click jumps,
    Shift = fine, double-click centres, Cmd-click centres), drop a sample on a cell to load that layer. */
class XYPad : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient,
              private juce::ChangeListener
{
public:
    XYPad (Services&, int pad);
    ~XYPad() override;

    void tick (double dt);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    Services& services;
    StSampleProcessor& proc;
    const int pad;
    juce::RangedAudioParameter &px, &py;
    float shownX = -1.0f, shownY = -1.0f;
    bool dragging = false, hovering = false;
    int dropCell = -1;
    juce::Point<float> dragStart, valueAtStart;

    juce::Rectangle<float> field() const;
    int cellAt (juce::Point<float>) const;
    juce::Point<float> valueAt (juce::Point<float>) const;
    void setValue (juce::Point<float>);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

} // namespace stsample
