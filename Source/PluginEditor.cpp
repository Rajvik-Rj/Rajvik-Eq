#include "PluginEditor.h"
#include "Presets.h"

using namespace RP;

namespace
{
juce::Colour bandColour (int b)
{
    return juce::Colour::fromHSV (std::fmod (0.08f + (float) b * 0.381966f, 1.0f), 0.65f, 0.95f, 1.0f);
}
}

//==============================================================================
EQDisplay::EQDisplay (RajvikEQAudioProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    toolWin.resize (4096);
    for (int i = 0; i < 4096; ++i) toolWin[(size_t) i] = 1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / 4095.0f);
    configureAnalyzer();
    setOpaque (true);   // the display paints its whole area itself (no parent repaint behind it every frame)
    startTimerHz (40);
}

float EQDisplay::dispRange() const
{
    static const float r[] = { 3.0f, 6.0f, 12.0f, 30.0f };
    return r[juce::jlimit (0, 3, (int) apvtsVal ("disp_range"))];
}
float EQDisplay::anaRange() const
{
    static const float r[] = { 60.0f, 90.0f, 120.0f };
    return r[juce::jlimit (0, 2, (int) apvtsVal ("ana_range"))];
}
float EQDisplay::anaTilt() const
{
    static const float t[] = { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };
    return t[juce::jlimit (0, 4, (int) apvtsVal ("ana_tilt"))];
}
float EQDisplay::anaY (float db) const
{
    return (float) getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, (db + rangeDb) / rangeDb));   // 0 dBFS at the top, -range at the bottom
}
float EQDisplay::freqToX (float f) const { return (float) getWidth() * std::log (juce::jmax (f, 10.0f) / 10.0f) / std::log (2000.0f); }
float EQDisplay::xToFreq (float x) const { return 10.0f * std::pow (2000.0f, x / (float) getWidth()); }
float EQDisplay::gainToY (float g) const { return (float) getHeight() * 0.5f * (1.0f - g / dispRange()); }
float EQDisplay::yToGain (float y) const { return (1.0f - 2.0f * y / (float) getHeight()) * dispRange(); }

void EQDisplay::setReal (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
void EQDisplay::setIndex (const juce::String& id, int v) { setReal (id, (float) v); }
void EQDisplay::gesture (const juce::String& id, bool begin)
{
    if (auto* p = proc.apvts.getParameter (id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

int EQDisplay::kindOf (int b) const
{
    return dynModeOf (apvtsVal (pid (b, "dyn")) > 0.5f, apvtsVal (pid (b, "spec")) > 0.5f, (int) apvtsVal (pid (b, "type")));
}

std::vector<int> EQDisplay::groupBands() const
{
    std::vector<int> g;
    for (int b = 0; b < numBands; ++b)
        if (selSet[(size_t) b] && apvtsVal (pid (b, "en")) > 0.5f) g.push_back (b);
    if (g.empty() && apvtsVal (pid (selected, "en")) > 0.5f) g.push_back (selected);
    return g;
}

void EQDisplay::toggleSel (int b)
{
    if (groupBands().size() == 1 && ! selSet[(size_t) selected]) selSet[(size_t) selected] = true;   // the lone selected band joins the group
    selSet[(size_t) b] = ! selSet[(size_t) b];
    const auto g = groupBands();
    if (selSet[(size_t) b]) selected = b;
    else if (! g.empty()) selected = g.front();
    if (g.size() == 1) { if (onSelect) onSelect (selected); }
    else if (g.size() > 1) { if (onMultiSelect) onMultiSelect(); }   // the band panel edits the whole group (relative changes)
    else if (onBackgroundClick) onBackgroundClick();
    repaint();
}

void EQDisplay::select (int b)
{
    selSet.fill (false);
    selSet[(size_t) b] = true;
    selected = b;
    if (onSelect) onSelect (b);
    repaint();
}

juce::Point<float> EQDisplay::nodePos (int b) const
{
    const int type = (int) apvtsVal (pid (b, "type"));
    const float g = (typeHasGain (type) && kindOf (b) != ModeSpectral) ? apvtsVal (pid (b, "gain")) : 0.0f;   // spectral nodes sit on the 0 dB line
    return { freqToX (apvtsVal (pid (b, "freq"))), gainToY (g) };
}

int EQDisplay::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 11.0f;
    for (int b = 0; b < numBands; ++b)
    {
        if (apvtsVal (pid (b, "en")) < 0.5f) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestD || (d == bestD && b == selected)) { bestD = d; best = b; }
    }
    return best;
}

void EQDisplay::configureAnalyzer()
{
    static const int lo[] = { 2048, 4096, 8192, 16384 }, hi[] = { 2048, 2048, 2048, 4096 };
    const int idx = juce::jlimit (0, 3, (int) apvtsVal ("ana_res"));
    if (lo[idx] == specNLo && hi[idx] == specNHi) return;
    specNLo = lo[idx];
    specNHi = hi[idx];
    auto orderOf = [] (int n) { int o = 0; while ((1 << o) < n) ++o; return o; };
    fftLo = std::make_unique<juce::dsp::FFT> (orderOf (specNLo));
    fftHi = std::make_unique<juce::dsp::FFT> (orderOf (specNHi));
    auto hann = [] (std::vector<float>& w, int n)
    {
        w.resize ((size_t) n);
        for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
    };
    hann (winLo, specNLo);
    hann (winHi, specNHi);
    snapL.assign (16384, 0.0f);
    snapR.assign (16384, 0.0f);
    fftData.assign (32768, 0.0f);
    powBuf.assign (8193, 0.0f);
    for (Spec* s : { &preSm, &postSm, &overlaySm }) { s->lo.assign (8193, -120.0f); s->hi.assign (8193, -120.0f); }
}

// Fixed 4096 point magnitude spectrum of the input (mono sum) for Auto Peaks and EQ Match: fftData[0..2048]
void EQDisplay::toolSpectrum()
{
    proc.preAnalyzer.snapshot (snapL.data(), snapR.data(), 4096);
    for (int i = 0; i < 4096; ++i) fftData[(size_t) i] = 0.5f * (snapL[(size_t) i] + snapR[(size_t) i]) * toolWin[(size_t) i];
    std::fill (fftData.begin() + 4096, fftData.begin() + 8192, 0.0f);
    toolFft.performFrequencyOnlyForwardTransform (fftData.data());
}

void EQDisplay::timerCallback()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    anaDt = lastAnaMs > 0.0 ? juce::jlimit (0.005f, 0.1f, (float) ((nowMs - lastAnaMs) * 0.001)) : 1.0f / 40.0f;
    lastAnaMs = nowMs;
    configureAnalyzer();   // the resolution setting may have changed
    rangeDb = anaRange();
    tiltDb = anaTilt();

    const bool ana = apvtsVal ("analyzer") > 0.5f && apvtsVal ("ana_freeze") < 0.5f;
    const int view = (int) apvtsVal ("ana_view");   // 0 pre, 1 post, 2 both
    bool force = false;

    if (capMode != 0 || peakListen > 0)
    {
        force = true;
        toolSpectrum();
        if (peakListen > 0)   // Auto Peaks: average the input spectrum over ~2.5 s
        {
            if (peakListen <= 65)   // the first frames still hold audio from before the button was pressed
            {
                for (int k = 0; k <= 2048; ++k) peakAcc[(size_t) k] += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];
                ++peakN;
            }
            if (--peakListen == 0) finishPeakSearch();
        }
        if (capMode != 0)   // fftData still holds the input magnitudes: accumulate for EQ Match
        {
            auto& acc = capMode == 1 ? refAcc : srcAcc;
            for (int k = 0; k <= 2048; ++k) acc[(size_t) k] += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];
            (capMode == 1 ? refN : srcN)++;
        }
    }
    if (ana)
    {
        const int lat = proc.latencyNow.load();   // the output is delayed by the latency: delay the input spectrum the same amount so Pre and Post line up in time
        if (view != 1) { updateSpectrum (proc.preAnalyzer, preSm, lat); buildLevels (preSm, lvPre); }
        if (view != 0) { updateSpectrum (proc.postAnalyzer, postSm, 0); buildLevels (postSm, lvPost); }
        if (overlayProc != nullptr)
        {
            if (RajvikEQAudioProcessor::snapshotPost (overlayProc, snapL.data(), snapR.data(), specNLo)) { computeSpectrum (overlaySm); buildLevels (overlaySm, lvOv); }
            else overlayProc = nullptr;
        }
        if (++peakTick >= 6) { peakTick = 0; updatePeakLabels(); }   // ~7 times per second: stable labels
    }

    // repaint only when something visible changed (no CPU while idle)
    const double sig = frameSignature();
    if (force || std::abs (sig - lastSig) > 1.0e-6) { lastSig = sig; repaint(); }
}

void EQDisplay::updateSpectrum (const Analyzer& a, Spec& s, int delay)
{
    a.snapshot (snapL.data(), snapR.data(), specNLo, juce::jlimit (0, Analyzer::size - specNLo, delay));
    computeSpectrum (s);
}

// bass region: chosen resolution; everything above: the shorter FFT (reacts faster, like the audio)
void EQDisplay::computeSpectrum (Spec& s)
{
    computeBins (snapL.data(), snapR.data(), specNLo, *fftLo, winLo, s.lo);
    if (specNHi != specNLo)
    {
        const int off = specNLo - specNHi;   // the newest specNHi samples
        computeBins (snapL.data() + off, snapR.data() + off, specNHi, *fftHi, winHi, s.hi);
    }
}

// Level of every FFT bin in dBFS, calibrated so a full scale sine reads 0 dB. Left and right are analyzed separately and
// their power is averaged (no phase cancellation like a mono sum); then attack / release smoothing (the Speed setting).
void EQDisplay::computeBins (const float* l, const float* r, int N, juce::dsp::FFT& fftObj, const std::vector<float>& window, SpecArr& sm)
{
    const int bins = N / 2 + 1;
    std::fill (powBuf.begin(), powBuf.begin() + bins, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = ch == 0 ? l : r;
        for (int i = 0; i < N; ++i) fftData[(size_t) i] = src[i] * window[(size_t) i];
        std::fill (fftData.begin() + N, fftData.begin() + 2 * N, 0.0f);
        fftObj.performFrequencyOnlyForwardTransform (fftData.data());
        for (int k = 0; k < bins; ++k) powBuf[(size_t) k] += fftData[(size_t) k] * fftData[(size_t) k];
    }
    static const float relTau[] = { 1.2f, 0.6f, 0.3f, 0.15f, 0.07f };   // release time constants (seconds)
    const float rel = relTau[juce::jlimit (0, 4, (int) apvtsVal ("ana_speed"))];
    const float aAtk = 1.0f - std::exp (-anaDt / 0.010f), aRel = 1.0f - std::exp (-anaDt / rel);   // attack 10 ms: peaks show up immediately
    const float cal = 0.5f * 16.0f / ((float) N * (float) N);   // 0.5 = mean of L and R, (4 / N)^2 = amplitude scaling of the Hann window
    for (int k = 0; k < bins; ++k)
    {
        const float db = 10.0f * std::log10 (powBuf[(size_t) k] * cal + 1.0e-12f);
        float& s = sm[(size_t) k];
        s += (db > s ? aAtk : aRel) * (db - s);
    }
}

void EQDisplay::buildLevels (const Spec& s, std::vector<float>& out) const
{
    const int nx = getWidth() / 2 + 2;
    out.resize ((size_t) nx);
    for (int xi = 0; xi < nx; ++xi) out[(size_t) xi] = spectrumDbAt (s, (float) (xi * 2));
}

// the three strongest, clearly separated peaks of the shown spectrum get a frequency tag (Pro-Q style)
void EQDisplay::updatePeakLabels()
{
    peakPts.clear();
    if (apvtsVal ("ana_peaks") < 0.5f) return;
    const auto& lv = apvtsVal ("ana_view") < 0.5f ? lvPre : lvPost;
    const int n = (int) lv.size(), W = 14;   // +/- 28 px
    if (n < 2 * W + 4) return;
    struct Cand { int i; float db; };
    std::vector<Cand> c;
    for (int i = W; i < n - W; ++i)
    {
        const float v = lv[(size_t) i];
        bool isMax = true;
        float mean = 0.0f;
        for (int j = -W; j <= W; ++j)
        {
            const float u = lv[(size_t) (i + j)];
            if (u > v) { isMax = false; break; }
            mean += u;
        }
        if (! isMax) continue;
        mean /= (float) (2 * W + 1);
        if (v - mean < 3.0f || v < -rangeDb * 0.85f) continue;   // a real peak, not a flat bit or the noise floor
        c.push_back ({ i, v });
    }
    std::sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.db > b.db; });
    for (const auto& k : c)
    {
        const float x = (float) (k.i * 2);
        bool far = true;
        for (const auto& p : peakPts) if (std::abs (p.x - x) < 90.0f) far = false;
        if (far) peakPts.push_back ({ x, anaY (k.db) });
        if (peakPts.size() >= 3) break;
    }
}

// a cheap number that changes whenever anything drawn here changes
double EQDisplay::frameSignature() const
{
    double s = 0.0;
    for (int b = 0; b < numBands; ++b)
    {
        if (apvtsVal (pid (b, "en")) < 0.5f) continue;
        double t = 1.0;
        int k = 1;
        for (const char* n : { "type", "freq", "gain", "q", "slope", "dyn", "spec", "thr", "range", "place", "dir", "sc" })
            t += (double) apvtsVal (pid (b, n)) * (1.0 + 0.37 * (double) k++);
        s += (t + (double) proc.dynDb[(size_t) b].load() * 3.1) * (double) (b + 1);
    }
    for (const char* n : { "disp_range", "analyzer", "ana_view", "ana_range", "ana_tilt", "ana_peaks", "notes", "mode", "os", "ana_res" })
        s += (double) apvtsVal (n) * 17.3 * (double) (1 + (int) (n[0] + n[1]));
    s += (double) selected * 101.0 + (double) proc.soloBand.load() * 77.0;
    for (const auto* v : { &lvPre, &lvPost, &lvOv })
        for (size_t i = 0; i < v->size(); i += 5) s += (double) (*v)[i] * 0.013 * (double) (1 + (i % 7));
    const auto& mk = proc.spectralStage().mask;
    for (int k = 0; k < SpectralStage::bins; k += 6) s += (double) mk[(size_t) k] * 0.021;
    return s;
}

// Level (dB) of the analyzer at pixel column x, without tilt.
// Many bins per pixel (highs): power average. Fewer than one bin per pixel (lows): smooth interpolation that cannot overshoot.
float EQDisplay::binsDbAt (const float* sm, int N, float x) const
{
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const float binHz = (float) fs / (float) N;
    const int maxK = N / 2 - 1;
    const float p0 = xToFreq (x - 1.5f) / binHz, p1 = xToFreq (x + 1.5f) / binHz;
    if (p1 - p0 > 1.0f)
    {
        const int k0 = juce::jlimit (1, maxK, (int) std::ceil (p0)), k1 = juce::jlimit (k0, maxK, (int) std::floor (p1));
        double pw = 0.0;
        for (int k = k0; k <= k1; ++k) pw += std::pow (10.0, (double) sm[k] * 0.1);
        return (float) (10.0 * std::log10 (pw / (double) (k1 - k0 + 1) + 1.0e-12));
    }
    const float pos = juce::jlimit (1.0f, (float) (maxK - 1), 0.5f * (p0 + p1));
    const int k = (int) pos;
    const float t = pos - (float) k;
    const float a = sm[k - 1], b = sm[k], c = sm[k + 1], d = sm[k + 2];
    const float db = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    return juce::jlimit (juce::jmin (b, c) - 2.0f, juce::jmax (b, c) + 1.5f, db);
}

// incl. the analyzer tilt (dB per octave around 1 kHz, like the Pro-Q "Tilt" setting)
float EQDisplay::spectrumDbAt (const Spec& s, float x) const
{
    const float f = juce::jmax (10.0f, xToFreq (x));
    float db;
    if (specNLo == specNHi) db = binsDbAt (s.lo.data(), specNLo, x);
    else
    {
        const float t = juce::jlimit (0.0f, 1.0f, std::log2 (f / 140.0f));   // bass detail below 140 Hz, the fast window above 280 Hz
        if (t <= 0.0f) db = binsDbAt (s.lo.data(), specNLo, x);
        else if (t >= 1.0f) db = binsDbAt (s.hi.data(), specNHi, x);
        else
        {
            const float k = t * t * (3.0f - 2.0f * t);
            db = binsDbAt (s.lo.data(), specNLo, x) * (1.0f - k) + binsDbAt (s.hi.data(), specNHi, x) * k;
        }
    }
    return db + tiltDb * std::log2 (f / 1000.0f);
}

void EQDisplay::drawLevels (juce::Graphics& g, const std::vector<float>& lv, juce::Colour fillCol, float fillAlpha, juce::Colour lineCol, float lineAlpha, float lineW)
{
    if (lv.size() < 2) return;
    const float h = (float) getHeight();
    juce::Path line;
    for (size_t xi = 0; xi < lv.size(); ++xi)
    {
        const float x = (float) (xi * 2), y = anaY (lv[xi]);
        if (xi == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    if (fillAlpha > 0.0f)
    {
        juce::Path fill (line);
        fill.lineTo ((float) ((lv.size() - 1) * 2), h);
        fill.lineTo (0.0f, h);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (fillCol.withAlpha (fillAlpha), 0.0f, h * 0.3f, fillCol.withAlpha (0.02f), 0.0f, h, false));
        g.fillPath (fill);
    }
    g.setColour (lineCol.withAlpha (lineAlpha));
    g.strokePath (line, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EQDisplay::renderBackground (juce::Image& img, float ps)
{
    const int pw = juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)), ph = juce::jmax (1, juce::roundToInt ((float) getHeight() * ps));
    img = juce::Image (juce::Image::RGB, pw, ph, false);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale ((float) pw / (float) juce::jmax (1, getWidth()), (float) ph / (float) juce::jmax (1, getHeight())));
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setGradientFill (juce::ColourGradient (Look::bgTop.darker (0.55f), 0.0f, 0.0f, Look::bgBot.darker (0.25f), 0.0f, h, false));
    g.fillAll();

    // grid
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqs)
    {
        const float x = freqToX (f);
        const bool major = (f == 100.0f || f == 1000.0f || f == 10000.0f);
        g.setColour (juce::Colours::white.withAlpha (major ? 0.085f : 0.045f));
        g.drawVerticalLine ((int) x, 0.0f, h);
        g.setColour (juce::Colours::white.withAlpha (0.38f));
        g.drawText (f >= 1000 ? juce::String (f / 1000) + "k" : juce::String ((int) f), juce::jmin ((int) x - 20, (int) w - 40), (int) h - 18, 40, 14, juce::Justification::centred);
    }
    const float range = dispRange();
    const float step = range <= 3.0f ? 1.0f : range <= 6.0f ? 2.0f : range <= 12.0f ? 3.0f : 10.0f;
    for (float gdb = -range; gdb <= range + 0.01f; gdb += step)
    {
        g.setColour (juce::Colours::white.withAlpha (std::abs (gdb) < 0.01f ? 0.16f : 0.045f));
        g.drawHorizontalLine ((int) gainToY (gdb), 0.0f, w);
    }
    if (apvtsVal ("analyzer") > 0.5f)   // faint analyzer scale lines (the numbers are in the strip on the right)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (float db = -10.0f; db > -anaRange() + 0.1f; db -= 10.0f)
            g.drawHorizontalLine ((int) (h * (1.0f - (db + anaRange()) / anaRange())), 0.0f, w);
    }
}

void EQDisplay::paint (juce::Graphics& g)
{
    if (getWidth() < 2 || getHeight() < 2) return;
    const float w = (float) getWidth(), h = (float) getHeight();

    {   // background + grid: rendered once, redrawn only when size, range, theme or scale change
        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        int key = getWidth() * 31 + getHeight();
        key ^= (int) apvtsVal ("disp_range") << 20;
        key ^= (int) apvtsVal ("analyzer") << 23;
        key ^= (int) apvtsVal ("ana_range") << 24;
        key ^= (int) ((Look::bgBot.getARGB() * 7u) ^ Look::bgTop.getARGB());
        if (bgImage.isNull() || key != bgKey || std::abs (ps - bgScale) > 0.01f)
        {
            renderBackground (bgImage, ps);
            bgKey = key;
            bgScale = ps;
        }
        g.drawImage (bgImage, 0, 0, getWidth(), getHeight(), 0, 0, bgImage.getWidth(), bgImage.getHeight());
    }

    const int view = (int) apvtsVal ("ana_view");   // 0 pre, 1 post, 2 both
    if (apvtsVal ("analyzer") > 0.5f)
    {
        if (view != 1) drawLevels (g, lvPre, juce::Colour (0xff9b97b0), 0.20f, juce::Colour (0xffb9b5cc), 0.40f, 1.0f);
        if (view != 0) drawLevels (g, lvPost, juce::Colour (0xffe4e0f2), 0.16f, juce::Colours::white, 0.88f, 1.4f);
        if (overlayProc != nullptr) drawLevels (g, lvOv, juce::Colour (0xffff9f43), 0.22f, juce::Colour (0xffff9f43), 0.90f, 1.3f);

        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        for (const auto& p : peakPts)   // frequency tags on the strongest peaks
        {
            const float f = xToFreq (p.x);
            const juce::String t = f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::lround (f)) + " Hz";
            const float tw = (float) t.length() * 6.6f + 14.0f;
            const float bx = juce::jlimit (2.0f, w - tw - 2.0f, p.x - tw * 0.5f), by = juce::jmax (2.0f, p.y - 30.0f);
            g.setColour (Look::gold);
            g.fillEllipse (p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f);
            g.fillRoundedRectangle (bx, by, tw, 18.0f, 5.0f);
            juce::Path tri;
            tri.addTriangle (p.x - 4.0f, by + 18.0f, p.x + 4.0f, by + 18.0f, p.x, by + 23.0f);
            g.fillPath (tri);
            g.setColour (juce::Colour (0xff1a1822));
            g.drawText (t, (int) bx, (int) by, (int) tw, 18, juce::Justification::centred);
        }
    }

    // live spectral cut (what the Spectral bands are doing to each frequency right now)
    {
        int firstSpec = -1;
        for (int b = 0; b < numBands; ++b) if (apvtsVal (pid (b, "en")) > 0.5f && kindOf (b) == ModeSpectral) { firstSpec = b; break; }
        if (firstSpec >= 0)
        {
            const auto col = kindOf (selected) == ModeSpectral && apvtsVal (pid (selected, "en")) > 0.5f ? bandColour (selected) : bandColour (firstSpec);
                const auto& mk = proc.spectralStage().mask;
                    const double sfs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
                    juce::Path mp;
                    const float zy = gainToY (0.0f);
                    for (int x = 0; x < getWidth(); x += 2)
                    {
                        const float pos = juce::jlimit (0.0f, (float) SpectralStage::bins - 2.0f, xToFreq ((float) x) * (float) SpectralStage::N / (float) sfs);
                        const int k = (int) pos;
                        const float fr = pos - (float) k;
                        const float db = mk[(size_t) k] * (1.0f - fr) + mk[(size_t) k + 1] * fr;
                        const float y = gainToY (juce::jlimit (-60.0f, 60.0f, db));
                        if (x == 0) mp.startNewSubPath (0.0f, y); else mp.lineTo ((float) x, y);
                    }
                    juce::Path fillP (mp);
                    fillP.lineTo ((float) getWidth(), zy);
                    fillP.lineTo (0.0f, zy);
                    fillP.closeSubPath();
                    g.setColour (col.withAlpha (0.28f));
                    g.fillPath (fillP);
                    g.setColour (col);
                    g.strokePath (mp, juce::PathStrokeType (1.6f));
            }
    }

    // threshold + live spectral action of the selected dynamic band
    {
        const int sb = selected;
        const int stype = (int) apvtsVal (pid (sb, "type"));
        const int skind = dynModeOf (apvtsVal (pid (sb, "dyn")) > 0.5f, apvtsVal (pid (sb, "spec")) > 0.5f, stype);
        if (apvtsVal (pid (sb, "en")) > 0.5f && skind != ModeNormal)
        {
            const auto col = bandColour (sb);
            const float f0 = apvtsVal (pid (sb, "freq")), qq = juce::jmax (0.1f, apvtsVal (pid (sb, "q")));
            const float hq = 1.0f / qq, hi = f0 * 0.5f * (hq + std::sqrt (hq * hq + 4.0f)), lo = f0 * 0.5f * (-hq + std::sqrt (hq * hq + 4.0f));
            const float x1 = freqToX (lo), x2 = freqToX (hi);
            const float thr = apvtsVal (pid (sb, "thr"));
            // the threshold is drawn on the analyzer scale incl. its tilt, so "the spectrum crosses the line" = "the band reacts"
            auto thrY = [this, thr] (float x) { return anaY (thr + tiltDb * std::log2 (juce::jmax (10.0f, xToFreq (x)) / 1000.0f)); };
            g.setColour (col.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, juce::jmax (1.0f, x2 - x1), h));
            const float dash[] = { 6.0f, 4.0f };
            g.setColour (col.withAlpha (0.95f));
            g.drawDashedLine (juce::Line<float> (x1 - 30.0f, thrY (x1 - 30.0f), x2 + 30.0f, thrY (x2 + 30.0f)), dash, 2, 1.5f);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawText (juce::String (apvtsVal (pid (sb, "thr")), 1) + " dB", (int) x1 - 30, (int) thrY (x1 - 30.0f) - 16, 70, 14, juce::Justification::left);

        }
    }

    // band designs (static gain + current dynamic offset), at the rate the bands really run at (oversampling / linear phase)
    const double fs = (proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0) * (double) proc.responseRateFactor();
    std::array<BandDesign, numBands> designs;
    std::array<bool, numBands> on {};
    int numOn = 0;
    for (int b = 0; b < numBands; ++b)
    {
        on[(size_t) b] = apvtsVal (pid (b, "en")) > 0.5f;
        if (! on[(size_t) b]) continue;
        ++numOn;
        const int bk = kindOf (b);
        design (designs[(size_t) b], (int) apvtsVal (pid (b, "type")), apvtsVal (pid (b, "freq")),
                bk == ModeSpectral ? 0.0f : apvtsVal (pid (b, "gain")) + (bk == ModeDynamic ? proc.dynDb[(size_t) b].load() : 0.0f), apvtsVal (pid (b, "q")),
                (int) apvtsVal (pid (b, "slope")), fs);
    }

    // per band curves (thin, coloured) + selected band fill; sum for the combined curve
    const int nx = getWidth() / 2 + 2;
    const float zeroY = gainToY (0.0f);
    std::vector<float> total ((size_t) nx, 0.0f);
    for (int b = 0; b < numBands; ++b)
    {
        if (! on[(size_t) b] || kindOf (b) == ModeSpectral) continue;   // spectral bands are drawn as the live spectral cut, not as a curve
        juce::Path bp;
        for (int xi = 0; xi < nx; ++xi)
        {
            const float x = (float) (xi * 2);
            const float db = (float) designDb (designs[(size_t) b], xToFreq (x), fs);
            total[(size_t) xi] += db;
            const float y = gainToY (juce::jlimit (-60.0f, 60.0f, db));
            if (xi == 0) bp.startNewSubPath (x, y); else bp.lineTo (x, y);
        }
        if (b == selected)
        {
            juce::Path fillP (bp);
            fillP.lineTo ((float) ((nx - 1) * 2), zeroY);
            fillP.lineTo (0.0f, zeroY);
            fillP.closeSubPath();
            g.setColour (bandColour (b).withAlpha (0.24f));
            g.fillPath (fillP);
        }
        g.setColour (bandColour (b).withAlpha (b == selected ? 0.85f : 0.45f));
        g.strokePath (bp, juce::PathStrokeType (1.2f));
    }

    // combined curve: soft fill, glow, solid gold line
    juce::Path curve;
    for (int xi = 0; xi < nx; ++xi)
    {
        const float x = (float) (xi * 2);
        const float y = gainToY (juce::jlimit (-100.0f, 100.0f, total[(size_t) xi]));
        if (xi == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    juce::Path curveFill (curve);
    curveFill.lineTo ((float) ((nx - 1) * 2), zeroY);
    curveFill.lineTo (0.0f, zeroY);
    curveFill.closeSubPath();
    g.setColour (Look::gold.withAlpha (0.08f));
    g.fillPath (curveFill);
    g.setColour (Look::gold.withAlpha (0.14f));
    g.strokePath (curve, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Look::gold);
    g.strokePath (curve, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (numOn == 0 && ! sketchMode)
    {
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("Double-click to add a band", getLocalBounds().withTrimmedBottom (60), juce::Justification::centred);
    }

    // sketch + status text
    if (sketch.size() > 1)
    {
        juce::Path sk;
        sk.startNewSubPath (sketch.front());
        for (auto& pt : sketch) sk.lineTo (pt);
        g.setColour (juce::Colour (0xfffacc15));
        g.strokePath (sk, juce::PathStrokeType (2.0f));
    }
    {
        juce::String st;
        if (sketchMode) st = "EQ SKETCH: draw a curve with the mouse";
        { const auto grp = groupBands(); if (grp.size() > 1) st = juce::String ((int) grp.size()) + " bands selected: drag / wheel / arrows move them together, the panel changes all of them by the same amount, Del = delete"; }
        if (proc.soloBand.load() >= 0) st = "SOLO band " + juce::String (proc.soloBand.load() + 1) + ": you hear only this band's region (press Solo again to release)";
        if (peakListen > 0) st = "AUTO PEAKS: listening... play your audio (" + juce::String (peakListen * 100 / 75) + "%)";
        if (capMode == 1) st = "MATCH: capturing REFERENCE - play the reference track (" + juce::String (refN) + ")";
        if (capMode == 2) st = "MATCH: capturing SOURCE - play your track (" + juce::String (srcN) + ")";
        if (st.isNotEmpty())
        {
            g.setColour (Look::gold);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (st, 14, 8, getWidth() - 28, 18, juce::Justification::left);
        }
    }

    // piano roll strip: one key per semitone on the same log frequency axis
    const bool piano = apvtsVal ("notes") > 0.5f;
    if (piano)
    {
        const float ky = h - 40.0f, kh = 22.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0.0f, ky - 1.0f, w, kh + 2.0f);
        const bool selOn = apvtsVal (pid (selected, "en")) > 0.5f;
        const float selF = apvtsVal (pid (selected, "freq"));
        auto noteNum = [] (float f) { return (int) std::lround (69.0 + 12.0 * std::log2 ((double) f / 440.0)); };
        const int selN = selOn ? noteNum (selF) : -999;
        const int hovN = hoverX >= 0.0f ? noteNum (xToFreq (hoverX)) : -999;
        for (int n = 3; n <= 136; ++n)
        {
            const float fc = 440.0f * std::pow (2.0f, (float) (n - 69) / 12.0f);
            const float x0 = freqToX (fc * std::pow (2.0f, -0.5f / 12.0f)), x1 = freqToX (fc * std::pow (2.0f, 0.5f / 12.0f));
            if (x1 < 0.0f || x0 > w) continue;
            const int pc = n % 12;
            const bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const juce::Rectangle<float> kr (x0, ky, juce::jmax (1.0f, x1 - x0), kh);
            juce::Colour base (0xffd9d6e6);
            if (n == selN) base = bandColour (selected);
            else if (n == hovN) base = juce::Colour (0xffffc857);
            g.setColour (base.withAlpha (0.92f));
            g.fillRect (kr);
            if (blk)
            {
                g.setColour (n == selN ? bandColour (selected).darker (0.4f) : n == hovN ? juce::Colour (0xffb8860b) : juce::Colour (0xff17151f));
                g.fillRect (kr.withHeight (kh * 0.62f));
            }
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawVerticalLine ((int) x0, ky, ky + kh);
            if (pc == 0)
            {
                g.setColour (juce::Colour (0xff17151f));
                g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                g.drawText ("C" + juce::String (n / 12 - 1), kr.expanded (12.0f, 0.0f).withTrimmedTop (kh * 0.55f).toNearestInt(), juce::Justification::centred);
            }
        }
        if (selOn)
        {
            const juce::String t = noteName (selF) + "  |  " + (selF >= 1000.0f ? juce::String (selF / 1000.0f, 2) + " kHz" : juce::String (selF, 1) + " Hz");
            const float tw = 130.0f;
            const juce::Rectangle<float> rc (juce::jlimit (0.0f, w - tw, freqToX (selF) - tw * 0.5f), ky - 22.0f, tw, 18.0f);
            g.setColour (bandColour (selected).withAlpha (0.85f));
            g.fillRoundedRectangle (rc, 5.0f);
            g.setColour (juce::Colour (0xff17151f));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (t, rc.toNearestInt(), juce::Justification::centred);
        }
    }

    // hover frequency readout
    if (hoverX >= 0.0f && ! dragging)
    {
        const float f = xToFreq (hoverX);
        juce::String t = f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String ((int) f) + " Hz";
        const bool showNotes = piano;
        if (showNotes) t += "  " + noteName (f);
        if (apvtsVal ("analyzer") > 0.5f)
            t += "   " + juce::String (spectrumDbAt (apvtsVal ("ana_view") < 0.5f ? preSm : postSm, hoverX), 1) + " dB";
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) hoverX, 0.0f, h);
        const float rw = 10.0f + 7.2f * (float) t.length();
        const juce::Rectangle<float> rc (juce::jlimit (0.0f, w - rw, hoverX - rw * 0.5f), h - 26.0f - (piano ? 62.0f : 0.0f), rw, 18.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (rc, 5.0f);
        g.setColour (Look::text);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (t, rc.toNearestInt(), juce::Justification::centred);
    }

    // nodes: glow + solid dot, no numbers
    for (int b = 0; b < numBands; ++b)
    {
        if (! on[(size_t) b]) continue;
        const auto p = nodePos (b);
        const auto c = bandColour (b);
        const int type = (int) apvtsVal (pid (b, "type"));
        const int kind = dynModeOf (apvtsVal (pid (b, "dyn")) > 0.5f, apvtsVal (pid (b, "spec")) > 0.5f, type);
        if (kind == ModeDynamic)
        {
            const float base = typeHasGain (type) ? apvtsVal (pid (b, "gain")) : 0.0f;
            const float y2 = gainToY (base + proc.dynDb[(size_t) b].load());
            g.setColour (c.withAlpha (0.8f));
            g.drawLine (p.x, p.y, p.x, y2, 2.0f);
            g.drawEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f, 1.2f);
        }
        else if (kind == ModeSpectral)
        {
            g.setColour (c.withAlpha (0.9f));
            const float dash[] = { 3.0f, 3.0f };
            juce::Path ring; ring.addEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
            juce::Path dashed; juce::PathStrokeType (2.0f).createDashedStroke (dashed, ring, dash, 2);
            g.fillPath (dashed);
        }
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.45f), p.x, p.y, c.withAlpha (0.0f), p.x + 22.0f, p.y, true));
        g.fillEllipse (p.x - 22.0f, p.y - 22.0f, 44.0f, 44.0f);
        g.setColour (c);
        g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 1.3f);
        if (b == selected || selSet[(size_t) b]) { g.setColour (juce::Colours::white.withAlpha (0.75f)); g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 1.2f); }
    }

    // rubber band selection rectangle
    if (marqueeing && ! mqRect.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (mqRect);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawRect (mqRect, 1.0f);
    }

    // Pro-Q style readout next to the node: always for the selected band (also while dragging), and for the node under the mouse
    {
        const int hov = (hoverX >= 0.0f && ! dragging) ? hitTest ({ hoverX, hoverY }) : -1;
        for (int b : { hov == selected ? -1 : hov, selected })
        {
            if (b < 0 || ! on[(size_t) b]) continue;
            const float f = apvtsVal (pid (b, "freq"));
            const int type = (int) apvtsVal (pid (b, "type"));
            juce::String t1 = f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (f, 1) + " Hz";
            if (piano) t1 += "  " + noteName (f);
            juce::String t2;
            if (typeHasGain (type)) { const float gn = apvtsVal (pid (b, "gain")); t2 = (gn > 0.0f ? "+" : "") + juce::String (gn, 1) + " dB"; }
            const auto np = nodePos (b);
            const juce::String t = t2.isEmpty() ? t1 : t1 + "   " + t2;
            const juce::Font fnt (juce::FontOptions (11.0f, juce::Font::bold));
            juce::GlyphArrangement ga;
            ga.addLineOfText (fnt, t, 0.0f, 0.0f);
            const float bw = ga.getBoundingBox (0, -1, true).getWidth() + 14.0f, bh = 17.0f;
            const float bx = juce::jlimit (3.0f, w - bw - 3.0f, np.x - bw * 0.5f);
            float by = np.y - bh - 13.0f;
            if (by < 3.0f) by = np.y + 13.0f;
            const juce::Rectangle<float> rc (bx, by, bw, bh);
            g.setColour (juce::Colour (0xd8151320));
            g.fillRoundedRectangle (rc, 4.0f);
            g.setColour (bandColour (b).withAlpha (0.8f));
            g.drawRoundedRectangle (rc.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.setFont (fnt);
            g.drawText (t, rc.toNearestInt(), juce::Justification::centred);
        }
    }
}

void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    repaint();
    grabKeyboardFocus();
    if (sketchMode) { sketch.clear(); sketch.push_back (e.position); return; }
    const int h = hitTest (e.position);
    if (h < 0)
    {
        if (e.mods.isPopupMenu()) return;
        marqueeing = true; mqStart = e.position; mqRect = {};   // drag on empty space = rubber band select
        return;
    }
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (additive && ! e.mods.isPopupMenu()) { toggleSel (h); return; }

    const bool inGroup = selSet[(size_t) h] && groupBands().size() > 1;
    if (! inGroup) select (h);
    else if (selected != h) selected = h;
    if (e.mods.isPopupMenu()) { showMenu (h); return; }

    pendingSingle = inGroup ? h : -1;   // a plain click (no drag) on a grouped node narrows the selection to it
    dragGroup = groupBands();
    dragDown = e.position;
    dragging = true;
    for (int b : dragGroup)
    {
        dragF[(size_t) b] = apvtsVal (pid (b, "freq"));
        dragG[(size_t) b] = apvtsVal (pid (b, "gain"));
        gesture (pid (b, "freq"), true);
        gesture (pid (b, "gain"), true);
    }
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    repaint();
    hoverX = e.position.x; hoverY = e.position.y;
    if (sketchMode) { sketch.push_back (e.position); repaint(); return; }
    if (marqueeing) { mqRect = juce::Rectangle<float> (mqStart, e.position); repaint(); return; }
    if (! dragging) return;
    if (e.getDistanceFromDragStart() > 2) pendingSingle = -1;
    const float ratio = xToFreq (e.position.x) / xToFreq (dragDown.x);
    const float dGain = yToGain (e.position.y) - yToGain (dragDown.y);
    for (int b : dragGroup)
    {
        setReal (pid (b, "freq"), juce::jlimit (10.0f, 20000.0f, dragF[(size_t) b] * ratio));
        if (typeHasGain ((int) apvtsVal (pid (b, "type"))) && kindOf (b) != ModeSpectral)
            setReal (pid (b, "gain"), juce::jlimit (-30.0f, 30.0f, dragG[(size_t) b] + dGain));
    }
}

void EQDisplay::finishMarquee (bool additive)
{
    const auto r = mqRect;
    marqueeing = false; mqRect = {};
    if (r.getWidth() < 4.0f && r.getHeight() < 4.0f)   // a plain click on empty space: deselect + close the panel
    {
        if (! additive) selSet.fill (false);
        if (onBackgroundClick) onBackgroundClick();
        repaint();
        return;
    }
    std::vector<int> hit;
    for (int b = 0; b < numBands; ++b)
        if (apvtsVal (pid (b, "en")) > 0.5f && r.contains (nodePos (b))) hit.push_back (b);
    if (! additive) selSet.fill (false);
    for (int b : hit) selSet[(size_t) b] = true;
    const auto g = groupBands();
    if (! hit.empty()) selected = hit.front();
    if (g.size() == 1) { if (onSelect) onSelect (g.front()); }
    else if (g.size() > 1) { if (onMultiSelect) onMultiSelect(); }
    else if (onBackgroundClick) onBackgroundClick();
    repaint();
}

void EQDisplay::mouseUp (const juce::MouseEvent& e)
{
    repaint();
    if (sketchMode) { finishSketch(); return; }
    if (marqueeing) { finishMarquee (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown()); return; }
    if (! dragging) return;
    dragging = false;
    for (int b : dragGroup)
    {
        gesture (pid (b, "freq"), false);
        gesture (pid (b, "gain"), false);
    }
    if (pendingSingle >= 0) { const int p = pendingSingle; pendingSingle = -1; select (p); }
}

bool EQDisplay::keyPressed (const juce::KeyPress& k)
{
    const auto g = groupBands();
    if (g.empty()) return false;
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        for (int b : g) setIndex (pid (b, "en"), 0);
        selSet.fill (false);
        if (onBackgroundClick) onBackgroundClick();
        repaint();
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0) || k == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        for (int b = 0; b < numBands; ++b) selSet[(size_t) b] = apvtsVal (pid (b, "en")) > 0.5f;
        const auto all = groupBands();
        if (all.size() > 1 && onMultiSelect) onMultiSelect();
        repaint();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        selSet.fill (false);
        if (onBackgroundClick) onBackgroundClick();
        repaint();
        return true;
    }
    const bool fine = k.getModifiers().isShiftDown();
    const float semi = fine ? 1.0f / 48.0f : 1.0f / 12.0f, dg = fine ? 0.1f : 0.5f;
    float fr = 1.0f, gn = 0.0f;
    if (k.getKeyCode() == juce::KeyPress::leftKey)  fr = std::pow (2.0f, -semi);
    else if (k.getKeyCode() == juce::KeyPress::rightKey) fr = std::pow (2.0f, semi);
    else if (k.getKeyCode() == juce::KeyPress::upKey)    gn = dg;
    else if (k.getKeyCode() == juce::KeyPress::downKey)  gn = -dg;
    else return false;
    for (int b : g)
    {
        if (fr != 1.0f) setReal (pid (b, "freq"), juce::jlimit (10.0f, 20000.0f, apvtsVal (pid (b, "freq")) * fr));
        if (gn != 0.0f && typeHasGain ((int) apvtsVal (pid (b, "type"))) && kindOf (b) != ModeSpectral)
            setReal (pid (b, "gain"), juce::jlimit (-30.0f, 30.0f, apvtsVal (pid (b, "gain")) + gn));
    }
    return true;
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    repaint();
    if (sketchMode) return;
    const int h = hitTest (e.position);
    if (h >= 0) { setIndex (pid (h, "en"), 0); repaint(); return; }

    for (int b = 0; b < numBands; ++b)
    {
        if (apvtsVal (pid (b, "en")) > 0.5f) continue;
        setIndex (pid (b, "type"), Bell);
        setIndex (pid (b, "dyn"), 0);
        setIndex (pid (b, "spec"), 0);
        setIndex (pid (b, "place"), Stereo);
        setReal (pid (b, "q"), 1.0f);
        setReal (pid (b, "freq"), juce::jlimit (10.0f, 20000.0f, xToFreq (e.position.x)));
        setReal (pid (b, "gain"), juce::jlimit (-30.0f, 30.0f, yToGain (e.position.y)));
        setIndex (pid (b, "en"), 1);
        select (b);
        return;
    }
}

void EQDisplay::mouseMove (const juce::MouseEvent& e) { hoverX = e.position.x; hoverY = e.position.y; repaint(); }
void EQDisplay::mouseExit (const juce::MouseEvent&) { hoverX = -1.0f; hoverY = -1.0f; repaint(); }

void EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd)
{
    int b = hitTest (e.position);
    if (b < 0) b = selected;
    if (apvtsVal (pid (b, "en")) < 0.5f) return;
    auto targets = groupBands();
    if (std::find (targets.begin(), targets.end(), b) == targets.end()) targets = { b };   // wheel on a node outside the group affects only that node
    for (int t : targets)
    {
        const float q = apvtsVal (pid (t, "q"));
        setReal (pid (t, "q"), juce::jlimit (0.025f, 40.0f, q * std::exp (wd.deltaY * 1.2f)));
    }
}

void EQDisplay::showMenu (int b)
{
    auto targets = groupBands();
    if (std::find (targets.begin(), targets.end(), b) == targets.end()) targets = { b };
    const bool multi = targets.size() > 1;

    juce::PopupMenu m;
    const int cur = (int) apvtsVal (pid (b, "type"));
    if (multi) { m.addSectionHeader (juce::String ((int) targets.size()) + " bands selected"); }
    for (int i = 0; i < numTypes; ++i) m.addItem (i + 1, typeNames[i], true, ! multi && i == cur);
    m.addSeparator();
    bool allGain = true;
    for (int t : targets) allGain = allGain && typeHasGain ((int) apvtsVal (pid (t, "type")));
    const int kind = dynModeOf (apvtsVal (pid (b, "dyn")) > 0.5f, apvtsVal (pid (b, "spec")) > 0.5f, cur);
    m.addItem (20, "Normal EQ",           true,   ! multi && kind == ModeNormal);
    m.addItem (21, "Dynamic EQ",          allGain, ! multi && kind == ModeDynamic);
    m.addItem (22, "Spectral Dynamic EQ", allGain, ! multi && kind == ModeSpectral);
    m.addSeparator();
    if (! multi) m.addItem (50, "Solo band", true, proc.soloBand.load() == b);
    m.addItem (40, "Select all bands");
    m.addItem (30, multi ? "Delete " + juce::String ((int) targets.size()) + " bands" : juce::String ("Delete band"));

    juce::Component::SafePointer<EQDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, targets] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* d = safe.getComponent();
        if (r == 50) { d->proc.soloBand = d->proc.soloBand.load() == targets.front() ? -1 : targets.front(); d->repaint(); return; }
        if (r == 40)
        {
            for (int k = 0; k < numBands; ++k) d->selSet[(size_t) k] = d->apvtsVal (pid (k, "en")) > 0.5f;
            if (d->groupBands().size() > 1) { if (d->onMultiSelect) d->onMultiSelect(); }
            d->repaint();
            return;
        }
        for (int t : targets)
        {
            if (r >= 1 && r <= numTypes) d->setIndex (pid (t, "type"), r - 1);
            else if (r >= 20 && r <= 22)
            {
                if (r != 20 && ! typeHasGain ((int) d->apvtsVal (pid (t, "type")))) continue;
                d->setIndex (pid (t, "dyn"),  r == 20 ? 0 : 1);
                d->setIndex (pid (t, "spec"), r == 22 ? 1 : 0);
            }
            else if (r == 30) d->setIndex (pid (t, "en"), 0);
        }
        if (r == 30) { d->selSet.fill (false); if (d->onBackgroundClick) d->onBackgroundClick(); }
        d->repaint();
    });
}

//==============================================================================
int EQDisplay::freeBand() const
{
    for (int b = 0; b < numBands; ++b)
        if (apvtsVal (pid (b, "en")) < 0.5f) return b;
    return -1;
}

// Greedy fit: repeatedly drop a Bell on the biggest remaining error, measure its width, subtract it.
void EQDisplay::fitToTarget (std::vector<float> r, int maxBands)
{
    const double fs = (proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0) * (double) proc.responseRateFactor();
    const int w = (int) r.size();
    if (w < 8) return;
    int last = -1;
    for (int it = 0; it < maxBands; ++it)
    {
        int xs = 0;
        float best = 0.0f;
        for (int x = 0; x < w; ++x)
            if (std::abs (r[(size_t) x]) > std::abs (best)) { best = r[(size_t) x]; xs = x; }
        if (std::abs (best) < 0.7f) break;
        const int b = freeBand();
        if (b < 0) break;

        const float half = std::abs (best) * 0.5f;
        int xl = xs, xr = xs;
        while (xl > 0 && std::abs (r[(size_t) xl]) > half) --xl;
        while (xr < w - 1 && std::abs (r[(size_t) xr]) > half) ++xr;
        const float bw = juce::jlimit (0.15f, 6.0f, std::log2 (xToFreq ((float) xr + 1.0f) / xToFreq ((float) xl)));
        const float q = juce::jlimit (0.2f, 15.0f, 1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bw)));
        const float f = juce::jlimit (10.0f, 20000.0f, xToFreq ((float) xs));
        const float g = juce::jlimit (-30.0f, 30.0f, best);

        setIndex (pid (b, "type"), Bell);
        setIndex (pid (b, "dyn"), 0);
        setIndex (pid (b, "spec"), 0);
        setIndex (pid (b, "place"), Stereo);
        setReal (pid (b, "freq"), f);
        setReal (pid (b, "gain"), g);
        setReal (pid (b, "q"), q);
        setIndex (pid (b, "en"), 1);
        last = b;

        BandDesign d;
        design (d, Bell, f, g, q, 1, fs);
        for (int x = 0; x < w; ++x) r[(size_t) x] -= (float) designDb (d, xToFreq ((float) x), fs);
    }
    if (last >= 0) select (last);
}

void EQDisplay::finishSketch()
{
    const int w = getWidth();
    if (sketch.size() < 4 || w < 8) { sketch.clear(); repaint(); return; }   // too short: stay in sketch mode
    std::vector<float> t ((size_t) w, 0.0f);
    std::vector<char> has ((size_t) w, 0);
    int xmin = w, xmax = -1;
    for (auto& p : sketch)
    {
        const int x = juce::jlimit (0, w - 1, (int) p.x);
        t[(size_t) x] = juce::jlimit (-30.0f, 30.0f, yToGain (p.y));
        has[(size_t) x] = 1;
        xmin = juce::jmin (xmin, x); xmax = juce::jmax (xmax, x);
    }
    int prev = xmin;
    for (int x = xmin + 1; x <= xmax; ++x)
    {
        if (! has[(size_t) x]) continue;
        for (int i = prev + 1; i < x; ++i)
            t[(size_t) i] = t[(size_t) prev] + (t[(size_t) x] - t[(size_t) prev]) * (float) (i - prev) / (float) (x - prev);
        prev = x;
    }
    sketch.clear();
    fitToTarget (t, 8);
    sketchMode = false;
    if (onSketchDone) onSketchDone();
    repaint();
}

void EQDisplay::applyMatch()
{
    if (refN == 0 || srcN == 0) return;
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const int w = getWidth();
    if (w < 8) return;
    std::vector<float> t ((size_t) w, 0.0f);
    double mean = 0.0;
    int cnt = 0;
    for (int x = 0; x < w; ++x)
    {
        const float f = xToFreq ((float) x);
        if (f < 30.0f || f > 18000.0f) continue;
        const int k0 = juce::jmax (1, (int) std::floor (f * std::pow (2.0f, -1.0f / 6.0f) * 4096.0 / fs));
        const int k1 = juce::jlimit (k0, 2048, (int) std::ceil (f * std::pow (2.0f, 1.0f / 6.0f) * 4096.0 / fs));
        double a = 0.0, s = 0.0;
        for (int k = k0; k <= k1; ++k) { a += refAcc[(size_t) k] / refN; s += srcAcc[(size_t) k] / srcN; }
        const float d = (float) (10.0 * std::log10 ((a + 1e-12) / (s + 1e-12)));
        t[(size_t) x] = d;
        if (f >= 100.0f && f <= 10000.0f) { mean += d; ++cnt; }
    }
    mean /= (double) juce::jmax (1, cnt);
    for (int x = 0; x < w; ++x)
    {
        const float f = xToFreq ((float) x);
        t[(size_t) x] = (f < 30.0f || f > 18000.0f) ? 0.0f : juce::jlimit (-12.0f, 12.0f, t[(size_t) x] - (float) mean);
    }
    refAcc.fill (0.0); srcAcc.fill (0.0); refN = srcN = 0;
    fitToTarget (t, 10);
}

// Auto Peaks: find the most prominent peaks of the averaged input spectrum, put a narrow bell on each and select them all,
// so they can be cut / moved / deleted together (drag, wheel, arrows, Delete).
void EQDisplay::finishPeakSearch()
{
    const int w = getWidth();
    std::array<float, 2049> avg {};
    float top = -200.0f;
    for (int k = 0; k <= 2048; ++k)
    {
        avg[(size_t) k] = 20.0f * std::log10 (std::sqrt ((float) (peakAcc[(size_t) k] / (double) juce::jmax (1, peakN))) / 1024.0f + 1.0e-9f);
        top = juce::jmax (top, avg[(size_t) k]);
    }
    peakAcc.fill (0.0); peakN = 0;
    if (w < 100 || top < -95.0f) return;   // nothing was playing

    std::vector<float> lev ((size_t) w);
    for (int x = 0; x < w; ++x) lev[(size_t) x] = binsDbAt (avg.data(), 4096, (float) x);
    struct Pk { int x; float prom; };
    std::vector<Pk> pks;
    for (int x = 8; x < w - 8; ++x)
    {
        const float f = xToFreq ((float) x);
        if (f < 30.0f || f > 18000.0f) continue;
        bool isMax = true;
        for (int d = -6; d <= 6 && isMax; ++d) if (lev[(size_t) (x + d)] > lev[(size_t) x]) isMax = false;
        if (! isMax) continue;
        double base = 0.0; int n = 0;
        for (int d = -50; d <= 50; ++d) { const int xx = juce::jlimit (0, w - 1, x + d); base += lev[(size_t) xx]; ++n; }
        const float prom = lev[(size_t) x] - (float) (base / n);
        if (prom >= 3.0f) pks.push_back ({ x, prom });
    }
    std::sort (pks.begin(), pks.end(), [] (const Pk& a, const Pk& b) { return a.prom > b.prom; });

    selSet.fill (false);
    int first = -1, made = 0;
    std::vector<int> used;
    for (const auto& pk : pks)
    {
        if (made >= 6) break;
        bool near = false;
        for (int ux : used) if (std::abs (std::log2 (xToFreq ((float) pk.x) / xToFreq ((float) ux))) < 0.4f) near = true;
        if (near) continue;
        const int b = freeBand();
        if (b < 0) break;
        const float half = lev[(size_t) pk.x] - pk.prom * 0.5f;
        int xl = pk.x, xr = pk.x;
        while (xl > 0 && lev[(size_t) xl] > half) --xl;
        while (xr < w - 1 && lev[(size_t) xr] > half) ++xr;
        const float bw = juce::jlimit (0.15f, 3.0f, std::log2 (xToFreq ((float) xr + 1.0f) / xToFreq ((float) xl)));
        const float q = juce::jlimit (2.0f, 20.0f, 1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bw)));
        setIndex (pid (b, "type"), Bell);
        setIndex (pid (b, "dyn"), 0);
        setIndex (pid (b, "spec"), 0);
        setIndex (pid (b, "place"), Stereo);
        setReal (pid (b, "freq"), juce::jlimit (10.0f, 20000.0f, xToFreq ((float) pk.x)));
        setReal (pid (b, "gain"), 0.0f);
        setReal (pid (b, "q"), q);
        setIndex (pid (b, "en"), 1);
        selSet[(size_t) b] = true;
        if (first < 0) first = b;
        used.push_back (pk.x);
        ++made;
    }
    if (first >= 0) { selected = first; if (made > 1) { if (onMultiSelect) onMultiSelect(); } else if (onSelect) onSelect (first); }
    repaint();
}

void EQDisplay::showInstances()
{
    auto all = RajvikEQAudioProcessor::getInstances();
    juce::PopupMenu m;
    int idx = 0, others = 0;
    for (auto* p : all)
    {
        ++idx;
        if (p == &proc) continue;
        ++others;
        const auto nm = p->displayName().isEmpty() ? "Instance " + juce::String (idx) : p->displayName();
        juce::PopupMenu sub;
        sub.addItem (100 + idx, "Show its spectrum behind this EQ", true, overlayProc == p);
        sub.addItem (200 + idx, "Copy its settings to this EQ");
        m.addSubMenu (nm, sub);
    }
    if (others == 0) m.addItem (-1, "No other RAJVIK Pro EQ instances found", false);
    else { m.addSeparator(); m.addItem (1, "Hide other spectrum"); }

    juce::Component::SafePointer<EQDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, all] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto* d = safe.getComponent();
        if (r == 1) { d->overlayProc = nullptr; return; }
        const int i = (r % 100) - 1;
        if (r >= 200 && i >= 0 && i < (int) all.size()) d->proc.copySettingsFrom (all[(size_t) i]);
        else if (r >= 100 && i >= 0 && i < (int) all.size()) d->overlayProc = all[(size_t) i];
    });
}

void BandPanel::Knob::init (juce::Component& parent, const juce::String& name, bool bipolar)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    if (bipolar) s.getProperties().set ("bipolar", true);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (juce::FontOptions (11.0f)));
    parent.addAndMakeVisible (s);
    parent.addAndMakeVisible (l);
}
void BandPanel::Knob::place (juce::Rectangle<int> r)
{
    l.setBounds (r.removeFromTop (14));
    s.setBounds (r);
}

BandPanel::BandPanel (RajvikEQAudioProcessor& p) : proc (p)
{
    enBtn.setComponentID ("power");
    for (auto* b : { &closeBtn, &prevBtn, &nextBtn }) b->setComponentID ("flat");
    closeBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
    prevBtn.setButtonText ("<");
    nextBtn.setButtonText (">");
    soloBtn.setClickingTogglesState (true);
    soloBtn.setTooltip ("Solo: listen only to the region this band affects (band-pass at its frequency and Q)");
    soloBtn.onClick = [this] { proc.soloBand = soloBtn.getToggleState() ? band : -1; };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    prevBtn.onClick  = [this] { if (onStep) onStep (-1); };
    nextBtn.onClick  = [this] { if (onStep) onStep (1); };

    numLabel.setJustificationType (juce::Justification::centred);
    numLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));

    for (auto* c : std::initializer_list<juce::Component*> { &enBtn, &closeBtn, &soloBtn, &prevBtn, &nextBtn, &numLabel,
                                                             &typeBox, &slopeBox, &placeBox, &dirBox, &dynModeBox, &scBtn })
        addAndMakeVisible (c);
    dynModeBox.addItem ("Normal EQ", ModeNormal + 1);
    dynModeBox.addItem ("Dynamic EQ", ModeDynamic + 1);
    dynModeBox.addItem ("Spectral Dyn.", ModeSpectral + 1);
    dynModeBox.setTooltip ("Band mode: Normal EQ / Dynamic EQ / Spectral Dynamic EQ");
    dynModeBox.onChange = [this] { setDynMode (dynModeBox.getSelectedId() - 1); };
    typeBox.addItemList (typeNames, 1);
    slopeBox.addItemList (slopeNames, 1);
    placeBox.addItemList (placementNames, 1);
    dirBox.addItemList (dirNames, 1);
    freq.init (*this, "FREQ");   gain.init (*this, "GAIN", true);   q.init (*this, "Q");
    thr.init (*this, "THRESH");  range.init (*this, "RANGE", true); att.init (*this, "ATTACK"); rel.init (*this, "RELEASE");
    select (0);
}

int BandPanel::dynMode() const
{
    const auto& v = proc.apvts;
    return dynModeOf (v.getRawParameterValue (pid (band, "dyn"))->load() > 0.5f,
                      v.getRawParameterValue (pid (band, "spec"))->load() > 0.5f,
                      (int) v.getRawParameterValue (pid (band, "type"))->load());
}
bool BandPanel::canDynamic() const { return typeHasGain ((int) proc.apvts.getRawParameterValue (pid (band, "type"))->load()); }

std::vector<int> BandPanel::targets() const
{
    if (groupMode) return group;
    return { band };
}

void BandPanel::setParam (int b, const char* name, float v)
{
    if (auto* p = proc.apvts.getParameter (pid (b, name))) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void BandPanel::setDynMode (int mode)
{
    if (mode < 0 || mode > ModeSpectral) return;
    for (int b : targets())
    {
        const int t = (int) proc.apvts.getRawParameterValue (pid (b, "type"))->load();
        if (mode != ModeNormal && ! typeHasGain (t)) continue;   // only gain type bands can be dynamic
        auto set = [this, b] (const char* n, bool on)
        {
            if (auto* p = proc.apvts.getParameter (pid (b, n)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        };
        set ("dyn",  mode != ModeNormal);
        set ("spec", mode == ModeSpectral);
    }
    update();
}

bool BandPanel::isCut() const
{
    const int t = (int) proc.apvts.getRawParameterValue (pid (band, "type"))->load();
    return t == LowCut || t == HighCut;
}
int BandPanel::idealWidth() const { return dynOn() ? 878 : 550; }

void BandPanel::select (int b)
{
    band = b;
    if (groupMode)   // back to single band editing: drop the group handlers
    {
        groupMode = false; group.clear(); groupSig = -1;
        typeBox.onChange = nullptr; slopeBox.onChange = nullptr; placeBox.onChange = nullptr; dirBox.onChange = nullptr;
        enBtn.onClick = nullptr; scBtn.onClick = nullptr;
        static const char* names[] = { "FREQ", "GAIN", "Q", "THRESH", "RANGE", "ATTACK", "RELEASE" };
        static const bool bip[] = { false, true, false, false, true, false, false };
        for (int k = 0; k < numKnobs; ++k)
        {
            auto& kn = knobRef (k);
            kn.s.onValueChange = nullptr; kn.s.onDragStart = nullptr; kn.s.onDragEnd = nullptr;
            kn.s.getProperties().set ("bipolar", bip[k]);
            kn.l.setText (names[k], juce::dontSendNotification);
        }
        prevBtn.setVisible (true); nextBtn.setVisible (true); soloBtn.setVisible (true);
    }
    freqA.reset(); gainA.reset(); qA.reset(); thrA.reset(); rangeA.reset(); attA.reset(); relA.reset();
    typeA.reset(); slopeA.reset(); placeA.reset(); dirA.reset(); enA.reset(); scA.reset();

    auto& v = proc.apvts;
    freqA  = std::make_unique<SA> (v, pid (b, "freq"),  freq.s);
    gainA  = std::make_unique<SA> (v, pid (b, "gain"),  gain.s);
    qA     = std::make_unique<SA> (v, pid (b, "q"),     q.s);
    thrA   = std::make_unique<SA> (v, pid (b, "thr"),   thr.s);
    rangeA = std::make_unique<SA> (v, pid (b, "range"), range.s);
    attA   = std::make_unique<SA> (v, pid (b, "att"),   att.s);
    relA   = std::make_unique<SA> (v, pid (b, "rel"),   rel.s);
    typeA  = std::make_unique<CA> (v, pid (b, "type"),  typeBox);
    slopeA = std::make_unique<CA> (v, pid (b, "slope"), slopeBox);
    placeA = std::make_unique<CA> (v, pid (b, "place"), placeBox);
    dirA   = std::make_unique<CA> (v, pid (b, "dir"),   dirBox);
    enA    = std::make_unique<BA> (v, pid (b, "en"),    enBtn);
    scA    = std::make_unique<BA> (v, pid (b, "sc"),    scBtn);

    const auto col = bandColour (b);
    for (auto* k : { &freq, &gain, &q, &thr, &range, &att, &rel })
        k->s.setColour (juce::Slider::rotarySliderFillColourId, col);
    numLabel.setText (juce::String (b + 1), juce::dontSendNotification);
    numLabel.setColour (juce::Label::textColourId, col);

    lastMode = -1;   // force a refresh
    lastCut = ! isCut();
    update();
    repaint();
}

void BandPanel::update()
{
    if (groupMode) { refreshGroupControls(); return; }
    soloBtn.setToggleState (proc.soloBand.load() == band, juce::dontSendNotification);
    const int m = dynMode();
    const bool d = m != ModeNormal, c = isCut(), cd = canDynamic();
    if (m == lastMode && c == lastCut && cd == lastCanDyn) return;
    lastMode = m; lastCut = c; lastCanDyn = cd;
    dynModeBox.setSelectedId (m + 1, juce::dontSendNotification);
    dynModeBox.setItemEnabled (ModeDynamic + 1, cd);
    dynModeBox.setItemEnabled (ModeSpectral + 1, cd);
    slopeBox.setVisible (c);
    dirBox.setVisible (d);
    scBtn.setVisible (d);
    gain.s.setVisible (m != ModeSpectral);   // a Spectral band has no static gain: only threshold / range / attack / release
    gain.l.setVisible (m != ModeSpectral);
    for (auto* k : { &thr, &range, &att, &rel }) { k->s.setVisible (d); k->l.setVisible (d); }
    resized();
    repaint();
}

// ---- group (multi band) editing: every change is applied to all selected bands by the same amount ----
void BandPanel::setGroup (const std::vector<int>& bands)
{
    if (bands.empty()) return;
    if (groupMode && bands == group) return;
    const bool wasGroup = groupMode;
    group = bands;
    band = bands.front();
    groupMode = true;
    groupSig = -1;
    lastMode = -1;

    if (! wasGroup)
    {
        freqA.reset(); gainA.reset(); qA.reset(); thrA.reset(); rangeA.reset(); attA.reset(); relA.reset();
        typeA.reset(); slopeA.reset(); placeA.reset(); dirA.reset(); enA.reset(); scA.reset();
        configureKnobsForGroup();
    }
    for (int k = 0; k < numKnobs; ++k)   // relative knobs always start at "no change"
    {
        const double neutral = (k == KQ || k == KAtt || k == KRel) ? 1.0 : 0.0;
        knobRef (k).s.setValue (neutral, juce::dontSendNotification);
        lastVal[k] = neutral;
        knobRef (k).s.updateText();
    }

    numLabel.setText (juce::String ((int) bands.size()) + " sel", juce::dontSendNotification);
    numLabel.setColour (juce::Label::textColourId, Look::gold);
    for (int k = 0; k < numKnobs; ++k) knobRef (k).s.setColour (juce::Slider::rotarySliderFillColourId, Look::gold);

    typeBox.onChange = [this]
    {
        const int t = typeBox.getSelectedId() - 1;
        if (t < 0) return;
        for (int b : group) setParam (b, "type", (float) t);
    };
    slopeBox.onChange = [this]
    {
        const int s = slopeBox.getSelectedId() - 1;
        if (s < 0) return;
        for (int b : group)
        {
            const int t = (int) proc.apvts.getRawParameterValue (pid (b, "type"))->load();
            if (t == LowCut || t == HighCut) setParam (b, "slope", (float) s);
        }
    };
    placeBox.onChange = [this]
    {
        const int p = placeBox.getSelectedId() - 1;
        if (p < 0) return;
        for (int b : group) setParam (b, "place", (float) p);
    };
    dirBox.onChange = [this]
    {
        const int d = dirBox.getSelectedId() - 1;
        if (d < 0) return;
        for (int b : group)
        {
            const auto& v = proc.apvts;
            if (dynModeOf (v.getRawParameterValue (pid (b, "dyn"))->load() > 0.5f, v.getRawParameterValue (pid (b, "spec"))->load() > 0.5f,
                           (int) v.getRawParameterValue (pid (b, "type"))->load()) != ModeNormal)
                setParam (b, "dir", (float) d);
        }
    };
    enBtn.onClick = [this]   // the power button switches the whole group off
    {
        if (enBtn.getToggleState()) return;
        for (int b : group) setParam (b, "en", 0.0f);
    };
    scBtn.onClick = [this]
    {
        const float on = scBtn.getToggleState() ? 1.0f : 0.0f;
        for (int b : group)
        {
            const auto& v = proc.apvts;
            if (dynModeOf (v.getRawParameterValue (pid (b, "dyn"))->load() > 0.5f, v.getRawParameterValue (pid (b, "spec"))->load() > 0.5f,
                           (int) v.getRawParameterValue (pid (b, "type"))->load()) != ModeNormal)
                setParam (b, "sc", on);
        }
    };

    prevBtn.setVisible (false);
    nextBtn.setVisible (false);
    soloBtn.setVisible (false);   // solo is for one band at a time
    refreshGroupControls();
    repaint();
}

void BandPanel::configureKnobsForGroup()
{
    struct GK { const char* label; double lo, hi; int kind; };   // kind: 0 octave shift, 1 dB offset, 2 multiplier
    static const GK gk[numKnobs] =
    {
        { "FREQ +/-",   -3.0,  3.0, 0 },
        { "GAIN +/-",  -30.0, 30.0, 1 },
        { "Q x",         0.25, 4.0, 2 },
        { "THRESH +/-", -40.0, 40.0, 1 },
        { "RANGE +/-",  -30.0, 30.0, 1 },
        { "ATTACK x",    0.1, 10.0, 2 },
        { "RELEASE x",   0.1, 10.0, 2 }
    };

    for (int k = 0; k < numKnobs; ++k)
    {
        auto& kn = knobRef (k);
        const auto& d = gk[k];
        juce::NormalisableRange<double> r (d.lo, d.hi, 0.01);
        if (d.kind == 2) r.setSkewForCentre (1.0);
        kn.s.setNormalisableRange (r);
        kn.s.setDoubleClickReturnValue (true, d.kind == 2 ? 1.0 : 0.0);
        kn.s.getProperties().set ("bipolar", true);
        kn.l.setText (d.label, juce::dontSendNotification);
        const int kind = d.kind;
        kn.s.textFromValueFunction = [kind] (double v)
        {
            if (kind == 2) return "x" + juce::String (v, 2);
            return (v > 0.004 ? juce::String ("+") : juce::String()) + juce::String (v, kind == 0 ? 2 : 1) + (kind == 0 ? " oct" : " dB");
        };
        kn.s.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-+.0123456789").getDoubleValue(); };
        kn.s.onValueChange = [this, k] { groupKnobChanged (k); };
        kn.s.onDragStart = [this, k] { groupGesture (k, true); };
        kn.s.onDragEnd = [this, k] { groupGesture (k, false); };
    }
}

void BandPanel::groupGesture (int k, bool begin)
{
    static const char* names[numKnobs] = { "freq", "gain", "q", "thr", "range", "att", "rel" };
    for (int b : group)
        if (auto* p = proc.apvts.getParameter (pid (b, names[k]))) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

void BandPanel::groupKnobChanged (int k)
{
    if (! groupMode || group.empty()) return;
    const double v = knobRef (k).s.getValue(), last = lastVal[k];
    lastVal[k] = v;
    const double d = v - last, ratio = last != 0.0 ? v / last : 1.0;
    if (d == 0.0) return;

    for (int b : group)
    {
        auto get = [this, b] (const char* n) { return proc.apvts.getRawParameterValue (pid (b, n))->load(); };
        const int t = (int) get ("type");
        const int kind = dynModeOf (get ("dyn") > 0.5f, get ("spec") > 0.5f, t);
        switch (k)
        {
            case KFreq:  setParam (b, "freq", juce::jlimit (10.0f, 20000.0f, get ("freq") * (float) std::pow (2.0, d))); break;
            case KGain:  if (typeHasGain (t) && kind != ModeSpectral) setParam (b, "gain", juce::jlimit (-30.0f, 30.0f, get ("gain") + (float) d)); break;
            case KQ:     setParam (b, "q", juce::jlimit (0.025f, 40.0f, get ("q") * (float) ratio)); break;
            case KThr:   if (kind != ModeNormal) setParam (b, "thr", juce::jlimit (-80.0f, 0.0f, get ("thr") + (float) d)); break;
            case KRange: if (kind != ModeNormal) setParam (b, "range", juce::jlimit (-30.0f, 30.0f, get ("range") + (float) d)); break;
            case KAtt:   if (kind != ModeNormal) setParam (b, "att", juce::jlimit (0.1f, 500.0f, get ("att") * (float) ratio)); break;
            case KRel:   if (kind != ModeNormal) setParam (b, "rel", juce::jlimit (5.0f, 3000.0f, get ("rel") * (float) ratio)); break;
            default: break;
        }
    }
}

// shows what the bands have in common (blank when they differ) and hides controls no band in the group can use
void BandPanel::refreshGroupControls()
{
    const auto& v = proc.apvts;
    int type = -2, slope = -2, place = -2, dir = -2, mode = -2;
    bool anyDyn = false, anyCut = false, anyGain = false, anyCanDyn = false, allSc = true, haveDyn = false;
    auto merge = [] (int& acc, int x) { if (acc == -2) acc = x; else if (acc != x) acc = -1; };

    for (int b : group)
    {
        auto get = [&v, b] (const char* n) { return v.getRawParameterValue (pid (b, n))->load(); };
        const int t = (int) get ("type");
        const int m = dynModeOf (get ("dyn") > 0.5f, get ("spec") > 0.5f, t);
        const bool cut = t == LowCut || t == HighCut;
        merge (type, t); merge (place, (int) get ("place")); merge (mode, m);
        if (cut) merge (slope, (int) get ("slope"));
        if (m != ModeNormal) { merge (dir, (int) get ("dir")); haveDyn = true; if (get ("sc") < 0.5f) allSc = false; }
        anyDyn = anyDyn || m != ModeNormal;
        anyCut = anyCut || cut;
        anyGain = anyGain || (typeHasGain (t) && m != ModeSpectral);
        anyCanDyn = anyCanDyn || typeHasGain (t);
    }

    typeBox.setSelectedId (type >= 0 ? type + 1 : 0, juce::dontSendNotification);
    placeBox.setSelectedId (place >= 0 ? place + 1 : 0, juce::dontSendNotification);
    slopeBox.setSelectedId (slope >= 0 ? slope + 1 : 0, juce::dontSendNotification);
    dirBox.setSelectedId (dir >= 0 ? dir + 1 : 0, juce::dontSendNotification);
    dynModeBox.setSelectedId (mode >= 0 ? mode + 1 : 0, juce::dontSendNotification);
    enBtn.setToggleState (true, juce::dontSendNotification);
    scBtn.setToggleState (haveDyn && allSc, juce::dontSendNotification);

    const int sig = (anyDyn ? 1 : 0) | (anyCut ? 2 : 0) | (anyGain ? 4 : 0) | (anyCanDyn ? 8 : 0);
    if (sig == groupSig) return;
    groupSig = sig;
    groupAnyDyn = anyDyn;
    dynModeBox.setItemEnabled (ModeDynamic + 1, anyCanDyn);
    dynModeBox.setItemEnabled (ModeSpectral + 1, anyCanDyn);
    slopeBox.setVisible (anyCut);
    dirBox.setVisible (anyDyn);
    scBtn.setVisible (anyDyn);
    gain.s.setVisible (anyGain);
    gain.l.setVisible (anyGain);
    for (auto* k : { &thr, &range, &att, &rel }) { k->s.setVisible (anyDyn); k->l.setVisible (anyDyn); }
    resized();
    repaint();
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto body = getLocalBounds().reduced (12, 10);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 16, { 0, 5 }).drawForRectangle (g, body);
    const auto r = body.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xf52d2a38), 0.0f, r.getY(), juce::Colour (0xf51a1822), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    g.setColour ((groupMode ? Look::gold : bandColour (band)).withAlpha (0.8f));
    g.drawLine (r.getX() + 34.0f, r.getY() + 1.0f, r.getRight() - 34.0f, r.getY() + 1.0f, 1.5f);
}

void BandPanel::resized()
{
    const auto body = getLocalBounds().reduced (12, 10);
    closeBtn.setBounds (body.getRight() - 32, body.getY() + 8, 22, 22);
    soloBtn.setBounds (body.getRight() - 32 - 6 - 46, body.getY() + 7, 46, 24);
    auto r = body.reduced (16, 12);

    auto rightCol = r.removeFromRight (120);
    rightCol.removeFromTop (30);
    placeBox.setBounds (rightCol.removeFromTop (26));
    rightCol.removeFromTop (6);
    dynModeBox.setBounds (rightCol.removeFromTop (26));

    auto left = r.removeFromLeft (112);
    auto row = left.removeFromTop (24);
    enBtn.setBounds (row.removeFromLeft (24));
    row.removeFromLeft (4);
    prevBtn.setBounds (row.removeFromLeft (20));
    nextBtn.setBounds (row.removeFromRight (20));
    numLabel.setBounds (row);
    left.removeFromTop (6);
    typeBox.setBounds (left.removeFromTop (28));
    left.removeFromTop (4);
    slopeBox.setBounds (left.removeFromTop (24));
    r.removeFromLeft (6);

    freq.place (r.removeFromLeft (76).withSizeKeepingCentre (76, 88));
    gain.place (r.removeFromLeft (100));
    q.place    (r.removeFromLeft (76).withSizeKeepingCentre (76, 88));

    if (dynOn())
    {
        r.removeFromLeft (8);
        auto tcol = r.removeFromLeft (88);
        scBtn.setBounds (tcol.removeFromTop (24));
        tcol.removeFromTop (4);
        dirBox.setBounds (tcol.removeFromTop (26));
        for (auto* k : { &thr, &range, &att, &rel })
            k->place (r.removeFromLeft (58).withSizeKeepingCentre (58, 80));
    }
}

//==============================================================================
void ScaleStrip::timerCallback()
{
    const float l = proc.peakL.exchange (0.0f), r = proc.peakR.exchange (0.0f);
    const float oldL = lvL, oldR = lvR;
    lvL = juce::jmax (l, lvL * 0.88f);
    lvR = juce::jmax (r, lvR * 0.88f);
    if (lvL < 1.0e-5f) lvL = 0.0f;
    if (lvR < 1.0e-5f) lvR = 0.0f;
    const int key = (int) proc.apvts.getRawParameterValue ("disp_range")->load() + 4 * (int) proc.apvts.getRawParameterValue ("analyzer")->load()
                  + 8 * (int) proc.apvts.getRawParameterValue ("ana_range")->load();
    if (key != lastKey || lvL != oldL || lvR != oldR) { lastKey = key; repaint(); }   // nothing moves while idle -> no repaint
}

void ScaleStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! rangeRect.contains (e.position)) return;
    static const char* names[] = { "3 dB", "6 dB", "12 dB", "30 dB" };
    const int cur = (int) proc.apvts.getRawParameterValue ("disp_range")->load();
    juce::PopupMenu m;
    for (int i = 0; i < 4; ++i) m.addItem (i + 1, names[i], true, i == cur);
    juce::Component::SafePointer<ScaleStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (auto* p = safe->proc.apvts.getParameter ("disp_range"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 1)));
            p->endChangeGesture();
        }
    });
}

void ScaleStrip::paint (juce::Graphics& g)
{
    const float h = (float) getHeight();
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawVerticalLine (0, 0.0f, h);

    static const float ranges[] = { 3.0f, 6.0f, 12.0f, 30.0f };
    const float range = ranges[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("disp_range")->load())];
    const float step = range <= 3.0f ? 1.0f : range <= 6.0f ? 2.0f : range <= 12.0f ? 3.0f : 10.0f;

    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    for (float gdb = range - step; gdb >= -range - 0.01f; gdb -= step)
    {
        const float y = juce::jlimit (8.0f, h - 8.0f, h * 0.5f * (1.0f - gdb / range));
        g.setColour (Look::gold.withAlpha (std::abs (gdb) < 0.01f ? 1.0f : 0.85f));
        g.drawText ((gdb > 0.01f ? "+" : "") + juce::String ((int) std::lround (gdb)), 2, (int) y - 8, 40, 16, juce::Justification::centredRight);
    }

    if (proc.apvts.getRawParameterValue ("analyzer")->load() > 0.5f)   // analyzer scale (dBFS), same mapping as the spectrum: 0 dB at the top
    {
        static const float aranges[] = { 60.0f, 90.0f, 120.0f };
        const float R = aranges[juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("ana_range")->load())];
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        for (float db = -10.0f; db > -R + 0.1f; db -= 10.0f)
        {
            const float y = h * (1.0f - (db + R) / R);
            if (y < 34.0f || y > h - 8.0f) continue;
            g.drawText (juce::String ((int) std::lround (db)), 44, (int) y - 8, 32, 16, juce::Justification::centredRight);
        }
    }

    // EQ range button (click to change), like the "12 dB" button of Pro-Q
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (rangeRect, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (rangeRect.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Look::gold);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (juce::String ((int) range) + " dB", rangeRect.toNearestInt(), juce::Justification::centred);
    }

    // level meters
    const float top = 34.0f, bottom = h - 8.0f;
    const float xs[2] = { 80.0f, 90.0f };
    const float lv[2] = { lvL, lvR };
    for (int i = 0; i < 2; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (xs[i], top, 6.0f, bottom - top, 3.0f);
        const float db = juce::Decibels::gainToDecibels (lv[i], -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (frac <= 0.0f) continue;
        juce::ColourGradient grad (juce::Colour (0xff35d49a), 0.0f, bottom, juce::Colour (0xffff5a5f), 0.0f, top, false);
        grad.addColour (0.75, Look::gold);
        g.setGradientFill (grad);
        const float fh = (bottom - top) * frac;
        g.fillRoundedRectangle (xs[i], bottom - fh, 6.0f, fh, 3.0f);
    }
}

//==============================================================================
// Settings panel (gear icon): GUI colour, background, GUI size and the less used display options
namespace
{
class Swatch : public juce::Component, public juce::SettableTooltipClient
{
public:
    Swatch (juce::Colour a, juce::Colour b, int k) : c1 (a), c2 (b), kind (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (isMouseOver() ? 2.0f : 3.0f);
        if (kind == 2)
        {
            juce::ColourGradient grad (juce::Colour (0xffff4d5e), r.getX(), r.getY(), juce::Colour (0xff4d8dff), r.getRight(), r.getBottom(), false);
            grad.addColour (0.33, juce::Colour (0xfff9c23c));
            grad.addColour (0.66, juce::Colour (0xff3ddc97));
            g.setGradientFill (grad);
        }
        else if (kind == 1) g.setGradientFill (juce::ColourGradient (c1, r.getX(), r.getY(), c2, r.getX(), r.getBottom(), false));
        else g.setColour (c1);
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawEllipse (r, 1.0f);
        if (selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (getLocalBounds().toFloat().reduced (0.8f), 1.8f);
        }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }

private:
    juce::Colour c1, c2;
    int kind;   // 0 solid, 1 two tone (background themes), 2 rainbow (custom colour)
};

class AccentPicker : public juce::Component, private juce::ChangeListener
{
public:
    AccentPicker (juce::Colour start, std::function<void (juce::Colour)> cb)
        : onPick (std::move (cb)), sel (juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace, 4, 4)
    {
        sel.setCurrentColour (start);
        sel.addChangeListener (this);
        addAndMakeVisible (sel);
        setSize (260, 300);
    }
    ~AccentPicker() override { sel.removeChangeListener (this); }
    void resized() override { sel.setBounds (getLocalBounds()); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onPick) onPick (sel.getCurrentColour().withAlpha (1.0f)); }
    std::function<void (juce::Colour)> onPick;
    juce::ColourSelector sel;
};

class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (RajvikEQAudioProcessor& p, juce::Component& hostComp, std::function<void()> look, std::function<void (float)> scale, std::function<void()> resetSizeFn)
        : proc (p), host (hostComp), onLook (std::move (look)), onScale (std::move (scale)), onResetSize (std::move (resetSizeFn)), customSw (juce::Colour(), juce::Colour(), 2)
    {
        for (int i = 0; i < Look::numAccents; ++i)
        {
            auto s = std::make_unique<Swatch> (juce::Colour (Look::accents[i].argb), juce::Colour(), 0);
            s->setTooltip (Look::accents[i].name);
            s->onClick = [this, i] { Look::prefs.useCustom = false; Look::prefs.accent = i; changed(); };
            addAndMakeVisible (*s);
            accentSw.push_back (std::move (s));
        }
        customSw.setTooltip ("Custom colour");
        customSw.onClick = [this] { pickCustom(); };
        addAndMakeVisible (customSw);
        for (int i = 0; i < Look::numThemes; ++i)
        {
            auto s = std::make_unique<Swatch> (juce::Colour (Look::themes[i].top).brighter (0.25f), juce::Colour (Look::themes[i].bot), 1);
            s->setTooltip (Look::themes[i].name);
            s->onClick = [this, i] { Look::prefs.theme = i; changed(); };
            addAndMakeVisible (*s);
            themeSw.push_back (std::move (s));
        }

        scaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        scaleSlider.setRange (40.0, 200.0, 5.0);
        scaleSlider.setValue ((double) Look::prefs.scale * 100.0, juce::dontSendNotification);
        scaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        scaleSlider.setTextValueSuffix (" %");
        scaleSlider.setDoubleClickReturnValue (true, 100.0);
        scaleSlider.onDragEnd = [this] { applyScale(); };
        scaleSlider.onValueChange = [this] { if (! scaleSlider.isMouseButtonDown()) applyScale(); };
        addAndMakeVisible (scaleSlider);

        charBox.addItemList (RP::charNames, 1);
        rangeBox.addItemList (RP::rangeNames, 1);
        anaViewBox.addItemList (RP::anaViewNames, 1);
        anaRangeBox.addItemList (RP::anaRangeNames, 1);
        anaResBox.addItemList (RP::anaResNames, 1);
        anaSpeedBox.addItemList (RP::anaSpeedNames, 1);
        anaTiltBox.addItemList (RP::anaTiltNames, 1);
        anaResBox.setTooltip ("Higher resolution = more precise lows, but slower reaction (like Pro-Q)");
        anaTiltBox.setTooltip ("Spectrum tilt in dB per octave around 1 kHz. 4.5 dB/oct makes typical music look flat");
        for (auto* c : std::initializer_list<juce::Component*> { &charBox, &rangeBox, &anaViewBox, &anaRangeBox, &anaResBox, &anaSpeedBox, &anaTiltBox,
                                                                 &analyzerBtn, &notesBtn, &invertBtn, &freezeBtn, &peaksBtn })
            addAndMakeVisible (c);
        charA     = std::make_unique<CA> (proc.apvts, "character", charBox);
        rangeA    = std::make_unique<CA> (proc.apvts, "disp_range", rangeBox);
        anaViewA  = std::make_unique<CA> (proc.apvts, "ana_view", anaViewBox);
        analyzerA = std::make_unique<BA> (proc.apvts, "analyzer", analyzerBtn);
        notesA    = std::make_unique<BA> (proc.apvts, "notes", notesBtn);
        invertA   = std::make_unique<BA> (proc.apvts, "invert", invertBtn);
        anaRangeA = std::make_unique<CA> (proc.apvts, "ana_range", anaRangeBox);
        anaResA   = std::make_unique<CA> (proc.apvts, "ana_res", anaResBox);
        anaSpeedA = std::make_unique<CA> (proc.apvts, "ana_speed", anaSpeedBox);
        anaTiltA  = std::make_unique<CA> (proc.apvts, "ana_tilt", anaTiltBox);
        freezeA   = std::make_unique<BA> (proc.apvts, "ana_freeze", freezeBtn);
        peaksA    = std::make_unique<BA> (proc.apvts, "ana_peaks", peaksBtn);

        for (auto* b : { &resetColoursBtn, &resetSizeBtn, &resetAllBtn }) addAndMakeVisible (b);
        resetColoursBtn.setTooltip ("Back to the default accent colour and background");
        resetSizeBtn.setTooltip ("Back to the default window size (100 %)");
        resetAllBtn.setTooltip ("Colours, size and all settings above back to default (bands are not touched)");
        resetColoursBtn.onClick = [this] { Look::prefs.useCustom = false; Look::prefs.accent = 0; Look::prefs.theme = 0; changed(); };
        resetSizeBtn.onClick = [this]
        {
            if (onResetSize) onResetSize();
            scaleSlider.setValue (100.0, juce::dontSendNotification);
        };
        resetAllBtn.onClick = [this]
        {
            Look::prefs.useCustom = false; Look::prefs.accent = 0; Look::prefs.theme = 0;
            changed();
            if (onResetSize) onResetSize();
            scaleSlider.setValue (100.0, juce::dontSendNotification);
            for (const char* id : { "out_gain", "mode", "character", "os", "disp_range", "analyzer", "ana_view", "ana_range", "ana_res", "ana_speed", "ana_tilt", "ana_freeze", "ana_peaks", "invert", "notes" })
                if (auto* prm = proc.apvts.getParameter (id))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->getDefaultValue());
                    prm->endChangeGesture();
                }
        };

        refreshSelection();
        setSize (590, 352);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Look::gold);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("SETTINGS", 18, 10, 200, 18, juce::Justification::left);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (299, 36.0f, (float) getHeight() - 18.0f);
        g.setColour (Look::muted);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        for (auto& l : labels) g.drawText (l.first, l.second, juce::Justification::centredLeft);
    }

    void resized() override
    {
        // two columns: look & size on the left, display / processing / analyzer on the right
        labels.clear();
        const int x0 = 18, colW = 270;
        int y = 36;
        labels.push_back ({ "GUI colour", { x0, y, 200, 16 } });
        y += 18;
        for (size_t i = 0; i < accentSw.size(); ++i) accentSw[i]->setBounds (x0 + (int) i * 30, y, 28, 28);
        customSw.setBounds (x0 + (int) accentSw.size() * 30, y, 28, 28);
        y += 38;
        labels.push_back ({ "Background", { x0, y, 200, 16 } });
        y += 18;
        for (size_t i = 0; i < themeSw.size(); ++i) themeSw[i]->setBounds (x0 + (int) i * 30, y, 28, 28);
        y += 38;
        labels.push_back ({ "GUI size", { x0, y, 200, 16 } });
        y += 18;
        scaleSlider.setBounds (x0, y, colW, 24);
        y += 38;
        labels.push_back ({ "Reset to default", { x0, y, 200, 16 } });
        y += 20;
        const int rw = (colW - 12) / 3;
        resetColoursBtn.setBounds (x0, y, rw, 26);
        resetSizeBtn.setBounds (x0 + rw + 6, y, rw, 26);
        resetAllBtn.setBounds (x0 + 2 * (rw + 6), y, rw, 26);

        const int x1 = 312, bx = x1 + 104, bw = 156;
        y = 36;
        labels.push_back ({ "Display & processing", { x1, y, 200, 16 } });
        y += 20;
        analyzerBtn.setBounds (x1, y, 84, 24);
        notesBtn.setBounds (x1 + 86, y, 84, 24);
        invertBtn.setBounds (x1 + 172, y, 100, 24);
        y += 32;
        labels.push_back ({ "EQ display range", { x1, y, 104, 24 } });
        rangeBox.setBounds (bx, y, bw, 24);
        y += 28;
        labels.push_back ({ "Character", { x1, y, 104, 24 } });
        charBox.setBounds (bx, y, bw, 24);
        y += 38;
        labels.push_back ({ "Analyzer", { x1, y, 200, 16 } });
        y += 20;
        labels.push_back ({ "View", { x1, y, 104, 24 } });
        anaViewBox.setBounds (bx, y, bw, 24);
        y += 28;
        labels.push_back ({ "Range", { x1, y, 104, 24 } });
        anaRangeBox.setBounds (bx, y, bw, 24);
        y += 28;
        labels.push_back ({ "Resolution", { x1, y, 104, 24 } });
        anaResBox.setBounds (bx, y, bw, 24);
        y += 28;
        labels.push_back ({ "Speed", { x1, y, 104, 24 } });
        anaSpeedBox.setBounds (bx, y, bw, 24);
        y += 28;
        labels.push_back ({ "Tilt", { x1, y, 104, 24 } });
        anaTiltBox.setBounds (bx, y, bw, 24);
        y += 30;
        freezeBtn.setBounds (x1, y, 110, 24);
        peaksBtn.setBounds (x1 + 112, y, 148, 24);
    }

private:
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void refreshSelection()
    {
        for (size_t i = 0; i < accentSw.size(); ++i) { accentSw[i]->selected = ! Look::prefs.useCustom && Look::prefs.accent == (int) i; accentSw[i]->repaint(); }
        customSw.selected = Look::prefs.useCustom;
        customSw.repaint();
        for (size_t i = 0; i < themeSw.size(); ++i) { themeSw[i]->selected = Look::prefs.theme == (int) i; themeSw[i]->repaint(); }
    }

    void changed()
    {
        Look::prefs.save();
        Look::prefs.apply();
        refreshSelection();
        if (onLook) onLook();
    }

    void applyScale()
    {
        const float s = (float) scaleSlider.getValue() / 100.0f;
        if (std::abs (s - Look::prefs.scale) < 0.001f) return;
        const float old = Look::prefs.scale;
        Look::prefs.scale = s;
        Look::prefs.save();
        if (onScale) onScale (old);
    }

    void pickCustom()
    {
        const auto start = Look::prefs.useCustom ? juce::Colour (Look::prefs.custom) : juce::Colour (Look::accents[Look::prefs.accent].argb);
        juce::Component::SafePointer<SettingsPanel> safe (this);
        auto picker = std::make_unique<AccentPicker> (start, [safe] (juce::Colour c)
        {
            if (safe == nullptr) return;
            Look::prefs.useCustom = true;
            Look::prefs.custom = c.getARGB();
            safe->changed();
        });
        juce::CallOutBox::launchAsynchronously (std::move (picker), host.getLocalArea (&customSw, customSw.getLocalBounds()), &host);
    }

    RajvikEQAudioProcessor& proc;
    juce::Component& host;
    std::function<void()> onLook;
    std::function<void (float)> onScale;
    std::function<void()> onResetSize;
    juce::TextButton resetColoursBtn { "Colours" }, resetSizeBtn { "Size" }, resetAllBtn { "Reset all" };
    std::vector<std::unique_ptr<Swatch>> accentSw, themeSw;
    Swatch customSw;
    juce::Slider scaleSlider;
    juce::ComboBox charBox, rangeBox, anaViewBox, anaRangeBox, anaResBox, anaSpeedBox, anaTiltBox;
    juce::ToggleButton analyzerBtn { "Analyzer" }, notesBtn { "Piano view" }, invertBtn { "Phase invert" }, freezeBtn { "Freeze" }, peaksBtn { "Peak labels" };
    std::unique_ptr<CA> charA, rangeA, anaViewA, anaRangeA, anaResA, anaSpeedA, anaTiltA;
    std::unique_ptr<BA> analyzerA, notesA, invertA, freezeA, peaksA;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
};
}

static void setParamValue (RajvikEQAudioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}

//==============================================================================
RajvikEQAudioProcessorEditor::RajvikEQAudioProcessorEditor (RajvikEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), display (p), strip (p), panel (p)
{
    Look::prefs.load();
    Look::prefs.apply();
    laf.refreshColours();
    setLookAndFeel (&laf);

    root.paintFn = [this] (juce::Graphics& g) { paintRoot (g); };
    root.resizedFn = [this] { layoutRoot(); };
    addAndMakeVisible (root);

    for (auto* c : std::initializer_list<juce::Component*> { &display, &strip, &modeBox, &osL, &osBox, &presetPrev, &presetBtn, &presetNext, &presetSave,
                                                             &toolsBtn, &gearBtn, &undoBtn, &redoBtn, &slotA, &slotB, &copyBtn, &anaBox, &pianoBtn, &scaleBtn, &nameBtn,
                                                             &outL, &outSlider })
        root.addAndMakeVisible (c);
    root.addAndMakeVisible (panel);

    display.onSelect = [this] (int b) { panelOpen = true; panel.select (b); };
    display.onMultiSelect = [this] { panelOpen = true; };   // placePanel() then switches the panel to group editing
    display.onBackgroundClick = [this] { panelOpen = false; };
    panel.onClose = [this] { panelOpen = false; };
    panel.onStep = [this] (int dir)
    {
        const int cur = display.selected;
        for (int i = 1; i <= numBands; ++i)
        {
            const int n = ((cur + dir * i) % numBands + numBands) % numBands;
            if (display.isBandEnabled (n)) { display.selectBand (n); break; }
        }
    };

    modeBox.addItemList (modeNames, 1);
    osBox.addItemList (osNames, 1);
    osBox.setTooltip ("Oversampling of the EQ bands: more accurate high frequencies, a few samples of latency. Natural Phase is always at least 2x. Not used in Linear Phase.");
    osL.setJustificationType (juce::Justification::centredRight);
    osL.setFont (juce::Font (juce::FontOptions (13.0f)));
    osL.setText ("Oversampling", juce::dontSendNotification);
    outL.setJustificationType (juce::Justification::centredRight);
    outL.setFont (juce::Font (juce::FontOptions (13.0f)));
    outL.setText ("Output", juce::dontSendNotification);

    presetBtn.onClick = [this] { showPresetMenu(); };
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetSave.onClick = [this] { promptSave(); };
    presetSave.setTooltip ("Save the current bands as your own preset");

    toolsBtn.setButtonText ("Tools " + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")));
    toolsBtn.setTooltip ("EQ Sketch, Auto Peaks, Instances, EQ Match");
    toolsBtn.onClick = [this] { showToolsMenu(); };
    gearBtn.onClick = [this] { showSettings(); };

    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    outSlider.setDoubleClickReturnValue (true, 0.0);

    modeA     = std::make_unique<CA> (p.apvts, "mode", modeBox);
    osA       = std::make_unique<CA> (p.apvts, "os", osBox);
    pianoA    = std::make_unique<BA> (p.apvts, "notes", pianoBtn);
    pianoBtn.setTooltip ("Piano keyboard under the spectrum: shows the note of every frequency");
    outA      = std::make_unique<SA> (p.apvts, "out_gain", outSlider);

    display.onSketchDone = [this] { display.repaint(); };

    // header: undo / redo, A/B compare
    undoBtn.setTooltip ("Undo the last edit");
    redoBtn.setTooltip ("Redo");
    slotA.setTooltip ("Setting A");
    slotB.setTooltip ("Setting B: switch between two settings to compare");
    copyBtn.setTooltip ("Copy the current setting to the other slot (A to B or B to A)");
    undoBtn.onClick = [this] { doUndo(); };
    redoBtn.onClick = [this] { doRedo(); };
    slotA.onClick = [this] { switchSlot (0); };
    slotB.onClick = [this] { switchSlot (1); };
    copyBtn.onClick = [this] { abState[abSlot ^ 1] = stateString(); };
    updateAB();

    // bottom bar: analyzer mode, window size, instance name
    anaBox.addItem ("Analyzer Off", 1);
    anaBox.addItem ("Analyzer Pre", 2);
    anaBox.addItem ("Analyzer Post", 3);
    anaBox.addItem ("Analyzer Pre+Post", 4);
    anaBox.setTooltip ("Spectrum analyzer: off / input / output / both (more options in the settings)");
    anaBox.onChange = [this]
    {
        const int id = anaBox.getSelectedId();
        if (id == 1) setParamValue (proc, "analyzer", 0.0f);
        else { setParamValue (proc, "analyzer", 1.0f); setParamValue (proc, "ana_view", (float) (id == 2 ? 0 : id == 3 ? 1 : 2)); }
    };
    scaleBtn.setTooltip ("Window size");
    scaleBtn.onClick = [this] { showScaleMenu(); };
    nameBtn.setTooltip ("Instance list: compare with other RAJVIK Pro EQ instances in this session");
    nameBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    nameBtn.setColour (juce::TextButton::textColourOffId, Look::muted);
    nameBtn.onClick = [this] { display.showInstances(); };

    root.addMouseListener (&activity, true);
    panel.setBufferedToImage (true);   // the band panel is repainted only when it changes, not with every analyzer frame behind it
    lastSnap = stateString();
    abState[0] = lastSnap;

    // window: freely resizable, from a small minimum up; "GUI size" in the settings scales everything
    const float s = Look::prefs.scale;
    setResizable (true, true);
    setResizeLimits (juce::roundToInt ((float) minW * s), juce::roundToInt ((float) minH * s),
                     juce::roundToInt ((float) maxW * s), juce::roundToInt ((float) maxH * s));
    setSize (juce::roundToInt ((float) Look::prefs.width * s), juce::roundToInt ((float) Look::prefs.height * s));
    startTimerHz (30);
}

RajvikEQAudioProcessorEditor::~RajvikEQAudioProcessorEditor()
{
    stopTimer();
    root.removeMouseListener (&activity);
    proc.soloBand = -1;   // never leave a band soloed after the window is closed
    const float s = Look::prefs.scale;
    Look::prefs.width  = juce::jlimit (minW, maxW, juce::roundToInt ((float) getWidth() / s));
    Look::prefs.height = juce::jlimit (minH, maxH, juce::roundToInt ((float) getHeight() / s));
    Look::prefs.save();
    setLookAndFeel (nullptr);
}

void RajvikEQAudioProcessorEditor::timerCallback()
{
    { const int sb = proc.soloBand.load(); if (sb >= 0 && ! display.isBandEnabled (sb)) proc.soloBand = -1; }   // a removed band cannot stay soloed
    placePanel();
    const auto nm = Presets::presetName (proc);
    if (presetBtn.getButtonText() != nm) presetBtn.setButtonText (nm);

    if (++histTick >= 4) { histTick = 0; trackHistory(); }
    undoBtn.setEnabled (! undoStack.empty());
    redoBtn.setEnabled (! redoStack.empty());

    const int aid = proc.apvts.getRawParameterValue ("analyzer")->load() < 0.5f ? 1 : (int) proc.apvts.getRawParameterValue ("ana_view")->load() + 2;
    if (anaBox.getSelectedId() != aid) anaBox.setSelectedId (aid, juce::dontSendNotification);
    const auto pct = juce::String (juce::roundToInt (Look::prefs.scale * 100.0f)) + "%";
    if (scaleBtn.getButtonText() != pct) scaleBtn.setButtonText (pct);
    if (--nameTick <= 0)
    {
        nameTick = 20;
        const auto inst = juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) + proc.displayName();
        if (nameBtn.getButtonText() != inst) nameBtn.setButtonText (inst);
    }
}

juce::String RajvikEQAudioProcessorEditor::stateString() const
{
    auto st = proc.apvts.copyState();
    if (auto xml = st.createXml()) return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {};
}

void RajvikEQAudioProcessorEditor::applyState (const juce::String& s)
{
    if (auto xml = juce::parseXML (s)) proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
    lastSnap = stateString();   // re-read in the normal form so the next comparison is exact
}

// every finished edit (mouse released) becomes one undo step; changes made by host automation without you touching the editor are not recorded
void RajvikEQAudioProcessorEditor::trackHistory()
{
    if (juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()) return;
    const bool active = (juce::Time::getMillisecondCounterHiRes() - activity.last) < 3000.0
                     || juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
    const auto cur = stateString();
    if (cur == lastSnap) return;
    if (active)
    {
        undoStack.push_back (lastSnap);
        if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    lastSnap = cur;
}

void RajvikEQAudioProcessorEditor::doUndo()
{
    trackHistory();   // include the edit that is just finishing
    if (undoStack.empty()) return;
    redoStack.push_back (lastSnap);
    const auto prev = undoStack.back();
    undoStack.pop_back();
    applyState (prev);
}

void RajvikEQAudioProcessorEditor::doRedo()
{
    trackHistory();
    if (redoStack.empty()) return;
    undoStack.push_back (lastSnap);
    const auto next = redoStack.back();
    redoStack.pop_back();
    applyState (next);
}

void RajvikEQAudioProcessorEditor::switchSlot (int s)
{
    if (s == abSlot) return;
    trackHistory();
    abState[abSlot] = stateString();
    abSlot = s;
    if (abState[abSlot].isNotEmpty()) applyState (abState[abSlot]);
    else abState[abSlot] = stateString();   // first visit: B starts as a copy of A
    updateAB();
}

void RajvikEQAudioProcessorEditor::updateAB()
{
    slotA.setToggleState (abSlot == 0, juce::dontSendNotification);
    slotB.setToggleState (abSlot == 1, juce::dontSendNotification);
}

void RajvikEQAudioProcessorEditor::setGuiScale (float s)
{
    const float old = Look::prefs.scale;
    if (std::abs (s - old) < 0.001f) return;
    Look::prefs.scale = s;
    Look::prefs.save();
    scaleChanged (old);
}

void RajvikEQAudioProcessorEditor::showScaleMenu()
{
    static const int pcts[] = { 50, 60, 75, 90, 100, 125, 150, 175, 200 };
    juce::PopupMenu m;
    for (int pc : pcts) m.addItem (pc, juce::String (pc) + "%", true, pc == juce::roundToInt (Look::prefs.scale * 100.0f));
    juce::Component::SafePointer<RajvikEQAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleBtn), [safe] (int r)
    {
        if (safe != nullptr && r > 0) safe->setGuiScale ((float) r / 100.0f);
    });
}

void RajvikEQAudioProcessorEditor::applyLook()
{
    laf.refreshColours();
    sendLookAndFeelChange();
    repaint();
}

void RajvikEQAudioProcessorEditor::resetSize()
{
    Look::prefs.scale = 1.0f;
    Look::prefs.width = 1060;
    Look::prefs.height = 600;
    Look::prefs.save();
    setResizeLimits (minW, minH, maxW, maxH);
    setSize (1060, 600);
    resized();
}

void RajvikEQAudioProcessorEditor::scaleChanged (float oldScale)
{
    const float s = Look::prefs.scale;
    const float lw = (float) getWidth() / oldScale, lh = (float) getHeight() / oldScale;   // keep the same unscaled layout size
    setResizeLimits (juce::roundToInt ((float) minW * s), juce::roundToInt ((float) minH * s),
                     juce::roundToInt ((float) maxW * s), juce::roundToInt ((float) maxH * s));
    setSize (juce::roundToInt (lw * s), juce::roundToInt (lh * s));
    resized();
}

void RajvikEQAudioProcessorEditor::showSettings()
{
    juce::Component::SafePointer<RajvikEQAudioProcessorEditor> safe (this);
    auto content = std::make_unique<SettingsPanel> (proc, root,
        [safe] { if (safe != nullptr) safe->applyLook(); },
        [safe] (float old) { if (safe != nullptr) safe->scaleChanged (old); },
        [safe] { if (safe != nullptr) safe->resetSize(); });
    juce::CallOutBox::launchAsynchronously (std::move (content), root.getLocalArea (&gearBtn, gearBtn.getLocalBounds()), &root);
}

void RajvikEQAudioProcessorEditor::showToolsMenu()
{
    const int cap = display.getCapture();
    juce::PopupMenu m, mm;
    m.addItem (1, "EQ Sketch (draw a curve)", true, display.sketchMode);
    m.addItem (2, display.isListeningForPeaks() ? "Auto Peaks: listening... (click to cancel)" : "Auto Peaks (find the loudest peaks)");
    m.addItem (3, "Instances (compare with other EQs)");
    m.addSeparator();
    mm.addItem (10, "1. Capture reference (play the reference)", true, cap == 1);
    mm.addItem (11, "2. Capture source (play your track)", true, cap == 2);
    mm.addItem (12, "3. Apply match");
    m.addSubMenu ("EQ Match", mm);

    juce::Component::SafePointer<RajvikEQAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsBtn), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& d = safe->display;
        if (r == 1) { d.sketchMode = ! d.sketchMode; d.repaint(); }
        else if (r == 2) d.startPeakSearch();
        else if (r == 3) d.showInstances();
        else if (r == 10) d.setCapture (d.getCapture() == 1 ? 0 : 1);
        else if (r == 11) d.setCapture (d.getCapture() == 2 ? 0 : 2);
        else if (r == 12) { d.setCapture (0); d.applyMatch(); }
    });
}

//==============================================================================
void RajvikEQAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu m, fm, um, dm;
    const auto& fac = Presets::factory();
    {
        std::vector<std::pair<juce::String, juce::PopupMenu>> cats;   // factory presets grouped by category
        for (int i = 0; i < (int) fac.size(); ++i)
        {
            const auto& f = fac[(size_t) i];
            size_t ci = 0;
            while (ci < cats.size() && cats[ci].first != f.category) ++ci;
            if (ci == cats.size()) cats.push_back ({ f.category, juce::PopupMenu() });
            cats[ci].second.addItem (1000 + i, f.name, true, f.name == Presets::presetName (proc));
        }
        for (auto& cat : cats) fm.addSubMenu (cat.first, cat.second);
    }
    const auto files = Presets::userFiles();
    if (files.isEmpty()) um.addItem (-1, "(none yet - use Save)", false);
    for (int i = 0; i < files.size(); ++i)
    {
        um.addItem (2000 + i, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == Presets::presetName (proc));
        dm.addItem (3000 + i, files[i].getFileNameWithoutExtension());
    }
    m.addSubMenu ("Factory presets", fm);
    m.addSubMenu ("My presets", um);
    m.addSeparator();
    m.addItem (1, "Save current as new preset...");
    m.addSubMenu ("Delete a preset", dm, ! files.isEmpty());
    m.addItem (2, "Open presets folder");
    m.addSeparator();
    m.addItem (3, "Reset all bands (Init)");

    juce::Component::SafePointer<RajvikEQAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe, files] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& p = safe->proc;
        if (r >= 1000 && r < 2000) Presets::applyFactory (p, r - 1000);
        else if (r >= 2000 && r < 3000) { if (r - 2000 < files.size()) Presets::loadUser (p, files[r - 2000]); }
        else if (r >= 3000 && r < 4000) { if (r - 3000 < files.size()) files[r - 3000].deleteFile(); }
        else if (r == 1) safe->promptSave();
        else if (r == 2) { Presets::userDir().createDirectory(); Presets::userDir().revealToUser(); }
        else if (r == 3) Presets::applyFactory (p, 0);
        safe->display.selSet.fill (false);
        safe->repaint();
    });
}

void RajvikEQAudioProcessorEditor::stepPreset (int dir)
{
    const auto& fac = Presets::factory();
    const auto files = Presets::userFiles();
    juce::StringArray names;
    for (auto& f : fac) names.add (f.name);
    for (auto& f : files) names.add (f.getFileNameWithoutExtension());
    if (names.isEmpty()) return;
    int cur = names.indexOf (Presets::presetName (proc));
    cur = cur < 0 ? (dir > 0 ? 0 : names.size() - 1) : ((cur + dir) % names.size() + names.size()) % names.size();
    if (cur < (int) fac.size()) Presets::applyFactory (proc, cur);
    else Presets::loadUser (proc, files[cur - (int) fac.size()]);
    display.selSet.fill (false);
    repaint();
}

void RajvikEQAudioProcessorEditor::promptSave()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Give your preset a name. It is stored in your presets folder and appears under \"My presets\".",
                                      juce::MessageBoxIconType::NoIcon, this);
    auto cur = Presets::presetName (proc);
    aw->addTextEditor ("name", cur == "Init (Flat)" ? juce::String ("My preset") : cur, "Name");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<RajvikEQAudioProcessorEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        Presets::saveUser (safe->proc, aw->getTextEditorContents ("name"));
    }), true);
}

// Pro-Q style: the band panel opens when a band is clicked and docks at the bottom centre of the display
// (it hops to the top when the selected node would be hidden behind it).
void RajvikEQAudioProcessorEditor::placePanel()
{
    const int b = display.selected;
    const auto grp = display.groupBands();
    const bool multi = grp.size() > 1;
    const bool show = panelOpen && (multi || display.isBandEnabled (b));
    panel.setVisible (show);
    if (! show) return;

    if (multi) panel.setGroup (grp);                         // several bands: every knob changes all of them by the same amount
    else if (panel.isGroupMode()) panel.select (b);          // selection shrank back to one band
    panel.update();

    // The band panel follows the window size: it shrinks with a small window and grows with a big one (1.0 = the default window).
    // A component transform is applied around the PARENT's origin, so the panel is scaled around its own top-left corner.
    const auto db = display.getBounds();
    const int pw = panel.idealWidth(), ph = BandPanel::totalHeight;
    const float fFit = (float) (db.getWidth() - 8) / (float) pw;                                             // never wider than the display
    const float fRef = juce::jmin ((float) db.getWidth() / 960.0f, (float) db.getHeight() / 534.0f);       // proportional to the window
    const float f = juce::jlimit (0.4f, 1.5f, juce::jmin (fFit, fRef));
    const float vw = (float) pw * f, vh = (float) ph * f;
    const int x = db.getX() + juce::roundToInt (((float) db.getWidth() - vw) * 0.5f);
    const int yBottom = db.getBottom() - juce::roundToInt (vh) - 20 - (proc.apvts.getRawParameterValue ("notes")->load() > 0.5f ? 44 : 0), yTop = db.getY() + 6;

    const juce::Rectangle<float> bottomR ((float) x, (float) yBottom, vw, vh);
    const juce::Rectangle<float> topR ((float) x, (float) yTop, vw, vh);
    auto covers = [&] (const juce::Rectangle<float>& r)   // does the panel hide any node of the selection?
    {
        const auto area = r.expanded (24.0f);
        if (multi) { for (int g : grp) if (area.contains (display.getNodePos (g) + db.getPosition().toFloat())) return true; return false; }
        return area.contains (display.getNodePos (b) + db.getPosition().toFloat());
    };
    if (covers (bottomR))   panelOnTop = true;
    else if (covers (topR)) panelOnTop = false;

    const int y = panelOnTop ? yTop : yBottom;
    panel.setTransform (juce::AffineTransform::scale (f, f, (float) x, (float) y));
    panel.setBounds (x, y, pw, ph);
}

void RajvikEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Look::bgBot);
}

void RajvikEQAudioProcessorEditor::paintRoot (juce::Graphics& g)
{
    const int w = root.getWidth(), h = root.getHeight();
    g.setGradientFill (juce::ColourGradient (Look::bgTop, 0.0f, 0.0f, Look::bgBot, 0.0f, (float) h, false));
    g.fillAll();

    // slim header
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (0, 0, w, headerH);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (headerH - 1, 0.0f, (float) w);

    juce::AttributedString as;
    as.append ("RAJVIK ", juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), Look::text);
    as.append ("Pro EQ", juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), Look::gold);
    as.draw (g, juce::Rectangle<float> (12.0f, 6.0f, 130.0f, 24.0f));

    // slim bottom bar
    const int bt = h - bottomH;
    g.setColour (Look::bar.withAlpha (0.92f));
    g.fillRect (0, bt, w, bottomH);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (bt, 0.0f, (float) w);
}

void RajvikEQAudioProcessorEditor::resized()
{
    const float s = Look::prefs.scale;
    root.setTransform (juce::AffineTransform::scale (s));
    root.setBounds (0, 0, juce::roundToInt ((float) getWidth() / s), juce::roundToInt ((float) getHeight() / s));
    layoutRoot();
}

void RajvikEQAudioProcessorEditor::layoutRoot()
{
    auto r = root.getLocalBounds();
    auto hdr = r.removeFromTop (headerH);

    // left: logo, undo / redo, A/B compare
    int x = 138;
    undoBtn.setBounds (x, 5, 42, 24);  x += 44;
    redoBtn.setBounds (x, 5, 42, 24);  x += 52;
    slotA.setBounds (x, 5, 26, 24);    x += 28;
    slotB.setBounds (x, 5, 26, 24);    x += 30;
    copyBtn.setBounds (x, 5, 42, 24);

    // right: presets (arrows, name, save) next to Tools and the settings icon
    int rx = hdr.getRight() - 8;
    gearBtn.setBounds (rx - 30, 4, 30, 26);          rx -= 36;
    toolsBtn.setBounds (rx - 70, 5, 70, 24);         rx -= 78;
    presetSave.setBounds (rx - 46, 5, 46, 24);       rx -= 50;
    presetNext.setBounds (rx - 22, 5, 22, 24);       rx -= 24;
    presetBtn.setBounds (rx - 130, 5, 130, 24);      rx -= 132;
    presetPrev.setBounds (rx - 22, 5, 22, 24);

    auto bot = r.removeFromBottom (bottomH).reduced (8, 2);
    modeBox.setBounds (bot.removeFromLeft (112));  bot.removeFromLeft (6);
    osL.setBounds (bot.removeFromLeft (80));
    osBox.setBounds (bot.removeFromLeft (52));  bot.removeFromLeft (8);
    pianoBtn.setBounds (bot.removeFromLeft (68));
    outSlider.setBounds (bot.removeFromRight (juce::jlimit (100, 200, bot.getWidth() / 4)));
    outL.setBounds (bot.removeFromRight (44));
    bot.removeFromRight (6);
    scaleBtn.setBounds (bot.removeFromRight (50));
    bot.removeFromRight (6);
    anaBox.setBounds (bot.removeFromRight (130));
    nameBtn.setBounds (bot.reduced (8, 0).withSizeKeepingCentre (juce::jmax (0, juce::jmin (200, bot.getWidth() - 16)), bot.getHeight()));

    auto stripArea = r.removeFromRight (stripW);
    display.setBounds (r);
    strip.setBounds (stripArea);
    placePanel();
}
