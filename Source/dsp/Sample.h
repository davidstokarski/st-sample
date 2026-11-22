#pragma once
#include "../Params.h"

namespace stsample
{

/** One decoded sample, immutable once built (shared between layers that use the same file and the voices playing it).

    Audio is stored with `kPad` zero frames before the first and after the last sample frame so the cubic
    interpolator can read 4 neighbours anywhere without bounds checks: frame i lives at index i + kPad. */
struct SampleData
{
    static constexpr int kPad = 2;
    static constexpr int kPeakBins = 2048;

    juce::AudioBuffer<float> audio;   // 1 or 2 channels, numFrames + 2 * kPad samples
    int numFrames = 0;
    double sampleRate = 48000.0;
    juce::String source;              // absolute path or "factory:<name>"
    juce::String name;                // shown in the UI
    juce::int64 fileTime = 0;         // modification time when loaded (cache check)

    /** Waveform overview: min / max of every bin over all channels (UI). */
    std::vector<float> peakMin, peakMax;
    float peak = 0.0f;

    double getLengthSeconds() const noexcept { return numFrames / juce::jmax (1.0, sampleRate); }
    int getNumChannels() const noexcept { return audio.getNumChannels(); }
    const float* channel (int c) const noexcept { return audio.getReadPointer (juce::jmin (c, audio.getNumChannels() - 1)) + kPad; }

    /** Builds a SampleData from raw audio (computes padding and peaks). */
    static std::shared_ptr<const SampleData> create (const juce::AudioBuffer<float>& audio, int numFrames, double sampleRate,
                                                     const juce::String& source, const juce::String& name);
};

using SamplePtr = std::shared_ptr<const SampleData>;

/** Sources: absolute file paths, or built-in sounds "factory:<name>". */
namespace sources
{
    inline const juce::String factoryPrefix = "factory:";
    inline bool isFactory (const juce::String& s) { return s.startsWith (factoryPrefix); }
    inline juce::String factory (const juce::String& name) { return factoryPrefix + name; }
    /** Display name of a source: file name without extension, or the factory sound's name. */
    juce::String displayName (const juce::String& source);

    /** Audio files the loader understands (for file choosers and drag & drop). */
    juce::String wildcard();
    bool isAudioFile (const juce::File&);
    /** Audio files in the same folder as `file`, natural order (sample browsing with ‹ ›). */
    juce::Array<juce::File> siblings (const juce::File& file);
}

/** Built-in drum sounds, synthesised on demand (no files needed, so a new instance makes sound right away). */
namespace factory
{
    const juce::StringArray& names();
    /** Renders a factory sound at 48 kHz. Empty buffer for an unknown name. */
    juce::AudioBuffer<float> render (const juce::String& name, double sampleRate = 48000.0);
}

/** Decodes a source (file or factory). Thread-safe; returns null and sets `error` on failure.
    Files are capped at `maxSeconds` (one-shots) and their first two channels. */
SamplePtr loadSample (const juce::String& source, juce::String& error, double maxSeconds = 60.0);

} // namespace stsample
