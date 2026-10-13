#include "PluginEditor.h"

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
    preSm.fill (-120.0f);
    postSm.fill (-120.0f);
    overlaySm.fill (-120.0f);
    startTimerHz (30);
}

float EQDisplay::dispRange() const
{
    static const float r[] = { 3.0f, 6.0f, 12.0f, 30.0f };
    return r[juce::jlimit (0, 3, (int) apvtsVal ("disp_range"))];
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

void EQDisplay::select (int b)
{
    selected = b;
    if (onSelect) onSelect (b);
    repaint();
}

juce::Point<float> EQDisplay::nodePos (int b) const
{
    const int type = (int) apvtsVal (pid (b, "type"));
    const float g = typeHasGain (type) ? apvtsVal (pid (b, "gain")) : 0.0f;
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

void EQDisplay::timerCallback()
{
    const bool ana = apvtsVal ("analyzer") > 0.5f;
    if (ana || capMode != 0)
    {
        updateSpectrum (proc.preAnalyzer, preSm);
        if (capMode != 0)   // fftData still holds the input magnitudes: accumulate for EQ Match
        {
            auto& acc = capMode == 1 ? refAcc : srcAcc;
            for (int k = 0; k <= 2048; ++k) acc[(size_t) k] += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];
            (capMode == 1 ? refN : srcN)++;
        }
    }
    if (ana)
    {
        updateSpectrum (proc.postAnalyzer, postSm);
        if (overlayProc != nullptr)
        {
            if (RajvikEQAudioProcessor::snapshotPost (overlayProc, snap.data())) spectrumFromSnap (overlaySm);
            else overlayProc = nullptr;
        }
    }
    repaint();
}

void EQDisplay::updateSpectrum (const Analyzer& a, std::array<float, 2049>& sm)
{
    a.snapshot (snap.data());
    spectrumFromSnap (sm);
}

void EQDisplay::spectrumFromSnap (std::array<float, 2049>& sm)
{
    std::copy (snap.begin(), snap.end(), fftData.begin());
    std::fill (fftData.begin() + 4096, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), 4096);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int k = 0; k <= 2048; ++k)
    {
        const float db = 20.0f * std::log10 (fftData[(size_t) k] / 1024.0f + 1.0e-9f);
        float& s = sm[(size_t) k];
        s += (db > s ? 0.6f : 0.15f) * (db - s);
    }
}

void EQDisplay::drawSpectrum (juce::Graphics& g, const std::array<float, 2049>& sm, juce::Colour c, float alpha)
{
    const float h = (float) getHeight();
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    juce::Path line;
    for (int x = 0; x < getWidth(); x += 2)
    {
        const float pos = juce::jlimit (0.0f, 2047.0f, xToFreq ((float) x) * 4096.0f / (float) fs);
        const int k = (int) pos;
        const float fr = pos - (float) k;
        const float db = sm[(size_t) k] * (1.0f - fr) + sm[(size_t) k + 1] * fr;
        const float y = h * (1.0f - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 80.0f));
        if (x == 0) line.startNewSubPath (0.0f, y); else line.lineTo ((float) x, y);
    }
    juce::Path fill (line);
    fill.lineTo ((float) getWidth(), h);
    fill.lineTo (0.0f, h);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (c.withAlpha (alpha), 0.0f, h * 0.25f, c.withAlpha (0.0f), 0.0f, h, false));
    g.fillPath (fill);
    g.setColour (c.withAlpha (juce::jmin (1.0f, alpha * 2.2f)));
    g.strokePath (line, juce::PathStrokeType (1.1f));
}

void EQDisplay::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (juce::Colours::black.withAlpha (0.30f));
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

    if (apvtsVal ("analyzer") > 0.5f)
    {
        drawSpectrum (g, preSm, juce::Colours::white, 0.10f);
        drawSpectrum (g, postSm, juce::Colour (0xffc9c4d8), 0.22f);
        if (overlayProc != nullptr) drawSpectrum (g, overlaySm, juce::Colour (0xffff9f43), 0.30f);
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
            const float ty = h * (1.0f - juce::jlimit (0.0f, 1.0f, (apvtsVal (pid (sb, "thr")) + 100.0f) / 80.0f));
            g.setColour (col.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, juce::jmax (1.0f, x2 - x1), h));
            const float dash[] = { 6.0f, 4.0f };
            g.setColour (col.withAlpha (0.95f));
            g.drawDashedLine (juce::Line<float> (x1 - 30.0f, ty, x2 + 30.0f, ty), dash, 2, 1.5f);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawText (juce::String (apvtsVal (pid (sb, "thr")), 1) + " dB", (int) x1 - 30, (int) ty - 16, 70, 14, juce::Justification::left);

            if (skind == ModeSpectral)
            {
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
    }

    // band designs (static gain + current dynamic offset)
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    std::array<BandDesign, numBands> designs;
    std::array<bool, numBands> on {};
    int numOn = 0;
    for (int b = 0; b < numBands; ++b)
    {
        on[(size_t) b] = apvtsVal (pid (b, "en")) > 0.5f;
        if (! on[(size_t) b]) continue;
        ++numOn;
        design (designs[(size_t) b], (int) apvtsVal (pid (b, "type")), apvtsVal (pid (b, "freq")),
                apvtsVal (pid (b, "gain")) + proc.dynDb[(size_t) b].load(), apvtsVal (pid (b, "q")),
                (int) apvtsVal (pid (b, "slope")), fs);
    }

    // per band curves (thin, coloured) + selected band fill; sum for the combined curve
    const int nx = getWidth() / 2 + 2;
    const float zeroY = gainToY (0.0f);
    std::vector<float> total ((size_t) nx, 0.0f);
    for (int b = 0; b < numBands; ++b)
    {
        if (! on[(size_t) b]) continue;
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
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) hoverX, 0.0f, h);
        const float rw = showNotes ? 150.0f : 76.0f;
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
        if (kind != ModeNormal)
        {
            const float base = typeHasGain (type) ? apvtsVal (pid (b, "gain")) : 0.0f;
            const float y2 = gainToY (base + proc.dynDb[(size_t) b].load());
            g.setColour (c.withAlpha (0.8f));
            g.drawLine (p.x, p.y, p.x, y2, 2.0f);
            g.drawEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f, kind == ModeSpectral ? 2.5f : 1.2f);
        }
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.45f), p.x, p.y, c.withAlpha (0.0f), p.x + 22.0f, p.y, true));
        g.fillEllipse (p.x - 22.0f, p.y - 22.0f, 44.0f, 44.0f);
        g.setColour (c);
        g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 1.3f);
        if (b == selected) { g.setColour (juce::Colours::white.withAlpha (0.75f)); g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 1.2f); }
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
    if (sketchMode) { sketch.clear(); sketch.push_back (e.position); return; }
    const int h = hitTest (e.position);
    if (h < 0) { if (onBackgroundClick) onBackgroundClick(); return; }
    select (h);
    if (e.mods.isPopupMenu()) { showMenu (h); return; }
    dragging = true;
    gesture (pid (h, "freq"), true);
    gesture (pid (h, "gain"), true);
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    hoverX = e.position.x;
    if (sketchMode) { sketch.push_back (e.position); repaint(); return; }
    if (! dragging) return;
    const int b = selected;
    setReal (pid (b, "freq"), juce::jlimit (10.0f, 20000.0f, xToFreq (e.position.x)));
    if (typeHasGain ((int) apvtsVal (pid (b, "type"))))
        setReal (pid (b, "gain"), juce::jlimit (-30.0f, 30.0f, yToGain (e.position.y)));
}

void EQDisplay::mouseUp (const juce::MouseEvent&)
{
    if (sketchMode) { finishSketch(); return; }
    if (! dragging) return;
    dragging = false;
    gesture (pid (selected, "freq"), false);
    gesture (pid (selected, "gain"), false);
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
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

void EQDisplay::mouseMove (const juce::MouseEvent& e) { hoverX = e.position.x; hoverY = e.position.y; }
void EQDisplay::mouseExit (const juce::MouseEvent&) { hoverX = -1.0f; hoverY = -1.0f; }

void EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd)
{
    int b = hitTest (e.position);
    if (b < 0) b = selected;
    if (apvtsVal (pid (b, "en")) < 0.5f) return;
    const float q = apvtsVal (pid (b, "q"));
    setReal (pid (b, "q"), juce::jlimit (0.025f, 40.0f, q * std::exp (wd.deltaY * 1.2f)));
}

void EQDisplay::showMenu (int b)
{
    juce::PopupMenu m;
    const int cur = (int) apvtsVal (pid (b, "type"));
    for (int i = 0; i < numTypes; ++i) m.addItem (i + 1, typeNames[i], true, i == cur);
    m.addSeparator();
    const bool canDyn = typeHasGain (cur);
    const int kind = dynModeOf (apvtsVal (pid (b, "dyn")) > 0.5f, apvtsVal (pid (b, "spec")) > 0.5f, cur);
    m.addItem (20, "Normal EQ",           true,   kind == ModeNormal);
    m.addItem (21, "Dynamic EQ",          canDyn, kind == ModeDynamic);
    m.addItem (22, "Spectral Dynamic EQ", canDyn, kind == ModeSpectral);
    m.addSeparator();
    m.addItem (30, "Delete band");

    juce::Component::SafePointer<EQDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* d = safe.getComponent();
        if (r >= 1 && r <= numTypes) d->setIndex (pid (b, "type"), r - 1);
        else if (r >= 20 && r <= 22)
        {
            d->setIndex (pid (b, "dyn"),  r == 20 ? 0 : 1);
            d->setIndex (pid (b, "spec"), r == 22 ? 1 : 0);
        }
        else if (r == 30) d->setIndex (pid (b, "en"), 0);
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
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
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
    if (others == 0) m.addItem (-1, "No other Rajvik EQ instances found", false);
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
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    prevBtn.onClick  = [this] { if (onStep) onStep (-1); };
    nextBtn.onClick  = [this] { if (onStep) onStep (1); };

    numLabel.setJustificationType (juce::Justification::centred);
    numLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));

    for (auto* c : std::initializer_list<juce::Component*> { &enBtn, &closeBtn, &prevBtn, &nextBtn, &numLabel,
                                                             &typeBox, &slopeBox, &placeBox, &dirBox, &dynModeBox })
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

void BandPanel::setDynMode (int mode)
{
    if (mode < 0 || mode > ModeSpectral || (mode != ModeNormal && ! canDynamic())) return;
    auto set = [this] (const char* n, bool on)
    {
        if (auto* p = proc.apvts.getParameter (pid (band, n)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    };
    set ("dyn",  mode != ModeNormal);
    set ("spec", mode == ModeSpectral);
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
    freqA.reset(); gainA.reset(); qA.reset(); thrA.reset(); rangeA.reset(); attA.reset(); relA.reset();
    typeA.reset(); slopeA.reset(); placeA.reset(); dirA.reset(); enA.reset();

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
    const int m = dynMode();
    const bool d = m != ModeNormal, c = isCut(), cd = canDynamic();
    if (m == lastMode && c == lastCut && cd == lastCanDyn) return;
    lastMode = m; lastCut = c; lastCanDyn = cd;
    dynModeBox.setSelectedId (m + 1, juce::dontSendNotification);
    dynModeBox.setItemEnabled (ModeDynamic + 1, cd);
    dynModeBox.setItemEnabled (ModeSpectral + 1, cd);
    slopeBox.setVisible (c);
    dirBox.setVisible (d);
    for (auto* k : { &thr, &range, &att, &rel }) { k->s.setVisible (d); k->l.setVisible (d); }
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
    g.setColour (bandColour (band).withAlpha (0.8f));
    g.drawLine (r.getX() + 34.0f, r.getY() + 1.0f, r.getRight() - 34.0f, r.getY() + 1.0f, 1.5f);
}

void BandPanel::resized()
{
    const auto body = getLocalBounds().reduced (12, 10);
    closeBtn.setBounds (body.getRight() - 32, body.getY() + 8, 22, 22);
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
        tcol.removeFromTop (24);
        dirBox.setBounds (tcol.removeFromTop (26));
        for (auto* k : { &thr, &range, &att, &rel })
            k->place (r.removeFromLeft (58).withSizeKeepingCentre (58, 80));
    }
}

//==============================================================================
void ScaleStrip::timerCallback()
{
    const float l = proc.peakL.exchange (0.0f), r = proc.peakR.exchange (0.0f);
    lvL = juce::jmax (l, lvL * 0.88f);
    lvR = juce::jmax (r, lvR * 0.88f);
    repaint();
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

    if (proc.apvts.getRawParameterValue ("analyzer")->load() > 0.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        for (int db = -30; db >= -100; db -= 10)
        {
            const float y = juce::jlimit (8.0f, h - 8.0f, h * (1.0f - ((float) db + 100.0f) / 80.0f));
            g.drawText (juce::String (db), 44, (int) y - 8, 32, 16, juce::Justification::centredRight);
        }
    }

    // level meters
    const float top = 30.0f, bottom = h - 8.0f;
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
RajvikEQAudioProcessorEditor::RajvikEQAudioProcessorEditor (RajvikEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), display (p), strip (p), panel (p)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (display);
    addAndMakeVisible (strip);
    for (auto* c : { &modeBox, &charBox, &rangeBox }) addAndMakeVisible (c);
    addAndMakeVisible (panel);
    display.onSelect = [this] (int b) { panelOpen = true; panel.select (b); };
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
    charBox.addItemList (charNames, 1);
    rangeBox.addItemList (rangeNames, 1);
    for (auto* l : { &charL, &outL })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (l);
    }
    charL.setText ("Character", juce::dontSendNotification);
    outL.setText ("Output", juce::dontSendNotification);
    addAndMakeVisible (analyzerBtn);
    addAndMakeVisible (invertBtn);
    addAndMakeVisible (notesBtn);
    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    outSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (outSlider);

    modeA     = std::make_unique<CA> (p.apvts, "mode", modeBox);
    charA     = std::make_unique<CA> (p.apvts, "character", charBox);
    rangeA    = std::make_unique<CA> (p.apvts, "disp_range", rangeBox);
    analyzerA = std::make_unique<BA> (p.apvts, "analyzer", analyzerBtn);
    invertA   = std::make_unique<BA> (p.apvts, "invert", invertBtn);
    notesA    = std::make_unique<BA> (p.apvts, "notes", notesBtn);
    outA      = std::make_unique<SA> (p.apvts, "out_gain", outSlider);

    for (auto* b : { &sketchBtn, &instBtn, &refBtn, &srcBtn, &applyBtn }) addAndMakeVisible (b);
    sketchBtn.setClickingTogglesState (true);
    refBtn.setClickingTogglesState (true);
    srcBtn.setClickingTogglesState (true);
    sketchBtn.onClick = [this] { display.sketchMode = sketchBtn.getToggleState(); display.repaint(); };
    display.onSketchDone = [this] { sketchBtn.setToggleState (false, juce::dontSendNotification); };
    instBtn.onClick = [this] { display.showInstances(); };
    refBtn.onClick = [this]
    {
        if (refBtn.getToggleState()) srcBtn.setToggleState (false, juce::dontSendNotification);
        display.setCapture (refBtn.getToggleState() ? 1 : 0);
    };
    srcBtn.onClick = [this]
    {
        if (srcBtn.getToggleState()) refBtn.setToggleState (false, juce::dontSendNotification);
        display.setCapture (srcBtn.getToggleState() ? 2 : 0);
    };
    applyBtn.onClick = [this]
    {
        refBtn.setToggleState (false, juce::dontSendNotification);
        srcBtn.setToggleState (false, juce::dontSendNotification);
        display.setCapture (0);
        display.applyMatch();
    };

    setResizable (true, true);
    setResizeLimits (980, 560, 1900, 1100);
    setSize (1120, 680);
    startTimerHz (30);
}

RajvikEQAudioProcessorEditor::~RajvikEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RajvikEQAudioProcessorEditor::timerCallback() { placePanel(); }

// Pro-Q style: the band panel opens when a band is clicked and docks at the bottom centre of the display
// (it hops to the top when the selected node would be hidden behind it).
void RajvikEQAudioProcessorEditor::placePanel()
{
    const int b = display.selected;
    const bool show = panelOpen && display.isBandEnabled (b);
    panel.setVisible (show);
    if (! show) return;
    panel.update();

    const auto db = display.getBounds();
    const int pw = juce::jmin (panel.idealWidth(), db.getWidth() - 8), ph = BandPanel::totalHeight;
    const int x = db.getX() + (db.getWidth() - pw) / 2;
    const int yBottom = db.getBottom() - ph - 20 - (proc.apvts.getRawParameterValue ("notes")->load() > 0.5f ? 44 : 0), yTop = db.getY() + 6;

    const auto np = display.getNodePos (b) + db.getPosition().toFloat();
    const juce::Rectangle<float> bottomR ((float) x, (float) yBottom, (float) pw, (float) ph);
    const juce::Rectangle<float> topR ((float) x, (float) yTop, (float) pw, (float) ph);
    if (bottomR.expanded (24.0f).contains (np))   panelOnTop = true;
    else if (topR.expanded (24.0f).contains (np)) panelOnTop = false;

    panel.setBounds (x, panelOnTop ? yTop : yBottom, pw, ph);
}

void RajvikEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    g.setGradientFill (juce::ColourGradient (Look::bgTop, 0.0f, 0.0f, Look::bgBot, 0.0f, (float) h, false));
    g.fillAll();

    // header
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (0, 0, w, headerH);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (headerH - 1, 0.0f, (float) w);

    juce::AttributedString as;
    as.append ("rajvik ", juce::Font (juce::FontOptions (28.0f)), Look::text);
    as.append ("EQ", juce::Font (juce::FontOptions (28.0f, juce::Font::bold)), Look::gold);
    as.draw (g, juce::Rectangle<float> (18.0f, 6.0f, 220.0f, 36.0f));
    g.setColour (Look::muted);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText ("24-band dynamic equalizer", 20, 38, 220, 14, juce::Justification::left);

    // bottom bar
    const int bt = h - bottomH;
    g.setColour (Look::bar.withAlpha (0.92f));
    g.fillRect (0, bt, w, bottomH);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (bt, 0.0f, (float) w);
}

void RajvikEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    auto hdr = r.removeFromTop (headerH).reduced (14, 13);
    applyBtn.setBounds (hdr.removeFromRight (100));   hdr.removeFromRight (6);
    srcBtn.setBounds   (hdr.removeFromRight (116));   hdr.removeFromRight (6);
    refBtn.setBounds   (hdr.removeFromRight (130));   hdr.removeFromRight (18);
    instBtn.setBounds  (hdr.removeFromRight (92));    hdr.removeFromRight (6);
    sketchBtn.setBounds (hdr.removeFromRight (92));

    auto bot = r.removeFromBottom (bottomH).reduced (14, 7);
    modeBox.setBounds (bot.removeFromLeft (140));     bot.removeFromLeft (14);
    charL.setBounds   (bot.removeFromLeft (70));      bot.removeFromLeft (4);
    charBox.setBounds (bot.removeFromLeft (90));      bot.removeFromLeft (18);
    analyzerBtn.setBounds (bot.removeFromLeft (96));
    invertBtn.setBounds   (bot.removeFromLeft (120));
    notesBtn.setBounds    (bot.removeFromLeft (80));
    outSlider.setBounds (bot.removeFromRight (250));
    outL.setBounds (bot.removeFromRight (56));

    auto stripArea = r.removeFromRight (stripW);
    display.setBounds (r);
    strip.setBounds (stripArea);
    rangeBox.setBounds (stripArea.getX() + 8, stripArea.getY() + 4, 68, 22);
    placePanel();
}
