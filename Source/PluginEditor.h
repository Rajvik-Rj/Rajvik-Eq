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
    using SpecArr = std::vector<float>;   // dBFS per FFT bin (smoothed)
    struct Spec { SpecArr lo, hi; };      // lo: chosen resolution (bass detail), hi: 2048 / 4096 point FFT (fast, same scale as the spectral stage)
    void updateSpectrum (const Analyzer&, Spec&, int delaySamples);
    void computeSpectrum (Spec&);
    void computeBins (const float* l, const float* r, int n, juce::dsp::FFT&, const std::vector<float>& window, SpecArr& out);
    void configureAnalyzer();
    void toolSpectrum();
    void buildLevels (const Spec&, std::vector<float>&) const;
    void drawLevels (juce::Graphics&, const std::vector<float>&, juce::Colour fillCol, float fillAlpha, juce::Colour lineCol, float lineAlpha, float lineW);
    void updatePeakLabels();
    void renderBackground (juce::Image&, float scale);
    double frameSignature() const;
    float spectrumDbAt (const Spec&, float x) const;                   // incl. tilt
    float binsDbAt (const float* bins, int fftSize, float x) const;   // no tilt
    float anaY (float db) const;
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
    Spec preSm, postSm, overlaySm;
    std::vector<float> lvPre, lvPost, lvOv;      // analyzer level per 2 pixel column (dB, incl. tilt), built once per frame
    std::vector<juce::Point<float>> peakPts;     // peak label positions
    juce::Image bgImage;                         // cached background + grid (rendered at the real pixel size)
    int bgKey = 0;
    float bgScale = 0.0f, tiltDb = 4.5f, rangeDb = 90.0f, anaDt = 1.0f / 40.0f;
    double lastSig = 0.0, lastAnaMs = 0.0;
    int peakTick = 0;
    std::vector<float> snapL, snapR, fftData, powBuf, winLo, winHi, toolWin;
    std::unique_ptr<juce::dsp::FFT> fftLo, fftHi;
    juce::dsp::FFT toolFft { 12 };   // fixed 4096 point FFT for Auto Peaks / EQ Match
    int specNLo = 0, specNHi = 0;    // FFT sizes of the display analyzer
};

class ScaleStrip : public juce::Component, private juce::Timer
{
public:
    explicit ScaleStrip (RajvikEQAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    RajvikEQAudioProcessor& proc;
    float lvL = 0.0f, lvR = 0.0f;
    juce::Rectangle<float> rangeRect { 8.0f, 6.0f, 58.0f, 22.0f };
    int lastKey = -1;
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
    void setGuiScale (float s);
    void showScaleMenu();
    void stepPreset (int dir);
    void promptSave();
    // undo / redo (snapshots of the whole state, recorded only while you edit) and A/B compare
    juce::String stateString() const;
    void applyState (const juce::String&);
    void trackHistory();
    void doUndo();
    void doRedo();
    void switchSlot (int s);
    void updateAB();

    static constexpr int headerH = 34, bottomH = 26, stripW = 100;
    static constexpr int minW = 720, minH = 400, maxW = 3200, maxH = 2000;   // unscaled units
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
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, slotA { "A" }, slotB { "B" }, copyBtn { "Copy" }, scaleBtn { "100%" }, nameBtn;
    juce::ComboBox anaBox;
    juce::ToggleButton pianoBtn { "Piano" };
    GearButton gearBtn;
    juce::Slider outSlider;

    std::vector<juce::String> undoStack, redoStack;
    juce::String lastSnap, abState[2];
    int abSlot = 0, histTick = 0, nameTick = 0;
    struct Activity : juce::MouseListener   // when did the user last touch the editor (so host automation is not recorded as an edit)
    {
        double last = 0.0;
        void bump() { last = juce::Time::getMillisecondCounterHiRes(); }
        void mouseDown (const juce::MouseEvent&) override { bump(); }
        void mouseDrag (const juce::MouseEvent&) override { bump(); }
        void mouseUp (const juce::MouseEvent&) override { bump(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { bump(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { bump(); }
    } activity;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA, osA;
    std::unique_ptr<BA> pianoA;
    std::unique_ptr<SA> outA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RajvikEQAudioProcessorEditor)
};
