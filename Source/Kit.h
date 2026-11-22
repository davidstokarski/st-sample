#pragma once
#include "dsp/Engine.h"

namespace stsample
{

/** The kit: which sample each pad layer plays and each pad's name.

    The truth is `apvts.state`'s "KIT" child (so presets, A/B, sessions and undo carry it):
        <KIT> <PAD i="0" name="Kick"> <LAYER i="0" source="/path/kick.wav"/> ... </PAD> ... </KIT>
    Every edit is an (undoable) tree change; `reconcile()` turns the tree into decoded samples in the engine.
    Decoding runs on a worker thread; results are installed on the message thread. Replaced samples go to a
    graveyard that is emptied on the message thread once no voice holds them any more (the audio thread never frees
    sample memory). Message thread only, except where noted. */
class Kit : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer, public juce::ChangeBroadcaster
{
public:
    Kit (juce::AudioProcessorValueTreeState&, juce::UndoManager&, Engine&);
    ~Kit() override;

    enum class Status { empty, loading, ready, missing };
    struct Layer
    {
        juce::String source, name, error;
        Status status = Status::empty;
        SamplePtr data;
    };

    const Layer& getLayer (int pad, int layer) const noexcept { return layers[(size_t) pad][(size_t) layer]; }
    bool isPadEmpty (int pad) const noexcept;
    int getNumLoadedLayers (int pad) const noexcept;
    /** The pad's name: its custom name, else its first sample's name, else "Pad <n>". */
    juce::String getPadName (int pad) const;
    bool hasCustomName (int pad) const;

    // ---- edits (undoable when `undoable`) ------------------------------------------------------------------
    void setLayerSource (int pad, int layer, const juce::String& source, bool undoable = true);
    void clearLayer (int pad, int layer) { setLayerSource (pad, layer, {}); }
    void clearPad (int pad);
    void setPadName (int pad, const juce::String& name, bool undoable = true);
    /** Loads files into a pad, starting at `firstLayer` and filling the following layers. Returns the number loaded. */
    int loadFiles (int pad, int firstLayer, const juce::StringArray& files);
    /** First layer without a sample, or -1. */
    int firstEmptyLayer (int pad) const noexcept;

    /** The whole kit tree for one pad (layers + name) and its plain copy / paste. */
    juce::ValueTree copyPad (int pad) const;
    void pastePad (int pad, const juce::ValueTree& padTree);

    /** Waits for pending loads and installs them (tests, offline). Returns false on timeout. */
    bool waitUntilLoaded (int timeoutMs = 10000);
    /** Number of decodes queued or running. Any thread. */
    int getNumPending() const noexcept { return pending.load(); }

    static juce::ValueTree buildPadTree (int pad, const juce::String& name, const juce::StringArray& layerSources);
    static const juce::Identifier kitType, padType, layerType, indexId, nameId, sourceId;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager& undo;
    Engine& engine;
    std::array<std::array<Layer, kNumLayers>, kNumPads> layers;
    std::array<std::array<juce::uint32, kNumLayers>, kNumPads> generation {};
    std::vector<SamplePtr> graveyard;

    // ---- loader thread ---------------------------------------------------------------------------------------
    struct Job { int pad, layer; juce::uint32 gen; juce::String source; };
    struct Result { int pad, layer; juce::uint32 gen; juce::String source, error; SamplePtr data; };
    class Loader;
    std::unique_ptr<Loader> loader;
    std::atomic<int> pending { 0 };

    juce::ValueTree kitTree (bool create);
    juce::ValueTree padTree (int pad, bool create);
    juce::ValueTree layerTree (int pad, int layer, bool create);

    void reconcile();
    void install (const Result&);
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
    static bool isKitNode (const juce::ValueTree&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Kit)
};

} // namespace stsample
