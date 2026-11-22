#pragma once
#include "Common.h"

namespace stsample
{

/** The MIDI map as a piano: seven octaves (‹ › shift them), keys with a pad lit in the accent colour, the selected
    pad's key brightest, keys flash when played (MIDI in or audition).
    Click plays the key (lower on the key = louder) and selects its pad; while learning, click maps the pad. Drag a pad
    from the list onto a key to map it; drop samples on a key to load them into its pad (or into a free pad mapped
    to that key). Right-click a key: map the selected pad there. */
class Keyboard : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget,
                 public juce::SettableTooltipClient
{
public:
    explicit Keyboard (Services&);
    ~Keyboard() override;

    void tick (double dt);
    void showNote (int note);   // scrolls so the note is visible
    int getLowestNote() const noexcept { return lowest; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    static constexpr int octaves = 7, arrowWidth = 18;

private:
    Services& services;
    StSampleProcessor& proc;
    int lowest = 12;   // C of the leftmost octave
    int hoverKey = -1, playingKey = -1, dropKey = -1, hoverArrow = 0;
    std::array<float, 128> flash {};
    std::array<juce::uint32, kNumPads> seenHits {};
    std::array<bool, 128> down {};
    juce::int64 mapSignature = 0;

    juce::Rectangle<float> keysArea() const;
    juce::Rectangle<float> keyBounds (int note) const;
    static bool isBlack (int note) noexcept;
    int keyAt (juce::Point<float>) const;
    int arrowAt (juce::Point<float>) const;
    void shift (int octavesDelta);
    void startNote (int note, float y);
    void stopNote();
};

} // namespace stsample
