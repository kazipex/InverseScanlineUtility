# InverseScanlineUtility

A desktop-wide overlay that captures your primary monitor via DXGI Desktop
Duplication, applies one or more independent, tunable periodic luminance
ripples ("bands," each targeting its own tones and colours) in a pixel
shader, and re-presents the corrected image through a click-through,
always-on-top window. The idea: dial each band's period/amplitude/phase/
targeted colour in until, together, they cancel your panel's own
row-banding artifact by eye. Multiple bands exist because that artifact's
strength usually isn't flat across brightness — one band tuned for midtones
and another for a saturated colour range can each be corrected fully at
once, which a single setting can't do. You start with 1 band and add more
as needed (up to 256) via the GUI or a hotkey.

## Build (Visual Studio)

1. Install the "Desktop development with C++" workload if you don't have it
   (Visual Studio Installer → Modify).
2. File → Open → Folder..., pick this folder. VS's built-in CMake support
   configures it automatically.
3. Select `InverseScanlineUtility.exe` as the startup target and build
   (Ctrl+Shift+B).

Or from a "Developer Command Prompt for VS":

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The first configure needs internet access — it fetches Dear ImGui and
nlohmann/json via CMake's `FetchContent`. They're cached in `build/`
afterward.

## How to tune it

1. Open a full-screen solid gray or slow-gradient image — a clean, uniform
   reference makes the artifact easiest to judge.
2. Start with just Band 1. Toggle it off (Ctrl+Alt+E) to confirm the real
   pattern and roughly how many rows apart it repeats.
3. Turn it on, set period close to your estimate, start with a small
   amplitude (default 0.01), and leave the luminance span wide for now.
4. Nudge phase in small steps until the pattern visually disappears rather
   than gets worse (worse = 180° out of phase — keep going, it passes
   through a minimum).
5. Fine-tune period by 0.5-row steps, then adjust amplitude slightly.
6. Try sine vs. square vs. sawtooth — whichever matches the real artifact's
   shape cancels most completely.
7. Check different tones (saturated colour, highlight, shadow, mid-gray).
   If one band can't cancel all of them, narrow its span and centre it on
   the tone needing the most correction, then add a second band (full copy
   of the first) for a different tone. Repeat as needed, up to 256. (If the
   artifact is actually flat across brightness, turn "Limit by luminance"
   off instead of narrowing — full amplitude everywhere.)
8. If two colours at the same brightness need different treatment, that's
   hue/saturation limiting: eyedropper-pick the colour a band already works
   for, turn on "Limit by hue" and narrow its span so it stops affecting the
   other colour, then set up a separate band (same period/phase, its own
   amplitude and hue/saturation pick) for the colour that still needs it.
9. Tuning is very likely refresh-rate dependent — export a profile per
   refresh rate and import the matching one after switching modes, instead
   of re-tuning from scratch.

## Known limitations — please read before filing these as bugs

- **Primary monitor only.** Multi-monitor needs one duplication + one
  overlay window per output; not done here.
- **No cursor rendering.** Desktop Duplication generally excludes the
  hardware cursor from the captured image, so nothing special is needed —
  but Windows occasionally falls back to a software-composited cursor baked
  directly into the desktop bitmap (most reliably seen while dragging a
  window), which DOES get captured and can show up as a second,
  one-frame-delayed cursor under the real one. A fix for that specific case
  was tried (cutting a hole in the overlay at the cursor's position) and
  reverted — it traded the delayed cursor for an ugly square gap in the
  scanline correction, which was worse. Treated as a known, accepted
  artifact for now.
- **DRM-protected content** (video playback, some games) shows as black or
  is excluded from the captured frame — a Windows content-protection
  feature, not something to bypass.
- **Exclusive-fullscreen games** invalidate the duplication handle; the
  overlay hides itself automatically and recovers once the game releases
  the display (see "Auto-hide on fullscreen" above).
- **Adds at least one frame of latency** versus the real display, since
  every frame is captured → processed → re-presented. Worth weighing
  against an earlier ReShade-shader approach for games specifically, and
  reserving this for desktop/static content where the extra latency
  doesn't matter.
- **The correction is a guessed periodic pattern, not a measurement.** If
  the real defect isn't a clean fixed ripple (e.g. it varies with motion),
  this helps static/flat content more than fast-changing scenes.
