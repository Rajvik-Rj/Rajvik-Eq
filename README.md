# RAJVIK Pro EQ

A 24-band equalizer, VST3 (JUCE / C++). Built on GitHub Actions - no local setup needed.

## Get the plugin
1. Push this folder to a GitHub repo.
2. Open the **Actions** tab -> **Build RAJVIK Pro EQ** -> wait for the green tick.
3. Download the **RajvikEQ-windows-latest-VST3** artifact, unzip.
4. Copy `RAJVIK Pro EQ.vst3` to `C:\Program Files\Common Files\VST3\` and rescan in your DAW.
(macOS artifact is built too: copy to `/Library/Audio/Plug-Ins/VST3/`.)

## Features
- 24 bands: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt
- Cut slopes 6 to 96 dB/oct plus Brickwall, per-band Q (cut resonance)
- Per band stereo placement: Stereo / Left / Right / Mid / Side
- Every band has a 3-way mode selector (band panel dropdown, or right-click menu on the node):
  - **Normal EQ**: plain static band
  - **Dynamic EQ**: threshold, range (+/-30 dB), attack, release, Above/Below; the detector follows the band's stereo placement (L / R / Mid / Side)
  - **Spectral Dynamic EQ**: STFT based, only bins over the threshold inside the band shape are processed (same controls)
- Dynamic and Spectral are available on gain type bands (Bell, Shelves, Tilt). Cut / Notch / Band Pass bands are always Normal.
- Processing modes: Zero Latency, Natural Phase (2x oversampled, analog-like response at high frequencies), Linear Phase (latency reported to the DAW)
- EQ Sketch: press "EQ Sketch", draw a curve (the button switches itself off afterwards), it is converted into bell bands
- EQ Match: capture Reference (play the reference), capture Source (play your track), press Apply - bands are fitted to the difference
- Instances list: see other RAJVIK Pro EQ instances in the project, show their spectrum behind this one, copy their settings
- Surround: mono up to 16 channels (9.1.6) in the plugin layout; bands apply to every channel
- Character: Clean / Subtle / Warm, output gain, phase invert
- Real-time pre/post analyzer, display range 3/6/12/30 dB
- Frequency range 10 Hz to 20 kHz. "Piano" toggle (bottom bar) shows a piano keyboard strip under the display (one key per semitone on the same frequency axis): the selected band's key lights up with its note name + frequency, and hovering shows the note under the mouse
- Readout: the selected band always shows its frequency and gain in a bubble next to the node (also while you drag it, wherever you take it); hovering any node shows the same
- Sidechain: optional second input bus ("Sidechain", mono or stereo). Each Dynamic or Spectral band has a **Sidechain** toggle: its detector then listens to the sidechain input instead of the main signal (works in Zero Latency, Natural and Linear Phase modes). Enable the sidechain input in your DAW's plugin routing.
- Analyzer: pre (grey line) and post (blue fill) spectrum, 4.5 dB/oct tilt, about 1/12 octave smoothing, and the hover readout also shows the level at the cursor
- **Multi select :** drag a rectangle on empty space, Shift/Ctrl-click to add or remove bands, Ctrl+A for all. Selected bands move together (drag), mouse wheel changes Q of all, arrow keys nudge (Shift = fine), Delete removes them, right-click menu changes type / mode for all. Esc deselects.
- **Presets:** header bar `< Preset > Save`. 14 factory presets (vocal, bass, de-esser, spectral resonance control, sidechain ducking, ...), "My presets" saved as `.rajpreset` files in your user application data folder (`RajvikEQ/Presets`), delete / open folder from the menu. Presets hold the 24 bands only (not output gain or processing mode).
- **Spectral is purely spectral:** a Spectral band has no static gain and no dynamic-EQ curve. Its node sits on the 0 dB line (move it for frequency, wheel for width), and the display shows the live per-frequency cut it applies. Dynamic bands keep their static gain + gain offset.
- **Pre / Post analyzer view:** the dropdown next to Analyzer shows the input spectrum (Pre), the output spectrum (Post) or both.
- **Auto Peaks:** press the button (bottom bar) and play your audio for ~2.5 seconds. The loudest peaks of the input spectrum are found (up to 6), each gets a narrow bell at 0 dB, and all of them are selected together, so one drag / mouse wheel / arrow key / Delete acts on every peak at once.
- Interactive display: double-click = add band, drag = freq/gain, wheel = Q, right-click = type/dynamic/delete, double-click a node = remove

## Look and mouse
Modern dark UI: purple-charcoal background, full-width response display with a glowing gold curve, glowing band nodes, dB scale + level meters on the right, mode/character/output in the bottom bar.
Double-click empty area to add a band. Click a band and a panel opens, docked at the bottom centre: power, type + slope, FREQ / GAIN / Q knobs, stereo placement, band stepper (< >), Dynamic toggle (expands Threshold / Range / Attack / Release + Spectral), close (x). Click empty space or x to close it. Hover shows the frequency under the cursor.

## Current limits (honest list)
- Linear Phase: static Stereo/Left/Right bands are true linear phase; dynamic and Mid/Side bands run as normal filters after the FIR.
- Natural Phase, Linear Phase and Spectral Dynamics only work on stereo tracks. Mono and surround layouts run in Zero Latency mode, and Spectral bands automatically fall back to time-domain Dynamic EQ there.
- Spectral Dynamics adds 2048 samples of latency (reported to the DAW) while at least one Spectral band is on.
- In surround, Left/Right/Mid/Side placement only affects the first two (front L/R) channels; "Stereo" placement hits every channel. No per-speaker placement UI yet.
- EQ Sketch/Match are approximations (up to 8 / 10 bell bands) - edit the bands afterwards.
- Instance list works for instances in the same process (normal in most DAWs); track names show if the host provides them.
- Compiled and tested headless on Linux (all three band modes, mono fallback, Natural and Linear Phase); the Windows/macOS GitHub build is still the final check.

## UI update: slim bars, settings, resizing
- **Slim layout:** header is 34 px (logo, preset `< name > Save`, **Tools** menu, **gear**), bottom bar is 26 px (processing mode, Analyzer toggle, Output).
- **Tools menu:** EQ Sketch, Auto Peaks, Instances, EQ Match (capture reference / capture source / apply).
- **Gear (settings):** GUI colour (8 presets + custom colour picker), background theme (5), **GUI size 60 - 150 %**, and the moved options: Analyzer, Piano view, Phase invert, Analyzer view, Display range, Character. Settings are saved in `<user app data>/RajvikEQ/settings.xml` (shared by all instances).
- **Resizing:** drag the window corner freely (minimum is 1000 x 520 at 100 %); use GUI size below 100 % to make the whole plugin smaller on screen (e.g. 60 % = 600 x 312 minimum).
- **Spectral dynamics follow Q:** a Spectral band only reacts inside its own width (f0 / Q, exactly the shaded region on the display). Higher Q = narrower, lower Q = wider; bins far from the band are untouched.

## Multi-band editing, smoother sound
- **Group panel:** select several bands (rectangle, Shift/Ctrl-click, Ctrl+A) and the same band panel opens for the whole group. Knobs are *relative*: GAIN +/- dB, FREQ +/- octaves, Q x factor, THRESH / RANGE +/- dB, ATTACK / RELEASE x factor - each change is applied to every selected band by the same amount (double-click a knob = back to no change). Type, slope, stereo placement, Normal/Dynamic/Spectral mode, direction and sidechain are set on all bands; a field is blank when the bands differ.
- **No zipper noise:** frequency, gain and Q glide (about 12 ms) instead of jumping; output gain and phase invert are smoothed; switching a band on or off crossfades over 5 ms (no clicks).
- **CPU:** spectral bands only process the bins inside their own width; the response curves in the editor use a cheaper exact formula.
- **Latency:** Zero Latency mode = 0 samples. Natural Phase = a few samples. Spectral bands add 2048 samples only while at least one is enabled. Linear Phase = 4096 samples.

## v4: oversampling, defaults, presets, resizing
- **Oversampling** (bottom bar): Off / 2x / 4x / 8x for the EQ bands (Zero Latency and Natural Phase modes; Natural Phase is always at least 2x). Minimum-phase half-band resampling = only a few samples of latency, which is reported to the host. Linear Phase designs its FIR at 2x internally.
- **What you see is what you hear:** the response curve, band fitting (EQ Sketch / Match / Auto Peaks) and the dynamic detector all use the same filter design and sample rate as the audio path (including oversampling), and a spectral band reacts exactly inside the region shaded on the display.
- **Settings (gear):** new *Reset to default* row: Colours, Size, Reset all (colours, size, mode, oversampling, character, display range, analyzer options, output gain; bands are never touched).
- **Presets:** 56 factory presets in categories (General, Vocals, Drums, Bass, Guitars & Keys, Mix Bus & Master, Fix & Creative) incl. Mid/Side, dynamic, spectral and hum-remover presets.
- **Resizing:** window minimum 640 x 400 (unscaled), maximum 3200 x 2000; GUI size 40 - 200 %. The band panel shrinks to fit small windows.

## Band solo
- **Solo** button in the band panel (next to the close button), also *Solo band* in the right-click menu of a band. You hear only the region that band affects: band-pass at its frequency and Q (low-pass for low shelf / low cut, high-pass for high shelf / high cut), respecting Left / Right / Mid / Side placement. Switching is crossfaded (10 ms).
- Solo is a monitoring aid: it is not saved, it ends when the editor closes or the band is switched off, and the display shows "SOLO band N" while active.

## v6: new analyzer 
- **Calibrated:** a full scale sine reads 0 dB (within the 1.4 dB scalloping of the Hann window). Left and right are analyzed separately and their power is averaged (a mono sum hides or invents peaks through phase cancellation).
- **Scale:** 0 dBFS at the top, range 60 / 90 / 120 dB (default 90) with its own dB labels on the left. Before, the analyzer was squeezed into a 80 dB window that reached the top of the panel.
- **Settings (gear > Analyzer):** View (Pre / Post / Both), Range, **Resolution** (Low 2048 / Medium 4096 / High 8192 / Maximum 16384 point FFT), **Speed** (release: very slow .. very fast), **Tilt** (0 / 1.5 / 3 / 4.5 / 6 dB per octave around 1 kHz, default 4.5), **Freeze**.
- **Accuracy:** low frequencies are interpolated without overshoot, high frequencies use a power average over about 3 pixels; a peak is drawn at the frequency where it really is. Only the shown spectrum (Pre or Post) is calculated, the analyzer uses no CPU when switched off.
- Auto Peaks and EQ Match keep their own fixed 4096 point analysis, independent of the resolution setting.
- Note: broadband noise reads about 3 dB lower for every higher resolution step (narrower bins); tones keep their level.

## v7 (final) changes
- Analyzer rebuilt as a multi-resolution analyzer: long FFT for the bass, a 4x shorter FFT for mids/highs, cross-faded by frequency. It now follows the sound with almost no delay.
- Analyzer smoothing uses the real frame time, display runs at 60 fps.
- Grid and EQ curves are cached as images, the band panel shadow is cached: much lower CPU while audio plays.
- Preset section ([<] name [>] Save) moved to the right, next to Tools and the settings icon.
- Piano button added to the bottom bar next to Oversampling / Analyzer.
- The band panel (all knobs: Freq, Gain, Q, dynamics ...) now scales with the window size, both smaller and bigger.

## v8 (final) changes
- **Dynamic and Spectral now use the same threshold.** Both modes measure the band level the same way (power inside the band, a 0 dBFS sine reads 0 dB), use the same reaction curve (half of the range at the threshold, nothing 6 dB below, full range 6 dB above it) and the same attack / release in dB. Before, Spectral compared single FFT bins (about 10-17 dB lower than the band level) and Dynamic read about 3 dB too high, so Spectral needed a much lower threshold. Switching a band between Dynamic and Spectral now keeps the threshold in the same place (tested: sine, white and pink noise agree within about 1 dB).
- **Everything reacts to the audio, not to the display.** The Dynamic detector and the Spectral FFT stage run inside the audio thread; the analyzer is only a drawing and is not used by the dynamics (it keeps working with the editor closed).
- **Threshold display:** the dashed line now uses the analyzer's dB scale (0 dBFS on top). A solid white marker shows the live band level the detector measures: when it is above the dashed line the band reacts. The analyzer curve itself is per bin and tilted, so compare the threshold with the marker, not with the curve.
- **Spectral bands honour Left / Right / Mid / Side placement** (detector and cut).
- **Window resize fixed:** the band panel's position was scaled together with its size, so it slid out of the window when the window was resized. It now stays inside the display at every size. Window size 480 x 300 up to 4000 x 2400 (unscaled); the bottom bar hides its less important items on narrow windows instead of overlapping.
- **Double click near the edges :** double click below about 30 Hz adds a Low Cut (high pass) at the clicked frequency, above about 13 kHz a High Cut (low pass) at the clicked frequency (12 dB/oct, change the slope in the panel). Anywhere else: Bell.
- Factory presets with Dynamic / Spectral bands got new thresholds for the unified detector.

## v9: new look
- Own visual identity: Deep Teal / Mint default theme, diamond band nodes, flat panel with a coloured edge bar, new dot-ring knobs.
- Knobs are about 25 % smaller; the band panel is smaller and lower (132 px instead of 156 px, 480 / 760 px wide).

## v10: black look, docked band strip
- Default look is now black (new "Black" background, white accent; every other colour and background is still in the settings menu).
- The band properties are no longer a floating panel: they sit in a slim strip docked at the bottom, directly above the bottom bar (Zero latency / oversampling / analyzer / piano / output). The strip keeps its place, scales with the window and shows Enable, prev / next band, type, slope, Freq, Gain, Q, placement, mode, Solo and Close; the Dynamic / Spectral controls (sidechain, direction, Threshold, Range, Attack, Release) appear in the same row.

## v11: screenshot look, band strip beside the meters, EQ range from the meter section
- Look follows the reference screenshot: Midnight background + gold accent (default), round band nodes, rounded band strip with the knobs centred and each knob's name and value to its right.
- The band strip is docked under the display and ends at the left edge of the meter section (it no longer runs under the meters). It scales with the window.
- Click the meter / scale section (the range badge or anywhere on it) to choose the EQ range: +/-3, 6, 12, 18, 24, 30, 36 or 48 dB. The mouse wheel over the section also steps the range. The same list is in the settings menu ("Display Range"). Default 12 dB.
- The analyzer dB numbers on the meter section now follow the analyzer range setting.
