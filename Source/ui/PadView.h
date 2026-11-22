#pragma once
#include "WaveDisplay.h"
#include "XYPad.h"
#include "LayerPanel.h"

namespace stsample
{

/** The selected pad's note: grey pill showing the note name. Click for a note menu (by octave, with the pads already
    mapped), wheel or drag vertically to move by semitones. */
class NotePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    NotePill (StSampleProcessor&, int pad);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    StSampleProcessor& proc;
    const int pad;
    juce::RangedAudioParameter& param;
    bool dragging = false;
    int noteAtStart = 0;
    float wheelAccum = 0.0f;
    void setNote (int note);
};

/** Everything about the selected pad (FabFilter-style: one surface, depth one click away): header (name, note,
    learn, trigger mode, choke group, mute / solo / play), the waveform lanes, the XY blend, the pad's Level (pan on
    its rim) and Velocity, and the four layer bars. The editor rebuilds it when the selection changes and cross-fades. */
class PadView : public juce::Component, private juce::ChangeListener
{
public:
    PadView (Services&, int pad);
    ~PadView() override;

    int getPad() const noexcept { return pad; }
    void tick (double dt);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    WaveDisplay& getWave() noexcept { return wave; }
    XYPad& getXY() noexcept { return xy; }
    LayerPanel& getLayerPanel (int l) noexcept { return *layers[(size_t) l]; }

private:
    Services& services;
    StSampleProcessor& proc;
    const int pad;

    NotePill note;
    st::PillButton learn { "Learn" };
    std::unique_ptr<st::Dropdown> mode, choke;
    std::unique_ptr<st::ToggleChip> mute, solo;
    GreyButton play { "", "Play this pad", st::icons::Icon::headphones };
    WaveDisplay wave;
    XYPad xy;
    std::unique_ptr<st::Knob> level, velocity;
    std::array<std::unique_ptr<LayerPanel>, kNumLayers> layers;
    juce::Rectangle<int> nameArea, gmArea;
    juce::String shownName;
    int shownNote = -1;
    bool learning = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace stsample
