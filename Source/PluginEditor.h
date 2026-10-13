#pragma once
#include "PluginProcessor.h"
#include "Look.h"

class EQDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EQDisplay (RajvikEQAudioProcessor&);
    std::function<void (int)> onSelect;
    std::function<void()> onSketchDone;
    std::function<void()> onBackgroundClick;
    void selectBand (int b) { select (b); }
    int selected = 0;
    bool sketchMode = false;
    void setCapture (int mode) { capMode = mode; }
    void applyMatch();
    void showInstances();
    bool isBandEnabled (int b) const { return apvtsVal (RP::pid (b, "en")) > 0.5f; }
    juce::Point<float> getNodePos (int b) const { return nodePos (b); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float val (const juce::String& id) const { return apvtsVal (id); }
    float apvtsVal (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setReal (const juce::String& id, float v);
    void setIndex (const juce::String& id, int v);
    void gesture (const juce::String& id, bool begin);
    void select (int b);
    void showMenu (int b);
    int hitTest (juce::Point<float>) const;
    juce::Point<float> nodePos (int b) const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    float dispRange() const;
    void timerCallback() override;
    void updateSpectrum (const Analyzer&, std::array<float, 2049>&);
    void drawSpectrum (juce::Graphics&, const std::array<float, 2049>&, juce::Colour, float alpha);

    RajvikEQAudioProcessor& proc;
    bool dragging = false;
    float hoverX = -1.0f;
    int capMode = 0, refN = 0, srcN = 0;
    std::array<double, 2049> refAcc {}, srcAcc {};
    std::vector<juce::Point<float>> sketch;
    RajvikEQAudioProcessor* overlayProc = nullptr;
    std::array<float, 2049> overlaySm {};
    void finishSketch();
    void fitToTarget (std::vector<float> residual, int maxBands);
    int freeBand() const;
    void spectrumFromSnap (std::array<float, 2049>&);
    juce::dsp::FFT fft { 12 };
    juce::dsp::WindowingFunction<float> window { 4096, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2049> preSm {}, postSm {};
    std::array<float, 4096> snap {};
    std::array<float, 8192> fftData {};
};

class ScaleStrip : public juce::Component, private juce::Timer
{
public:
    explicit ScaleStrip (RajvikEQAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    RajvikEQAudioProcessor& proc;
    float lvL = 0.0f, lvR = 0.0f;
};

class BandPanel : public juce::Component
{
public:
    explicit BandPanel (RajvikEQAudioProcessor&);
    static constexpr int totalHeight = 156;

    std::function<void()> onClose;
    std::function<void (int)> onStep;

    void select (int band);
    void update();            // re-layout when the band type / dynamic state changes
    int idealWidth() const;   // component width including the soft shadow margin
    bool hitTest (int x, int y) override { return getLocalBounds().reduced (12, 10).contains (x, y); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider s;
        juce::Label l;
        void init (juce::Component& parent, const juce::String& name, bool bipolar = false);
        void place (juce::Rectangle<int> r);
    };

    int  dynMode() const;     // ModeNormal / ModeDynamic / ModeSpectral as the DSP sees it
    bool dynOn() const { return dynMode() != RP::ModeNormal; }
    bool isCut() const;
    bool canDynamic() const;  // only gain type bands can be dynamic
    void setDynMode (int mode);

    RajvikEQAudioProcessor& proc;
    int band = 0;
    int lastMode = -1;
    bool lastCut = false, lastCanDyn = true;
    juce::Label numLabel;
    juce::ToggleButton enBtn;
    juce::TextButton closeBtn, prevBtn, nextBtn;
    juce::ComboBox typeBox, slopeBox, placeBox, dirBox, dynModeBox;
    Knob freq, gain, q, thr, range, att, rel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> freqA, gainA, qA, thrA, rangeA, attA, relA;
    std::unique_ptr<CA> typeA, slopeA, placeA, dirA;
    std::unique_ptr<BA> enA;
};

class RajvikEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RajvikEQAudioProcessorEditor (RajvikEQAudioProcessor&);
    ~RajvikEQAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void placePanel();

    static constexpr int headerH = 56, bottomH = 40, stripW = 100;
    RajvikEQAudioProcessor& proc;
    RajvikLAF laf;
    EQDisplay display;
    ScaleStrip strip;
    BandPanel panel;
    bool panelOnTop = false, panelOpen = false;

    juce::ComboBox modeBox, charBox, rangeBox;
    juce::Label charL, outL;
    juce::ToggleButton analyzerBtn { "Analyzer" }, invertBtn { "Phase Invert" }, notesBtn { "Piano" };
    juce::Slider outSlider;
    juce::TextButton sketchBtn { "EQ Sketch" }, instBtn { "Instances" }, refBtn { "Match: Reference" }, srcBtn { "Match: Source" }, applyBtn { "Match: Apply" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA, charA, rangeA;
    std::unique_ptr<BA> analyzerA, invertA, notesA;
    std::unique_ptr<SA> outA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RajvikEQAudioProcessorEditor)
};
