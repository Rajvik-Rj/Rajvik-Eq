#pragma once
#include "Common.h"
#include "Params.h"
#include "DSP.h"
#include "SpectralStage.h"

// Keeps the last 16384 samples of both channels (the editor analyzes L and R separately and averages their power).
class Analyzer
{
public:
    static constexpr int size = 16384;
    void push (const float* l, const float* r, int n)
    {
        int w = wpos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { ringL[(size_t) w] = l[i]; ringR[(size_t) w] = r[i]; w = (w + 1) & (size - 1); }
        wpos.store (w, std::memory_order_release);
    }
    // the newest n samples (n <= size), oldest first
    void snapshot (float* outL, float* outR, int n) const
    {
        const int w = wpos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) ((w - n + i) & (size - 1));
            outL[i] = ringL[idx];
            outR[i] = ringR[idx];
        }
    }
private:
    std::array<float, size> ringL {}, ringR {};
    std::atomic<int> wpos { 0 };
};

class RajvikEQAudioProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::Timer
{
public:
    RajvikEQAudioProcessor();
    ~RajvikEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "RAJVIK Pro EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // instance list (all RAJVIK Pro EQ instances living in this process)
    static std::vector<RajvikEQAudioProcessor*> getInstances();
    static bool snapshotPost (RajvikEQAudioProcessor* other, float* outL, float* outR, int n);
    bool copySettingsFrom (RajvikEQAudioProcessor* other);
    juce::String displayName();
    void updateTrackProperties (const TrackProperties& p) override;

    const SpectralStage& spectralStage() const { return spec; }
    // sample rate multiplier the bands are really designed at (oversampling / linear phase), so the display matches what you hear
    int responseRateFactor() const { return linearActive.load() ? 2 : (1 << osStage.load()); }

    juce::AudioProcessorValueTreeState apvts;
    Analyzer preAnalyzer, postAnalyzer;
    std::array<std::atomic<float>, RP::numBands> dynDb;
    std::array<std::atomic<float>, RP::numBands> detDb;   // live detector level (dBFS) of every dynamic / spectral band, for the threshold display
    std::atomic<int> soloBand { -1 };   // band being soloed (-1 = none); not saved with the project, set from the editor
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };   // output peaks for the level meters

private:
    struct BandVals
    {
        bool en, dyn, spec, sc;
        int type, slope, place, dir;
        float freq, gain, q, thr, range, att, rel;
        int relmode;
        float relamt, relf, relq, onset;
    };
    struct BandPtrs { std::atomic<float> *en, *type, *freq, *gain, *q, *slope, *place, *dyn, *spec, *thr, *range, *att, *rel, *dir, *sc,
                                         *relamt, *relmode, *relf, *relq, *onset; };
    struct BandState
    {
        RP::BandDesign design;
        RP::Biquad f[16][RP::maxSections];
        RP::BiquadDD fd[16][RP::maxSections];   // same filters with double-double state (117-bit mode)
        RP::Biquad det, relDet;
        double renv = 0.0, renv2 = 0.0, fastE = 0.0, slowE = 0.0, keyRF = 0.0, keyRQ = 0.0, keyRFs = 0.0;
        double env = 0.0, env2 = 0.0, envDb = -120.0, appliedDyn = 0.0, sF = 0.0, sG = 0.0, sQ = 1.0, mix = 0.0;
        bool smInit = false;
        int keyType = -1, keySlope = -1;
        double keyFreq = 0, keyGain = 1e9, keyQ = 0, keyDF = 0, keyDQ = 0, keyFs = 0, keyDFs = 0;
        bool wasOn = false;
        void reset()
        {
            for (auto& ch : f) for (auto& s : ch) s.reset();
            for (auto& ch : fd) for (auto& s : ch) s.reset();
            det.reset(); relDet.reset(); renv = renv2 = fastE = slowE = 0.0; keyRF = 0.0; env = 0.0; env2 = 0.0; envDb = -120.0; appliedDyn = 0.0; keyType = -1; keyDF = 0; smInit = false;
        }
    };

    int wantedIrLen() const;
    int wantedSpecOrder() const;   // FFT size (log2) of the Spectral Dynamic stage: depends on the processing mode   // FIR length of the Linear / Mixed Phase modes (latency = half of it)
    juce::dsp::FFT& irFFTFor (int n) { return n <= 2048 ? irFFT11 : (n <= 4096 ? irFFT12 : irFFT13); }

    BandVals readBand (int b) const;
    void ensureDesign (BandState&, const BandVals&, double fs);
    int modeOf (const BandVals& v) const { return RP::dynModeOf (v.dyn, v.spec, v.type); }
    void runBands (float* const* chs, int nCh, int n, bool linear, double fs, const float* scL, const float* scR);
    void rebuildIR();
    void updateFlags();
    void parameterChanged (const juce::String&, float) override { irDirty = true; }
    void timerCallback() override;

    std::array<BandPtrs, RP::numBands> ptrs;
    std::array<BandState, RP::numBands> bands;
    SpectralStage spec;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain { 1.0f };
    std::vector<float> scUp[2];       // sidechain upsampled for Natural Phase mode
    float scPrev[2] { 0.0f, 0.0f };
    juce::dsp::Convolution conv { juce::dsp::Convolution::Latency { 0 } };
    juce::dsp::FFT irFFT11 { 11 }, irFFT12 { 12 }, irFFT13 { 13 };

    std::atomic<bool> irDirty { true }, linearActive { false }, mixedActive { false }, spectralActive { false };
    bool cur117 = false, lastP117 = false;   // 117-bit (double-double) filter state
    std::atomic<int> osStage { 0 };   // 0 = off, 1..3 = 2x..8x
    bool lastLinear = false, lastSpectral = false;
    int lastStage = 0;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> os;   // 2x, 4x, 8x
    int maxBlock = 512;
    RP::Biquad soloF[16];
    juce::AudioBuffer<float> soloBuf;
    double soloMix = 0.0, soloKeyF = 0.0, soloKeyQ = 0.0;
    int soloKeyType = -1, soloKeyBand = -1, soloPlace = 0;
    juce::String trackName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RajvikEQAudioProcessor)
};
