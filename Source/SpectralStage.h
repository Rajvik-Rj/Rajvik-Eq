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
        int place = 0;   // RP::Placement: decides which signal the detector listens to and which channel(s) the cut is applied to
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
        lvlAt.fill (-120.0f);
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

                // Detector: exactly the band-pass the time-domain Dynamic EQ listens through (centre f0, Q, 0 dB at the centre).
                // The band level is the power sum of the bins under that band-pass, so Dynamic and Spectral read the same level
                // for the same sound and one threshold value means the same thing in both modes.
                const double dq = juce::jmax (0.1, (double) bc.q);
                auto& dw = detW[(size_t) b];
                int dlo = bins, dhi = -1;
                double sum = 0.0;
                for (int k = 0; k < bins; ++k)
                {
                    double h = 0.0;
                    if (k > 0)
                    {
                        const double f = (double) k * fs / (double) N;
                        const double x = f / f0 - f0 / f;
                        h = 1.0 / (1.0 + dq * dq * x * x);
                        if (h < 0.003) h = 0.0;
                    }
                    dw[(size_t) k] = (float) h;
                    if (h > 0.0) { dlo = juce::jmin (dlo, k); dhi = juce::jmax (dhi, k); sum += h; }
                }
                dLo[(size_t) b] = dlo; dHi[(size_t) b] = dhi;
                // power sum of a flat spectrum under the detector (1.2337 = noise bandwidth of the window in bins):
                // adding this to a bin level gives the band level a flat signal would have
                const double eff = juce::jmax (1.0, sum / 1.2337);
                detGain[(size_t) b] = (float) (10.0 * std::log10 (eff));
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
    std::array<float, RP::numBands> lvlAt {};   // live detector level (dB) of each band: the same quantity the Dynamic EQ detector reads

private:
    // detector sets: 0..3 = Mid / Side / Left / Right of the main input, 4..7 = same for the sidechain input
    static int setOf (int place, bool sc)
    {
        const int p = place == RP::Side ? 1 : place == RP::Left ? 2 : place == RP::Right ? 3 : 0;   // Stereo and Mid listen to (L+R)/2
        return p + (sc ? 4 : 0);
    }

    void spectraOf (const std::array<std::array<float, 2 * N>, 2>& f, int base)
    {
        const float norm = 1.0f / ((float) N * 0.3183f);   // 0 dBFS sine -> 0 dB
        const float n2 = norm * norm;
        for (int k = 0; k < bins; ++k)
        {
            const float lr = f[0][(size_t) (2 * k)],     li = f[0][(size_t) (2 * k + 1)];
            const float rr = f[1][(size_t) (2 * k)],     ri = f[1][(size_t) (2 * k + 1)];
            const float mr = 0.5f * (lr + rr), mi = 0.5f * (li + ri);
            const float sr = 0.5f * (lr - rr), si = 0.5f * (li - ri);
            pw[(size_t) base + 0][(size_t) k] = (mr * mr + mi * mi) * n2;
            pw[(size_t) base + 1][(size_t) k] = (sr * sr + si * si) * n2;
            pw[(size_t) base + 2][(size_t) k] = (lr * lr + li * li) * n2;
            pw[(size_t) base + 3][(size_t) k] = (rr * rr + ri * ri) * n2;
        }
    }

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
        spectraOf (fr, 0);

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
            spectraOf (frSc, 4);
        }
        dbDone.fill (false);

        // gain accumulators (dB per bin): applied to both channels / left only / right only / mid only / side only
        enum { GSt = 0, GL, GR, GM, GS, numG };
        for (auto& g : gain) g.fill (0.0f);
        std::array<bool, numG> used {};

        const float hopSec = (float) hop / (float) fs;
        for (int b = 0; b < RP::numBands; ++b)
        {
            const auto& bc = cfg[(size_t) b];
            lvlAt[(size_t) b] = -120.0f;
            if (! bc.active || hi_[(size_t) b] < 0 || dHi[(size_t) b] < 0) continue;

            const int set = setOf (bc.place, bc.sc);
            const auto& p = pw[(size_t) set];
            if (! dbDone[(size_t) set])
            {
                for (int k = 0; k < bins; ++k) dbv[(size_t) set][(size_t) k] = 10.0f * std::log10 (p[(size_t) k] + 1.0e-18f);
                dbDone[(size_t) set] = true;
            }
            const auto& d = dbv[(size_t) set];

            // band level = power sum under the Dynamic EQ band-pass (identical to what the time-domain detector measures)
            const auto& dw = detW[(size_t) b];
            double acc = 0.0;
            for (int k = dLo[(size_t) b]; k <= dHi[(size_t) b]; ++k) acc += (double) dw[(size_t) k] * (double) p[(size_t) k];
            const float bandDb = 10.0f * std::log10 ((float) (acc / 1.2337) + 1.0e-18f);
            lvlAt[(size_t) b] = bandDb;
            const float cDb = detGain[(size_t) b];

            const float aA = 1.0f - std::exp (-hopSec / (juce::jmax (0.1f, bc.att) * 0.001f));
            const float aR = 1.0f - std::exp (-hopSec / (juce::jmax (1.0f, bc.rel) * 0.001f));
            auto& e = env[(size_t) b];
            const auto& w = weight[(size_t) b];
            const int g0 = bc.place == RP::Left ? GL : bc.place == RP::Right ? GR : bc.place == RP::Mid ? GM : bc.place == RP::Side ? GS : GSt;
            used[(size_t) g0] = true;
            float peakAmt = 0.0f;
            for (int k = lo_[(size_t) b]; k <= hi_[(size_t) b]; ++k)
            {
                // Level a bin is judged by: its own level lifted to band level units (a flat sound then reads exactly the band level,
                // like the Dynamic EQ does) but never above the band level itself (a pure tone reads its true level).
                // Bins that stick out therefore react like the Dynamic band, bins far below the band level react less.
                const float L = juce::jmin (d[(size_t) k] + cDb, bandDb);
                e[(size_t) k] += (L > e[(size_t) k] ? aA : aR) * (L - e[(size_t) k]);
                const float am = amount (e[(size_t) k], bc);
                gain[(size_t) g0][(size_t) k] += w[(size_t) k] * bc.range * am;
                if (w[(size_t) k] > 0.5f) peakAmt = juce::jmax (peakAmt, am);
            }
            dynAt[(size_t) b] = bc.range * peakAmt;   // strongest action inside the band (the peak need not be at the centre)
        }

        // smooth the gain masks across frequency (soft edges, peaks keep their full depth) so isolated bins do not produce "musical noise"
        for (int g = 0; g < numG; ++g)
        {
            if (! used[(size_t) g]) continue;
            auto& a = gain[(size_t) g];
            float prev = a[0];
            for (int k = 0; k < bins; ++k)
            {
                const float cur = a[(size_t) k], next = k + 1 < bins ? a[(size_t) k + 1] : cur;
                const float sm = 0.25f * prev + 0.5f * cur + 0.25f * next;
                a[(size_t) k] = std::abs (sm) > std::abs (cur) ? sm : cur;   // soften edges, never weaken a peak
                prev = cur;
            }
        }
        for (int k = 0; k < bins; ++k)
        {
            const float lr = std::abs (gain[GL][(size_t) k]) > std::abs (gain[GR][(size_t) k]) ? gain[GL][(size_t) k] : gain[GR][(size_t) k];
            mask[(size_t) k] = gain[GSt][(size_t) k] + lr + gain[GM][(size_t) k] + gain[GS][(size_t) k];
        }

        auto lin = [] (float g) { return std::pow (10.0f, g * 0.05f); };
        auto& XL = fr[0];
        auto& XR = fr[1];
        for (int k = 0; k < bins; ++k)
        {
            const float gl = gain[GSt][(size_t) k] + gain[GL][(size_t) k], gr = gain[GSt][(size_t) k] + gain[GR][(size_t) k];
            if (std::abs (gl) > 1.0e-4f) { const float x = lin (gl); XL[(size_t) (2 * k)] *= x; XL[(size_t) (2 * k + 1)] *= x; }
            if (std::abs (gr) > 1.0e-4f) { const float x = lin (gr); XR[(size_t) (2 * k)] *= x; XR[(size_t) (2 * k + 1)] *= x; }
            const float gm = gain[GM][(size_t) k], gs = gain[GS][(size_t) k];
            if (std::abs (gm) > 1.0e-4f || std::abs (gs) > 1.0e-4f)   // Mid / Side bands: cut the M and S parts separately
            {
                const float xm = lin (gm), xs = lin (gs);
                for (int c = 0; c < 2; ++c)
                {
                    const float lre = XL[(size_t) (2 * k + c)], rre = XR[(size_t) (2 * k + c)];
                    const float m = 0.5f * (lre + rre) * xm, sd = 0.5f * (lre - rre) * xs;
                    XL[(size_t) (2 * k + c)] = m + sd;
                    XR[(size_t) (2 * k + c)] = m - sd;
                }
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fr[(size_t) ch];
            fft.performRealOnlyInverseTransform (f.data());
            for (int i = 0; i < N; ++i)
                outBuf[(size_t) ch][(size_t) ((pos + i) & (N - 1))] += f[(size_t) i] * win[(size_t) i] * 0.5f;
        }
    }

    static float amount (float level, const BandCfg& bc)
    {
        // same curve as the time-domain Dynamic EQ: half of the range at the threshold, nothing 6 dB below it, the full range 6 dB above it
        const float d = bc.above ? (level - bc.thr) : (bc.thr - level);
        const float x = juce::jlimit (0.0f, 1.0f, d / 12.0f + 0.5f);
        return x * x * (3.0f - 2.0f * x);
    }

    juce::dsp::FFT fft;
    double fs = 44100.0;
    int pos = 0, hopCount = 0;
    std::array<float, N> win {};
    std::array<std::array<float, N>, 2> inBuf {}, inSc {}, outBuf {};
    std::array<std::array<float, 2 * N>, 2> fr {}, frSc {};
    std::array<std::array<float, bins>, 8> pw {}, dbv {};
    std::array<bool, 8> dbDone {};
    std::array<std::array<float, bins>, 5> gain {};
    std::array<std::array<float, bins>, RP::numBands> env {}, weight {}, detW {};
    std::array<int, RP::numBands> lo_ {}, hi_ {}, dLo {}, dHi {};
    std::array<float, RP::numBands> detGain {};
    std::array<float, RP::numBands> cachedFreq {}, cachedQ {};
    std::array<BandCfg, RP::numBands> cfg {};
};
