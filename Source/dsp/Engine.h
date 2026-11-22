#pragma once
#include "Sample.h"

namespace stsample
{

struct LayerSettings
{
    float gainDb = 0.0f;     // ≤ -60 = silent
    float pitch = 0.0f;      // semitones
    float start = 0.0f;      // 0..1 of the sample
    float length = 1.0f;     // 0..1 of what follows the start
    bool invert = false;
};

struct PadSettings
{
    int note = 36;
    float levelDb = 0.0f, pan = 0.0f;   // pan -1..1
    float velFloor = 0.2f;              // 0..1
    float x = 0.5f, y = 0.5f;           // XY blend
    bool gate = false;                  // false = one-shot (note off ignored)
    int choke = 0;                      // 0 = none, 1..8
    bool audible = true;                // mute / solo
    std::array<LayerSettings, kNumLayers> layers {};
};

struct EngineSettings
{
    std::array<PadSettings, kNumPads> pads {};
    float outputDb = 0.0f;
    int polyphony = 32;
};

/** The sampler: a pool of voices, each one hit of one pad playing up to four layers.

    - Samples are installed from the message thread (`setSample`) under a spin lock that the audio thread only
      try-locks when a voice starts; a voice keeps a reference to what it plays, so swapping or clearing a sample
      never cuts a ringing voice. Old samples are returned to the caller, who frees them on the message thread
      (they must never be destroyed by the audio thread: see Kit's graveyard).
    - Per voice and layer: cubic (Hermite) interpolation, pitch as a playback-rate ratio (sample rate conversion
      included), start / length region with short fades at cut points, gains (layer, XY blend, velocity, pad level,
      pan, mute / solo) ramped across each chunk so nothing zips.
    - Choke groups, one-shot or gate triggering, polyphony with click-free stealing.
    No allocation, locks or I/O in `render`, `noteOn`, `noteOff`, `triggerPad`. */
class Engine
{
public:
    Engine();

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const EngineSettings& s) noexcept { settings = s; }
    const EngineSettings& getSettings() const noexcept { return settings; }

    /** Message thread. Installs `sample` (may be null) and returns the one it replaced (free it on the message thread). */
    SamplePtr setSample (int pad, int layer, SamplePtr sample);
    SamplePtr getSample (int pad, int layer) const;
    /** Bit l set = layer l holds a sample. Any thread. */
    unsigned getLoadedMask (int pad) const noexcept { return loadedMask[(size_t) pad].load (std::memory_order_relaxed); }

    // ---- audio thread ------------------------------------------------------------------------------------
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    /** Plays one pad regardless of its note (audition). `layerMask` limits the layers; `ignoreBlend` plays them at
        their own gain without the XY weighting (auditioning one layer). */
    void triggerPad (int pad, float velocity, unsigned layerMask = 0xfu, bool ignoreBlend = false) noexcept;
    void releaseAll (bool fast) noexcept;
    /** Adds `n` samples into `left` / `right` (the caller clears them). Any n; internally chunked. */
    void render (float* left, float* right, int n) noexcept;
    /** Publishes play positions for the UI (call once per host block). */
    void publishPlayheads() noexcept;

    // ---- UI feedback (any thread) ------------------------------------------------------------------------
    juce::uint32 getHitCount (int pad) const noexcept { return hits[(size_t) pad].load (std::memory_order_relaxed); }
    float getLastVelocity (int pad) const noexcept { return lastVelocity[(size_t) pad].load (std::memory_order_relaxed); }
    /** Position of the newest voice of `pad` in layer `layer`'s sample, 0..1, or -1 when it isn't playing. */
    float getPlayhead (int pad, int layer) const noexcept { return playheads[(size_t) (pad * kNumLayers + layer)].load (std::memory_order_relaxed); }
    int getNumActiveVoices() const noexcept { return activeVoices.load (std::memory_order_relaxed); }
    /** Incoming note held (note on without note off yet), for the keyboard. */
    bool isNoteDown (int note) const noexcept;

    static constexpr int chunk = 64;

private:
    struct LayerVoice
    {
        SamplePtr hold;                  // keeps the sample alive while this voice plays it
        const SampleData* data = nullptr;
        double pos = 0.0, inc = 1.0, startPos = 0.0, endPos = 0.0, fadeIn = 0.0, fadeOut = 0.0;
        float gainL = 0.0f, gainR = 0.0f;
        bool active = false;
    };

    struct Voice
    {
        bool active = false, releasing = false, ignoreBlend = false;
        int pad = 0, note = -1;
        unsigned mask = 0;
        float velGain = 1.0f, env = 1.0f, envStep = 0.0f;
        juce::uint64 order = 0;
        std::array<LayerVoice, kNumLayers> layers;
    };

    double sampleRate = 48000.0;
    EngineSettings settings;
    std::array<Voice, kVoicePool> voices;
    juce::uint64 voiceCounter = 0;

    mutable juce::SpinLock sampleLock;
    std::array<std::array<SamplePtr, kNumLayers>, kNumPads> samples;
    std::array<std::atomic<unsigned>, kNumPads> loadedMask {};

    std::array<std::atomic<juce::uint32>, kNumPads> hits {};
    std::array<std::atomic<float>, kNumPads> lastVelocity {};
    std::array<std::atomic<float>, kNumPads * kNumLayers> playheads {};
    std::array<std::atomic<juce::uint64>, 2> notesDown {};
    std::atomic<int> activeVoices { 0 };
    float outGain = 1.0f;

    void startVoice (int pad, float velocity, unsigned layerMask, bool ignoreBlend, int note) noexcept;
    Voice* allocateVoice() noexcept;
    void release (Voice&, float seconds) noexcept;
    void targetGains (const Voice&, std::array<float, kNumLayers>& l, std::array<float, kNumLayers>& r) const noexcept;
    void renderVoice (Voice&, float* left, float* right, int n) noexcept;
    void setNoteDown (int note, bool down) noexcept;
};

} // namespace stsample
