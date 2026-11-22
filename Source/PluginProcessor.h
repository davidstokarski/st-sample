#pragma once
#include "Params.h"
#include "Kit.h"

class StSampleProcessor : public st::ProcessorBase
{
public:
    StSampleProcessor();
    ~StSampleProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    bool acceptsMidi() const override { return true; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    //==============================================================================
    stsample::Engine& getEngine() noexcept { return engine; }
    stsample::Kit& getKit() noexcept { return kit; }

    juce::RangedAudioParameter& padParam (int pad, const juce::String& suffix) { return param (stsample::ids::pad (pad, suffix)); }
    juce::RangedAudioParameter& layerParam (int pad, int layer, const juce::String& suffix) { return param (stsample::ids::layer (pad, layer, suffix)); }
    int getPadNote (int pad) const noexcept { return padRefs[(size_t) pad].note.getInt(); }
    float getPadValue (int pad, const juce::String& suffix) { return apvts.getRawParameterValue (stsample::ids::pad (pad, suffix))->load(); }
    float getLayerValue (int pad, int layer, const juce::String& suffix) { return apvts.getRawParameterValue (stsample::ids::layer (pad, layer, suffix))->load(); }
    /** Sets a parameter to a plain value (message thread), optionally as one gesture. */
    void setPlain (juce::RangedAudioParameter&, float plain, bool asGesture = true);
    /** Maps a pad to a note (undoable). */
    void setPadNote (int pad, int note);
    /** First pad mapped to `note`, or -1. */
    int findPadForNote (int note) const noexcept;

    // ---- audition from the UI (message thread → audio thread, lock-free) ------------------------------------
    /** Plays a pad (layer -1 = the whole pad with its blend, 0..3 = that layer alone). */
    void auditionPad (int pad, float velocity = 0.8f, int layer = -1);
    void auditionNote (int note, float velocity, bool on);

    // ---- MIDI learn ---------------------------------------------------------------------------------------
    /** The next incoming note maps this pad (-1 = off). */
    void setLearnPad (int pad) noexcept { learnPad.store (pad); learnedNote.store (-1); }
    int getLearnPad() const noexcept { return learnPad.load(); }
    /** The note received while learning (or -1); clears it. */
    int takeLearnedNote() noexcept { return learnedNote.exchange (-1); }

    /** Last incoming MIDI note and velocity (footer readout), -1 = none yet. */
    int getLastNote() const noexcept { return lastNote.load (std::memory_order_relaxed); }
    int getLastVelocity() const noexcept { return lastVelocityValue.load (std::memory_order_relaxed); }
    juce::uint32 getMidiCounter() const noexcept { return midiCounter.load (std::memory_order_relaxed); }

private:
    stsample::Engine engine;
    stsample::Kit kit;
    bool prepared = false;

    st::params::Ref outGain, polyphony;
    struct LayerRefs { st::params::Ref gain, pitch, start, length, invert; };
    struct PadRefs
    {
        st::params::Ref note, level, pan, vel, x, y, mode, choke, mute, solo;
        std::array<LayerRefs, stsample::kNumLayers> layers;
    };
    std::array<PadRefs, stsample::kNumPads> padRefs;
    stsample::EngineSettings settings;

    struct UiEvent { int kind = 0, pad = 0, layer = -1, note = 0; float velocity = 0.0f; };   // kind: 0 pad, 1 note on, 2 note off
    juce::AbstractFifo uiFifo { 128 };
    std::array<UiEvent, 128> uiEvents;
    void pushUiEvent (const UiEvent&);

    std::atomic<int> learnPad { -1 }, learnedNote { -1 }, lastNote { -1 }, lastVelocityValue { 0 };
    std::atomic<juce::uint32> midiCounter { 0 };
    std::array<float, 256> monoL {}, monoR {};

    void updateSettings() noexcept;
    void renderRange (juce::AudioBuffer<float>&, int from, int to) noexcept;
    void handleMidi (const juce::MidiMessage&) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StSampleProcessor)
};
