#pragma once
#include "Common.h"

namespace RP
{
constexpr int numBands = 24;

enum FilterType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt, numTypes };
enum Placement  { Stereo = 0, Left, Right, Mid, Side };

inline const juce::StringArray typeNames      { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
inline const juce::StringArray slopeNames     { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
inline const int               slopeOrders[]  { 1, 2, 3, 4, 6, 8, 12, 16, 32 };
inline const juce::StringArray placementNames { "Stereo", "Left", "Right", "Mid", "Side" };
inline const juce::StringArray dirNames       { "Above", "Below" };
inline const juce::StringArray modeNames      { "Zero Latency", "Natural Phase", "Linear Phase" };
inline const juce::StringArray osNames        { "Off", "2x", "4x", "8x" };
// analyzer settings (same idea as the Pro-Q analyzer panel)
inline const juce::StringArray anaRangeNames   { "60 dB", "90 dB", "120 dB" };
inline const juce::StringArray anaResNames     { "Low", "Medium", "High", "Maximum" };          // FFT 2048 / 4096 / 8192 / 16384
inline const juce::StringArray anaSpeedNames   { "Very slow", "Slow", "Medium", "Fast", "Very fast" };   // release speed
inline const juce::StringArray anaTiltNames    { "0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
inline const juce::StringArray charNames      { "Clean", "Subtle", "Warm" };
inline const juce::StringArray anaViewNames   { "Pre", "Post", "Pre + Post" };
inline const juce::StringArray rangeNames     { "3 dB", "6 dB", "12 dB", "30 dB" };

// "A4 +12c" style note name for a frequency (A4 = 440 Hz)
inline juce::String noteName (float f)
{
    if (f <= 0.0f) return {};
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double m = 69.0 + 12.0 * std::log2 ((double) f / 440.0);
    const int n = (int) std::lround (m), cents = (int) std::lround ((m - n) * 100.0);
    const int pc = ((n % 12) + 12) % 12, oct = (int) std::floor (n / 12.0) - 1;
    return juce::String (nm[pc]) + juce::String (oct) + (cents == 0 ? juce::String() : " " + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + "c");
}

inline juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }
inline bool typeHasGain (int t) { return t == Bell || t == LowShelf || t == HighShelf || t == TiltShelf || t == FlatTilt; }

// Per band processing kind, derived from the "dyn" / "spec" parameters:
//   Normal   : plain static EQ band
//   Dynamic  : time-domain dynamic EQ band (detector + gain offset)
//   Spectral : STFT based spectral dynamic band (stereo only; mono / surround fall back to Dynamic)
// Only gain type bands (Bell, shelves, tilts) can be dynamic; cuts / notch / band pass are always Normal.
enum DynMode { ModeNormal = 0, ModeDynamic, ModeSpectral };
inline int dynModeOf (bool dyn, bool spec, int type)
{
    if (! dyn || ! typeHasGain (type)) return ModeNormal;
    return spec ? ModeSpectral : ModeDynamic;
}

namespace detail
{
    inline std::unique_ptr<juce::AudioParameterFloat> mk (const juce::String& id, const juce::String& name,
                                                          juce::NormalisableRange<float> r, float def,
                                                          std::function<juce::String (float, int)> toStr,
                                                          std::function<float (const juce::String&)> fromStr)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toStr).withValueFromStringFunction (fromStr));
    }
    inline std::function<float (const juce::String&)> parse()
    {
        return [] (const juce::String& t) { return t.getFloatValue(); };
    }
    inline std::function<juce::String (float, int)> fmt (const char* unit, int dec, bool sign)
    {
        juce::String u (unit);
        return [u, dec, sign] (float v, int) { return (sign && v > 0.0f ? juce::String ("+") : juce::String()) + juce::String (v, dec) + " " + u; };
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using namespace detail;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < numBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";

        NormalisableRange<float> fr (10.0f, 20000.0f, 0.01f);
        fr.setSkewForCentre (1000.0f);
        NormalisableRange<float> qr (0.025f, 40.0f, 0.001f);
        qr.setSkewForCentre (1.0f);
        NormalisableRange<float> ar (0.1f, 500.0f, 0.01f);
        ar.setSkewForCentre (20.0f);
        NormalisableRange<float> rr (5.0f, 3000.0f, 0.1f);
        rr.setSkewForCentre (150.0f);

        const float defF = jlimit (20.0f, 20000.0f, 30.0f * std::pow (600.0f, (float) b / (float) (numBands - 1)));

        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (b, "en"), 1 },    n + "Enabled", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (b, "type"), 1 },  n + "Type", typeNames, 0));
        layout.add (mk (pid (b, "freq"), n + "Frequency", fr, defF,
                        [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; },
                        [] (const String& t) { float v = t.getFloatValue(); return t.containsIgnoreCase ("k") ? v * 1000.0f : v; }));
        layout.add (mk (pid (b, "gain"), n + "Gain", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, fmt ("dB", 1, true), parse()));
        layout.add (mk (pid (b, "q"), n + "Q", qr, 1.0f, fmt ("", 2, false), parse()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (b, "slope"), 1 }, n + "Slope", slopeNames, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (b, "place"), 1 }, n + "Stereo Placement", placementNames, 0));
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (b, "dyn"), 1 },   n + "Dynamic", false));
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (b, "spec"), 1 },  n + "Spectral", false));
        layout.add (mk (pid (b, "thr"), n + "Threshold", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -24.0f, fmt ("dB", 1, false), parse()));
        layout.add (mk (pid (b, "range"), n + "Range", NormalisableRange<float> (-30.0f, 30.0f, 0.1f), -6.0f, fmt ("dB", 1, true), parse()));
        layout.add (mk (pid (b, "att"), n + "Attack", ar, 20.0f, fmt ("ms", 1, false), parse()));
        layout.add (mk (pid (b, "rel"), n + "Release", rr, 150.0f, fmt ("ms", 0, false), parse()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (b, "dir"), 1 },   n + "Dynamic Direction", dirNames, 0));
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (b, "sc"), 1 },    n + "Sidechain", false));
    }

    layout.add (mk ("out_gain", "Output Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, fmt ("dB", 1, true), parse()));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 },       "Processing Mode", modeNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "character", 1 },  "Character", charNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 },         "Oversampling", osNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "disp_range", 1 }, "Display Range", rangeNames, 2));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "analyzer", 1 },   "Analyzer", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ana_view", 1 },   "Analyzer View", anaViewNames, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ana_range", 1 },  "Analyzer Range", anaRangeNames, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ana_res", 1 },    "Analyzer Resolution", anaResNames, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ana_speed", 1 },  "Analyzer Speed", anaSpeedNames, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ana_tilt", 1 },   "Analyzer Tilt", anaTiltNames, 3));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "ana_freeze", 1 }, "Analyzer Freeze", false));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "invert", 1 },     "Phase Invert", false));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "notes", 1 },      "Piano View", false));
    return layout;
}
}
