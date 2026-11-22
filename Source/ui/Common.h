#pragma once
#include "Style.h"
#include "../PluginProcessor.h"

namespace stsample
{

/** What the editor offers its parts (selection, sample browsing, menus), so components don't depend on the editor. */
struct Services
{
    virtual ~Services() = default;
    virtual StSampleProcessor& getProcessor() = 0;
    virtual int getSelectedPad() const = 0;
    virtual void selectPad (int pad) = 0;
    /** File chooser for a layer (layer -1: several files into the pad, from its first free layer). */
    virtual void browseSamples (int pad, int layer) = 0;
    /** Native sample menu for a layer: load, files in the same folder, built-in sounds, clear. */
    virtual void showSampleMenu (int pad, int layer, juce::Component& target) = 0;
    /** Previous / next file in the sample's folder (or built-in sound), auditioned. */
    virtual void stepSample (int pad, int layer, int delta) = 0;
    /** Dropped files: into a layer (and the following ones), or layer -1 = the pad's first free layer. */
    virtual void dropFiles (int pad, int layer, const juce::StringArray& files) = 0;
    virtual void showPadMenu (int pad, juce::Component& target) = 0;
    virtual void renamePad (int pad) = 0;
};

/** True when a drag carries at least one audio file. */
inline bool hasAudioFiles (const juce::StringArray& files)
{
    for (auto& f : files)
        if (sources::isAudioFile (juce::File (f))) return true;
    return false;
}

/** Drag description for a pad dragged from the pad list ("stpad:<index>"). */
inline juce::String padDragDescription (int pad) { return "stpad:" + juce::String (pad); }
inline int padFromDragDescription (const juce::var& d)
{
    const auto s = d.toString();
    return s.startsWith ("stpad:") ? s.fromFirstOccurrenceOf (":", false, false).getIntValue() : -1;
}

//==============================================================================
/** Grey hardware button with an icon or short text (sits on a glass bar's top edge). */
class GreyButton : public juce::Button
{
public:
    GreyButton (const juce::String& text, const juce::String& tooltip, std::optional<st::icons::Icon> icon = {})
        : juce::Button (text), glyph (icon)
    {
        setTooltip (tooltip);
    }

    void setOnColour (juce::Colour c) { onColour = c; repaint(); }
    void setLit (bool b) { if (lit != b) { lit = b; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = st::Theme::get();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        st::paint::greyButton (g, r, t.radius - 1.0f, over && isEnabled(), down);
        const auto fg = lit ? onColour.brighter (0.3f) : (over ? t.text : t.text.withAlpha (isEnabled() ? 0.8f : 0.35f));
        if (glyph.has_value())
            st::icons::draw (g, *glyph, r.withTrimmedBottom (2.0f).reduced (r.getHeight() * 0.24f), fg, 1.7f);
        else
        {
            g.setColour (fg);
            g.setFont (st::fonts::semibold (juce::jlimit (10.0f, 13.0f, r.getHeight() * 0.5f)));
            g.drawText (getButtonText(), r.withTrimmedBottom (2.0f), juce::Justification::centred, false);
        }
        if (lit)
        {
            g.setColour (onColour.withAlpha (0.9f));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - 4.5f).withHeight (2.0f).reduced (5.0f, 0.0f), 1.0f);
        }
    }

private:
    std::optional<st::icons::Icon> glyph;
    juce::Colour onColour { st::Theme::get().accent };
    bool lit = false;
};

} // namespace stsample
