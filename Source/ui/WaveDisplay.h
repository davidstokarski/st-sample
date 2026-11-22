#pragma once
#include "Common.h"

namespace stsample
{

/** The selected pad's four layers as stacked waveform lanes (A–D, in their colours, brightness = XY blend weight).
    Each lane has a start handle and an end handle (Saturn-style dashed lines with tent grips) dragged sideways; the
    end fades over the cut. Live playheads. Click plays the layer alone, an empty lane opens the file chooser,
    right-click opens the sample menu, dropping a file loads it into that lane. */
class WaveDisplay : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    WaveDisplay (Services&, int pad);
    ~WaveDisplay() override;

    void tick (double dt);

    void paint (juce::Graphics&) override;
    void resized() override { cache = {}; }
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

    juce::Rectangle<float> laneBounds (int layer) const;

private:
    Services& services;
    StSampleProcessor& proc;
    const int pad;

    enum class Handle { none, start, end };
    struct Hover { int lane = -1; Handle handle = Handle::none; };
    Hover hover, drag;
    int dropLane = -1;
    std::array<float, kNumLayers> playheads {};
    juce::int64 paramSignature = 0;

    struct LaneCache { const SampleData* data = nullptr; int w = 0, h = 0; juce::Path path; };
    std::array<LaneCache, kNumLayers> cache;

    juce::Rectangle<float> waveArea (int layer) const;
    float xForPosition (int layer, float pos01) const;
    float positionForX (int layer, float x) const;
    float startOf (int layer);
    float endOf (int layer);
    Hover hitTestHandles (juce::Point<float>) const;
    int laneAt (juce::Point<float>) const;
    const juce::Path& wavePath (int layer, const SampleData&, juce::Rectangle<float> area);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { cache = {}; repaint(); }
};

} // namespace stsample
