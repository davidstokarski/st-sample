#include "Sample.h"

namespace stsample
{

SamplePtr SampleData::create (const juce::AudioBuffer<float>& in, int frames, double rate,
                              const juce::String& source, const juce::String& name)
{
    auto s = std::make_shared<SampleData>();
    const int channels = juce::jlimit (1, 2, in.getNumChannels());
    frames = juce::jlimit (0, in.getNumSamples(), frames);
    s->numFrames = frames;
    s->sampleRate = rate > 0.0 ? rate : 48000.0;
    s->source = source;
    s->name = name;
    s->audio.setSize (channels, frames + 2 * kPad);
    s->audio.clear();
    for (int c = 0; c < channels; ++c)
        s->audio.copyFrom (c, kPad, in, c, 0, frames);

    // A stereo file whose channels are identical is stored once (half the work per voice).
    if (channels == 2)
    {
        bool same = true;
        const auto* l = s->audio.getReadPointer (0);
        const auto* r = s->audio.getReadPointer (1);
        for (int i = 0; i < s->audio.getNumSamples() && same; ++i)
            same = l[i] == r[i];
        if (same) s->audio.setSize (1, s->audio.getNumSamples(), true);
    }

    const int bins = juce::jmax (1, juce::jmin (kPeakBins, frames));
    s->peakMin.assign ((size_t) bins, 0.0f);
    s->peakMax.assign ((size_t) bins, 0.0f);
    for (int b = 0; b < bins && frames > 0; ++b)
    {
        const int from = (int) ((juce::int64) b * frames / bins);
        const int to = juce::jmax (from + 1, (int) ((juce::int64) (b + 1) * frames / bins));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < s->audio.getNumChannels(); ++c)
        {
            const auto* d = s->audio.getReadPointer (c) + kPad;
            for (int i = from; i < to; ++i)
            {
                lo = juce::jmin (lo, d[i]);
                hi = juce::jmax (hi, d[i]);
            }
        }
        s->peakMin[(size_t) b] = lo;
        s->peakMax[(size_t) b] = hi;
        s->peak = juce::jmax (s->peak, hi, -lo);
    }
    return s;
}

namespace sources
{
    juce::String displayName (const juce::String& source)
    {
        if (source.isEmpty()) return {};
        if (isFactory (source)) return source.fromFirstOccurrenceOf (factoryPrefix, false, false);
        return juce::File (source).getFileNameWithoutExtension();
    }

    juce::String wildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg"; }

    bool isAudioFile (const juce::File& f)
    {
        static const juce::StringArray ext { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".caf", ".ogg" };
        return f.existsAsFile() && ext.contains (f.getFileExtension().toLowerCase());
    }

    juce::Array<juce::File> siblings (const juce::File& file)
    {
        juce::Array<juce::File> out;
        const auto dir = file.getParentDirectory();
        if (! dir.isDirectory()) return out;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles))
            if (isAudioFile (entry.getFile()) && ! entry.getFile().getFileName().startsWithChar ('.'))
                out.add (entry.getFile());
        std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
        return out;
    }
}

SamplePtr loadSample (const juce::String& source, juce::String& error, double maxSeconds)
{
    error.clear();
    if (source.isEmpty()) { error = "No sample"; return nullptr; }

    if (sources::isFactory (source))
    {
        const auto name = sources::displayName (source);
        auto audio = factory::render (name, 48000.0);
        if (audio.getNumSamples() == 0) { error = "Unknown built-in sound"; return nullptr; }
        return SampleData::create (audio, audio.getNumSamples(), 48000.0, source, name);
    }

    const juce::File file (source);
    if (! file.existsAsFile()) { error = "File not found"; return nullptr; }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported file"; return nullptr; }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) { error = "Empty file"; return nullptr; }

    const auto frames = (int) juce::jmin ((juce::int64) (reader->sampleRate * maxSeconds), reader->lengthInSamples);
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> audio (channels, frames);
    if (! reader->read (&audio, 0, frames, 0, true, channels > 1)) { error = "Could not read file"; return nullptr; }

    auto s = SampleData::create (audio, frames, reader->sampleRate, source, file.getFileNameWithoutExtension());
    const_cast<SampleData&> (*s).fileTime = file.getLastModificationTime().toMilliseconds();
    return s;
}

} // namespace stsample
