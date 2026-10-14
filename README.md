# RAJVIK Pro EQ

A Pro-Q 4 style 24-band equalizer, VST3 (JUCE / C++). Built on GitHub Actions - no local setup needed.

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
- Pro-Q style readout: the selected band always shows its frequency and gain in a bubble next to the node (also while you drag it, wherever you take it); hovering any node shows the same
- Sidechain: optional second input bus ("Sidechain", mono or stereo). Each Dynamic or Spectral band has a **Sidechain** toggle: its detector then listens to the sidechain input instead of the main signal (works in Zero Latency, Natural and Linear Phase modes). Enable the sidechain input in your DAW's plugin routing.
- Analyzer: pre (grey line) and post (blue fill) spectrum, 4.5 dB/oct tilt, about 1/12 octave smoothing, and the hover readout also shows the level at the cursor
- **Multi select (like Pro-Q):** drag a rectangle on empty space, Shift/Ctrl-click to add or remove bands, Ctrl+A for all. Selected bands move together (drag), mouse wheel changes Q of all, arrow keys nudge (Shift = fine), Delete removes them, right-click menu changes type / mode for all. Esc deselects.
- **Presets:** header bar `< Preset > Save`. 14 factory presets (vocal, bass, de-esser, spectral resonance control, sidechain ducking, ...), "My presets" saved as `.rajpreset` files in your user application data folder (`RajvikEQ/Presets`), delete / open folder from the menu. Presets hold the 24 bands only (not output gain or processing mode).
- **Spectral is purely spectral:** a Spectral band has no static gain and no dynamic-EQ curve. Its node sits on the 0 dB line (move it for frequency, wheel for width), and the display shows the live per-frequency cut it applies. Dynamic bands keep their static gain + gain offset.
- **Pre / Post analyzer view:** the dropdown next to Analyzer shows the input spectrum (Pre), the output spectrum (Post) or both.
- **Auto Peaks:** press the button (bottom bar) and play your audio for ~2.5 seconds. The loudest peaks of the input spectrum are found (up to 6), each gets a narrow bell at 0 dB, and all of them are selected together, so one drag / mouse wheel / arrow key / Delete acts on every peak at once.
- Interactive display: double-click = add band, drag = freq/gain, wheel = Q, right-click = type/dynamic/delete, double-click a node = remove

## Look and mouse
Modern dark UI: purple-charcoal background, full-width response display with a glowing gold curve, glowing band nodes, dB scale + level meters on the right, mode/character/output in the bottom bar.
Double-click empty area to add a band. Click a band and a Pro-Q style panel opens, docked at the bottom centre: power, type + slope, FREQ / GAIN / Q knobs, stereo placement, band stepper (< >), Dynamic toggle (expands Threshold / Range / Attack / Release + Spectral), close (x). Click empty space or x to close it. Hover shows the frequency under the cursor.

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
