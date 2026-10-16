#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

using namespace RP;

namespace
{
juce::CriticalSection regLock, nameLock;
std::vector<RajvikEQAudioProcessor*> registry;
}

std::vector<RajvikEQAudioProcessor*> RajvikEQAudioProcessor::getInstances()
{
    const juce::ScopedLock sl (regLock);
    return registry;
}
bool RajvikEQAudioProcessor::snapshotPost (RajvikEQAudioProcessor* o, float* outL, float* outR, int n)
{
    const juce::ScopedLock sl (regLock);
    if (std::find (registry.begin(), registry.end(), o) == registry.end()) return false;
    o->postAnalyzer.snapshot (outL, outR, juce::jlimit (1, Analyzer::size, n));
    return true;
}
bool RajvikEQAudioProcessor::copySettingsFrom (RajvikEQAudioProcessor* o)
{
    const juce::ScopedLock sl (regLock);
    if (o == this || std::find (registry.begin(), registry.end(), o) == registry.end()) return false;
    apvts.replaceState (o->apvts.copyState());
    return true;
}
juce::String RajvikEQAudioProcessor::displayName()
{
    const juce::ScopedLock sl (nameLock);
    return trackName;
}
void RajvikEQAudioProcessor::updateTrackProperties (const TrackProperties& p)
{
    const juce::ScopedLock sl (nameLock);
    if (p.name.has_value()) trackName = *p.name;
}

RajvikEQAudioProcessor::RajvikEQAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& d : dynDb) d.store (0.0f);
    for (auto& d : detDb) d.store (-120.0f);
    for (int i = 0; i < 3; ++i)   // created once (never replaced) so the editor / timer can always read them
        os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (i + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    for (int b = 0; b < numBands; ++b)
    {
        auto g = [this, b] (const char* n) { return apvts.getRawParameterValue (pid (b, n)); };
        ptrs[(size_t) b] = { g ("en"), g ("type"), g ("freq"), g ("gain"), g ("q"), g ("slope"), g ("place"),
                             g ("dyn"), g ("spec"), g ("thr"), g ("range"), g ("att"), g ("rel"), g ("dir"), g ("sc") };
    }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);
    { const juce::ScopedLock sl (regLock); registry.push_back (this); }
    startTimerHz (20);
}

RajvikEQAudioProcessor::~RajvikEQAudioProcessor()
{
    { const juce::ScopedLock sl (regLock); registry.erase (std::remove (registry.begin(), registry.end(), this), registry.end()); }
    stopTimer();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool RajvikEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet();
    if (in != l.getMainOutputChannelSet() || in.isDisabled() || in.size() < 1 || in.size() > 16) return false;
    if (l.inputBuses.size() > 1)   // optional sidechain input: mono or stereo
    {
        const auto sc = l.inputBuses.getReference (1);
        if (! sc.isDisabled() && sc.size() > 2) return false;
    }
    return true;
}

void RajvikEQAudioProcessor::prepareToPlay (double sr, int block)
{
    conv.prepare ({ sr, (juce::uint32) block, 2 });
    maxBlock = juce::jmax (1, block);
    soloBuf.setSize (16, maxBlock);
    soloBuf.clear();
    soloMix = 0.0; soloKeyBand = -1; soloKeyType = -1;
    for (auto& s : soloF) s.reset();
    for (auto& o : os)
    {
        o->initProcessing ((size_t) maxBlock);
        o->reset();
    }
    spec.prepare (sr);
    outGain.reset (sr, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out_gain")->load())
                                      * (apvts.getRawParameterValue ("invert")->load() > 0.5f ? -1.0f : 1.0f));
    for (auto& b : bands) b.reset();
    lastLinear = lastSpectral = false;
    lastStage = 0;
    updateFlags();
    irDirty = true;
    if (linearActive.load()) { rebuildIR(); irDirty = false; }
}

RajvikEQAudioProcessor::BandVals RajvikEQAudioProcessor::readBand (int b) const
{
    const auto& p = ptrs[(size_t) b];
    BandVals v;
    v.en = p.en->load() > 0.5f;   v.dyn = p.dyn->load() > 0.5f;   v.spec = p.spec->load() > 0.5f;   v.sc = p.sc->load() > 0.5f;
    v.type = (int) p.type->load(); v.slope = (int) p.slope->load(); v.place = (int) p.place->load(); v.dir = (int) p.dir->load();
    v.freq = p.freq->load(); v.gain = p.gain->load(); v.q = p.q->load(); v.thr = p.thr->load();
    v.range = p.range->load(); v.att = p.att->load(); v.rel = p.rel->load();
    return v;
}

void RajvikEQAudioProcessor::updateFlags()
{
    // Linear / Natural / Spectral need a plain stereo layout; surround and mono run in zero latency mode.
    const bool two = getMainBusNumInputChannels() == 2;
    const int mode = (int) apvts.getRawParameterValue ("mode")->load();
    const bool lin = two && mode == 2, nat = two && mode == 1;
    // oversampling: Zero Latency / Natural Phase run the bands at 2x / 4x / 8x (Natural Phase is always at least 2x)
    const int osParam = (int) apvts.getRawParameterValue ("os")->load();
    const int stage = (two && ! lin) ? juce::jmax (osParam, nat ? 1 : 0) : 0;
    bool sp = false;
    if (two)
        for (int b = 0; b < numBands; ++b)
        {
            const auto v = readBand (b);
            if (v.en && modeOf (v) == ModeSpectral) sp = true;
        }
    linearActive = lin;
    osStage = stage;
    spectralActive = sp;
    const int osLat = (stage > 0 && os[(size_t) (stage - 1)] != nullptr) ? (int) std::lround (os[(size_t) (stage - 1)]->getLatencyInSamples()) : 0;
    const int lat = (lin ? irLen / 2 : 0) + osLat + (sp ? SpectralStage::N : 0);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void RajvikEQAudioProcessor::timerCallback()
{
    updateFlags();
    if (irDirty.exchange (false) && linearActive.load())
        rebuildIR();
}

// Linear phase: build a symmetric FIR from the combined static magnitude response.
void RajvikEQAudioProcessor::rebuildIR()
{
    const double fs = getSampleRate();
    if (fs <= 0.0) return;
    constexpr int N = irLen, half = N / 2;
    juce::AudioBuffer<float> ir (2, N);
    ir.clear();
    std::vector<float> data ((size_t) (2 * N));
    std::vector<double> mag ((size_t) (half + 1));

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (mag.begin(), mag.end(), 1.0);
        for (int b = 0; b < numBands; ++b)
        {
            const auto v = readBand (b);
            if (! v.en) continue;
            if (modeOf (v) == ModeSpectral) continue;   // spectral bands never act as a static filter
            if (modeOf (v) == ModeDynamic || v.place == Mid || v.place == Side) continue;   // handled by the hybrid path
            const bool affects = v.place == Stereo || (ch == 0 && v.place == Left) || (ch == 1 && v.place == Right);
            if (! affects) continue;
            BandDesign d;
            design (d, v.type, v.freq, v.gain, v.q, v.slope, fs * 2.0);   // designed at 2x: the linear phase FIR has no cramping towards Nyquist
            for (int k = 0; k <= half; ++k)
            {
                const double w = detail::twoPi * 0.5 * (double) k / (double) N;
                double m = 1.0;
                for (int s = 0; s < d.n; ++s) m *= magSq (d.sec[s], w);
                mag[(size_t) k] *= std::sqrt (m);
            }
        }
        std::fill (data.begin(), data.end(), 0.0f);
        for (int k = 0; k <= half; ++k) data[(size_t) (2 * k)] = (float) mag[(size_t) k];
        for (int k = 1; k < half; ++k)  data[(size_t) (2 * (N - k))] = (float) mag[(size_t) k];
        irFFT.performRealOnlyInverseTransform (data.data());
        for (int n = 0; n < N; ++n)
        {
            const int idx = (n + half) % N;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) idx / (float) (N - 1));
            ir.setSample (ch, idx, data[(size_t) n] * w);
        }
    }
    conv.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
}

void RajvikEQAudioProcessor::ensureDesign (BandState& s, const BandVals& v, double fs)
{
    const double g = (double) v.gain + s.appliedDyn;
    if (s.keyType != v.type || s.keySlope != v.slope || s.keyFreq != v.freq || s.keyQ != v.q || s.keyGain != g || s.keyFs != fs)
    {
        design (s.design, v.type, v.freq, g, v.q, v.slope, fs);
        for (int ch = 0; ch < 16; ++ch)
            for (int k = 0; k < s.design.n; ++k)
                s.f[ch][k].c = s.design.sec[k];
        s.keyType = v.type; s.keySlope = v.slope; s.keyFreq = v.freq; s.keyQ = v.q; s.keyGain = g; s.keyFs = fs;
    }
}

void RajvikEQAudioProcessor::runBands (float* const* chs, int nCh, int n, bool linear, double fs, const float* scL, const float* scR)
{
    constexpr int sub = 32;
    const double glide = 1.0 - std::exp (-(double) sub / (0.012 * fs));   // parameter smoothing (about 12 ms) so moving a knob never zippers
    const double fadeStep = 1.0 / (0.005 * fs);                              // band on / off crossfade (5 ms)

    for (int b = 0; b < numBands; ++b)
    {
        auto v = readBand (b);
        auto& s = bands[(size_t) b];
        dynDb[(size_t) b].store (0.0f);
        detDb[(size_t) b].store (-120.0f);
        if (modeOf (v) == ModeSpectral) v.gain = 0.0f;   // a Spectral band carries no static EQ gain: it only does spectral processing

        // In linear phase mode the FIR handles static Stereo/Left/Right bands; dynamic and Mid/Side
        // bands run here in the zero-latency path, after the FIR.
        // Spectral bands are handled by the STFT stage on stereo; on mono / surround they fall back to time-domain dynamics.
        const int kind = modeOf (v);
        const bool dynamic = kind == ModeDynamic || (kind == ModeSpectral && nCh != 2);
        const bool hybrid = dynamic || v.place == Mid || v.place == Side;
        const bool active = v.en && ! (kind == ModeSpectral && nCh == 2) && ! (linear && ! hybrid);   // stereo Spectral bands live in the STFT stage only
        if (! active && s.mix <= 0.0) { s.wasOn = false; continue; }
        if (! s.wasOn) { s.reset(); s.mix = 0.0; s.wasOn = true; }

        if (! dynamic) s.appliedDyn = 0.0;

        auto casc = [&s] (int ch, double x) { for (int k = 0; k < s.design.n; ++k) x = s.f[ch][k].process (x); return x; };

        for (int start = 0; start < n; start += sub)
        {
            const int len = juce::jmin (sub, n - start);

            // smooth frequency / gain / Q towards their targets (log domain for frequency and Q)
            if (! s.smInit) { s.sF = v.freq; s.sG = v.gain; s.sQ = v.q; s.smInit = true; }
            else
            {
                auto glideLog = [glide] (double& cur, double tgt) { const double r = tgt / cur; if (std::abs (r - 1.0) < 1.0e-5) cur = tgt; else cur *= std::pow (r, glide); };
                glideLog (s.sF, v.freq);
                glideLog (s.sQ, juce::jmax (0.02, (double) v.q));
                if (std::abs ((double) v.gain - s.sG) < 1.0e-4) s.sG = v.gain; else s.sG += ((double) v.gain - s.sG) * glide;
            }
            v.freq = (float) s.sF; v.q = (float) s.sQ; v.gain = (float) s.sG;
            if (! dynamic) ensureDesign (s, v, fs);
            if (dynamic && (s.keyDF != v.freq || s.keyDQ != v.q || s.keyDFs != fs))
            {
                s.det.c = detail::bandpass (juce::jlimit (5.0, fs * 0.49, (double) v.freq), juce::jmax (0.1, (double) v.q), fs);
                s.keyDF = v.freq; s.keyDQ = v.q; s.keyDFs = fs;
            }

            // band on / off: short crossfade instead of a hard switch (no clicks)
            const double m0 = s.mix;
            const double m1 = active ? juce::jmin (1.0, m0 + fadeStep * len) : juce::jmax (0.0, m0 - fadeStep * len);
            s.mix = m1;
            const double dm = (m1 - m0) / (double) len;
            auto bl = [m0, dm] (double dry, double wet, int i) { return dry + (m0 + dm * (double) (i + 1)) * (wet - dry); };

            if (dynamic)
            {
                // RMS detector: band-pass -> mean square (two smoothing poles, fast enough for the band's frequency) -> level in dBFS
                // (a full scale sine reads 0 dB, noise reads its sine-equivalent level). The Spectral Dynamic stage measures the very same
                // quantity from its FFT, so one threshold value means the same thing in Dynamic and in Spectral mode.
                const double tau = juce::jlimit (0.0003, 0.03, 0.45 / juce::jmax (20.0, (double) v.freq));
                const double cs = 1.0 - std::exp (-1.0 / (tau * fs));
                for (int i = 0; i < len; ++i)
                {
                    // detector input: the main signal, or the sidechain input when this band uses it (silence if none is connected)
                    const float* d0 = v.sc ? scL : chs[0];
                    const float* d1 = v.sc ? scR : (nCh > 1 ? chs[1] : chs[0]);
                    const double l = d0 != nullptr ? (double) d0[start + i] : 0.0, r = d1 != nullptr ? (double) d1[start + i] : 0.0;
                    double in;
                    switch (v.place)
                    {
                        case Left:  in = l; break;
                        case Right: in = r; break;
                        case Side:  in = 0.5 * (l - r); break;
                        default:    in = 0.5 * (l + r); break;   // Stereo / Mid
                    }
                    const double y = s.det.process (in);
                    s.env  += cs * (y * y - s.env);
                    s.env2 += cs * (s.env - s.env2);
                }
                const double lvlInst = 10.0 * std::log10 (2.0 * s.env2 + 1.0e-12);   // sine of amplitude A: mean square A^2/2 -> A^2 -> 20 log A
                // attack / release act on the level in dB (one step per sub-block of len samples)
                const double ca = 1.0 - std::exp (-(double) len / (0.001 * juce::jmax (0.1f, v.att) * fs));
                const double cr = 1.0 - std::exp (-(double) len / (0.001 * juce::jmax (1.0f, v.rel) * fs));
                s.envDb += (lvlInst > s.envDb ? ca : cr) * (lvlInst - s.envDb);
                const float lvl = (float) s.envDb;
                const float x = juce::jlimit (0.0f, 1.0f, (lvl - v.thr) / 12.0f + 0.5f);
                const float sm = x * x * (3.0f - 2.0f * x);
                s.appliedDyn = (double) v.range * (double) (v.dir == 0 ? sm : 1.0f - sm);
                ensureDesign (s, v, fs);
                dynDb[(size_t) b].store ((float) s.appliedDyn);
                detDb[(size_t) b].store (lvl);
            }

            switch (v.place)
            {
                case Stereo:
                    for (int c = 0; c < nCh; ++c)
                    {
                        float* p = chs[c] + start;
                        for (int i = 0; i < len; ++i) p[i] = (float) bl (p[i], casc (c, p[i]), i);
                    }
                    break;
                case Left:
                {
                    float* p = chs[0] + start;
                    for (int i = 0; i < len; ++i) p[i] = (float) bl (p[i], casc (0, p[i]), i);
                    break;
                }
                case Right:
                    if (nCh > 1)
                    {
                        float* p = chs[1] + start;
                        for (int i = 0; i < len; ++i) p[i] = (float) bl (p[i], casc (1, p[i]), i);
                    }
                    break;
                case Mid:
                case Side:
                    if (nCh > 1)
                    {
                        float* l = chs[0] + start;
                        float* r = chs[1] + start;
                        for (int i = 0; i < len; ++i)
                        {
                            const double m = 0.5 * ((double) l[i] + r[i]), sd = 0.5 * ((double) l[i] - r[i]);
                            if (v.place == Mid) { const double mo = bl (m, casc (0, m), i); l[i] = (float) (mo + sd); r[i] = (float) (mo - sd); }
                            else                { const double so = bl (sd, casc (1, sd), i); l[i] = (float) (m + so); r[i] = (float) (m - so); }
                        }
                    }
                    break;
                default: break;
            }
        }
    }
}

void RajvikEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = juce::jmin (buffer.getNumChannels(), getMainBusNumInputChannels(), 16);   // main bus only (the sidechain channels follow it)
    if (nCh < 1 || n == 0) return;

    const float* scL = nullptr;
    const float* scR = nullptr;
    if (auto* sb = getBus (true, 1))
        if (sb->isEnabled() && sb->getNumberOfChannels() > 0)
        {
            const int idx = getChannelIndexInProcessBlockBuffer (true, 1, 0);
            if (idx >= 0 && idx < buffer.getNumChannels())
            {
                scL = buffer.getReadPointer (idx);
                scR = (sb->getNumberOfChannels() > 1 && idx + 1 < buffer.getNumChannels()) ? buffer.getReadPointer (idx + 1) : scL;
            }
        }
    float* chs[16];
    for (int c = 0; c < nCh; ++c) chs[c] = buffer.getWritePointer (c);
    float* L = chs[0];
    float* R = nCh > 1 ? chs[1] : chs[0];

    preAnalyzer.push (L, R, n);

    // ---- band solo: listen to the region of one band (band-pass at its frequency / Q, low-pass for low shelf / cut, high-pass for high shelf / cut) ----
    const int soloIdx = soloBand.load();
    const bool soloOn = soloIdx >= 0 && soloIdx < numBands && readBand (soloIdx).en;
    const bool soloWork = soloOn || soloMix > 0.0;   // keep going while it fades out
    if (soloWork)
    {
        if (soloBuf.getNumSamples() < n) soloBuf.setSize (16, n, false, false, true);
        for (int c = 0; c < nCh; ++c) soloBuf.copyFrom (c, 0, chs[c], n);   // the input, before any EQ
        if (soloOn)
        {
            const auto sv = readBand (soloIdx);
            const double sf = juce::jlimit (5.0, getSampleRate() * 0.49, (double) sv.freq), sq = juce::jmax (0.1, (double) sv.q);
            if (soloKeyBand != soloIdx) { for (auto& s : soloF) s.reset(); soloKeyBand = soloIdx; soloKeyType = -1; }
            if (soloKeyType != sv.type || soloKeyF != sf || soloKeyQ != sq)
            {
                const bool low = sv.type == LowShelf || sv.type == LowCut, high = sv.type == HighShelf || sv.type == HighCut;
                const auto sc = low ? detail::lp2 (sf, 0.7071, getSampleRate()) : high ? detail::hp2 (sf, 0.7071, getSampleRate()) : detail::bandpass (sf, sq, getSampleRate());
                for (auto& s : soloF) s.c = sc;
                soloKeyType = sv.type; soloKeyF = sf; soloKeyQ = sq;
            }
            soloPlace = sv.place;
        }
        float* sb[16];
        for (int c = 0; c < nCh; ++c) sb[c] = soloBuf.getWritePointer (c);
        if (nCh > 1 && (soloPlace == Left || soloPlace == Right))
        {
            for (int i = 0; i < n; ++i) { const float x = (float) soloF[0].process ((double) (soloPlace == Left ? sb[0][i] : sb[1][i])); sb[0][i] = x; sb[1][i] = x; }
        }
        else if (nCh > 1 && soloPlace == Mid)
        {
            for (int i = 0; i < n; ++i) { const float x = (float) soloF[0].process (0.5 * ((double) sb[0][i] + (double) sb[1][i])); sb[0][i] = x; sb[1][i] = x; }
        }
        else if (nCh > 1 && soloPlace == Side)
        {
            for (int i = 0; i < n; ++i) { const float x = (float) soloF[1].process (0.5 * ((double) sb[0][i] - (double) sb[1][i])); sb[0][i] = x; sb[1][i] = -x; }
        }
        else
        {
            for (int c = 0; c < nCh; ++c)
                for (int i = 0; i < n; ++i) sb[c][i] = (float) soloF[c].process ((double) sb[c][i]);
        }
    }

    const bool linear = linearActive.load() && nCh == 2;
    const int stage = nCh == 2 ? osStage.load() : 0;
    const bool spectral = spectralActive.load() && nCh == 2;
    if (linear != lastLinear)     { conv.reset(); for (auto& b : bands) b.reset(); lastLinear = linear; }
    if (stage != lastStage)       { for (auto& o : os) if (o != nullptr) o->reset(); for (auto& b : bands) b.reset(); lastStage = stage; }
    if (spectral != lastSpectral) { spec.reset(); lastSpectral = spectral; }

    const double fs = getSampleRate();

    if (linear)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, 2);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        conv.process (ctx);
    }

    if (stage > 0 && os[(size_t) (stage - 1)] != nullptr)
    {
        // Oversampled bands (2x / 4x / 8x): the filters behave like their analog prototypes (no cramping towards Nyquist),
        // resampled with a minimum-phase IIR half-band chain (lowest latency).
        auto& o = *os[(size_t) (stage - 1)];
        const int factor = 1 << stage;
        juce::dsp::AudioBlock<float> block (buffer);
        auto stereo = block.getSubsetChannelBlock (0, 2);
        for (int start = 0; start < n; start += maxBlock)   // never hand the resampler more than the prepared block size
        {
            const int len = juce::jmin (maxBlock, n - start);
            auto sub = stereo.getSubBlock ((size_t) start, (size_t) len);
            auto up = o.processSamplesUp (sub);
            float* upCh[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
            const int upN = (int) up.getNumSamples();
            const float* upL = nullptr;
            const float* upR = nullptr;
            if (scL != nullptr)   // sidechain to the oversampled rate (linear interpolation)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    if ((int) scUp[ch].size() < upN) scUp[ch].resize ((size_t) upN);
                    const float* src = (ch == 0 ? scL : scR) + start;
                    for (int i = 0; i < len; ++i)
                    {
                        const float cur = src[i];
                        for (int j = 0; j < factor; ++j)
                            scUp[ch][(size_t) (i * factor + j)] = scPrev[ch] + (cur - scPrev[ch]) * (float) (j + 1) / (float) factor;
                        scPrev[ch] = cur;
                    }
                }
                upL = scUp[0].data(); upR = scUp[1].data();
            }
            runBands (upCh, 2, upN, false, fs * (double) factor, upL, upR);
            o.processSamplesDown (sub);
        }
    }
    else
    {
        runBands (chs, nCh, n, linear, fs, scL, scR);
    }

    if (spectral)
    {
        std::array<SpectralStage::BandCfg, numBands> cfg;
        for (int b = 0; b < numBands; ++b)
        {
            const auto v = readBand (b);
            auto& c = cfg[(size_t) b];
            c.active = v.en && modeOf (v) == ModeSpectral;
            c.sc = v.sc; c.place = v.place; c.above = v.dir == 0; c.freq = v.freq; c.q = v.q; c.thr = v.thr; c.range = v.range; c.att = v.att; c.rel = v.rel;
        }
        spec.setBands (cfg);
        spec.process (L, R, scL, scR, n);
        for (int b = 0; b < numBands; ++b)
            if (cfg[(size_t) b].active) { dynDb[(size_t) b].store (spec.dynAt[(size_t) b]); detDb[(size_t) b].store (spec.lvlAt[(size_t) b]); }
    }

    if (soloWork)   // crossfade to / from the solo signal (10 ms)
    {
        const double target = soloOn ? 1.0 : 0.0, step = 1.0 / (0.010 * getSampleRate());
        for (int i = 0; i < n; ++i)
        {
            soloMix = target > soloMix ? juce::jmin (target, soloMix + step) : juce::jmax (target, soloMix - step);
            for (int c = 0; c < nCh; ++c) chs[c][i] += (float) soloMix * (soloBuf.getReadPointer (c)[i] - chs[c][i]);
        }
    }

    const int mode = (int) apvts.getRawParameterValue ("character")->load();
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out_gain")->load())
                            * (apvts.getRawParameterValue ("invert")->load() > 0.5f ? -1.0f : 1.0f));
    if (outGain.isSmoothing() || mode != 0 || std::abs (outGain.getTargetValue() - 1.0f) > 1.0e-6f)
    {
        for (int i = 0; i < n; ++i)   // one smoothed gain per sample, shared by all channels (no zipper noise, click free phase invert)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < nCh; ++c)
            {
                float x = chs[c][i];
                if (mode == 1)      x = std::tanh (x);
                else if (mode == 2) x = (std::tanh (1.4f * x + 0.08f) - 0.07983f) / 1.391f;
                chs[c][i] = x * g;
            }
        }
    }
    else outGain.skip (n);

    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i) { pkL = juce::jmax (pkL, std::abs (L[i])); pkR = juce::jmax (pkR, std::abs (R[i])); }
    if (pkL > peakL.load()) peakL.store (pkL);
    if (pkR > peakR.load()) peakR.store (pkR);

    postAnalyzer.push (L, R, n);
}

juce::AudioProcessorEditor* RajvikEQAudioProcessor::createEditor() { return new RajvikEQAudioProcessorEditor (*this); }

void RajvikEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RajvikEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RajvikEQAudioProcessor(); }
