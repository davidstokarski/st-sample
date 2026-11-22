#include "Kit.h"

namespace stsample
{

const juce::Identifier Kit::kitType ("KIT"), Kit::padType ("PAD"), Kit::layerType ("LAYER"),
                       Kit::indexId ("i"), Kit::nameId ("name"), Kit::sourceId ("source");

//==============================================================================
/** Decodes on its own thread. Identical sources share one decoded sample (cache of weak references). */
class Kit::Loader : public juce::Thread
{
public:
    explicit Loader (Kit& k) : juce::Thread ("ST Sample loader"), kit (k) { startThread (juce::Thread::Priority::normal); }
    ~Loader() override
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (4000);
    }

    void add (Job j)
    {
        {
            const juce::ScopedLock sl (lock);
            // a newer request for the same layer replaces a queued one
            jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [&] (const Job& o) { return o.pad == j.pad && o.layer == j.layer; }), jobs.end());
            jobs.push_back (std::move (j));
        }
        recount();
        wake.signal();
    }

    std::vector<Result> takeResults()
    {
        const juce::ScopedLock sl (lock);
        auto r = std::move (results);
        results.clear();
        return r;
    }

    bool hasResults() const
    {
        const juce::ScopedLock sl (lock);
        return ! results.empty();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::optional<Job> job;
            {
                const juce::ScopedLock sl (lock);
                if (! jobs.empty()) { job = std::move (jobs.front()); jobs.erase (jobs.begin()); busy = true; }
            }
            if (! job) { wake.wait (500); continue; }

            Result r { job->pad, job->layer, job->gen, job->source, {}, nullptr };
            r.data = fromCache (job->source);
            if (r.data == nullptr)
            {
                r.data = loadSample (job->source, r.error);
                if (r.data != nullptr)
                {
                    const juce::ScopedLock sl (lock);
                    cache[job->source] = r.data;
                }
            }
            {
                const juce::ScopedLock sl (lock);
                results.push_back (std::move (r));
                busy = false;
            }
            recount();
            kit.triggerAsyncUpdate();
        }
    }

private:
    Kit& kit;
    juce::CriticalSection lock;
    juce::WaitableEvent wake;
    std::vector<Job> jobs;
    std::vector<Result> results;
    std::map<juce::String, std::weak_ptr<const SampleData>> cache;
    bool busy = false;

    void recount()
    {
        const juce::ScopedLock sl (lock);
        kit.pending.store ((int) jobs.size() + (busy ? 1 : 0));
    }

    SamplePtr fromCache (const juce::String& source)
    {
        SamplePtr s;
        {
            const juce::ScopedLock sl (lock);
            for (auto it = cache.begin(); it != cache.end();)
                it = it->second.expired() ? cache.erase (it) : std::next (it);
            if (auto it = cache.find (source); it != cache.end()) s = it->second.lock();
        }
        // a file edited since it was decoded is decoded again
        if (s != nullptr && ! sources::isFactory (source)
            && juce::File (source).getLastModificationTime().toMilliseconds() != s->fileTime)
            return nullptr;
        return s;
    }
};

//==============================================================================
Kit::Kit (juce::AudioProcessorValueTreeState& s, juce::UndoManager& u, Engine& e)
    : apvts (s), undo (u), engine (e)
{
    loader = std::make_unique<Loader> (*this);
    apvts.state.addListener (this);
    startTimerHz (2);
    triggerAsyncUpdate();
}

Kit::~Kit()
{
    apvts.state.removeListener (this);
    cancelPendingUpdate();
    loader.reset();
}

bool Kit::isKitNode (const juce::ValueTree& t)
{
    return t.hasType (kitType) || t.hasType (padType) || t.hasType (layerType);
}

void Kit::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
{
    if (isKitNode (t)) triggerAsyncUpdate();
}

void Kit::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (isKitNode (parent) || isKitNode (child)) triggerAsyncUpdate();
}

void Kit::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (isKitNode (parent) || isKitNode (child)) triggerAsyncUpdate();
}

//==============================================================================
juce::ValueTree Kit::kitTree (bool create)
{
    auto k = apvts.state.getChildWithName (kitType);
    if (! k.isValid() && create)
    {
        k = juce::ValueTree (kitType);
        apvts.state.appendChild (k, nullptr);
    }
    return k;
}

juce::ValueTree Kit::padTree (int pad, bool create)
{
    auto k = kitTree (create);
    if (! k.isValid()) return {};
    auto p = k.getChildWithProperty (indexId, pad);
    if (! p.isValid() && create)
    {
        p = juce::ValueTree (padType);
        p.setProperty (indexId, pad, nullptr);
        k.appendChild (p, nullptr);
    }
    return p;
}

juce::ValueTree Kit::layerTree (int pad, int layer, bool create)
{
    auto p = padTree (pad, create);
    if (! p.isValid()) return {};
    auto l = p.getChildWithProperty (indexId, layer);
    if (! l.isValid() && create)
    {
        l = juce::ValueTree (layerType);
        l.setProperty (indexId, layer, nullptr);
        p.appendChild (l, nullptr);
    }
    return l;
}

juce::ValueTree Kit::buildPadTree (int pad, const juce::String& name, const juce::StringArray& layerSources)
{
    juce::ValueTree p (padType);
    p.setProperty (indexId, pad, nullptr);
    if (name.isNotEmpty()) p.setProperty (nameId, name, nullptr);
    for (int l = 0; l < juce::jmin (kNumLayers, layerSources.size()); ++l)
    {
        if (layerSources[l].isEmpty()) continue;
        juce::ValueTree lt (layerType);
        lt.setProperty (indexId, l, nullptr);
        lt.setProperty (sourceId, layerSources[l], nullptr);
        p.appendChild (lt, nullptr);
    }
    return p;
}

//==============================================================================
bool Kit::isPadEmpty (int pad) const noexcept
{
    for (auto& l : layers[(size_t) pad])
        if (l.source.isNotEmpty()) return false;
    return true;
}

int Kit::getNumLoadedLayers (int pad) const noexcept
{
    int n = 0;
    for (auto& l : layers[(size_t) pad]) n += l.source.isNotEmpty() ? 1 : 0;
    return n;
}

int Kit::firstEmptyLayer (int pad) const noexcept
{
    for (int l = 0; l < kNumLayers; ++l)
        if (layers[(size_t) pad][(size_t) l].source.isEmpty()) return l;
    return -1;
}

bool Kit::hasCustomName (int pad) const
{
    auto p = const_cast<Kit*> (this)->padTree (pad, false);
    return p.isValid() && p[nameId].toString().isNotEmpty();
}

juce::String Kit::getPadName (int pad) const
{
    auto p = const_cast<Kit*> (this)->padTree (pad, false);
    if (p.isValid() && p[nameId].toString().isNotEmpty()) return p[nameId].toString();
    for (auto& l : layers[(size_t) pad])
        if (l.name.isNotEmpty()) return l.name;
    return "Pad " + juce::String (pad + 1);
}

void Kit::setLayerSource (int pad, int layer, const juce::String& source, bool undoable)
{
    if (! juce::isPositiveAndBelow (pad, kNumPads) || ! juce::isPositiveAndBelow (layer, kNumLayers)) return;
    if (undoable) undo.beginNewTransaction (source.isEmpty() ? "Clear sample" : "Load sample");
    auto l = layerTree (pad, layer, true);
    l.setProperty (sourceId, source, undoable ? &undo : nullptr);
}

void Kit::clearPad (int pad)
{
    undo.beginNewTransaction ("Clear pad");
    for (int l = 0; l < kNumLayers; ++l)
        if (auto t = layerTree (pad, l, false); t.isValid())
            t.setProperty (sourceId, juce::String(), &undo);
    if (auto p = padTree (pad, false); p.isValid())
        p.setProperty (nameId, juce::String(), &undo);
}

void Kit::setPadName (int pad, const juce::String& name, bool undoable)
{
    if (undoable) undo.beginNewTransaction ("Rename pad");
    padTree (pad, true).setProperty (nameId, name.trim(), undoable ? &undo : nullptr);
}

int Kit::loadFiles (int pad, int firstLayer, const juce::StringArray& files)
{
    undo.beginNewTransaction ("Load samples");
    int loaded = 0;
    for (int i = 0, l = juce::jmax (0, firstLayer); i < files.size() && l < kNumLayers; ++i)
    {
        if (! sources::isAudioFile (juce::File (files[i])) && ! sources::isFactory (files[i])) continue;
        layerTree (pad, l, true).setProperty (sourceId, files[i], &undo);
        ++l;
        ++loaded;
    }
    return loaded;
}

juce::ValueTree Kit::copyPad (int pad) const
{
    juce::ValueTree copy ("PADCOPY");
    if (auto p = const_cast<Kit*> (this)->padTree (pad, false); p.isValid())
        copy.appendChild (p.createCopy(), nullptr);

    auto store = [&] (const juce::String& id, const juce::String& key)
    {
        if (auto* prm = apvts.getParameter (id))
            copy.setProperty (key, prm->convertFrom0to1 (prm->getValue()), nullptr);
    };
    for (auto& s : ids::padSuffixes)
        if (s != "note") store (ids::pad (pad, s), s);
    for (int l = 0; l < kNumLayers; ++l)
        for (auto& s : ids::layerSuffixes)
            store (ids::layer (pad, l, s), layerLetter (l) + "_" + s);
    return copy;
}

void Kit::pastePad (int pad, const juce::ValueTree& copy)
{
    if (! copy.hasType ("PADCOPY")) return;
    undo.beginNewTransaction ("Paste pad");

    auto k = kitTree (true);
    if (auto old = padTree (pad, false); old.isValid())
        k.removeChild (old, &undo);
    auto src = copy.getChildWithName (padType);
    auto p = src.isValid() ? src.createCopy() : juce::ValueTree (padType);
    p.setProperty (indexId, pad, nullptr);
    k.appendChild (p, &undo);

    auto apply = [&] (const juce::String& id, const juce::String& key)
    {
        if (! copy.hasProperty (key)) return;
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) copy[key]));
    };
    for (auto& s : ids::padSuffixes)
        if (s != "note") apply (ids::pad (pad, s), s);
    for (int l = 0; l < kNumLayers; ++l)
        for (auto& s : ids::layerSuffixes)
            apply (ids::layer (pad, l, s), layerLetter (l) + "_" + s);
}

//==============================================================================
void Kit::reconcile()
{
    bool changed = false;
    for (int p = 0; p < kNumPads; ++p)
    {
        auto pt = padTree (p, false);
        for (int l = 0; l < kNumLayers; ++l)
        {
            juce::String src;
            if (pt.isValid())
                if (auto lt = pt.getChildWithProperty (indexId, l); lt.isValid())
                    src = lt[sourceId].toString();

            auto& L = layers[(size_t) p][(size_t) l];
            if (src == L.source) continue;

            changed = true;
            L.source = src;
            L.name = sources::displayName (src);
            L.error.clear();
            const auto gen = ++generation[(size_t) p][(size_t) l];
            if (src.isEmpty())
            {
                L.status = Status::empty;
                L.data = nullptr;
                graveyard.push_back (engine.setSample (p, l, nullptr));
            }
            else
            {
                // the previous sample keeps playing until the new one is decoded
                L.status = Status::loading;
                loader->add ({ p, l, gen, src });
            }
        }
    }
    if (changed) sendChangeMessage();
}

void Kit::install (const Result& r)
{
    if (r.gen != generation[(size_t) r.pad][(size_t) r.layer]) return;   // superseded by a newer edit
    auto& L = layers[(size_t) r.pad][(size_t) r.layer];
    L.data = r.data;
    L.error = r.error;
    L.status = r.data != nullptr ? Status::ready : Status::missing;
    if (r.data != nullptr) L.name = r.data->name;
    graveyard.push_back (engine.setSample (r.pad, r.layer, r.data));
}

void Kit::handleAsyncUpdate()
{
    reconcile();
    auto results = loader->takeResults();
    for (auto& r : results) install (r);
    if (! results.empty()) sendChangeMessage();
}

void Kit::timerCallback()
{
    // free replaced samples once no voice plays them (use_count 1 = only the graveyard holds it)
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const SamplePtr& s) { return s == nullptr || s.use_count() <= 1; }),
                     graveyard.end());
}

bool Kit::waitUntilLoaded (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        handleUpdateNowIfNeeded();
        if (pending.load() == 0 && ! loader->hasResults())
        {
            handleAsyncUpdate();   // may queue new decodes (a tree change since the last pass): keep waiting then
            if (pending.load() == 0 && ! loader->hasResults()) return true;
        }
        if (juce::Time::getMillisecondCounter() > until) return false;
        juce::Thread::sleep (2);
    }
}

} // namespace stsample
