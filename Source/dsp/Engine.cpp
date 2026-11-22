#include "Engine.h"

namespace stsample
{

namespace
{
    inline float dbToGain (float db) noexcept { return db <= -59.95f ? 0.0f : std::pow (10.0f, db * 0.05f); }

    /** 4-point, 3rd-order Hermite. `d` may be read at [i-1, i+2] (SampleData pads both ends). */
    inline float hermite (const float* d, double pos) noexcept
    {
        const int i = (int) pos;
        const float f = (float) (pos - (double) i);
        const float xm1 = d[i - 1], x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    constexpr float stealSeconds = 0.004f, chokeSeconds = 0.012f, gateSeconds = 0.045f;
}

Engine::Engine()
{
    for (auto& p : playheads) p.store (-1.0f);
}

void Engine::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    reset();
}

void Engine::reset() noexcept
{
    for (auto& v : voices)
    {
        v.active = false;
        for (auto& l : v.layers) { l.active = false; l.data = nullptr; l.hold.reset(); }
    }
    notesDown[0] = 0; notesDown[1] = 0;
    activeVoices = 0;
    for (auto& p : playheads) p.store (-1.0f, std::memory_order_relaxed);
}

SamplePtr Engine::setSample (int pad, int layer, SamplePtr sample)
{
    jassert (juce::isPositiveAndBelow (pad, kNumPads) && juce::isPositiveAndBelow (layer, kNumLayers));
    const bool loaded = sample != nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        std::swap (samples[(size_t) pad][(size_t) layer], sample);
    }
    auto mask = loadedMask[(size_t) pad].load();
    mask = loaded ? (mask | (1u << layer)) : (mask & ~(1u << layer));
    loadedMask[(size_t) pad].store (mask);
    return sample;   // the previous one
}

SamplePtr Engine::getSample (int pad, int layer) const
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return samples[(size_t) pad][(size_t) layer];
}

bool Engine::isNoteDown (int note) const noexcept
{
    if (! juce::isPositiveAndBelow (note, 128)) return false;
    return ((notesDown[(size_t) (note >> 6)].load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0;
}

void Engine::setNoteDown (int note, bool down) noexcept
{
    if (! juce::isPositiveAndBelow (note, 128)) return;
    auto& word = notesDown[(size_t) (note >> 6)];
    const auto bit = (juce::uint64) 1 << (note & 63);
    word.store (down ? (word.load() | bit) : (word.load() & ~bit), std::memory_order_relaxed);
}

//==============================================================================
void Engine::noteOn (int note, float velocity) noexcept
{
    setNoteDown (note, true);
    for (int p = 0; p < kNumPads; ++p)
        if (settings.pads[(size_t) p].note == note)
            startVoice (p, velocity, 0xfu, false, note);
}

void Engine::noteOff (int note) noexcept
{
    setNoteDown (note, false);
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.note == note && settings.pads[(size_t) v.pad].gate)
            release (v, gateSeconds);
}

void Engine::triggerPad (int pad, float velocity, unsigned layerMask, bool ignoreBlend) noexcept
{
    if (juce::isPositiveAndBelow (pad, kNumPads))
        startVoice (pad, velocity, layerMask, ignoreBlend, -1);
}

void Engine::releaseAll (bool fast) noexcept
{
    for (auto& v : voices)
        if (v.active) release (v, fast ? stealSeconds : gateSeconds);
    notesDown[0] = 0; notesDown[1] = 0;
}

void Engine::release (Voice& v, float seconds) noexcept
{
    const float step = 1.0f / juce::jmax (1.0f, seconds * (float) sampleRate);
    if (v.releasing && v.envStep >= step) return;   // already fading at least as fast
    v.releasing = true;
    v.envStep = juce::jmax (v.envStep, step);
}

Engine::Voice* Engine::allocateVoice() noexcept
{
    // Polyphony: the oldest sounding voice fades out quickly to make room.
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.releasing)
        {
            ++sounding;
            if (oldest == nullptr || v.order < oldest->order) oldest = &v;
        }
    if (sounding >= juce::jlimit (1, kMaxVoices, settings.polyphony) && oldest != nullptr)
        release (*oldest, stealSeconds);

    for (auto& v : voices)
        if (! v.active) return &v;

    // Every voice busy (all fading): take the quietest.
    Voice* quietest = &voices[0];
    for (auto& v : voices)
        if (v.env < quietest->env) quietest = &v;
    return quietest;
}

void Engine::startVoice (int pad, float velocity, unsigned layerMask, bool ignoreBlend, int note) noexcept
{
    const auto& ps = settings.pads[(size_t) pad];

    // Choke group: every voice in the same group (this pad included) fades out.
    if (ps.choke > 0)
        for (auto& v : voices)
            if (v.active && settings.pads[(size_t) v.pad].choke == ps.choke)
                release (v, chokeSeconds);

    // Grab the samples (try-lock: the message thread only holds the lock for a pointer swap).
    std::array<SamplePtr, kNumLayers> picked;
    {
        juce::SpinLock::ScopedTryLockType tl (sampleLock);
        for (int attempt = 0; ! tl.isLocked() && attempt < 64; ++attempt)
            tl.retryLock();
        if (! tl.isLocked()) return;
        for (int l = 0; l < kNumLayers; ++l)
            if ((layerMask >> l) & 1u)
                picked[(size_t) l] = samples[(size_t) pad][(size_t) l];
    }

    unsigned mask = 0;
    for (int l = 0; l < kNumLayers; ++l)
        if (picked[(size_t) l] != nullptr) mask |= 1u << l;

    hits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
    lastVelocity[(size_t) pad].store (velocity, std::memory_order_relaxed);
    if (mask == 0) return;

    auto* v = allocateVoice();
    v->active = true;
    v->releasing = false;
    v->ignoreBlend = ignoreBlend;
    v->pad = pad;
    v->note = note;
    v->mask = mask;
    v->env = 1.0f;
    v->envStep = 0.0f;
    v->velGain = velocityGain (velocity, ps.velFloor);
    v->order = ++voiceCounter;

    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& lv = v->layers[(size_t) l];
        lv.hold = std::move (picked[(size_t) l]);
        lv.data = lv.hold.get();
        lv.active = lv.data != nullptr;
        lv.gainL = lv.gainR = 0.0f;
        if (! lv.active) continue;

        const auto& ls = ps.layers[(size_t) l];
        const auto& d = *lv.data;
        const double frames = (double) d.numFrames;
        lv.startPos = juce::jlimit (0.0, juce::jmax (0.0, frames - 1.0), (double) ls.start * frames);
        lv.endPos = juce::jlimit (lv.startPos + 1.0, frames, lv.startPos + (frames - lv.startPos) * (double) juce::jlimit (0.0f, 1.0f, ls.length));
        lv.pos = lv.startPos;
        lv.inc = d.sampleRate / sampleRate * std::exp2 ((double) ls.pitch / 12.0);
        // short fades where the sample is cut (start offset, shortened length) so cut points never click
        lv.fadeIn = lv.startPos > 0.0 ? juce::jmin (0.0015 * d.sampleRate, (lv.endPos - lv.startPos) * 0.25) : 0.0;
        const double region = lv.endPos - lv.startPos;
        lv.fadeOut = lv.endPos < frames - 0.5 ? juce::jlimit (0.002 * d.sampleRate, juce::jmax (0.002 * d.sampleRate, 0.06 * d.sampleRate), region * 0.2) : 0.0;
        lv.fadeOut = juce::jmin (lv.fadeOut, region * 0.5);
    }

    // first chunk starts at the target gains (no fade-in on the attack)
    std::array<float, kNumLayers> gl {}, gr {};
    targetGains (*v, gl, gr);
    for (int l = 0; l < kNumLayers; ++l)
    {
        v->layers[(size_t) l].gainL = gl[(size_t) l];
        v->layers[(size_t) l].gainR = gr[(size_t) l];
    }
}

void Engine::targetGains (const Voice& v, std::array<float, kNumLayers>& gl, std::array<float, kNumLayers>& gr) const noexcept
{
    const auto& ps = settings.pads[(size_t) v.pad];
    const float padGain = ps.audible ? dbToGain (ps.levelDb) * v.velGain * outGain : 0.0f;
    const float pan = juce::jlimit (-1.0f, 1.0f, ps.pan);
    const float panL = juce::jmin (1.0f, 1.0f - pan), panR = juce::jmin (1.0f, 1.0f + pan);
    const auto weights = blendWeights (ps.x, ps.y, v.mask);

    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto& ls = ps.layers[(size_t) l];
        const float w = v.ignoreBlend ? 1.0f : weights[(size_t) l];
        const float g = padGain * w * dbToGain (ls.gainDb) * (ls.invert ? -1.0f : 1.0f);
        gl[(size_t) l] = g * panL;
        gr[(size_t) l] = g * panR;
    }
}

void Engine::render (float* left, float* right, int n) noexcept
{
    outGain = dbToGain (settings.outputDb);
    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, left + start, right + start, len);
    }

    int active = 0;
    for (auto& v : voices) active += v.active ? 1 : 0;
    activeVoices.store (active, std::memory_order_relaxed);
}

void Engine::renderVoice (Voice& v, float* left, float* right, int n) noexcept
{
    std::array<float, kNumLayers> tl {}, tr {};
    targetGains (v, tl, tr);
    const float invN = 1.0f / (float) n;

    // envelope for this chunk (release fades); computed per sample below
    bool anyActive = false;
    float envEnd = v.env;
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& lv = v.layers[(size_t) l];
        if (! lv.active) continue;

        const auto& d = *lv.data;
        const float* c0 = d.channel (0);
        const float* c1 = d.channel (1);
        const bool stereo = d.getNumChannels() > 1;
        const float dl = (tl[(size_t) l] - lv.gainL) * invN, dr = (tr[(size_t) l] - lv.gainR) * invN;
        float gL = lv.gainL, gR = lv.gainR;
        float env = v.env;
        double pos = lv.pos;
        const double endPos = lv.endPos;

        int i = 0;
        for (; i < n; ++i)
        {
            if (pos >= endPos) break;
            gL += dl; gR += dr;
            if (v.releasing) env = juce::jmax (0.0f, env - v.envStep);

            float shape = env;
            if (lv.fadeOut > 0.0 && endPos - pos < lv.fadeOut) shape *= (float) ((endPos - pos) / lv.fadeOut);
            if (lv.fadeIn > 0.0 && pos - lv.startPos < lv.fadeIn) shape *= (float) ((pos - lv.startPos) / lv.fadeIn);

            const float s0 = hermite (c0, pos);
            const float s1 = stereo ? hermite (c1, pos) : s0;
            left[i] += s0 * gL * shape;
            right[i] += s1 * gR * shape;
            pos += lv.inc;
        }

        lv.pos = pos;
        lv.gainL = tl[(size_t) l];
        lv.gainR = tr[(size_t) l];
        if (i < n || pos >= endPos)
        {
            lv.active = false;
            lv.data = nullptr;
            lv.hold.reset();   // never the last reference: the engine slot or the Kit's graveyard still owns it
        }
        else anyActive = true;
        envEnd = env;
    }

    if (v.releasing)
    {
        // advance the envelope even if every layer ended inside this chunk
        v.env = juce::jmax (0.0f, v.env - v.envStep * (float) n);
        juce::ignoreUnused (envEnd);
        if (v.env <= 0.0f) anyActive = false;
    }

    if (! anyActive)
    {
        v.active = false;
        for (auto& lv : v.layers) { lv.active = false; lv.data = nullptr; lv.hold.reset(); }
    }
}

void Engine::publishPlayheads() noexcept
{
    std::array<const Voice*, kNumPads> newest {};
    for (auto& v : voices)
        if (v.active && (newest[(size_t) v.pad] == nullptr || v.order > newest[(size_t) v.pad]->order))
            newest[(size_t) v.pad] = &v;

    for (int p = 0; p < kNumPads; ++p)
        for (int l = 0; l < kNumLayers; ++l)
        {
            float pos = -1.0f;
            if (auto* v = newest[(size_t) p])
            {
                const auto& lv = v->layers[(size_t) l];
                if (lv.active && lv.data != nullptr && lv.data->numFrames > 0)
                    pos = (float) (lv.pos / (double) lv.data->numFrames);
            }
            playheads[(size_t) (p * kNumLayers + l)].store (pos, std::memory_order_relaxed);
        }
}

} // namespace stsample
