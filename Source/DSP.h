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

// Builds the cascade of biquads for one band.
inline void design (BandDesign& d, int type, double f, double gainDb, double q, int slopeIdx, double fs)
{
    using namespace detail;
    d.n = 0;
    f = juce::jlimit (5.0, fs * 0.49, f);
    q = juce::jmax (0.02, q);

    switch (type)
    {
        case Bell:      d.sec[d.n++] = peak (f, gainDb, q, fs); break;
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
        default: break;
    }
}

inline double magSq (const Coeffs& c, double w)
{
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return std::norm (num) / std::max (std::norm (den), 1e-30);
}

inline double designDb (const BandDesign& d, double f, double fs)
{
    const double w = detail::twoPi * juce::jmin (f, fs * 0.499) / fs;
    double m = 1.0;
    for (int k = 0; k < d.n; ++k) m *= magSq (d.sec[k], w);
    return 10.0 * std::log10 (std::max (m, 1e-30));
}
}
