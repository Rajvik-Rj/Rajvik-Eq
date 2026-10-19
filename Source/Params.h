#pragma once
#include "Common.h"

namespace RP
{
constexpr int numBands = 24;

// Shape recipes of the vintage-style types (built in DSP.h): shelf / bell / shelf with a dip (boost + attenuate interaction) / bell + shelf
enum VKind { VK_LowShelf = 0, VK_HighShelf, VK_LowShelf1, VK_HighShelf1, VK_Bell, VK_LowDip, VK_HighDip, VK_BellHi, VK_BellLo };
struct VDef
{
    const char* name;
    int unit;          // index into vintageUnits
    int kind;          // VKind
    double q;          // base shape: shelf Q / bell Q
    double qGain;      // proportional Q: Q grows by (1 + qGain * |gain dB|)
    double dip;        // dip amount for VK_*Dip (fraction of the boost) / shelf share for VK_Bell*
    double dipMul;     // dip frequency multiplier
    double fMul;       // maps the user frequency to the corner / centre (hardware "turnover" vs 3 dB point)
};
inline const char* const vintageUnits[] = { "Passive Tube", "Brit Console", "American Console", "Studio Console", "Air Band",
                                            "Passive Mastering", "Tube Program", "Mastering Parametric", "Tone Control" };
constexpr int numVintageUnits = 9;
inline const VDef vintageDefs[] = {
    // Passive Tube: broad shelves, boost + attenuate interaction
    { "Tube Low Shelf",          0, VK_LowDip,    0.60, 0.00, 0.28, 2.2, 1.00 },
    { "Tube Low Shelf Wide",     0, VK_LowDip,    0.42, 0.00, 0.38, 3.0, 0.85 },
    { "Tube High Shelf",         0, VK_HighDip,   0.55, 0.00, 0.20, 0.50, 1.00 },
    { "Tube High Bell",          0, VK_BellHi,    0.55, 0.00, 0.45, 0.85, 1.00 },
    // Brit Console: shelves with a little overshoot, proportional Q bells
    { "Brit Low Shelf",          1, VK_LowShelf,  0.90, 0.00, 0.00, 1.0, 1.00 },
    { "Brit Mid Bell",           1, VK_Bell,      0.60, 0.075, 0.00, 1.0, 1.00 },
    { "Brit Hi-Mid Bell",        1, VK_Bell,      0.75, 0.085, 0.00, 1.0, 1.00 },
    { "Brit High Shelf",         1, VK_HighShelf, 0.85, 0.00, 0.00, 1.0, 1.00 },
    // American Console: smooth shelves, strongly proportional Q
    { "Amer Low Shelf",          2, VK_LowShelf,  0.60, 0.00, 0.00, 1.0, 1.15 },
    { "Amer Low-Mid Bell",       2, VK_Bell,      0.50, 0.12, 0.00, 1.0, 1.00 },
    { "Amer High-Mid Bell",      2, VK_Bell,      0.55, 0.12, 0.00, 1.0, 1.00 },
    { "Amer High Shelf",         2, VK_HighShelf, 0.60, 0.00, 0.00, 1.0, 0.87 },
    // Studio Console: constant Q bells, tight shelves
    { "Studio Low Shelf",        3, VK_LowShelf,  0.70, 0.00, 0.00, 1.0, 1.00 },
    { "Studio Low Bell",         3, VK_Bell,      0.80, 0.00, 0.00, 1.0, 1.00 },
    { "Studio High Bell",        3, VK_Bell,      1.00, 0.00, 0.00, 1.0, 1.00 },
    { "Studio High Shelf",       3, VK_HighShelf, 0.70, 0.00, 0.00, 1.0, 1.00 },
    // Air Band: very gentle shelves and a wide presence bell
    { "Air Shelf",               4, VK_HighShelf, 0.36, 0.00, 0.00, 1.0, 1.90 },
    { "Air Sub Shelf",           4, VK_LowShelf,  0.36, 0.00, 0.00, 1.0, 0.55 },
    { "Air Presence Bell",       4, VK_Bell,      0.45, 0.00, 0.00, 1.0, 1.00 },
    // Passive Mastering: first order shelves and very wide bells
    { "Master Low Shelf",        5, VK_LowShelf1, 0.00, 0.00, 0.00, 1.0, 1.00 },
    { "Master Low Bell",         5, VK_Bell,      0.35, 0.00, 0.00, 1.0, 1.00 },
    { "Master High Bell",        5, VK_Bell,      0.38, 0.00, 0.00, 1.0, 1.00 },
    { "Master High Shelf",       5, VK_HighShelf1,0.00, 0.00, 0.00, 1.0, 1.00 },
    // Tube Program: shelves with a mild dip and a presence bump
    { "Program Low",             6, VK_LowDip,    0.55, 0.00, 0.18, 2.0, 1.00 },
    { "Program Mid Bell",        6, VK_Bell,      0.50, 0.03, 0.00, 1.0, 1.00 },
    { "Program High",            6, VK_BellHi,    0.50, 0.00, 0.30, 0.70, 1.00 },
    // Mastering Parametric: clean, constant Q bells
    { "Mastering Low Bell",      7, VK_Bell,      0.70, 0.00, 0.00, 1.0, 1.00 },
    { "Mastering Mid Bell",      7, VK_Bell,      1.40, 0.00, 0.00, 1.0, 1.00 },
    { "Mastering High Bell",     7, VK_Bell,      0.90, 0.00, 0.00, 1.0, 1.00 },
    // Tone Control: 6 dB/oct bass / treble and a wide mid scoop
    { "Tone Bass",               8, VK_LowShelf1, 0.00, 0.00, 0.00, 1.0, 0.60 },
    { "Tone Treble",             8, VK_HighShelf1,0.00, 0.00, 0.00, 1.0, 1.70 },
    { "Tone Mid",                8, VK_Bell,      0.30, 0.02, 0.00, 1.0, 1.00 },
};
constexpr int numVintage = 32;

enum FilterType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt,
                  Sword, VintageBell, TubeLow, TubeHigh, BaxLow, BaxHigh,
                  VintageFirst, numTypes = VintageFirst + numVintage };
enum Placement  { Stereo = 0, Left, Right, Mid, Side };

inline juce::StringArray makeTypeNames()
{
    juce::StringArray a { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt",
                          "Sword", "Prop-Q Bell", "Tube Low (boost+cut)", "Tube High (air)", "Baxandall Low", "Baxandall High" };
    for (int i = 0; i < numVintage; ++i) a.add (vintageDefs[i].name);
    return a;
}
inline const juce::StringArray typeNames = makeTypeNames();
inline const juce::StringArray slopeNames     { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
inline const int               slopeOrders[]  { 1, 2, 3, 4, 6, 8, 12, 16, 32 };
inline const juce::StringArray placementNames { "Stereo", "Left", "Right", "Mid", "Side" };
inline const juce::StringArray dirNames       { "Above", "Below" };
inline const juce::StringArray modeNames      { "Zero Latency", "Natural Phase", "Linear Phase", "Mixed Phase" };
inline const juce::StringArray linLenNames   { "Fast (23 ms)", "Balanced (46 ms)", "Precise (93 ms)" };   // at 44.1 kHz: latency of the Linear Phase mode
inline constexpr int linLenValues[] = { 2048, 4096, 8192 };   // FIR length; the latency is half of it
inline const juce::StringArray precNames      { "64-bit", "117-bit" };
inline const juce::StringArray relModeNames   { "Full signal", "Band" };
inline const juce::StringArray osNames        { "Off", "2x", "4x", "8x" };
// analyzer settings (analyzer panel)
inline const juce::StringArray anaRangeNames   { "60 dB", "90 dB", "120 dB" };
inline const juce::StringArray anaResNames     { "Low", "Medium", "High", "Maximum" };          // FFT 2048 / 4096 / 8192 / 16384
inline const juce::StringArray anaSpeedNames   { "Very slow", "Slow", "Medium", "Fast", "Very fast" };   // release speed
inline const juce::StringArray anaTiltNames    { "0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
inline const juce::StringArray charNames      { "Clean", "Subtle", "Warm" };
inline const juce::StringArray anaViewNames   { "Pre", "Post", "Pre + Post" };
inline const juce::StringArray rangeNames     { "3 dB", "6 dB", "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "48 dB" };
inline constexpr float rangeValues[] = { 3.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f, 48.0f };
inline constexpr int numRanges = 8;
inline float rangeStep (float range) { return range <= 3.0f ? 1.0f : range <= 6.0f ? 2.0f : range <= 12.0f ? 3.0f : range <= 24.0f ? 6.0f : range <= 30.0f ? 10.0f : 12.0f; }

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
inline bool typeHasGain (int t) { return t == Bell || t == LowShelf || t == HighShelf || t == TiltShelf || t == FlatTilt || t >= Sword; }

// Mixed Phase: share of a band that is linear phase, depending on its frequency (low = minimum phase, high = linear phase)
inline double mixedLinearWeight (double f)
{
    const double t = juce::jlimit (0.0, 1.0, std::log2 (juce::jmax (1.0, f) / 150.0) / std::log2 (3000.0 / 150.0));
    return t * t * (3.0 - 2.0 * t);
}

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
        // relative / onset detection (Dynamic bands)
        layout.add (mk (pid (b, "relamt"), n + "Relative Detection", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, fmt ("%", 0, false), parse()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (b, "relmode"), 1 }, n + "Relative Source", relModeNames, 0));
        layout.add (mk (pid (b, "relf"), n + "Relative Frequency", fr, 1000.0f,
                        [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; },
                        [] (const String& t) { float v = t.getFloatValue(); return t.containsIgnoreCase ("k") ? v * 1000.0f : v; }));
        layout.add (mk (pid (b, "relq"), n + "Relative Q", qr, 1.0f, fmt ("", 2, false), parse()));
        layout.add (mk (pid (b, "onset"), n + "Onset Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, fmt ("%", 0, false), parse()));
    }

    layout.add (mk ("out_gain", "Output Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, fmt ("dB", 1, true), parse()));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 },       "Processing Mode", modeNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "character", 1 },  "Character", charNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 },         "Oversampling", osNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "prec", 1 },       "Processing Precision", precNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lin_len", 1 },    "Linear Phase Length", linLenNames, 1));
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
