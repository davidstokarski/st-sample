// Whole-plugin tests (engine, sample loading, kit state, undo, stress) and editor snapshots.
//   StSamplePluginTests                    run the tests
//   StSamplePluginTests --snapshot <dir>   render the editor to PNGs
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace stsample;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

/** A sample with known content: value(i) = f(i). */
SamplePtr makeSample (int frames, double rate, std::function<float (int)> f, int channels = 1, const juce::String& name = "test")
{
    juce::AudioBuffer<float> b (channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            b.setSample (c, i, f (i) * (c == 0 ? 1.0f : 0.5f));
    return SampleData::create (b, frames, rate, "test:" + name, name);
}

EngineSettings flatSettings()
{
    EngineSettings s;
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& ps = s.pads[(size_t) p];
        ps.note = 36 + p;
        ps.velFloor = 1.0f;   // velocity ignored
    }
    return s;
}

std::vector<float> renderEngine (Engine& e, int total, int channel = 0, std::function<void (int)> atSample = {})
{
    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    for (int i = 0; i < total; i += 32)
    {
        if (atSample) atSample (i);
        const int n = juce::jmin (32, total - i);
        e.render (l.data() + i, r.data() + i, n);
    }
    return channel == 0 ? l : r;
}

float peakOf (const std::vector<float>& v, int from = 0, int to = -1)
{
    float p = 0.0f;
    for (int i = from; i < (to < 0 ? (int) v.size() : to); ++i) p = juce::jmax (p, std::abs (v[(size_t) i]));
    return p;
}

int lastNonZero (const std::vector<float>& v)
{
    for (int i = (int) v.size() - 1; i >= 0; --i)
        if (std::abs (v[(size_t) i]) > 1.0e-7f) return i;
    return -1;
}

void engineTests()
{
    std::cout << "Engine\n";
    constexpr double sr = 48000.0;

    {
        Engine e;
        e.prepare (sr);
        e.setSettings (flatSettings());
        e.noteOn (36, 1.0f);
        check (peakOf (renderEngine (e, 4800)) == 0.0f, "no samples: silence");
    }

    // ---- unity playback is sample exact (no pitch, same rate)
    {
        Engine e;
        e.prepare (sr);
        e.setSettings (flatSettings());
        auto s = makeSample (4000, sr, [] (int i) { return 0.6f * std::sin ((float) i * 0.05f) * std::exp (-(float) i / 2000.0f); });
        e.setSample (0, 0, s);
        e.noteOn (36, 1.0f);
        const auto out = renderEngine (e, 6000);
        float err = 0.0f;
        for (int i = 0; i < 4000; ++i) err = juce::jmax (err, std::abs (out[(size_t) i] - s->channel (0)[i]));
        check (err < 1.0e-6f, "unity playback equals the sample (max err " + juce::String (err, 8) + ")");
        check (peakOf (out, 4001) == 0.0f, "stops at the end of the sample");
        const auto right = renderEngine (e, 10);
        juce::ignoreUnused (right);
        check (e.getNumActiveVoices() == 0, "voice freed after the sample ends");
    }

    // ---- pitch and sample-rate conversion change the duration
    {
        auto s = makeSample (4800, sr, [] (int) { return 0.5f; });
        for (auto [pitch, rate, expect] : { std::tuple { 12.0f, sr, 2400 }, std::tuple { -12.0f, sr, 9600 }, std::tuple { 0.0f, 44100.0, 5224 } })
        {
            Engine e;
            e.prepare (sr);
            auto st = flatSettings();
            st.pads[0].layers[0].pitch = pitch;
            e.setSettings (st);
            e.setSample (0, 0, rate == sr ? s : makeSample (4800, rate, [] (int) { return 0.5f; }));
            e.noteOn (36, 1.0f);
            const int len = lastNonZero (renderEngine (e, 12000)) + 1;
            check (std::abs (len - expect) <= 2, "pitch " + juce::String (pitch) + " st @ " + juce::String (rate) + " Hz: "
                   + juce::String (len) + " samples (expected " + juce::String (expect) + ")");
        }
    }

    // ---- XY blend: equal power, empty corners ignored
    {
        auto a = makeSample (2000, sr, [] (int) { return 0.5f; }, 1, "a");
        auto b = makeSample (2000, sr, [] (int) { return 0.25f; }, 1, "b");
        auto run = [&] (float x, float y, bool withB)
        {
            Engine e;
            e.prepare (sr);
            auto st = flatSettings();
            st.pads[0].x = x;
            st.pads[0].y = y;
            e.setSettings (st);
            e.setSample (0, 0, a);
            if (withB) e.setSample (0, 1, b);
            e.noteOn (36, 1.0f);
            return renderEngine (e, 1000)[500];
        };
        check (std::abs (run (0.5f, 0.5f, false) - 0.5f) < 1.0e-5f, "one layer at the centre plays at full level");
        check (std::abs (run (0.0f, 0.0f, true) - 0.5f) < 1.0e-5f, "corner A plays only A");
        check (std::abs (run (1.0f, 0.0f, true) - 0.25f) < 1.0e-5f, "corner B plays only B");
        const float mid = run (0.5f, 0.5f, true);
        check (std::abs (mid - (0.5f + 0.25f) * std::sqrt (0.5f)) < 1.0e-4f, "two layers at the centre: equal power (" + juce::String (mid, 5) + ")");
        check (std::abs (run (0.5f, 1.0f, false)) < 1.0e-5f, "moving to the empty bottom edge fades A out");
        const auto w = blendWeights (0.5f, 0.5f, 0xfu);
        float power = 0.0f;
        for (float v : w) power += v * v;
        check (std::abs (power - 1.0f) < 1.0e-5f, "four layers at the centre: power sums to 1");
    }

    // ---- gain, invert, pan, velocity floor, mute
    {
        auto s = makeSample (2000, sr, [] (int) { return 0.5f; });
        auto run = [&] (std::function<void (EngineSettings&)> edit, float velocity = 1.0f, int channel = 0)
        {
            Engine e;
            e.prepare (sr);
            auto st = flatSettings();
            edit (st);
            e.setSettings (st);
            e.setSample (0, 0, s);
            e.noteOn (36, velocity);
            return renderEngine (e, 1000, channel)[500];
        };
        check (std::abs (run ([] (auto& s) { s.pads[0].layers[0].gainDb = -6.0206f; }) - 0.25f) < 1.0e-4f, "layer gain -6 dB halves");
        check (std::abs (run ([] (auto& s) { s.pads[0].levelDb = 6.0206f; }) - 1.0f) < 1.0e-4f, "pad level +6 dB doubles");
        check (run ([] (auto& s) { s.pads[0].layers[0].invert = true; }) < -0.49f, "phase invert");
        check (std::abs (run ([] (auto& s) { s.pads[0].pan = 1.0f; }, 1.0f, 0)) < 1.0e-6f && run ([] (auto& s) { s.pads[0].pan = 1.0f; }, 1.0f, 1) > 0.49f, "pan hard right");
        const float v = run ([] (auto& s) { s.pads[0].velFloor = 0.0f; }, 0.5f);
        check (std::abs (v - 0.5f * std::pow (0.5f, 1.5f)) < 1.0e-4f, "velocity floor 0: velocity scales the level");
        check (std::abs (run ([] (auto& s) { s.pads[0].velFloor = 0.25f; }, 0.0f) - 0.125f) < 1.0e-4f, "velocity floor sets the softest level");
        check (std::abs (run ([] (auto& s) { s.pads[0].audible = false; })) < 1.0e-6f, "muted pad is silent");
        check (std::abs (run ([] (auto& s) { s.outputDb = -6.0206f; }) - 0.25f) < 1.0e-4f, "output gain");
    }

    // ---- start / length with fades at the cuts
    {
        auto s = makeSample (10000, sr, [] (int) { return 0.5f; });
        Engine e;
        e.prepare (sr);
        auto st = flatSettings();
        st.pads[0].layers[0].start = 0.5f;
        st.pads[0].layers[0].length = 0.4f;
        e.setSettings (st);
        e.setSample (0, 0, s);
        e.noteOn (36, 1.0f);
        const auto out = renderEngine (e, 12000);
        const int len = lastNonZero (out) + 1;
        check (std::abs (len - 2000) <= 2, "start 50% + length 40% plays 2000 samples (" + juce::String (len) + ")");
        check (std::abs (out[0]) < 0.01f && std::abs (out[1000] - 0.5f) < 1.0e-4f, "fade in at the start cut, full level inside");
        check (out[(size_t) len - 2] < 0.05f, "fade out at the end cut");
    }

    // ---- choke group
    {
        auto lng = makeSample (48000, sr, [] (int) { return 0.5f; });
        Engine e;
        e.prepare (sr);
        auto st = flatSettings();
        st.pads[0].choke = st.pads[1].choke = 1;
        e.setSettings (st);
        e.setSample (0, 0, lng);
        e.noteOn (36, 1.0f);
        const auto out = renderEngine (e, 4800, 0, [&] (int i) { if (i == 960) e.noteOn (37, 1.0f); });
        check (std::abs (out[900] - 0.5f) < 1.0e-4f && std::abs (out[960 + 800]) < 1.0e-6f, "choke: a hit in the group fades the other pad within 17 ms");
        check (e.getNumActiveVoices() == 0, "choked voice freed");
    }

    // ---- gate mode
    {
        auto lng = makeSample (48000, sr, [] (int) { return 0.5f; });
        Engine e;
        e.prepare (sr);
        auto st = flatSettings();
        st.pads[0].gate = true;
        e.setSettings (st);
        e.setSample (0, 0, lng);
        e.noteOn (36, 1.0f);
        const auto out = renderEngine (e, 9600, 0, [&] (int i) { if (i == 960) e.noteOff (36); });
        check (std::abs (out[900] - 0.5f) < 1.0e-4f && std::abs (out[960 + 2400]) < 1.0e-6f, "gate: note off fades out (45 ms)");

        Engine o;
        o.prepare (sr);
        o.setSettings (flatSettings());
        o.setSample (0, 0, lng);
        o.noteOn (36, 1.0f);
        const auto one = renderEngine (o, 9600, 0, [&] (int i) { if (i == 960) o.noteOff (36); });
        check (std::abs (one[5000] - 0.5f) < 1.0e-4f, "one-shot ignores note off");
    }

    // ---- polyphony
    {
        auto lng = makeSample (96000, sr, [] (int) { return 0.1f; });
        Engine e;
        e.prepare (sr);
        auto st = flatSettings();
        st.polyphony = 8;
        e.setSettings (st);
        e.setSample (0, 0, lng);
        for (int i = 0; i < 30; ++i) { e.noteOn (36, 1.0f); renderEngine (e, 64); }
        renderEngine (e, 960);
        check (e.getNumActiveVoices() <= 8, "polyphony 8: " + juce::String (e.getNumActiveVoices()) + " voices after 30 hits");
    }

    // ---- replacing a sample while it plays keeps the old one alive until the voice ends
    {
        auto a = makeSample (4800, sr, [] (int) { return 0.5f; });
        std::weak_ptr<const SampleData> weak = a;
        Engine e;
        e.prepare (sr);
        e.setSettings (flatSettings());
        e.setSample (0, 0, std::move (a));
        e.noteOn (36, 1.0f);
        renderEngine (e, 1000);
        auto old = e.setSample (0, 0, makeSample (100, sr, [] (int) { return 0.1f; }));
        check (old.use_count() == 2, "playing voice still holds the replaced sample");
        const auto out = renderEngine (e, 1000);
        check (std::abs (out[500] - 0.5f) < 1.0e-4f, "voice keeps playing the replaced sample");
        renderEngine (e, 4800);
        check (old.use_count() == 1, "voice released it when done (only the caller holds it)");
        old.reset();
        check (weak.expired(), "freed by the message thread side");
    }
}

//==============================================================================
void setParam (StSampleProcessor& p, const juce::String& id, float plain)
{
    auto& prm = p.param (id);
    prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
}

juce::File writeTestWav (const juce::String& name, double rate, int frames, float freq, int channels = 1)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("st-sample-tests");
    dir.createDirectory();
    auto f = dir.getChildFile (name + ".wav");
    f.deleteFile();
    juce::AudioBuffer<float> b (channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            b.setSample (c, i, (c == 0 ? 0.7f : 0.5f) * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) rate) * std::exp (-(float) i / (float) frames * 4.0f));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (channels).withBitsPerSample (24));
    if (writer != nullptr) writer->writeFromAudioSampleBuffer (b, 0, frames);
    return f;
}

float renderMidi (StSampleProcessor& p, int blocks, int blockSize, bool& finite, juce::Random& rng, float density = 0.2f)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (rng.nextFloat() < density)
        {
            const int note = 36 + rng.nextInt (16);
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (20 + rng.nextInt (107))), rng.nextInt (blockSize));
            midi.addEvent (juce::MidiMessage::noteOff (1, note), blockSize - 1);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
            }
    }
    return peak;
}

/** Lets every ringing voice end (all sound off, then a few blocks). */
void silence (StSampleProcessor& p)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
    p.processBlock (buf, midi);
    for (int i = 0; i < 4; ++i) { juce::MidiBuffer none; p.processBlock (buf, none); }
}

float hitPeak (StSampleProcessor& p, int note, int blocks = 40)
{
    silence (p);
    juce::AudioBuffer<float> buf (2, 512);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
        p.processBlock (buf, midi);
        const float m = buf.getMagnitude (0, 512);   // every channel
        peak = juce::jmax (peak, m);
    }
    return peak;
}

void processorTests()
{
    std::cout << "Processor\n";
    StSampleProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& kit = p.getKit();

    check (kit.waitUntilLoaded(), "ST Kit loads");
    check (p.presets.getCurrentName() == "ST Kit", "new instance starts with the ST Kit");
    check (kit.getNumLoadedLayers (0) == 3 && kit.getLayer (0, 0).status == Kit::Status::ready, "kick has three ready layers");
    check (kit.getPadName (2) == "Snare" && p.getPadNote (2) == 38, "snare on D1 (38)");
    check (factory::names().size() >= 18, "built-in sounds: " + juce::String (factory::names().size()));
    {
        bool allOk = true;
        for (auto& n : factory::names())
        {
            juce::String err;
            auto s = loadSample (sources::factory (n), err);
            allOk &= s != nullptr && s->peak > 0.5f && s->peak < 0.95f && s->numFrames > 1000;
        }
        check (allOk, "every built-in sound renders, normalised to -1 dBFS");
    }

    for (int pad : { 0, 2, 6, 15 })
        check (hitPeak (p, p.getPadNote (pad)) > 0.05f, "MIDI " + notes::name (p.getPadNote (pad)) + " plays " + kit.getPadName (pad));
    check (hitPeak (p, 100) == 0.0f, "unmapped note is silent");

    // ---- note names
    check (notes::name (60) == "C3" && notes::name (36) == "C1" && notes::name (39) == "D#1", "note names (middle C = C3)");
    check (notes::parse ("d#2") == 51 && notes::parse ("Eb1") == 39 && notes::parse ("C-2") == 0 && notes::parse ("x") == -1, "note parsing");
    check (p.param (ids::pad (0, "note")).getText (p.param (ids::pad (0, "note")).getValue(), 8) == "C1", "note parameter shows the note name");

    // ---- loading files, missing files, undo
    {
        const auto wav = writeTestWav ("tone 220", 44100.0, 22050, 220.0f, 2);
        p.undoManager.clearUndoHistory();
        kit.setLayerSource (5, 3, wav.getFullPathName());
        kit.waitUntilLoaded();
        const auto& l = kit.getLayer (5, 3);
        check (l.status == Kit::Status::ready && l.name == "tone 220" && l.data->getNumChannels() == 2
               && std::abs (l.data->sampleRate - 44100.0) < 0.1 && l.data->numFrames == 22050, "WAV file loads (stereo, 44.1 kHz)");
        check (p.getEngine().getLoadedMask (5) == 0x9u, "engine has layers A + D on pad 6");

        p.undoManager.undo();
        kit.waitUntilLoaded();
        check (kit.getLayer (5, 3).source.isEmpty() && p.getEngine().getLoadedMask (5) == 0x1u, "undo removes the loaded sample");
        p.undoManager.redo();
        kit.waitUntilLoaded();
        check (kit.getLayer (5, 3).status == Kit::Status::ready, "redo loads it again");

        kit.setLayerSource (5, 2, "/nowhere/missing kick.wav");
        kit.waitUntilLoaded();
        check (kit.getLayer (5, 2).status == Kit::Status::missing && kit.getLayer (5, 2).name == "missing kick", "missing file keeps its slot and name");
        check (kit.getLayer (5, 2).source == "/nowhere/missing kick.wav", "missing file's path stays in the state");

        const auto files = sources::siblings (wav);
        check (files.contains (wav), "folder browsing finds the file");

        // same source in two layers shares one decoded sample
        kit.setLayerSource (7, 0, wav.getFullPathName());
        kit.waitUntilLoaded();
        check (kit.getLayer (7, 0).data == kit.getLayer (5, 3).data, "identical files are decoded once");
    }

    // ---- learn, audition
    {
        silence (p);
        p.setLearnPad (4);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 10);
        p.processBlock (buf, midi);
        check (p.takeLearnedNote() == 72 && buf.getMagnitude (0, 512) == 0.0f, "learn captures the note without playing");
        p.setLearnPad (-1);
        for (int i = 0; i < 100; ++i) { juce::MidiBuffer none; p.processBlock (buf, none); }
        p.auditionPad (2);
        juce::MidiBuffer none;
        p.processBlock (buf, none);
        check (buf.getMagnitude (0, 512) > 0.05f, "audition from the UI plays the pad");
        check (p.getLastNote() == 72, "last MIDI note reported");
    }

    // ---- state round trip (params + kit) and presets
    {
        setParam (p, ids::layer (2, 1, "pitch"), -3.5f);
        setParam (p, ids::pad (2, "x"), 0.8f);
        kit.setPadName (2, "Big Snare");
        juce::MemoryBlock state;
        p.getStateInformation (state);

        StSampleProcessor q;
        q.setPlayConfigDetails (0, 2, 48000.0, 512);
        q.prepareToPlay (48000.0, 512);
        q.setStateInformation (state.getData(), (int) state.getSize());
        q.getKit().waitUntilLoaded();
        bool same = true;
        for (int pd = 0; pd < kNumPads; ++pd)
            for (int l = 0; l < kNumLayers; ++l)
                same &= q.getKit().getLayer (pd, l).source == kit.getLayer (pd, l).source;
        check (same, "state round trip: every layer's source");
        check (std::abs (q.getLayerValue (2, 1, "pitch") + 3.5f) < 1.0e-4f && std::abs (q.getPadValue (2, "x") - 0.8f) < 1.0e-4f, "state round trip: parameters");
        check (q.getKit().getPadName (2) == "Big Snare", "state round trip: pad name");

        q.presets.loadPreset (0);
        q.getKit().waitUntilLoaded();
        bool empty = true;
        for (int pd = 0; pd < kNumPads; ++pd) empty &= q.getKit().isPadEmpty (pd);
        const float initPeak = hitPeak (q, 36);
        check (empty && initPeak == 0.0f, "Init preset is an empty kit (empty " + juce::String ((int) empty) + ", peak " + juce::String (initPeak) + ")");

        const int layerLab = [&] { for (int i = 0; i < (int) q.presets.getPresets().size(); ++i) if (q.presets.getPresets()[(size_t) i].name == "Layer Lab") return i; return -1; }();
        q.presets.loadPreset (layerLab);
        q.getKit().waitUntilLoaded();
        check (q.getKit().getNumLoadedLayers (0) == 4 && q.getKit().getNumLoadedLayers (2) == 4, "Layer Lab: four layers on kick and snare");

        // A/B keeps kits apart
        q.ab.copyActiveToOther();
        q.getKit().clearPad (0);
        q.getKit().waitUntilLoaded();
        q.ab.toggle();
        q.getKit().waitUntilLoaded();
        check (q.getKit().getNumLoadedLayers (0) == 4, "A/B: the other slot keeps its own kit");
    }

    // ---- copy / paste pad
    {
        const auto copy = kit.copyPad (0);
        kit.pastePad (9, copy);
        kit.waitUntilLoaded();
        check (kit.getNumLoadedLayers (9) == 3 && std::abs (p.getPadValue (9, "x") - p.getPadValue (0, "x")) < 1.0e-5f, "paste pad copies layers and settings");
        check (p.getPadNote (9) != p.getPadNote (0), "paste keeps the target's note");
        p.undoManager.undo();
        kit.waitUntilLoaded();
        check (kit.getLayer (9, 0).name == "Tom Mid", "undo paste restores the pad");
    }

    // ---- stress: block sizes, dense MIDI, bounded, no NaN
    {
        p.presets.loadPreset (1);
        kit.waitUntilLoaded();
        juce::Random rng (7);
        bool finite = true;
        float peak = 0.0f;
        for (int bs : { 1, 7, 63, 64, 65, 333, 2048 })
            peak = juce::jmax (peak, renderMidi (p, juce::jmax (4, 48000 / bs / 4), bs, finite, rng, juce::jmin (0.6f, (float) bs / 600.0f)));
        check (finite && peak < 8.0f, "odd block sizes + dense MIDI: finite, bounded (peak " + juce::String (peak, 2) + ")");

        // every parameter at random values
        for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (rng.nextFloat());
        setParam (p, ids::outputGain, 0.0f);
        peak = renderMidi (p, 400, 256, finite, rng, 0.8f);
        check (finite && peak < 16.0f, "random parameters: finite, bounded (peak " + juce::String (peak, 2) + ")");
        p.presets.loadPreset (1);
        kit.waitUntilLoaded();

        // mono output
        p.setPlayConfigDetails (0, 1, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> mono (1, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
        p.processBlock (mono, midi);
        check (mono.getMagnitude (0, 512) > 0.05f, "mono output plays");
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (layout), "stereo out, no input: supported");

        // CPU: 10 s at 48 kHz with a busy pattern (16th notes on every pad)
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        juce::AudioBuffer<float> buf (2, 256);
        for (int b = 0; b < 48000 * 10 / 256; ++b)
        {
            juce::MidiBuffer m;
            if (b % 23 == 0)
                for (int pd = 0; pd < 6; ++pd) m.addEvent (juce::MidiMessage::noteOn (1, p.getPadNote (pd * 2), (juce::uint8) 110), pd * 7);
            p.processBlock (buf, m);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  info CPU: 10 s of busy drums rendered in " << juce::String (ms, 1) << " ms ("
                  << juce::String (100.0 * ms / 10000.0, 2) << "% of real time)\n";
    }
}

int runTests()
{
    juce::ScopedJuceInitialiser_GUI gui;
    engineTests();
    processorTests();
    std::cout << (failures == 0 ? "\nAll tests passed\n" : "\n" + juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

//==============================================================================
struct Snapshotter : juce::Timer
{
    struct Job { std::function<void()> setup; juce::String name; float scale = 1.0f; std::function<void (StSampleEditor&)> afterOpen; std::vector<int> hits {}; };
    StSampleProcessor& p;
    juce::File dir;
    std::vector<Job> jobs;
    size_t index = 0;
    int ticks = 0;
    bool opened = false;
    std::unique_ptr<juce::AudioProcessorEditor> ed;

    Snapshotter (StSampleProcessor& proc, juce::File d) : p (proc), dir (d) {}

    void timerCallback() override
    {
        if (ed == nullptr)
        {
            if (index >= jobs.size()) { stopTimer(); juce::JUCEApplicationBase::quit(); return; }
            if (jobs[index].setup) jobs[index].setup();
            p.getKit().waitUntilLoaded();
            ed.reset (p.createEditor());
            ed->setSize (juce::roundToInt ((float) StSampleEditor::designWidth * jobs[index].scale),
                         juce::roundToInt ((float) StSampleEditor::designHeight * jobs[index].scale));
            ticks = 0;
            opened = false;
        }

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int note : jobs[index].hits)
            if (ticks == 38) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 120), 0);
        p.processBlock (buf, midi);

        if (! opened && ticks > 6 && jobs[index].afterOpen)
        {
            if (auto* e = dynamic_cast<StSampleEditor*> (ed.get())) jobs[index].afterOpen (*e);
            opened = true;
        }

        if (++ticks > 46)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto file = dir.getChildFile (jobs[index].name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
            std::cout << "wrote " << file.getFullPathName() << "\n";
            ed.reset();
            ++index;
        }
    }
};

juce::File snapshotDir;

/** Renders a looping clip's frames (1560 x 1036 at 46.875 fps = 1024 samples per frame at 48 kHz): a groove on the
    Layer Lab kit, the XY puck circling, the selection moving from the kick to the snare halfway. */
int renderVideo (const juce::File& dir, int frames)
{
    juce::ScopedJuceInitialiser_GUI gui;
    dir.createDirectory();
    StSampleProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 1024);
    p.prepareToPlay (48000.0, 1024);
    for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
        if (p.presets.getPresets()[(size_t) i].name == "Layer Lab") p.presets.loadPreset (i);
    p.getKit().waitUntilLoaded();
    p.uiState.setProperty ("selectedPad", 0, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* editor = dynamic_cast<StSampleEditor*> (ed.get());
    editor->useExternalClock();
    ed->setSize (StSampleEditor::designWidth, StSampleEditor::designHeight);

    // 16ths at 100 BPM; pattern per step: kick, snare, closed hat, open hat, clap
    const int step = 7200;
    const char* kick  = "x.....x...x.....x.....x..x......";
    const char* snare = "....x.......x.......x.......x..x";
    const char* hat   = "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.";
    const char* open  = "..............x...............x.";
    const double dt = 1024.0 / 48000.0;
    juce::AudioBuffer<float> buf (2, 1024);
    for (int f = 0; f < frames; ++f)
    {
        const juce::int64 t0 = (juce::int64) f * 1024;
        juce::MidiBuffer midi;
        for (juce::int64 s = ((t0 + step - 1) / step) * step; s < t0 + 1024; s += step)
        {
            const int i = (int) ((s / step) % 32);
            const int off = (int) (s - t0);
            auto hit = [&] (int note, int vel) { midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), off); };
            if (kick[i] == 'x') hit (36, 120);
            if (snare[i] == 'x') hit (38, i == 31 ? 70 : 115);
            if (hat[i] == 'x' && open[i] != 'x') hit (42, i % 4 == 0 ? 100 : 70);
            if (open[i] == 'x') hit (46, 95);
        }

        const int pad = f < frames / 2 ? 0 : 2;
        if (f == frames / 2) editor->selectPad (2);
        const double ph = juce::MathConstants<double>::twoPi * (double) f / (double) (frames / 2);
        p.setPlain (p.padParam (pad, "x"), (float) (0.5 + 0.32 * std::cos (ph + (pad == 0 ? 2.4 : 0.6))), false);
        p.setPlain (p.padParam (pad, "y"), (float) (0.5 + 0.30 * std::sin (ph + (pad == 0 ? 2.4 : 0.6))), false);

        p.processBlock (buf, midi);
        editor->advanceFrame (dt);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        auto file = dir.getChildFile (juce::String (f).paddedLeft ('0', 4) + ".png");
        juce::FileOutputStream out (file);
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
    std::cout << "wrote " << frames << " frames to " << dir.getFullPathName() << "\n";
    return 0;
}

struct SnapshotApp : juce::JUCEApplicationBase
{
    std::unique_ptr<StSampleProcessor> proc;
    std::unique_ptr<Snapshotter> snap;

    const juce::String getApplicationName() override { return "StSampleSnapshots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override { snap.reset(); proc.reset(); }

    int presetIndex (const juce::String& name) const
    {
        for (int i = 0; i < (int) proc->presets.getPresets().size(); ++i)
            if (proc->presets.getPresets()[(size_t) i].name == name) return i;
        return 0;
    }

    void initialise (const juce::String&) override
    {
        proc = std::make_unique<StSampleProcessor>();
        auto& p = *proc;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        snapshotDir.createDirectory();

        snap = std::make_unique<Snapshotter> (p, snapshotDir);
        snap->jobs.push_back ({ [&p] { p.uiState.setProperty ("selectedPad", 0, nullptr); }, "01_st_kit_kick", 1.0f, {}, { 36 } });
        snap->jobs.push_back ({ [&p] { p.uiState.setProperty ("selectedPad", 2, nullptr); }, "02_snare_playing", 1.0f, {}, { 38, 42 } });
        snap->jobs.push_back ({ [&p, this]
        {
            p.presets.loadPreset (presetIndex ("Layer Lab"));
            p.uiState.setProperty ("selectedPad", 0, nullptr);
        }, "03_layer_lab", 1.0f, {}, { 36 } });
        snap->jobs.push_back ({ [&p]
        {
            p.uiState.setProperty ("selectedPad", 2, nullptr);
            auto& l = p.layerParam (2, 2, "length");
            l.setValueNotifyingHost (l.convertTo0to1 (35.0f));
            auto& s = p.layerParam (2, 0, "start");
            s.setValueNotifyingHost (s.convertTo0to1 (6.0f));
            p.padParam (2, "solo").setValueNotifyingHost (1.0f);
        }, "04_snare_trimmed_solo", 1.0f, {}, { 38 } });
        snap->jobs.push_back ({ [&p]
        {
            p.padParam (2, "solo").setValueNotifyingHost (0.0f);
            p.presets.loadPreset (1);
            p.getKit().waitUntilLoaded();
            const auto wav = writeTestWav ("Room Kick 03", 44100.0, 30000, 70.0f, 2);
            p.getKit().setLayerSource (0, 3, wav.getFullPathName());
            p.getKit().setLayerSource (0, 2, "/Volumes/Samples/Kicks/Missing Kick.wav");
            p.uiState.setProperty ("selectedPad", 0, nullptr);
        }, "05_user_files_missing" });
        snap->jobs.push_back ({ [&p] { p.presets.loadPreset (0); p.uiState.setProperty ("selectedPad", 0, nullptr); }, "06_init_empty" });
        snap->jobs.push_back ({ [&p, this] { p.presets.loadPreset (presetIndex ("ST Kit")); p.uiState.setProperty ("selectedPad", 10, nullptr); }, "07_small_scale", 0.75f });
        snap->jobs.push_back ({ [&p] { p.uiState.setProperty ("selectedPad", 6, nullptr); }, "08_learning", 1.0f,
                                [&p] (StSampleEditor&) { p.setLearnPad (6); } });
        snap->startTimerHz (60);
    }
};

juce::JUCEApplicationBase* createSnapshotApp() { return new SnapshotApp(); }
} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--video"))
        return renderVideo (juce::File (args[args.indexOf ("--video") + 1]), 367);
    if (args.contains ("--snapshot"))
    {
        snapshotDir = juce::File (args[args.indexOf ("--snapshot") + 1]);
        juce::JUCEApplicationBase::createInstance = &createSnapshotApp;
        return juce::JUCEApplicationBase::main (argc, (const char**) argv);
    }
    return runTests();
}
