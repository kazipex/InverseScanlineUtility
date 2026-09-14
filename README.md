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

## Run

A small "InverseScanlineUtility Controls" panel appears near the top-left,
starting with a single "Band 1" tab (there's no console window — logging to
one is disabled entirely). The panel is a fixed width,
sized for its DPI, and grows only in height to fit whatever's open (see
"Panel sizing"). It also correctly rescales if you drag it to a different
monitor with a different DPI/scaling setting. The corrective overlay itself
is invisible-by-design when nothing is enabled — the rest of your screen
just looks like your normal desktop, re-drawn through the capture pipeline.

**Auto-hide on fullscreen.** The overlay hides itself automatically
whenever something else fills the whole primary monitor (exclusive or
borderless fullscreen), and again if Desktop Duplication itself gets
invalidated (which typically happens when a game grabs exclusive
fullscreen) — it keeps retrying capture in the background and reappears on
its own once the display is free again, rather than freezing on a stale
frame.

**Minimize to tray.** The panel's titlebar minimize button does a normal
taskbar minimize. Sending it to the system tray instead — dropping a
notification-area icon and removing the taskbar entry — is a separate
action: the panel's own "Minimize to tray" button, or Ctrl+Alt+M.
Double-click the tray icon to restore, or right-click it to Quit. Global
hotkeys are suspended while minimized to the tray and re-armed on restore,
so they can't fire on band values you can't currently see.

## GUI panel

Bands appear as tabs. Click the trailing "+" tab (or Ctrl+Alt+=) to add
another, up to 256; a new band is a full copy of whichever band was active
when you added it (forced enabled), since a new band is almost always a
small variation on the one you just tuned. "Remove last band" (or
Ctrl+Alt+-) removes the most recently added one; "Remove selected band"
removes whichever band's tab is currently open instead — either way, down
to a minimum of 1. Bands are shown a page at a time (however many tabs
actually fit across the window, computed from its real width) with "<"/">"
arrows on either side of the tab strip and a "Bands X-Y of Z" indicator
below it; jumping to a band by hotkey, or adding/removing one, automatically
flips to whichever page contains it.

Each band tab has:

- **Enabled** checkbox, and sliders for **period**, **amplitude**,
  **phase**, and **waveform** (sine/square/sawtooth).
- A **targeted-colour picker and eyedropper**, shared by the three limits
  below it. Click the swatch for a full colour picker, or click
  "Eyedropper" to arm live picking from **anywhere on screen** — not just
  the picker's own wheel. While armed, all clicks are blocked system-wide
  (so a pick-click doesn't also click through to whatever's underneath),
  a live swatch previews the colour under your cursor, and clicking (Esc
  cancels) samples that pixel's hue, saturation, AND luminance all at once
  into the three limits below.
- **Limit by luminance** (on by default) — target luminance and span,
  shown only while checked. Off means the band applies at full amplitude
  across every brightness level, for a band that should only be restricted
  by hue/saturation instead.
- **Limit by hue** and **Limit by saturation** (both off by default) —
  optional extra restriction on top of luminance, since two colours at the
  same brightness (a saturated red vs. a saturated cyan) can need different
  correction. When more than one limit is on, a band applies only where all
  of them overlap.

Below the tabs: a master **Enabled** checkbox that bypasses everything at
once, **Reset all**, **Remove last band** / **Remove selected band**, a
live `(N/256 bands)` counter, **Export/Import Profile...** (see below), and
**Minimize to tray**. Every slider/checkbox shows its matching hotkey right
next to it; the period/amplitude/phase/luminance-center/span hotkeys all
support press-and-hold (Windows' own keyboard repeat rate). Editing a band
— slider, checkbox, add/remove — takes effect on the very next frame, no
extra delay.

## Panel sizing

The window is a fixed width (wide enough that no slider needs horizontal
scrolling) and grows only in height, to fit whatever's currently open —
opening a band's hue/saturation limits adds rows, and the window expands to
show them rather than clipping. It never shrinks back down on its own.
Growth is capped to your monitor's height; past that, the window scrolls
internally.

## Profiles

"Export Profile..." writes every active band's full settings (period,
amplitude, phase, waveform, luminance center/span, hue/saturation limits)
plus the master-enabled flag to a JSON file via a save dialog. "Import
Profile..." reads one back and REPLACES all current bands — a full swap,
not a merge. A status line confirms what happened or explains a failure.
The file is plain JSON, so it's easy to hand-edit or diff. This is the
natural way to keep one profile per refresh rate: tune, export as
`120hz.json`, switch modes, tune again, export as `144hz.json`, and import
whichever one matches. A profile with more bands than the current build's
cap (256) is truncated on import rather than rejected.

## Controls (global hotkeys, work without focusing any window)

| Hotkey                    | Effect                                   |
|---------------------------|-------------------------------------------|
| Ctrl+Alt+PageUp/PageDown  | Cycle through however many bands exist    |
| Ctrl+Alt+= / -            | Add a new band / remove the last band (1-256) |
| Ctrl+Alt+Up/Down          | Period (distance between scanlines) — hold to repeat |
| Ctrl+Alt+Right/Left       | Amplitude (scanline intensity) — hold to repeat |
| Ctrl+Alt+, / .            | Shift phase left/right — hold to repeat   |
| Ctrl+Alt+W                | Cycle waveform: sine → square → sawtooth  |
| Ctrl+Alt+E                | Toggle that band on/off                   |
| Ctrl+Alt+[ / ]            | Target luminance toward black/white — hold to repeat |
| Ctrl+Alt+; / '            | Narrow/widen luminance span — hold to repeat |
| Ctrl+Alt+Q                | Quit                                      |
| Ctrl+Alt+M                | Minimize the panel to the system tray     |

Suspended while the panel is minimized to the tray (see "Minimize to
tray" above). Amplitude is a band's own peak correction strength at its own
`ampCenter`, tapering off across `ampWidth`; all enabled bands' results are
summed and applied once (with a single headroom taper near pure
black/white, so overlapping bands can't each clip independently), which is
what lets you target midtones and saturated colours as separate problems.

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
