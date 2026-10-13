# Rajvik EQ

A Pro-Q 4 style 24-band equalizer, VST3 (JUCE / C++). Built on GitHub Actions - no local setup needed.

## Get the plugin
1. Push this folder to a GitHub repo.
2. Open the **Actions** tab -> **Build Rajvik EQ** -> wait for the green tick.
3. Download the **RajvikEQ-windows-latest-VST3** artifact, unzip.
4. Copy `Rajvik EQ.vst3` to `C:\Program Files\Common Files\VST3\` and rescan in your DAW.
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
- Instances list: see other Rajvik EQ instances in the project, show their spectrum behind this one, copy their settings
- Surround: mono up to 16 channels (9.1.6) in the plugin layout; bands apply to every channel
- Character: Clean / Subtle / Warm, output gain, phase invert
- Real-time pre/post analyzer, display range 3/6/12/30 dB
- Frequency range 10 Hz to 20 kHz. "Piano" toggle (bottom bar) shows a piano keyboard strip under the display (one key per semitone on the same frequency axis): the selected band's key lights up with its note name + frequency, and hovering shows the note under the mouse
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
