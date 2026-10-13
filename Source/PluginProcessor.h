#pragma once
#include "Common.h"
#include "Params.h"
#include "DSP.h"
#include "SpectralStage.h"

class Analyzer
{
public:
    static constexpr int size = 4096;
    void push (const float* l, const float* r, int n)
    {
        int w = wpos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { ring[(size_t) w] = 0.5f * (l[i] + r[i]); w = (w + 1) & (size - 1); }
        wpos.store (w, std::memory_order_release);
    }
    void snapshot (float* out) const
    {
        const int w = wpos.load (std::memory_order_acquire);
        for (int i = 0; i < size; ++i) out[i] = ring[(size_t) ((w + i) & (size - 1))];
    }
private:
    std::array<float, size> ring {};
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
    const juce::String getName() const override { return "Rajvik EQ"; }
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

    // instance list (all Rajvik EQ instances living in this process)
    static std::vector<RajvikEQAudioProcessor*> getInstances();
    static bool snapshotPost (RajvikEQAudioProcessor* other, float* out);
    bool copySettingsFrom (RajvikEQAudioProcessor* other);
    juce::String displayName();
    void updateTrackProperties (const TrackProperties& p) override;

    juce::AudioProcessorValueTreeState apvts;
    Analyzer preAnalyzer, postAnalyzer;
    std::array<std::atomic<float>, RP::numBands> dynDb;
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };   // output peaks for the level meters

private:
    struct BandVals
    {
        bool en, dyn, spec;
        int type, slope, place, dir;
        float freq, gain, q, thr, range, att, rel;
    };
    struct BandPtrs { std::atomic<float> *en, *type, *freq, *gain, *q, *slope, *place, *dyn, *spec, *thr, *range, *att, *rel, *dir; };
    struct BandState
    {
        RP::BandDesign design;
        RP::Biquad f[16][RP::maxSections];
        RP::Biquad det;
        double env = 0.0, appliedDyn = 0.0;
        int keyType = -1, keySlope = -1;
        double keyFreq = 0, keyGain = 1e9, keyQ = 0, keyDF = 0, keyDQ = 0, keyFs = 0, keyDFs = 0;
        bool wasOn = false;
        void reset()
        {
            for (auto& ch : f) for (auto& s : ch) s.reset();
            det.reset(); env = 0.0; appliedDyn = 0.0; keyType = -1; keyDF = 0;
        }
    };

    static constexpr int irLen = 8192;

    BandVals readBand (int b) const;
    void ensureDesign (BandState&, const BandVals&, double fs);
    int modeOf (const BandVals& v) const { return RP::dynModeOf (v.dyn, v.spec, v.type); }
    void runBands (float* const* chs, int nCh, int n, bool linear, double fs);
    void rebuildIR();
    void updateFlags();
    void parameterChanged (const juce::String&, float) override { irDirty = true; }
    void timerCallback() override;

    std::array<BandPtrs, RP::numBands> ptrs;
    std::array<BandState, RP::numBands> bands;
    SpectralStage spec;
    juce::dsp::Convolution conv { juce::dsp::Convolution::Latency { 0 } };
    juce::dsp::FFT irFFT { 13 };

    std::atomic<bool> irDirty { true }, linearActive { false }, spectralActive { false }, naturalActive { false };
    bool lastLinear = false, lastSpectral = false, lastNatural = false;
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::String trackName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RajvikEQAudioProcessor)
};
