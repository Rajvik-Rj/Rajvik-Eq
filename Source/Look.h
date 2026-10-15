#pragma once
#include "Common.h"

// Modern dark "glass" look. Accent colour and background theme are user settings (gear icon in the editor).
namespace Look
{
inline juce::Colour gold  { 0xfff9c23c };   // the accent colour (name kept: used all over the editor)
inline juce::Colour text  { 0xffe9e6f2 };
inline juce::Colour muted { 0xff8f8aa0 };
inline juce::Colour bgTop { 0xff221e2b };
inline juce::Colour bgBot { 0xff121016 };
inline juce::Colour bar   { 0xff0d0c11 };

struct Accent { const char* name; juce::uint32 argb; };
struct Theme  { const char* name; juce::uint32 top, bot, bar; };

constexpr int numAccents = 8, numThemes = 5;
inline const Accent accents[numAccents] =
{
    { "Gold",   0xfff9c23c }, { "Cyan",   0xff39d0ff }, { "Green",  0xff3ddc97 }, { "Orange", 0xffff8a3d },
    { "Red",    0xffff4d5e }, { "Pink",   0xffff6bcb }, { "Purple", 0xffa78bfa }, { "Blue",   0xff4d8dff }
};
inline const Theme themes[numThemes] =
{
    { "Midnight Purple", 0xff221e2b, 0xff121016, 0xff0d0c11 },
    { "Graphite",        0xff2b2d31, 0xff17181b, 0xff101113 },
    { "Deep Blue",       0xff182231, 0xff0c121b, 0xff080d14 },
    { "Forest",          0xff1b2822, 0xff0c1411, 0xff080e0b },
    { "Pure Black",      0xff131313, 0xff000000, 0xff000000 }
};

// Saved to <user app data>/RajvikEQ/settings.xml so it is shared by every instance and survives restarts.
struct Prefs
{
    int accent = 0, theme = 0;
    bool useCustom = false;
    juce::uint32 custom = 0xfff9c23c;
    float scale = 1.0f;          // GUI size: 0.4 .. 2.0
    int width = 1060, height = 600;   // window size in unscaled units

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("RajvikEQ").getChildFile ("settings.xml");
    }

    void load()
    {
        if (auto xml = juce::parseXML (file()))
        {
            accent    = juce::jlimit (0, numAccents - 1, xml->getIntAttribute ("accent", 0));
            theme     = juce::jlimit (0, numThemes - 1, xml->getIntAttribute ("theme", 0));
            useCustom = xml->getBoolAttribute ("useCustom", false);
            custom    = (juce::uint32) xml->getStringAttribute ("custom", "fff9c23c").getHexValue64();
            scale     = juce::jlimit (0.4f, 2.0f, (float) xml->getDoubleAttribute ("scale", 1.0));
            width     = juce::jlimit (640, 3200, xml->getIntAttribute ("width", 1060));
            height    = juce::jlimit (400, 2000, xml->getIntAttribute ("height", 600));
        }
    }

    void save() const
    {
        juce::XmlElement x ("RajvikEQSettings");
        x.setAttribute ("accent", accent);
        x.setAttribute ("theme", theme);
        x.setAttribute ("useCustom", useCustom ? 1 : 0);
        x.setAttribute ("custom", juce::String::toHexString ((juce::int64) custom));
        x.setAttribute ("scale", (double) scale);
        x.setAttribute ("width", width);
        x.setAttribute ("height", height);
        file().getParentDirectory().createDirectory();
        file().replaceWithText (x.toString());
    }

    void apply() const
    {
        gold  = useCustom ? juce::Colour (custom).withAlpha (1.0f) : juce::Colour (accents[accent].argb);
        bgTop = juce::Colour (themes[theme].top);
        bgBot = juce::Colour (themes[theme].bot);
        bar   = juce::Colour (themes[theme].bar);
    }
};
inline Prefs prefs;
}

class RajvikLAF : public juce::LookAndFeel_V4
{
public:
    RajvikLAF() { refreshColours(); }

    void refreshColours()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Look::bgBot);
        setColour (juce::Label::textColourId, Look::muted);
        setColour (juce::Slider::rotarySliderFillColourId, Look::gold);
        setColour (juce::Slider::textBoxTextColourId, Look::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Look::gold.withAlpha (0.3f));
        setColour (juce::ComboBox::textColourId, Look::text);
        setColour (juce::ComboBox::arrowColourId, Look::muted);
        setColour (juce::TextButton::textColourOffId, Look::text);
        setColour (juce::TextButton::textColourOnId, Look::gold);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1924));
        setColour (juce::PopupMenu::textColourId, Look::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Look::gold.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, Look::gold);
        setColour (juce::PopupMenu::headerTextColourId, Look::muted);
    }

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        g.setColour (juce::Colour (0xfa1c1924));
        g.fillPath (path);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (box.isMouseOver (true) ? 0.10f : 0.05f));
        g.fillRoundedRectangle (r, 6.0f);
        const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
        juce::Path p;
        p.startNewSubPath (cx - 3.5f, cy - 1.5f);
        p.lineTo (cx, cy + 2.0f);
        p.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        if (b.getComponentID() == "flat")
        {
            if (hl || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.08f)); g.fillRoundedRectangle (r, 6.0f); }
            return;
        }
        const bool on = b.getToggleState();
        g.setColour (on ? Look::gold.withAlpha (0.20f)
                        : juce::Colours::white.withAlpha (down ? 0.14f : (hl ? 0.09f : 0.045f)));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? Look::gold.withAlpha (0.7f) : juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool) override
    {
        const bool on = b.getToggleState();
        if (b.getComponentID() == "power")
        {
            auto pr = b.getLocalBounds().toFloat().reduced (3.0f);
            const float cx = pr.getCentreX(), cy = pr.getCentreY();
            const float rad = juce::jmin (pr.getWidth(), pr.getHeight()) * 0.5f - 2.0f;
            const auto col = on ? Look::gold : Look::muted;
            if (on) { g.setColour (col.withAlpha (0.18f)); g.fillEllipse (cx - rad - 3.0f, cy - rad - 3.0f, 2.0f * rad + 6.0f, 2.0f * rad + 6.0f); }
            juce::Path arc;
            arc.addCentredArc (cx, cy, rad, rad, 0.0f, 0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (cx, cy - rad - 1.0f, cx, cy - 1.0f, 1.8f);
            return;
        }
        const float h = (float) b.getHeight();
        const float ds = 9.0f;
        juce::Rectangle<float> dot (4.0f, (h - ds) * 0.5f, ds, ds);
        if (on)
        {
            g.setColour (Look::gold.withAlpha (0.25f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (Look::gold);
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (juce::Colour (0xff5b566a));
            g.drawEllipse (dot, 1.3f);
        }
        g.setColour (on || hl ? Look::text : Look::muted);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (b.getButtonText(), (int) dot.getRight() + 8, 0, b.getWidth() - 22, b.getHeight(), juce::Justification::centredLeft);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();
        const bool big = rad > 30.0f;
        const float thick = big ? 4.5f : 3.2f;
        const float ar = rad - thick * 0.5f;
        const float ang = a0 + pos * (a1 - a0);
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

        float from = a0, to = ang;
        if (bipolar) { const float mid = 0.5f * (a0 + a1); from = juce::jmin (mid, ang); to = juce::jmax (mid, ang); }

        juce::Path track, val;
        track.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff3a3644));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (to - from > 0.001f)
        {
            val.addCentredArc (c.x, c.y, ar, ar, 0.0f, from, to, true);
            g.setColour (col);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float ir = ar - thick - 3.0f;
        if (ir > 5.0f)
        {
            g.setColour (juce::Colour (0xff25222d));
            g.fillEllipse (c.x - ir, c.y - ir, 2.0f * ir, 2.0f * ir);
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.drawEllipse (c.x - ir, c.y - ir, 2.0f * ir, 2.0f * ir, 1.0f);
            const float dx = std::sin (ang), dy = -std::cos (ang);
            g.setColour (Look::text);
            g.drawLine (c.x + dx * ir * 0.35f, c.y + dy * ir * 0.35f, c.x + dx * ir * 0.88f, c.y + dy * ir * 0.88f, 2.0f);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (juce::Colour (0xff3a3644));
        g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
        g.setColour (Look::gold);
        g.fillRoundedRectangle ((float) x, cy - 1.5f, juce::jmax (0.0f, pos - (float) x), 3.0f, 1.5f);
        g.setColour (Look::text);
        g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }
};
