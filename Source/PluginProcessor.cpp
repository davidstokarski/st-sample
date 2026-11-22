#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryKits.h"
#include "ui/Style.h"

using namespace stsample;

StSampleProcessor::StSampleProcessor()
    : ProcessorBase (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                     createParameterLayout()),
      kit (apvts, undoManager, engine)
{
    stsample::style::applyTheme();   // ST family look, ST Sample colours (ui/Style.cpp)

    outGain.bind (apvts, ids::outputGain);
    polyphony.bind (apvts, ids::polyphony);
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& r = padRefs[(size_t) p];
        r.note.bind (apvts, ids::pad (p, "note"));
        r.level.bind (apvts, ids::pad (p, "level"));
        r.pan.bind (apvts, ids::pad (p, "pan"));
        r.vel.bind (apvts, ids::pad (p, "vel"));
        r.x.bind (apvts, ids::pad (p, "x"));
        r.y.bind (apvts, ids::pad (p, "y"));
        r.mode.bind (apvts, ids::pad (p, "mode"));
        r.choke.bind (apvts, ids::pad (p, "choke"));
        r.mute.bind (apvts, ids::pad (p, "mute"));
        r.solo.bind (apvts, ids::pad (p, "solo"));
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& lr = r.layers[(size_t) l];
            lr.gain.bind (apvts, ids::layer (p, l, "gain"));
            lr.pitch.bind (apvts, ids::layer (p, l, "pitch"));
            lr.start.bind (apvts, ids::layer (p, l, "start"));
            lr.length.bind (apvts, ids::layer (p, l, "length"));
            lr.invert.bind (apvts, ids::layer (p, l, "invert"));
        }
    }

    registerFactoryKits (presets);
    presets.loadPreset (1);   // a new instance starts with the ST Kit (Init is the empty kit)
}

StSampleProcessor::~StSampleProcessor()
{
    unregisterInstance();   // first: views of this instance (elsewhere) still use its members
}

bool StSampleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet().isDisabled();
}

//==============================================================================
void StSampleProcessor::prepareToPlay (double sampleRate, int)
{
    prepareShell (sampleRate);
    engine.prepare (sampleRate);
    updateSettings();
    prepared = true;
}

void StSampleProcessor::updateSettings() noexcept
{
    settings.outputDb = outGain.get();
    settings.polyphony = kPolyphonyChoices[juce::jlimit (0, 3, polyphony.getInt())];

    bool anySolo = false;
    for (auto& r : padRefs) anySolo |= r.solo.getBool();

    for (int p = 0; p < kNumPads; ++p)
    {
        const auto& r = padRefs[(size_t) p];
        auto& ps = settings.pads[(size_t) p];
        ps.note = r.note.getInt();
        ps.levelDb = r.level.get();
        ps.pan = r.pan.get() * 0.01f;
        ps.velFloor = r.vel.get() * 0.01f;
        ps.x = r.x.get();
        ps.y = r.y.get();
        ps.gate = r.mode.getInt() == 1;
        ps.choke = r.choke.getInt();
        ps.audible = ! (r.mute.getBool() || (anySolo && ! r.solo.getBool()));
        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto& lr = r.layers[(size_t) l];
            auto& ls = ps.layers[(size_t) l];
            ls.gainDb = lr.gain.get();
            ls.pitch = lr.pitch.get();
            ls.start = lr.start.get() * 0.01f;
            ls.length = lr.length.get() * 0.01f;
            ls.invert = lr.invert.getBool();
        }
    }
    engine.setSettings (settings);
}

void StSampleProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        lastNote.store (note, std::memory_order_relaxed);
        lastVelocityValue.store (m.getVelocity(), std::memory_order_relaxed);
        midiCounter.fetch_add (1, std::memory_order_relaxed);
        if (learnPad.load (std::memory_order_relaxed) >= 0)
        {
            learnedNote.store (note);
            return;
        }
        engine.noteOn (note, m.getFloatVelocity());
    }
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.releaseAll (m.isAllSoundOff());
}

void StSampleProcessor::renderRange (juce::AudioBuffer<float>& buffer, int from, int to) noexcept
{
    if (to <= from) return;
    if (buffer.getNumChannels() >= 2)
    {
        engine.render (buffer.getWritePointer (0, from), buffer.getWritePointer (1, from), to - from);
        return;
    }
    // mono output: render stereo into scratch, fold down
    auto* out = buffer.getWritePointer (0);
    for (int pos = from; pos < to;)
    {
        const int n = juce::jmin ((int) monoL.size(), to - pos);
        std::fill (monoL.begin(), monoL.begin() + n, 0.0f);
        std::fill (monoR.begin(), monoR.begin() + n, 0.0f);
        engine.render (monoL.data(), monoR.data(), n);
        for (int i = 0; i < n; ++i) out[pos + i] += 0.5f * (monoL[(size_t) i] + monoR[(size_t) i]);
        pos += n;
    }
}

void StSampleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (! prepared || buffer.getNumChannels() == 0) { midi.clear(); return; }

    updateSettings();

    // auditions from the editor
    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        scope.forEach ([this] (int i)
        {
            const auto& e = uiEvents[(size_t) i];
            if (e.kind == 0)      engine.triggerPad (e.pad, e.velocity, e.layer < 0 ? 0xfu : (1u << e.layer), e.layer >= 0);
            else if (e.kind == 1) engine.noteOn (e.note, e.velocity);
            else                  engine.noteOff (e.note);
        });
    }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, total, meta.samplePosition);
        renderRange (buffer, pos, t);
        pos = t;
        handleMidi (meta.getMessage());
    }
    renderRange (buffer, pos, total);
    midi.clear();

    engine.publishPlayheads();
    outputMeter.push (buffer, total);
}

//==============================================================================
void StSampleProcessor::pushUiEvent (const UiEvent& e)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0) uiEvents[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) uiEvents[(size_t) scope.startIndex2] = e;
}

void StSampleProcessor::auditionPad (int pad, float velocity, int layer)
{
    pushUiEvent ({ 0, pad, layer, 0, velocity });
}

void StSampleProcessor::auditionNote (int note, float velocity, bool on)
{
    pushUiEvent ({ on ? 1 : 2, 0, -1, note, velocity });
}

void StSampleProcessor::setPlain (juce::RangedAudioParameter& p, float plain, bool asGesture)
{
    if (asGesture) p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
    if (asGesture) p.endChangeGesture();
}

void StSampleProcessor::setPadNote (int pad, int note)
{
    if (! juce::isPositiveAndBelow (pad, kNumPads) || ! juce::isPositiveAndBelow (note, 128)) return;
    undoManager.beginNewTransaction ("Map pad");
    setPlain (padParam (pad, "note"), (float) note);
}

int StSampleProcessor::findPadForNote (int note) const noexcept
{
    for (int p = 0; p < kNumPads; ++p)
        if (getPadNote (p) == note) return p;
    return -1;
}

juce::AudioProcessorEditor* StSampleProcessor::createEditor()
{
    return new StSampleEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StSampleProcessor();
}
