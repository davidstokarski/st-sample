#include "PluginEditor.h"
#include "ui/Style.h"

using namespace stsample;

namespace
{
    const juce::Rectangle<int> padListBounds { 12, 50, 222, 530 };
    const juce::Rectangle<int> padViewBounds { 246, 52, 782, 528 };
    const juce::Rectangle<int> keyboardBounds { 8, 590, 1024, 50 };
}

StSampleEditor::StSampleEditor (StSampleProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      padList (*this),
      keyboard (*this),
      footer (p)
{
    notes::middleCOctave() = juce::jlimit (3, 5, (int) proc.uiState.getProperty ("middleC", notes::middleCOctave()));
    header.setProductName ("Sample");

    auto& content = getContent();
    content.addAndMakeVisible (padList);
    content.addAndMakeVisible (keyboard);
    content.addAndMakeVisible (footer);
    footer.setBufferedToImage (true);
    header.setBufferedToImage (true);
    footer.onNoteNamesChanged = [this]
    {
        padList.repaint();
        keyboard.repaint();
        if (padView != nullptr) padView->repaint();
    };

    proc.getKit().addChangeListener (this);
    clock = std::make_unique<st::FrameClock> (content, [this] (double dt) { animate (dt); });

    viewFade.snap (1.0f);
    selectPad (juce::jlimit (0, kNumPads - 1, (int) proc.uiState.getProperty ("selectedPad", 0)));

    header.toFront (false);
    relayout();
}

StSampleEditor::~StSampleEditor()
{
    proc.getKit().removeChangeListener (this);
    clock.reset();
}

//==============================================================================
void StSampleEditor::selectPad (int pad)
{
    pad = juce::jlimit (0, kNumPads - 1, pad);
    const bool changed = padView == nullptr || padView->getPad() != pad;
    selectedPad = pad;
    proc.uiState.setProperty ("selectedPad", pad, nullptr);
    padList.setSelected (pad);
    keyboard.showNote (proc.getPadNote (pad));
    if (! changed) return;

    if (proc.getLearnPad() >= 0 && proc.getLearnPad() != pad) proc.setLearnPad (-1);

    // the pad surface is rebuilt and cross-fades (ST Sat's band panel)
    const bool animateChange = padView != nullptr;
    if (animateChange)
    {
        outgoingView = std::move (padView);
        outgoingView->setInterceptsMouseClicks (false, false);
        outgoingFade.snap (1.0f);
    }
    padView = std::make_unique<PadView> (*this, pad);
    getContent().addAndMakeVisible (*padView);
    padView->setBounds (padViewBounds);
    header.toFront (false);
    viewFade.snap (animateChange ? 0.0f : 1.0f);
    animate (0.0);
}

void StSampleEditor::animate (double dt)
{
    padList.tick (dt);
    keyboard.tick (dt);

    // MIDI learn: the audio thread reports the note, we map it here
    if (const int lp = proc.getLearnPad(); lp >= 0)
        if (const int n = proc.takeLearnedNote(); n >= 0)
        {
            proc.setPadNote (lp, n);
            proc.setLearnPad (-1);
            keyboard.showNote (n);
        }

    if (padView != nullptr)
    {
        padView->tick (dt);
        viewFade.setTarget (1.0f);
        viewFade.tick (dt, 16.0f);
        padView->setAlpha (viewFade.current);
        padView->setTopLeftPosition (padViewBounds.getX(), padViewBounds.getY() + juce::roundToInt ((1.0f - viewFade.current) * 6.0f));
    }
    if (outgoingView != nullptr)
    {
        outgoingFade.setTarget (0.0f);
        if (! outgoingFade.tick (dt, 20.0f) || outgoingFade.current < 0.02f) outgoingView.reset();
        else outgoingView->setAlpha (outgoingFade.current);
    }
}

void StSampleEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // a sample stepped to with ‹ › plays once it is decoded
    auto& pa = pendingAudition;
    if (pa.pad < 0) return;
    const auto& l = proc.getKit().getLayer (pa.pad, pa.layer);
    if (l.source != pa.source) { pa = {}; return; }
    if (l.status == Kit::Status::ready)
    {
        proc.auditionPad (pa.pad, 0.85f, pa.layer);
        pa = {};
    }
    else if (l.status == Kit::Status::missing) pa = {};
}

//==============================================================================
juce::File StSampleEditor::lastFolder() const
{
    const juce::File f (proc.uiState.getProperty ("lastFolder", "").toString());
    return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

juce::ValueTree& StSampleEditor::padClipboard()
{
    static juce::ValueTree clip;
    return clip;
}

void StSampleEditor::setLayerSourceAndAudition (int pad, int layer, const juce::String& source)
{
    proc.getKit().setLayerSource (pad, layer, source);
    pendingAudition = { pad, layer, source };
}

void StSampleEditor::browseSamples (int pad, int layer)
{
    const bool multi = layer < 0;
    chooser = std::make_unique<juce::FileChooser> (multi ? "Load samples into " + proc.getKit().getPadName (pad)
                                                         : "Load sample into layer " + layerLetter (layer),
                                                   lastFolder(), sources::wildcard());
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (multi) flags |= juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (flags, [safe = juce::Component::SafePointer<StSampleEditor> (this), pad, layer] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        juce::StringArray files;
        for (auto& f : fc.getResults()) files.add (f.getFullPathName());
        if (! files.isEmpty()) safe->dropFiles (pad, layer, files);
    });
}

void StSampleEditor::dropFiles (int pad, int layer, const juce::StringArray& files)
{
    juce::StringArray audio;
    for (auto& f : files)
        if (sources::isAudioFile (juce::File (f))) audio.add (f);
    if (audio.isEmpty()) return;

    proc.uiState.setProperty ("lastFolder", juce::File (audio[0]).getParentDirectory().getFullPathName(), nullptr);
    auto& kit = proc.getKit();
    if (layer < 0)
    {
        layer = kit.firstEmptyLayer (pad);
        if (layer < 0) layer = 0;   // full pad: replace A
    }
    if (audio.size() == 1) setLayerSourceAndAudition (pad, layer, audio[0]);
    else kit.loadFiles (pad, layer, audio);
    selectPad (pad);
}

void StSampleEditor::stepSample (int pad, int layer, int delta)
{
    const auto src = proc.getKit().getLayer (pad, layer).source;
    if (src.isEmpty()) return;
    if (sources::isFactory (src))
    {
        const auto& names = factory::names();
        const int i = names.indexOf (sources::displayName (src));
        const int n = names.size();
        setLayerSourceAndAudition (pad, layer, sources::factory (names[((i + delta) % n + n) % n]));
        return;
    }
    const auto files = sources::siblings (juce::File (src));
    if (files.isEmpty()) return;
    const int i = files.indexOf (juce::File (src));
    const int n = files.size();
    setLayerSourceAndAudition (pad, layer, files[((juce::jmax (0, i) + delta) % n + n) % n].getFullPathName());
}

void StSampleEditor::showSampleMenu (int pad, int layer, juce::Component& target)
{
    const auto& l = proc.getKit().getLayer (pad, layer);
    juce::PopupMenu m;
    m.addSectionHeader ("Layer " + layerLetter (layer));
    m.addItem ("Load sample...", [this, pad, layer] { browseSamples (pad, layer); });

    if (l.source.isNotEmpty() && ! sources::isFactory (l.source))
    {
        const juce::File file (l.source);
        const auto files = sources::siblings (file);
        if (files.size() > 1)
        {
            juce::PopupMenu folder;
            for (int i = 0; i < juce::jmin (200, files.size()); ++i)
            {
                const auto f = files[i];
                folder.addItem (f.getFileNameWithoutExtension(), true, f == file,
                                [this, pad, layer, f] { setLayerSourceAndAudition (pad, layer, f.getFullPathName()); });
            }
            m.addSubMenu (file.getParentDirectory().getFileName(), folder);
        }
    }

    juce::PopupMenu builtIn;
    for (auto& n : factory::names())
        builtIn.addItem (n, true, l.source == sources::factory (n),
                         [this, pad, layer, n] { setLayerSourceAndAudition (pad, layer, sources::factory (n)); });
    m.addSubMenu ("Built-in sounds", builtIn);

    if (l.source.isNotEmpty())
    {
        m.addSeparator();
        m.addItem ("Play layer", [this, pad, layer] { proc.auditionPad (pad, 0.85f, layer); });
        if (! sources::isFactory (l.source))
            m.addItem ("Show in Finder", juce::File (l.source).existsAsFile(), false, [src = l.source] { juce::File (src).revealToUser(); });
        m.addItem ("Clear layer", [this, pad, layer] { proc.getKit().clearLayer (pad, layer); });
    }
    st::showMenu (m, &target, nullptr, ! dynamic_cast<SampleSelector*> (&target));
}

void StSampleEditor::showPadMenu (int pad, juce::Component& target)
{
    auto& kit = proc.getKit();
    juce::PopupMenu m;
    m.addSectionHeader (kit.getPadName (pad) + "  " + notes::name (proc.getPadNote (pad)));
    m.addItem ("Play", [this, pad] { proc.auditionPad (pad); });
    m.addItem ("Rename...", [this, pad] { renamePad (pad); });
    m.addItem ("Load samples...", [this, pad] { browseSamples (pad, -1); });
    m.addItem ("Learn MIDI note", [this, pad] { selectPad (pad); proc.setLearnPad (pad); });
    m.addSeparator();
    m.addItem ("Copy pad", [this, pad] { padClipboard() = proc.getKit().copyPad (pad); });
    m.addItem ("Paste pad", padClipboard().isValid(), false, [this, pad] { proc.getKit().pastePad (pad, padClipboard()); });
    m.addItem ("Clear pad", ! kit.isPadEmpty (pad), false, [this, pad] { proc.getKit().clearPad (pad); });
    st::showMenu (m, &target, nullptr, true);
}

void StSampleEditor::renamePad (int pad)
{
    auto& kit = proc.getKit();
    st::TextPrompt::show (getContent(), "Rename pad", kit.getPadName (pad), "Rename",
                          [this, pad] (const juce::String& name) { proc.getKit().setPadName (pad, name); });
}

bool StSampleEditor::isInterestedInFileDrag (const juce::StringArray& files) { return hasAudioFiles (files); }

void StSampleEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropFiles (selectedPad, files.size() > 1 ? 0 : -1, files);
}

//==============================================================================
void StSampleEditor::paintBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    // static: render once per physical scale, blit afterwards
    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto full = getContent().getLocalBounds();
    if (backdrop.isNull() || std::abs (pixelScale - backdropScale) > 0.01f)
    {
        backdropScale = pixelScale;
        backdrop = juce::Image (juce::Image::RGB, juce::roundToInt ((float) full.getWidth() * pixelScale),
                                juce::roundToInt ((float) full.getHeight() * pixelScale), false);
        juce::Graphics bg (backdrop);
        bg.addTransform (juce::AffineTransform::scale (pixelScale));
        style::paintBackdrop (bg, full.toFloat());
        // a faint divider between the pad list and the pad surface
        st::paint::fadedLine (bg, { (float) padListBounds.getRight() + 5.5f, (float) padListBounds.getY() + 10.0f },
                              { (float) padListBounds.getRight() + 5.5f, (float) padListBounds.getBottom() - 6.0f },
                              juce::Colours::white.withAlpha (0.07f), 1.0f, 0.25f);
    }
    g.drawImage (backdrop, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void StSampleEditor::layout (juce::Rectangle<int>)
{
    auto all = getContent().getLocalBounds();
    footer.setBounds (all.removeFromBottom (footerHeight));
    padList.setBounds (padListBounds);
    keyboard.setBounds (keyboardBounds);
    if (padView != nullptr) padView->setBounds (padViewBounds);
    animate (0.0);
}
