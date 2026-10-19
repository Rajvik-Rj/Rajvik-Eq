#pragma once
#include "Common.h"
#include "Params.h"

namespace RP
{
constexpr int maxSections = 17;

struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
struct BandDesign { Coeffs sec[maxSections]; int n = 0; };

struct Biquad
{
    Coeffs c;
    double z1 = 0.0, z2 = 0.0;
    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0; }
};

// ---- double-double arithmetic (about 106 bit mantissa: "117-bit" processing in the sense of the extra headroom of the state) ----
struct DD { double hi = 0.0, lo = 0.0; };
inline DD ddQuickSum (double a, double b) { const double s = a + b; return { s, b - (s - a) }; }
inline DD ddSum (double a, double b) { const double s = a + b, bb = s - a; return { s, (a - (s - bb)) + (b - bb) }; }
inline DD operator+ (DD a, DD b)
{
    DD s = ddSum (a.hi, b.hi), t = ddSum (a.lo, b.lo);
    s.lo += t.hi; s = ddQuickSum (s.hi, s.lo);
    s.lo += t.lo; return ddQuickSum (s.hi, s.lo);
}
inline DD operator- (DD a, DD b) { return a + DD { -b.hi, -b.lo }; }
inline DD ddMul (double a, DD b)
{
    const double p = a * b.hi;
    DD r { p, std::fma (a, b.hi, -p) };
    r.lo += a * b.lo;
    return ddQuickSum (r.hi, r.lo);
}
struct BiquadDD
{
    Coeffs c;
    DD z1, z2;
    inline DD process (DD x) noexcept
    {
        const DD y = ddMul (c.b0, x) + z1;
        z1 = ddMul (c.b1, x) - ddMul (c.a1, y) + z2;
        z2 = ddMul (c.b2, x) - ddMul (c.a2, y);
        return y;
    }
    void reset() noexcept { z1 = {}; z2 = {}; }
};

namespace detail
{
    constexpr double twoPi = 6.283185307179586;

    inline Coeffs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
    inline Coeffs peak (double f, double g, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, g / 40.0), al = s / (2.0 * q);
        return norm (1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
    }
    inline Coeffs lowShelf (double f, double g, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, g / 40.0), al = s / (2.0 * q), t = 2.0 * std::sqrt (A) * al;
        return norm (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
                     (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    inline Coeffs highShelf (double f, double g, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, g / 40.0), al = s / (2.0 * q), t = 2.0 * std::sqrt (A) * al;
        return norm (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
                     (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    // Nyquist-matched peaking filter: poles by matched-z, numerator fitted so that the digital response hits the analogue
    // prototype at DC, at the band centre and at Nyquist. Removes the high-frequency "cramping" of the plain bilinear bell.
    inline bool peakMatched (Coeffs& out, double f, double g, double q, double fs)
    {
        const double w0 = twoPi * f / fs, A = std::pow (10.0, g / 40.0), zt = 1.0 / (2.0 * A * q);
        double a1;
        if (zt < 1.0) a1 = -2.0 * std::exp (-zt * w0) * std::cos (w0 * std::sqrt (1.0 - zt * zt));
        else          a1 = -2.0 * std::exp (-zt * w0) * std::cosh (w0 * std::sqrt (zt * zt - 1.0));
        const double a2 = std::exp (-2.0 * zt * w0);
        const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = -4.0 * (a1 + 4.0 * a2 + a1 * a2), A2 = 16.0 * a2;
        const double x = juce::MathConstants<double>::pi / w0;   // Nyquist relative to the band centre
        const double den = (1 - x * x) * (1 - x * x) + x * x / (A * A * q * q);
        const double N = ((1 - x * x) * (1 - x * x) + x * x * A * A / (q * q)) / den;
        const double p0 = std::sin (0.5 * w0) * std::sin (0.5 * w0);
        auto D = [&] (double p) { return A0 + A1 * p + A2 * p * p; };
        const double B0 = A0, R1 = A * A * A * A * D (p0) - B0, R2 = N * D (1.0) - B0;
        const double B2 = (R1 - R2 * p0) / (p0 * (p0 - 1.0)), B1 = R2 - B2;
        const double S = std::sqrt (B0), inner = S * S + B1 + B2;
        if (! (inner >= 0.0)) return false;
        const double u = 0.5 * (S + std::sqrt (inner)), disc = u * u - B2 / 4.0;
        if (! (disc >= 0.0)) return false;
        const double b0 = 0.5 * (u + std::sqrt (disc)), b2 = u - b0, b1 = S - u;
        if (! std::isfinite (b0 + b1 + b2 + a1 + a2) || std::abs (a2) >= 1.0 || std::abs (a1) >= 1.0 + a2) return false;
        out = { b0, b1, b2, a1, a2 };
        return true;
    }
    inline Coeffs peakNM (double f, double g, double q, double fs)
    {
        Coeffs c;
        if (f > 0.03 * fs && std::abs (g) > 0.01 && peakMatched (c, f, g, q, fs)) return c;
        return peak (f, g, q, fs);
    }
    // first order shelves (6 dB/oct, Baxandall style), gain g (dB) at DC / at high frequencies
    inline Coeffs shelf1Low (double f, double g, double fs)
    {
        const double K = std::tan (juce::MathConstants<double>::pi * f / fs), V = std::pow (10.0, g / 20.0);
        return { (1 + V * K) / (1 + K), (V * K - 1) / (1 + K), 0.0, (K - 1) / (1 + K), 0.0 };
    }
    inline Coeffs shelf1High (double f, double g, double fs)
    {
        const double K = std::tan (juce::MathConstants<double>::pi * f / fs), V = std::pow (10.0, g / 20.0);
        return { (V + K) / (1 + K), (K - V) / (1 + K), 0.0, (K - 1) / (1 + K), 0.0 };
    }
    inline Coeffs notch (double f, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (1.0, -2.0 * c, 1.0, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    inline Coeffs bandpass (double f, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (al, 0.0, -al, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    inline Coeffs lp2 (double f, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    inline Coeffs hp2 (double f, double q, double fs)
    {
        const double w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    inline Coeffs lp1 (double f, double fs)
    {
        const double K = std::tan (twoPi * f / fs * 0.5);
        return { K / (1 + K), K / (1 + K), 0.0, (K - 1) / (1 + K), 0.0 };
    }
    inline Coeffs hp1 (double f, double fs)
    {
        const double K = std::tan (twoPi * f / fs * 0.5);
        return { 1 / (1 + K), -1 / (1 + K), 0.0, (K - 1) / (1 + K), 0.0 };
    }
}

// Vintage-style types: parametric interpretations of classic circuit behaviour (proportional Q, shelf overshoot,
// boost + attenuate interaction, passive wide bells). Ideal linear models, no non-linear behaviour.
inline void designVintage (BandDesign& d, const VDef& v, double f, double g, double q, double fs)
{
    using namespace detail;
    const double fe = juce::jlimit (5.0, fs * 0.49, f * v.fMul);
    const double qs = v.q * std::pow (juce::jlimit (0.3, 3.0, q), 0.6);   // shelves: user Q only gently changes the shape
    switch (v.kind)
    {
        case VK_LowShelf:   d.sec[d.n++] = lowShelf (fe, g, qs, fs); break;
        case VK_HighShelf:  d.sec[d.n++] = highShelf (fe, g, qs, fs); break;
        case VK_LowShelf1:  d.sec[d.n++] = shelf1Low (fe, g, fs); break;
        case VK_HighShelf1: d.sec[d.n++] = shelf1High (fe, g, fs); break;
        case VK_Bell:       d.sec[d.n++] = peakNM (fe, g, q * v.q * (1.0 + v.qGain * std::abs (g)), fs); break;
        case VK_LowDip:
            d.sec[d.n++] = lowShelf (fe, g, qs, fs);
            if (g > 0.0) d.sec[d.n++] = lowShelf (juce::jmin (fe * v.dipMul, fs * 0.45), -v.dip * g, qs * 0.9, fs);
            break;
        case VK_HighDip:
            d.sec[d.n++] = highShelf (fe, g, qs, fs);
            if (g > 0.0) d.sec[d.n++] = highShelf (fe * v.dipMul, -v.dip * g, qs * 0.9, fs);
            break;
        case VK_BellHi:   // bell + shelf: broad presence that carries on upwards
            d.sec[d.n++] = peakNM (fe * v.dipMul, g * (1.0 - v.dip), q * v.q, fs);
            d.sec[d.n++] = highShelf (fe, g * v.dip, qs, fs);
            break;
        case VK_BellLo:
            d.sec[d.n++] = peakNM (fe / v.dipMul, g * (1.0 - v.dip), q * v.q, fs);
            d.sec[d.n++] = lowShelf (fe, g * v.dip, qs, fs);
            break;
        default: break;
    }
}

// Builds the cascade of biquads for one band.
inline void design (BandDesign& d, int type, double f, double gainDb, double q, int slopeIdx, double fs)
{
    using namespace detail;
    d.n = 0;
    f = juce::jlimit (5.0, fs * 0.49, f);
    q = juce::jmax (0.02, q);

    switch (type)
    {
        case Bell:      d.sec[d.n++] = peakNM (f, gainDb, q, fs); break;
        case VintageBell:   // proportional Q: the bell gets narrower the more you boost / cut (console style)
            d.sec[d.n++] = peakNM (f, gainDb, q * (0.55 + 0.085 * std::abs (gainDb)), fs); break;
        case Sword:         // narrow core with a wide opposite skirt: steeper flanks than a bell, made to dig out resonances
            d.sec[d.n++] = peakNM (f, gainDb * 1.45, q * 1.35, fs);
            d.sec[d.n++] = peakNM (f, -gainDb * 0.45, q * 0.42, fs);
            break;
        case TubeLow:     // boost with a dip above the corner (the classic "boost + attenuate at the same frequency" curve)
            if (gainDb >= 0.0) { d.sec[d.n++] = lowShelf (f, gainDb, 0.6, fs); d.sec[d.n++] = lowShelf (f * 2.2, -0.28 * gainDb, 0.55, fs); }
            else d.sec[d.n++] = lowShelf (f, gainDb, 0.55, fs);
            break;
        case TubeHigh:    // smooth air boost with a soft dip below the corner
            if (gainDb >= 0.0) { d.sec[d.n++] = highShelf (f, gainDb, 0.55, fs); d.sec[d.n++] = highShelf (f * 0.5, -0.2 * gainDb, 0.5, fs); }
            else d.sec[d.n++] = highShelf (f, gainDb, 0.55, fs);
            break;
        case BaxLow:    d.sec[d.n++] = shelf1Low (f, gainDb, fs); break;
        case BaxHigh:   d.sec[d.n++] = shelf1High (f, gainDb, fs); break;
        case LowShelf:  d.sec[d.n++] = lowShelf (f, gainDb, q * 0.7071, fs); break;
        case HighShelf: d.sec[d.n++] = highShelf (f, gainDb, q * 0.7071, fs); break;
        case Notch:     d.sec[d.n++] = notch (f, q, fs); break;
        case BandPass:  d.sec[d.n++] = bandpass (f, q, fs); break;
        case TiltShelf:
        case FlatTilt:
        {
            const double qt = (type == TiltShelf) ? 0.55 : 0.25;
            d.sec[d.n++] = lowShelf  (f, -gainDb * 0.5, qt, fs);
            d.sec[d.n++] = highShelf (f,  gainDb * 0.5, qt, fs);
            break;
        }
        case LowCut:
        case HighCut:
        {
            const bool hp = (type == LowCut);
            const int order = slopeOrders[juce::jlimit (0, 8, slopeIdx)];
            const int pairs = order / 2;
            if (order % 2 == 1)
                d.sec[d.n++] = hp ? hp1 (f, fs) : lp1 (f, fs);
            for (int k = 1; k <= pairs; ++k)
            {
                const double theta = juce::MathConstants<double>::pi * (2 * k - 1) / (2.0 * order);
                double qk = 1.0 / (2.0 * std::cos (theta));
                if (k == pairs) qk *= q;   // user Q = resonance on the last section (1.0 = Butterworth)
                d.sec[d.n++] = hp ? hp2 (f, qk, fs) : lp2 (f, qk, fs);
            }
            break;
        }
        default:
            if (type >= VintageFirst && type < numTypes) designVintage (d, vintageDefs[type - VintageFirst], f, gainDb, q, fs);
            break;
    }
}

// |H(e^jw)|^2 of one biquad. Closed form in phi = sin^2(w/2): one sin() instead of two complex exponentials
// (the editor evaluates this thousands of times per frame) and no cancellation error at very low frequencies.
inline double magSq (const Coeffs& c, double w)
{
    const double s = std::sin (0.5 * w), phi = s * s;
    auto f = [phi] (double x0, double x1, double x2)
    {
        return (x0 + x1 + x2) * (x0 + x1 + x2) - 4.0 * (x0 * x1 + 4.0 * x0 * x2 + x1 * x2) * phi + 16.0 * x0 * x2 * phi * phi;
    };
    return std::max (f (c.b0, c.b1, c.b2), 0.0) / std::max (f (1.0, c.a1, c.a2), 1e-30);
}

inline double designDb (const BandDesign& d, double f, double fs)
{
    const double w = detail::twoPi * juce::jmin (f, fs * 0.499) / fs;
    double m = 1.0;
    for (int k = 0; k < d.n; ++k) m *= magSq (d.sec[k], w);
    return 10.0 * std::log10 (std::max (m, 1e-30));
}
}
