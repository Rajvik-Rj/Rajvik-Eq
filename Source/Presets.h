#pragma once
#include "PluginProcessor.h"

// Factory presets + user presets (saved as .rajpreset files in the user's application data folder).
// A preset only holds the 24 bands (not output gain / processing mode / analyzer settings).
namespace Presets
{
using namespace RP;

struct BandSpec
{
    int type = Bell;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    int slope = 1;                         // index into slopeNames (used by cut filters)
    int mode = 0;                          // 0 Normal, 1 Dynamic, 2 Spectral
    float thr = -24.0f, range = -6.0f, att = 20.0f, rel = 150.0f;
    int dir = 0;                           // 0 Above, 1 Below
    bool sc = false;                       // detector on the sidechain input
    int place = Stereo;
};

struct Factory { juce::String name; std::vector<BandSpec> bands; };

inline BandSpec bell (float f, float g, float q) { BandSpec s; s.type = Bell; s.freq = f; s.gain = g; s.q = q; return s; }
inline BandSpec shelfLo (float f, float g, float q = 0.7f) { BandSpec s; s.type = LowShelf; s.freq = f; s.gain = g; s.q = q; return s; }
inline BandSpec shelfHi (float f, float g, float q = 0.7f) { BandSpec s; s.type = HighShelf; s.freq = f; s.gain = g; s.q = q; return s; }
inline BandSpec lowCut (float f, int slope) { BandSpec s; s.type = LowCut; s.freq = f; s.slope = slope; s.q = 0.71f; return s; }
inline BandSpec highCut (float f, int slope) { BandSpec s; s.type = HighCut; s.freq = f; s.slope = slope; s.q = 0.71f; return s; }
inline BandSpec dynBell (float f, float q, float thr, float range, float att, float rel, bool sc = false)
{ BandSpec s = bell (f, 0.0f, q); s.mode = 1; s.thr = thr; s.range = range; s.att = att; s.rel = rel; s.sc = sc; return s; }
inline BandSpec specBell (float f, float q, float thr, float range, float att = 15.0f, float rel = 120.0f)
{ BandSpec s = bell (f, 0.0f, q); s.mode = 2; s.thr = thr; s.range = range; s.att = att; s.rel = rel; return s; }

inline const std::vector<Factory>& factory()
{
    static const std::vector<Factory> list =
    {
        { "Init (Flat)", {} },
        { "Vocal Presence",              { lowCut (90.0f, 3), bell (300.0f, -2.5f, 1.2f), bell (3200.0f, 2.5f, 1.0f), shelfHi (10000.0f, 2.0f) } },
        { "Vocal De-Esser (Dynamic)",    { lowCut (80.0f, 3), dynBell (7000.0f, 3.0f, -30.0f, -7.0f, 2.0f, 60.0f) } },
        { "Vocal Resonances (Spectral)", { specBell (3000.0f, 0.35f, -48.0f, -8.0f), specBell (800.0f, 0.5f, -48.0f, -6.0f) } },
        { "Bass Boost",                  { shelfLo (90.0f, 4.0f), bell (250.0f, -2.0f, 1.0f) } },
        { "Bass Control (Dynamic)",      { dynBell (120.0f, 1.2f, -28.0f, -5.0f, 15.0f, 120.0f) } },
        { "Bass Ducks for Kick (Sidechain)", { dynBell (80.0f, 1.0f, -35.0f, -6.0f, 5.0f, 120.0f, true) } },
        { "Mud Cleaner",                 { lowCut (30.0f, 1), bell (250.0f, -3.0f, 1.5f), bell (500.0f, -2.0f, 2.0f) } },
        { "Kick Punch",                  { bell (60.0f, 3.0f, 1.5f), bell (350.0f, -3.0f, 2.0f), bell (4000.0f, 2.0f, 1.5f) } },
        { "Guitar Warmth",               { lowCut (80.0f, 3), bell (200.0f, 2.0f, 1.0f), bell (2500.0f, -2.0f, 1.2f), highCut (14000.0f, 1) } },
        { "Harshness Taming (Spectral)", { specBell (3500.0f, 0.5f, -50.0f, -6.0f), specBell (6000.0f, 0.5f, -52.0f, -5.0f) } },
        { "Air Boost",                   { shelfHi (12000.0f, 3.0f), bell (16000.0f, 1.5f, 0.8f) } },
        { "Master Sparkle",              { lowCut (25.0f, 1), bell (300.0f, -1.0f, 0.8f), shelfHi (9000.0f, 1.5f) } },
        { "Telephone",                   { lowCut (500.0f, 4), highCut (3400.0f, 4), bell (1500.0f, 4.0f, 1.0f) } },
    };
    return list;
}

inline void setP (RajvikEQAudioProcessor& p, const juce::String& id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}

inline bool isBandId (const juce::String& id) { return id.length() > 2 && id[0] == 'b' && juce::CharacterFunctions::isDigit (id[1]); }

// every band parameter back to its default (all bands off)
inline void resetBands (RajvikEQAudioProcessor& p)
{
    for (auto* prm : p.getParameters())
        if (auto* pp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (isBandId (pp->paramID))
            {
                pp->beginChangeGesture();
                pp->setValueNotifyingHost (pp->getDefaultValue());
                pp->endChangeGesture();
            }
}

inline void setPresetName (RajvikEQAudioProcessor& p, const juce::String& n) { p.apvts.state.setProperty ("preset", n, nullptr); }
inline juce::String presetName (const RajvikEQAudioProcessor& p) { return p.apvts.state.getProperty ("preset", "Init (Flat)").toString(); }

inline void applyFactory (RajvikEQAudioProcessor& p, int index)
{
    const auto& list = factory();
    if (index < 0 || index >= (int) list.size()) return;
    resetBands (p);
    int b = 0;
    for (const auto& s : list[(size_t) index].bands)
    {
        if (b >= numBands) break;
        setP (p, pid (b, "type"), (float) s.type);
        setP (p, pid (b, "freq"), s.freq);
        setP (p, pid (b, "gain"), s.gain);
        setP (p, pid (b, "q"), s.q);
        setP (p, pid (b, "slope"), (float) s.slope);
        setP (p, pid (b, "place"), (float) s.place);
        setP (p, pid (b, "dyn"), s.mode != 0 ? 1.0f : 0.0f);
        setP (p, pid (b, "spec"), s.mode == 2 ? 1.0f : 0.0f);
        setP (p, pid (b, "thr"), s.thr);
        setP (p, pid (b, "range"), s.range);
        setP (p, pid (b, "att"), s.att);
        setP (p, pid (b, "rel"), s.rel);
        setP (p, pid (b, "dir"), (float) s.dir);
        setP (p, pid (b, "sc"), s.sc ? 1.0f : 0.0f);
        setP (p, pid (b, "en"), 1.0f);
        ++b;
    }
    setPresetName (p, list[(size_t) index].name);
}

//==============================================================================
inline juce::File userDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("RajvikEQ").getChildFile ("Presets");
}

inline juce::Array<juce::File> userFiles()
{
    juce::Array<juce::File> files = userDir().findChildFiles (juce::File::findFiles, false, "*.rajpreset");
    files.sort();
    return files;
}

inline bool saveUser (RajvikEQAudioProcessor& p, const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) return false;
    auto dir = userDir();
    if (! dir.createDirectory().wasOk()) return false;
    setPresetName (p, name);
    auto state = p.apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr) return false;
    return xml->writeTo (dir.getChildFile (juce::File::createLegalFileName (name) + ".rajpreset"));
}

inline bool loadUser (RajvikEQAudioProcessor& p, const juce::File& file)
{
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
    if (xml == nullptr) return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid()) return false;
    resetBands (p);
    for (const auto& child : tree)
        if (child.hasType ("PARAM"))
        {
            const auto id = child.getProperty ("id").toString();
            if (isBandId (id)) setP (p, id, (float) (double) child.getProperty ("value"));
        }
    setPresetName (p, file.getFileNameWithoutExtension());
    return true;
}
}
