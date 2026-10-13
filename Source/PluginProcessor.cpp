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
bool RajvikEQAudioProcessor::snapshotPost (RajvikEQAudioProcessor* o, float* out)
{
    const juce::ScopedLock sl (regLock);
    if (std::find (registry.begin(), registry.end(), o) == registry.end()) return false;
    o->postAnalyzer.snapshot (out);
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
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& d : dynDb) d.store (0.0f);
    for (int b = 0; b < numBands; ++b)
    {
        auto g = [this, b] (const char* n) { return apvts.getRawParameterValue (pid (b, n)); };
        ptrs[(size_t) b] = { g ("en"), g ("type"), g ("freq"), g ("gain"), g ("q"), g ("slope"), g ("place"),
                             g ("dyn"), g ("spec"), g ("thr"), g ("range"), g ("att"), g ("rel"), g ("dir") };
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
    return in == l.getMainOutputChannelSet() && ! in.isDisabled() && in.size() >= 1 && in.size() <= 16;
}

void RajvikEQAudioProcessor::prepareToPlay (double sr, int block)
{
    conv.prepare ({ sr, (juce::uint32) block, 2 });
    os.initProcessing ((size_t) block);
    os.reset();
    spec.prepare (sr);
    for (auto& b : bands) b.reset();
    lastLinear = lastSpectral = lastNatural = false;
    updateFlags();
    irDirty = true;
    if (linearActive.load()) { rebuildIR(); irDirty = false; }
}

RajvikEQAudioProcessor::BandVals RajvikEQAudioProcessor::readBand (int b) const
{
    const auto& p = ptrs[(size_t) b];
    BandVals v;
    v.en = p.en->load() > 0.5f;   v.dyn = p.dyn->load() > 0.5f;   v.spec = p.spec->load() > 0.5f;
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
    bool sp = false;
    if (two)
        for (int b = 0; b < numBands; ++b)
        {
            const auto v = readBand (b);
            if (v.en && modeOf (v) == ModeSpectral) sp = true;
        }
    linearActive = lin;
    naturalActive = nat;
    spectralActive = sp;
    const int lat = (lin ? irLen / 2 : 0) + (nat ? (int) os.getLatencyInSamples() : 0) + (sp ? SpectralStage::N : 0);
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
            if (modeOf (v) == ModeDynamic || v.place == Mid || v.place == Side) continue;   // handled by the hybrid path
            const bool affects = v.place == Stereo || (ch == 0 && v.place == Left) || (ch == 1 && v.place == Right);
            if (! affects) continue;
            BandDesign d;
            design (d, v.type, v.freq, v.gain, v.q, v.slope, fs);
            for (int k = 0; k <= half; ++k)
            {
                const double w = detail::twoPi * (double) k / (double) N;
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

void RajvikEQAudioProcessor::runBands (float* const* chs, int nCh, int n, bool linear, double fs)
{
    constexpr int sub = 32;

    for (int b = 0; b < numBands; ++b)
    {
        const auto v = readBand (b);
        auto& s = bands[(size_t) b];
        dynDb[(size_t) b].store (0.0f);

        // In linear phase mode the FIR handles static Stereo/Left/Right bands; dynamic and Mid/Side
        // bands run here in the zero-latency path, after the FIR.
        // Spectral bands are handled by the STFT stage on stereo; on mono / surround they fall back to time-domain dynamics.
        const int kind = modeOf (v);
        const bool dynamic = kind == ModeDynamic || (kind == ModeSpectral && nCh != 2);
        const bool hybrid = dynamic || v.place == Mid || v.place == Side;
        if (! v.en || (linear && ! hybrid)) { s.wasOn = false; continue; }
        if (! s.wasOn) { s.reset(); s.wasOn = true; }

        if (! dynamic) s.appliedDyn = 0.0;
        ensureDesign (s, v, fs);

        if (dynamic && (s.keyDF != v.freq || s.keyDQ != v.q || s.keyDFs != fs))
        {
            s.det.c = detail::bandpass (juce::jlimit (5.0, fs * 0.49, (double) v.freq), juce::jmax (0.5, (double) v.q), fs);
            s.keyDF = v.freq; s.keyDQ = v.q; s.keyDFs = fs;
        }
        const double ac = std::exp (-1.0 / (0.001 * juce::jmax (0.1f, v.att) * fs));
        const double rc = std::exp (-1.0 / (0.001 * juce::jmax (1.0f, v.rel) * fs));

        auto casc = [&s] (int ch, double x) { for (int k = 0; k < s.design.n; ++k) x = s.f[ch][k].process (x); return x; };

        for (int start = 0; start < n; start += sub)
        {
            const int len = juce::jmin (sub, n - start);

            if (dynamic)
            {
                for (int i = 0; i < len; ++i)
                {
                    double in = (double) chs[0][start + i];
                    if (nCh > 1)
                    {
                        const double l = (double) chs[0][start + i], r = (double) chs[1][start + i];
                        switch (v.place)
                        {
                            case Left:  in = l; break;
                            case Right: in = r; break;
                            case Side:  in = 0.5 * (l - r); break;
                            default:    in = 0.5 * (l + r); break;   // Stereo / Mid
                        }
                    }
                    const double a = std::abs (s.det.process (in));
                    s.env = a + (a > s.env ? ac : rc) * (s.env - a);
                }
                // env follows the rectified signal (mean of |sine| = 2/pi of its peak): +3.9 dB so a 0 dBFS sine reads 0 dB
                const float lvl = 20.0f * std::log10 ((float) s.env + 1.0e-6f) + 3.92f;
                const float x = juce::jlimit (0.0f, 1.0f, (lvl - v.thr) / 12.0f + 0.5f);
                const float sm = x * x * (3.0f - 2.0f * x);
                s.appliedDyn = (double) v.range * (double) (v.dir == 0 ? sm : 1.0f - sm);
                ensureDesign (s, v, fs);
                dynDb[(size_t) b].store ((float) s.appliedDyn);
            }

            switch (v.place)
            {
                case Stereo:
                    for (int c = 0; c < nCh; ++c)
                    {
                        float* p = chs[c] + start;
                        for (int i = 0; i < len; ++i) p[i] = (float) casc (c, p[i]);
                    }
                    break;
                case Left:
                {
                    float* p = chs[0] + start;
                    for (int i = 0; i < len; ++i) p[i] = (float) casc (0, p[i]);
                    break;
                }
                case Right:
                    if (nCh > 1)
                    {
                        float* p = chs[1] + start;
                        for (int i = 0; i < len; ++i) p[i] = (float) casc (1, p[i]);
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
                            if (v.place == Mid) { const double mo = casc (0, m); l[i] = (float) (mo + sd); r[i] = (float) (mo - sd); }
                            else                { const double so = casc (1, sd); l[i] = (float) (m + so); r[i] = (float) (m - so); }
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
    const int nCh = juce::jmin (buffer.getNumChannels(), 16);
    if (nCh < 1 || n == 0) return;
    float* chs[16];
    for (int c = 0; c < nCh; ++c) chs[c] = buffer.getWritePointer (c);
    float* L = chs[0];
    float* R = nCh > 1 ? chs[1] : chs[0];

    preAnalyzer.push (L, R, n);

    const bool linear = linearActive.load() && nCh == 2;
    const bool natural = naturalActive.load() && nCh == 2;
    const bool spectral = spectralActive.load() && nCh == 2;
    if (linear != lastLinear)     { conv.reset(); for (auto& b : bands) b.reset(); lastLinear = linear; }
    if (natural != lastNatural)   { os.reset(); for (auto& b : bands) b.reset(); lastNatural = natural; }
    if (spectral != lastSpectral) { spec.reset(); lastSpectral = spectral; }

    const double fs = getSampleRate();

    if (linear)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, 2);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        conv.process (ctx);
    }

    if (natural)
    {
        // Natural phase: run the bands at 2x so the filters behave like their analog prototypes
        // (no cramping towards Nyquist), with a minimum-phase IIR resampler.
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, 2);
        auto up = os.processSamplesUp (sub);
        float* upCh[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
        runBands (upCh, 2, (int) up.getNumSamples(), false, fs * 2.0);
        os.processSamplesDown (sub);
    }
    else
    {
        runBands (chs, nCh, n, linear, fs);
    }

    if (spectral)
    {
        std::array<SpectralStage::BandCfg, numBands> cfg;
        for (int b = 0; b < numBands; ++b)
        {
            const auto v = readBand (b);
            auto& c = cfg[(size_t) b];
            c.active = v.en && modeOf (v) == ModeSpectral;
            c.above = v.dir == 0; c.freq = v.freq; c.q = v.q; c.thr = v.thr; c.range = v.range; c.att = v.att; c.rel = v.rel;
        }
        spec.setBands (cfg);
        spec.process (L, R, n);
        for (int b = 0; b < numBands; ++b)
            if (cfg[(size_t) b].active) dynDb[(size_t) b].store (spec.dynAt[(size_t) b]);
    }

    const int mode = (int) apvts.getRawParameterValue ("character")->load();
    const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out_gain")->load())
                     * (apvts.getRawParameterValue ("invert")->load() > 0.5f ? -1.0f : 1.0f);
    for (int c = 0; c < nCh; ++c)
    {
        float* d = chs[c];
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            if (mode == 1)      x = std::tanh (x);
            else if (mode == 2) x = (std::tanh (1.4f * x + 0.08f) - 0.07983f) / 1.391f;
            d[i] = x * gain;
        }
    }

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
