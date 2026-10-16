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
    std::function<void()> onMultiSelect;   // several bands selected: the band panel switches to group (relative) editing
    void selectBand (int b) { select (b); }
    int selected = 0;
    bool sketchMode = false;
    void setCapture (int mode) { capMode = mode; }
    int getCapture() const { return capMode; }
    void applyMatch();
    void startPeakSearch() { peakListen = peakListen > 0 ? 0 : 75; peakAcc.fill (0.0); peakN = 0; }
    bool isListeningForPeaks() const { return peakListen > 0; }
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
    bool keyPressed (const juce::KeyPress&) override;

    // multi selection (marquee / shift-click / Ctrl+A): bands move, change and delete together
    std::vector<int> groupBands() const;
    std::array<bool, RP::numBands> selSet {};

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
    // Two resolutions per analyzer: 'lo' = the chosen FFT size (fine bass detail), 'hi' = a 4x shorter FFT (fast, reacts to
    // the sound immediately in mids / highs). They are cross-faded by frequency, like a multi-resolution analyzer.
    struct Spec { std::vector<float> lo, hi; std::vector<double> cLo, cHi; };   // dB per bin + running sums of linear power
    using SpecArr = Spec;
    void updateSpectrum (const Analyzer&, SpecArr&, float dt);
    void computeSpectrum (SpecArr&, float dt);
    void initSpec (SpecArr&) const;
    float fastWeight (float freq) const;
    void paintGrid (juce::Graphics&);
    void paintCurves (juce::Graphics&);
    std::vector<float> gridKeyNow (float scale) const;
    std::vector<float> curveKeyNow (float scale) const;
    void drawCached (juce::Graphics&, juce::Image&, std::vector<float>& cachedKey, const std::vector<float>& key, float scale, bool curves);
    void configureAnalyzer();
    void toolSpectrum();
    void drawSpectrum (juce::Graphics&, const SpecArr&, juce::Colour, float alpha, bool filled);
    float spectrumDbAt (const SpecArr&, float x) const;           // incl. tilt
    float binsDbAt (const std::vector<float>& bins, const std::vector<double>& cum, int fftSize, float x) const;   // no tilt
    float anaRange() const;
    float anaTilt() const;

    RajvikEQAudioProcessor& proc;
    bool dragging = false, marqueeing = false;
    juce::Point<float> mqStart, dragDown;
    juce::Rectangle<float> mqRect;
    std::vector<int> dragGroup;
    std::array<float, RP::numBands> dragF {}, dragG {};
    int pendingSingle = -1;
    int peakListen = 0, peakN = 0;
    std::array<double, 2049> peakAcc {};
    void finishPeakSearch();
    void toggleSel (int b);
    void finishMarquee (bool additive);
    int kindOf (int b) const;
    float hoverX = -1.0f, hoverY = -1.0f;
    int capMode = 0, refN = 0, srcN = 0;
    std::array<double, 2049> refAcc {}, srcAcc {};
    std::vector<juce::Point<float>> sketch;
    RajvikEQAudioProcessor* overlayProc = nullptr;
    void finishSketch();
    void fitToTarget (std::vector<float> residual, int maxBands);
    int freeBand() const;
    SpecArr preSm, postSm, overlaySm;
    std::vector<float> snapL, snapR, fftData, powBuf, win, toolWin;
    std::unique_ptr<juce::dsp::FFT> fft, fftFast;
    std::vector<float> winFast, powFast;
    int fastN = 1024;
    double lastTick = 0.0;
    juce::Image gridImg, curveImg;                 // cached static layers (grid, EQ curves): rebuilt only when something changes
    std::vector<float> gridKey, curveKey;
    juce::dsp::FFT toolFft { 12 };   // fixed 4096 point FFT for Auto Peaks / EQ Match
    int specN = 0;                   // FFT size of the display analyzer (2048 .. 16384)
};

class ScaleStrip : public juce::Component, private juce::Timer
{
public:
    explicit ScaleStrip (RajvikEQAudioProcessor& p) : proc (p) { startTimerHz (60); }
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

    void select (int band);                         // edit one band
    void setGroup (const std::vector<int>& bands);  // edit several bands at once: knobs are relative (+/- dB, shift in octaves, x factors)
    bool isGroupMode() const { return groupMode; }
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

    enum { KFreq, KGain, KQ, KThr, KRange, KAtt, KRel, numKnobs };
    Knob& knobRef (int k) { Knob* a[] = { &freq, &gain, &q, &thr, &range, &att, &rel }; return *a[k]; }
    void configureKnobsForGroup();
    void refreshGroupControls();
    void groupKnobChanged (int k);
    void groupGesture (int k, bool begin);
    void setParam (int b, const char* name, float v);
    std::vector<int> targets() const;

    int  dynMode() const;     // ModeNormal / ModeDynamic / ModeSpectral as the DSP sees it
    bool dynOn() const { return groupMode ? groupAnyDyn : dynMode() != RP::ModeNormal; }
    bool isCut() const;
    bool canDynamic() const;  // only gain type bands can be dynamic
    void setDynMode (int mode);

    RajvikEQAudioProcessor& proc;
    int band = 0;
    bool groupMode = false, groupAnyDyn = false;
    int groupSig = -1;
    std::vector<int> group;
    double lastVal[numKnobs] {};
    int lastMode = -1;
    bool lastCut = false, lastCanDyn = true;
    juce::Label numLabel;
    juce::ToggleButton enBtn, scBtn { "Sidechain" };
    juce::TextButton closeBtn, prevBtn, nextBtn, soloBtn { "Solo" };
    juce::ComboBox typeBox, slopeBox, placeBox, dirBox, dynModeBox;
    Knob freq, gain, q, thr, range, att, rel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> freqA, gainA, qA, thrA, rangeA, attA, relA;
    std::unique_ptr<CA> typeA, slopeA, placeA, dirA;
    std::unique_ptr<BA> enA, scA;
};

// Gear icon button for the settings panel
class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("settings") { setTooltip ("Settings: GUI colour, size and display options"); }
    void paintButton (juce::Graphics& g, bool hl, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (hl || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.08f)); g.fillRoundedRectangle (r, 6.0f); }
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 4.0f;
        juce::Path teeth;
        for (int i = 0; i < 8; ++i)
        {
            juce::Path t;
            t.addRoundedRectangle (-rad * 0.18f, -rad, rad * 0.36f, rad * 0.40f, 1.0f);
            teeth.addPath (t, juce::AffineTransform::rotation ((float) i * juce::MathConstants<float>::twoPi / 8.0f).translated (c.x, c.y));
        }
        g.setColour (hl || down ? Look::gold : Look::text);
        g.fillPath (teeth);
        const float rr = rad * 0.58f;
        g.drawEllipse (c.x - rr, c.y - rr, 2.0f * rr, 2.0f * rr, rad * 0.40f);
    }
};

// Everything of the editor lives in this view so the whole GUI can be scaled (Settings > GUI size)
// independently of the host, while the window itself stays freely resizable.
class RootView : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> paintFn;
    std::function<void()> resizedFn;
    void paint (juce::Graphics& g) override { if (paintFn) paintFn (g); }
    void resized() override { if (resizedFn) resizedFn(); }
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
    void paintRoot (juce::Graphics&);
    void layoutRoot();
    void placePanel();
    void showPresetMenu();
    void showToolsMenu();
    void showSettings();
    void applyLook();
    void resetSize();
    void scaleChanged (float oldScale);
    void stepPreset (int dir);
    void promptSave();

    static constexpr int headerH = 34, bottomH = 26, stripW = 100;
    static constexpr int minW = 640, minH = 400, maxW = 3200, maxH = 2000;   // unscaled units
    RajvikEQAudioProcessor& proc;
    RajvikLAF laf;
    RootView root;
    EQDisplay display;
    ScaleStrip strip;
    BandPanel panel;
    bool panelOnTop = false, panelOpen = false;

    juce::ComboBox modeBox, osBox;
    juce::Label outL, osL;
    juce::TextButton presetBtn { "Init" }, presetPrev { "<" }, presetNext { ">" }, presetSave { "Save" }, toolsBtn;
    GearButton gearBtn;
    juce::ToggleButton analyzerBtn { "Analyzer" }, pianoBtn { "Piano" };
    juce::Slider outSlider;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA, osA;
    std::unique_ptr<BA> analyzerA, pianoA;
    std::unique_ptr<SA> outA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RajvikEQAudioProcessorEditor)
};
