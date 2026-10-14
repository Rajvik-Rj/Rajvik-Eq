#pragma once
#include "Common.h"
#include "Params.h"

// STFT based "Spectral Dynamics": every bin is compared against the band threshold and
// only the bins that exceed it - and only inside the band's own Q width - are turned down/up.
class SpectralStage
{
public:
    static constexpr int order = 11;
    static constexpr int N = 1 << order;   // 2048, also the latency in samples
    static constexpr int hop = N / 4;
    static constexpr int bins = N / 2 + 1;

    struct BandCfg
    {
        bool active = false, above = true, sc = false;
        float freq = 1000.f, q = 1.f, thr = -24.f, range = -6.f, att = 20.f, rel = 150.f;
    };

    SpectralStage() : fft (order)
    {
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = std::sin (juce::MathConstants<float>::pi * ((float) i + 0.5f) / (float) N);
        for (auto& c : cachedFreq) c = -1.0f;
        reset();
    }

    void prepare (double sr)
    {
        fs = sr;
        for (auto& c : cachedFreq) c = -1.0f;
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { inBuf[(size_t) ch].fill (0.0f); inSc[(size_t) ch].fill (0.0f); outBuf[(size_t) ch].fill (0.0f); }
        for (auto& e : env) e.fill (-120.0f);
        pos = 0; hopCount = 0;
        mask.fill (0.0f);
    }

    void setBands (const std::array<BandCfg, RP::numBands>& c)
    {
        cfg = c;
        for (int b = 0; b < RP::numBands; ++b)
        {
            const auto& bc = cfg[(size_t) b];
            if (! bc.active) { dynAt[(size_t) b] = 0.0f; continue; }
            if (bc.freq != cachedFreq[(size_t) b] || bc.q != cachedQ[(size_t) b])
            {
                cachedFreq[(size_t) b] = bc.freq; cachedQ[(size_t) b] = bc.q;
                int lo = bins, hi = -1;
                auto& w = weight[(size_t) b];

                // The band covers exactly the region the display shades: its bell edges f0/Q wide.
                // Weight is 1 at the centre, 0.5 at the band edges and falls to zero just outside them,
                // so only the bins inside the band (and a short skirt) react. More Q = narrower, less Q = wider.
                const double f0 = juce::jmax (10.0, (double) bc.freq);
                const double hq = 1.0 / juce::jmax (0.025, (double) bc.q);
                const double rt = std::sqrt (hq * hq + 4.0);
                const double fHi = f0 * 0.5 * (hq + rt), fLo = f0 * 0.5 * (-hq + rt);
                double halfOct = 0.5 * std::log2 (fHi / fLo);
                halfOct = juce::jmax (halfOct, std::log2 (1.0 + fs / (double) N / f0));   // never narrower than one FFT bin

                for (int k = 0; k < bins; ++k)
                {
                    const double f = (double) k * fs / (double) N;
                    double wt = 0.0;
                    if (k > 0)
                    {
                        const double t = std::abs (std::log2 (f / f0)) / halfOct;
                        wt = std::exp (-0.693147 * t * t * t * t);   // flat top, steep sides
                        if (wt < 0.01) wt = 0.0;
                    }
                    w[(size_t) k] = (float) wt;
                    if (wt > 0.0) { lo = juce::jmin (lo, k); hi = juce::jmax (hi, k); }
                }
                lo_[(size_t) b] = lo; hi_[(size_t) b] = hi;
            }
        }
    }

    // scL / scR: optional sidechain input (nullptr = silence). Bands with cfg.sc detect on it instead of the main signal.
    void process (float* l, float* r, const float* scL, const float* scR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            inBuf[0][(size_t) pos] = l[i];
            inBuf[1][(size_t) pos] = r[i];
            inSc[0][(size_t) pos] = scL != nullptr ? scL[i] : 0.0f;
            inSc[1][(size_t) pos] = scR != nullptr ? scR[i] : 0.0f;
            l[i] = outBuf[0][(size_t) pos];
            r[i] = outBuf[1][(size_t) pos];
            outBuf[0][(size_t) pos] = 0.0f;
            outBuf[1][(size_t) pos] = 0.0f;
            pos = (pos + 1) & (N - 1);
            if (++hopCount >= hop) { hopCount = 0; processFrame(); }
        }
    }

    std::array<float, bins> mask {};            // live per-bin gain (dB) the stage applies, for the display
    std::array<float, RP::numBands> dynAt {};   // current dynamic offset (dB) at each band's centre bin

private:
    void processFrame()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fr[(size_t) ch];
            for (int i = 0; i < N; ++i)
                f[(size_t) i] = inBuf[(size_t) ch][(size_t) ((pos + i) & (N - 1))] * win[(size_t) i];
            std::fill (f.begin() + N, f.end(), 0.0f);
            fft.performRealOnlyForwardTransform (f.data(), true);
        }

        const float norm = 1.0f / ((float) N * 0.3183f);   // 0 dBFS sine -> 0 dB
        auto levels = [norm] (const std::array<std::array<float, 2 * N>, 2>& f, std::array<float, bins>& out)
        {
            for (int k = 0; k < bins; ++k)
            {
                const float l2 = f[0][(size_t) (2 * k)] * f[0][(size_t) (2 * k)] + f[0][(size_t) (2 * k + 1)] * f[0][(size_t) (2 * k + 1)];
                const float r2 = f[1][(size_t) (2 * k)] * f[1][(size_t) (2 * k)] + f[1][(size_t) (2 * k + 1)] * f[1][(size_t) (2 * k + 1)];
                out[(size_t) k] = 20.0f * std::log10 (std::sqrt (0.5f * (l2 + r2)) * norm + 1e-9f);
            }
        };
        levels (fr, lvl);

        bool anySc = false;
        for (const auto& bc : cfg) if (bc.active && bc.sc) anySc = true;
        if (anySc)   // spectrum of the sidechain input, only computed when a band needs it
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& f = frSc[(size_t) ch];
                for (int i = 0; i < N; ++i)
                    f[(size_t) i] = inSc[(size_t) ch][(size_t) ((pos + i) & (N - 1))] * win[(size_t) i];
                std::fill (f.begin() + N, f.end(), 0.0f);
                fft.performRealOnlyForwardTransform (f.data(), true);
            }
            levels (frSc, lvlSc);
        }

        gainDb.fill (0.0f);
        const float hopSec = (float) hop / (float) fs;
        for (int b = 0; b < RP::numBands; ++b)
        {
            const auto& bc = cfg[(size_t) b];
            if (! bc.active || hi_[(size_t) b] < 0) continue;
            const float aA = 1.0f - std::exp (-hopSec / (juce::jmax (0.1f, bc.att) * 0.001f));
            const float aR = 1.0f - std::exp (-hopSec / (juce::jmax (1.0f, bc.rel) * 0.001f));
            auto& e = env[(size_t) b];
            const auto& w = weight[(size_t) b];
            float peakAmt = 0.0f;
            for (int k = lo_[(size_t) b]; k <= hi_[(size_t) b]; ++k)
            {
                const float L = bc.sc ? lvlSc[(size_t) k] : lvl[(size_t) k];
                e[(size_t) k] += (L > e[(size_t) k] ? aA : aR) * (L - e[(size_t) k]);
                const float am = amount (e[(size_t) k], bc);
                gainDb[(size_t) k] += w[(size_t) k] * bc.range * am;
                if (w[(size_t) k] > 0.5f) peakAmt = juce::jmax (peakAmt, am);
            }
            dynAt[(size_t) b] = bc.range * peakAmt;   // strongest action inside the band (the peak need not be at the centre)
        }

        // smooth the gain mask across frequency (soft edges, peaks keep their full depth) so isolated bins do not produce "musical noise"
        {
            float prev = gainDb[0];
            for (int k = 0; k < bins; ++k)
            {
                const float cur = gainDb[(size_t) k], next = k + 1 < bins ? gainDb[(size_t) k + 1] : cur;
                const float sm = 0.25f * prev + 0.5f * cur + 0.25f * next;
                gainDb[(size_t) k] = std::abs (sm) > std::abs (cur) ? sm : cur;   // soften edges, never weaken a peak
                prev = cur;
            }
            mask = gainDb;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fr[(size_t) ch];
            for (int k = 0; k < bins; ++k)
            {
                const float g = gainDb[(size_t) k];
                if (std::abs (g) > 1.0e-4f)
                {
                    const float lin = std::pow (10.0f, g * 0.05f);
                    f[(size_t) (2 * k)] *= lin; f[(size_t) (2 * k + 1)] *= lin;
                }
            }
            fft.performRealOnlyInverseTransform (f.data());
            for (int i = 0; i < N; ++i)
                outBuf[(size_t) ch][(size_t) ((pos + i) & (N - 1))] += f[(size_t) i] * win[(size_t) i] * 0.5f;
        }
    }

    static float amount (float level, const BandCfg& bc)
    {
        // nothing happens until a bin crosses the threshold; the full range is reached 12 dB past it
        const float d = bc.above ? (level - bc.thr) : (bc.thr - level);
        const float x = juce::jlimit (0.0f, 1.0f, d / 12.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    juce::dsp::FFT fft;
    double fs = 44100.0;
    int pos = 0, hopCount = 0;
    std::array<float, N> win {};
    std::array<std::array<float, N>, 2> inBuf {}, inSc {}, outBuf {};
    std::array<std::array<float, 2 * N>, 2> fr {}, frSc {};
    std::array<float, bins> lvl {}, lvlSc {}, gainDb {};
    std::array<std::array<float, bins>, RP::numBands> env {}, weight {};
    std::array<int, RP::numBands> lo_ {}, hi_ {};
    std::array<float, RP::numBands> cachedFreq {}, cachedQ {};
    std::array<BandCfg, RP::numBands> cfg {};
};
