#pragma once
#include "PluginProcessor.h"
#include "ui/PadList.h"
#include "ui/PadView.h"
#include "ui/Keyboard.h"
#include "ui/Footer.h"

/** ST Sample: pads on the left (ST DC's list), the selected pad's surface on the right (waveform lanes, XY blend,
    Level / Velocity, four layer bars), the MIDI map as a keyboard along the bottom, footer tray. The pad surface is
    rebuilt per selection and cross-fades like ST Sat's band panel. Samples can be dropped anywhere: on a pad, a
    lane, a corner of the XY pad, a layer bar, a key, or the window (selected pad, first free layer). */
class StSampleEditor : public st::EditorBase, public stsample::Services, public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    explicit StSampleEditor (StSampleProcessor&);
    ~StSampleEditor() override;

    static constexpr int designWidth = 1040, designHeight = 690;
    static constexpr int footerHeight = 40;

    // ---- Services ---------------------------------------------------------------------------------------
    StSampleProcessor& getProcessor() override { return proc; }
    int getSelectedPad() const override { return selectedPad; }
    void selectPad (int pad) override;
    void browseSamples (int pad, int layer) override;
    void showSampleMenu (int pad, int layer, juce::Component& target) override;
    void stepSample (int pad, int layer, int delta) override;
    void dropFiles (int pad, int layer, const juce::StringArray& files) override;
    void showPadMenu (int pad, juce::Component& target) override;
    void renamePad (int pad) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    stsample::PadView* getPadView() noexcept { return padView.get(); }
    stsample::Keyboard& getKeyboard() noexcept { return keyboard; }

private:
    StSampleProcessor& proc;
    stsample::PadList padList;
    stsample::Keyboard keyboard;
    stsample::FooterBar footer;
    std::unique_ptr<stsample::PadView> padView, outgoingView;
    st::Animated viewFade, outgoingFade;
    std::unique_ptr<st::FrameClock> clock;
    std::unique_ptr<juce::FileChooser> chooser;
    int selectedPad = 0;
    struct PendingAudition { int pad = -1, layer = -1; juce::String source; } pendingAudition;

    static juce::ValueTree& padClipboard();

    void layout (juce::Rectangle<int> body) override;
    void paintBackground (juce::Graphics&, juce::Rectangle<int>) override;
    void animate (double dt);
    void setLayerSourceAndAudition (int pad, int layer, const juce::String& source);
    juce::File lastFolder() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::Image backdrop;
    float backdropScale = 0.0f;
};
