# InverseScanlineUtility

A desktop-wide overlay that captures the primary monitor via DXGI Desktop
Duplication, applies one or more independent tunable periodic luminance
ripples ("bands," each targeting its own tones and colours) in a pixel
shader, and re-presents the corrected image through a click-through,
always-on-top window. The goal is to dial each band's period/amplitude/
phase/targeted colour in until, together, they cancel the panel's own
row-banding artifact by eye — multiple bands exist because that artifact's
strength usually isn't flat across brightness, so one band tuned for
midtones and another for saturated colors can each be corrected fully at
once, which a single setting can't do. You start with 1 band and add more
as needed (up to 64) via the GUI or a hotkey.

**This has not been built or run** — I don't have a Windows machine, GPU, or
your monitor available to compile and tune it, so treat this as a working
starting point you build and calibrate yourself, not a finished tool.

## Build (Visual Studio)

1. Install the "Desktop development with C++" workload if you don't have it
   (Visual Studio Installer → Modify).
2. File → Open → Folder..., pick this folder. VS's built-in CMake support
   will configure it automatically.
3. Select `InverseScanlineUtility.exe` as the startup target and build
   (Ctrl+Shift+B).

Or from a "Developer Command Prompt for VS", using whichever generator name
matches your installed VS version (run `cmake --help` to list them):

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The first configure step needs internet access — it fetches Dear ImGui
(the GUI library) and nlohmann/json (used for profile export/import) from
GitHub via CMake's `FetchContent` rather than vendoring them into this
small project tree. They're cached in `build/` afterward, so subsequent
configures don't re-download them unless you delete that folder.

If you have an existing `build/` folder from before the project was renamed
(it used to be called `ScanlineCorrector`), delete it and reconfigure from
scratch — CMake caches the old project/target name inside it, so a stale
`build/` folder can leave you with both an old `ScanlineCorrector.exe` and
the new name confusingly mixed together. No new dependency was added this
time, so this is just a clean reconfigure+rebuild — no internet access is
needed for that step, only for a genuinely fresh `build/` folder.

There's no more no-CMake single-file compile option (`cl main.cpp ...`)
now that ImGui is part of the build — its source files need to compile and
link in alongside `main.cpp`, which CMake's `FetchContent` handles for you.

## Run

Run the .exe. Two things should appear: a console window showing every
band's current values, and a small "InverseScanlineUtility Controls" GUI
panel near the top-left of the screen, starting with a single "Band 1" tab.
The panel sizes itself to your display's actual DPI scaling, so text and
sliders should be a readable, correctly-proportioned size. It also stays
correctly sized and proportioned if you drag it to a different monitor with
a different resolution or scaling setting (see "Multi-monitor DPI handling"
below) — earlier versions only read the DPI once at startup, so dragging
to a monitor with a different scale factor would leave the window stretched
and its contents cut off. The corrective overlay itself is invisible-by-
design when nothing is enabled — the rest of your screen just looks like
your normal desktop, re-drawn through the capture pipeline.

### Multi-monitor DPI handling

The app is now per-monitor DPI aware rather than only system-DPI aware, so
Windows re-layouts (rather than bitmap-stretches) the control panel whenever
you drag it to a monitor with a different DPI/scaling setting. On a
`WM_DPICHANGED` notification the window resizes and repositions itself to
Windows' own suggested rectangle for the new monitor, its Direct3D swap
chain is resized to match, and the ImGui style and font are rescaled from a
saved unscaled snapshot (so repeated moves between monitors don't compound
the scaling). This is also what fixes the earlier "elements get cut off
after dragging to a different-resolution monitor" bug.

### What was wrong before (sizing/hover-offset bug, now fixed)

The control window's requested size was being treated as its *client*
area (the drawable interior) and handed straight to the swap chain, when a
window with a title bar actually gets that size as its *outer* dimensions —
the client area ends up smaller once the title bar and borders are
subtracted. That mismatch meant the rendered UI was being stretched to fit
a differently-sized area than where Windows was actually measuring your
mouse clicks, which is exactly the "have to point a bit lower than the
slider actually is" symptom. It's fixed now by always reading the window's
real client rectangle back after creation (`GetClientRect`) and using that
everywhere — the swap chain, and the ImGui window's own size — instead of
the originally-requested logical size.

### What was wrong before (disabling a band jumped you back to Band 1)

ImGui identifies tabs by their label text by default, and a disabled band's
tab label grows a `" (off)"` suffix — so the instant you disabled the band
you were working on (via `Ctrl+Alt+E` or its checkbox), ImGui saw what
looked like a brand new tab, lost track of which one had been selected, and
silently fell back to the first tab. That's a real bug, not a
misunderstanding of the UI — you'd have had no way to know it happened
short of noticing you were suddenly looking at Band 1. Fixed by giving each
tab a stable identity (`"###bandN"`) that's tracked independently of its
displayed text, so the label can change freely without disturbing which tab
is actually selected.

## Minimize behavior: taskbar vs. tray

The panel's own titlebar minimize button now does a normal, ordinary
taskbar minimize — the same thing minimizing any other window does. Sending
the panel to the system tray instead (dropping a notification-area icon and
removing its taskbar presence entirely) is a separate, dedicated action:
click the panel's own "Minimize to tray" button, or press Ctrl+Alt+M.
Double-click the tray icon to bring the panel straight back, or right-click
it for a small menu: Restore, Show/Hide Debug Console, and Quit. The
corrective overlay itself is completely unaffected by any of this — it has
no titlebar or taskbar presence to begin with, so it just keeps running
invisibly whether the panel is shown, minimized, or sent to the tray.

There's also a console window that prints every band's current values
after each change (handy if you're scripting around this or just want a
running log) — it's allocated but hidden by default. Use the tray icon's
right-click menu ("Show Debug Console") if you ever want to see it.

## GUI panel

Bands appear as tabs, starting with just "Band 1." Click the trailing "+"
tab (or press Ctrl+Alt+=) to add another, up to 64; "Remove last band" (or
Ctrl+Alt+-) removes the most recently added one, down to a minimum of 1 —
bands are a simple ordered stack rather than individually deletable, so
there's always a well-defined "last" one to remove. Because a large number
of bands would otherwise force the tab strip to scroll horizontally (and
scrolling all the way back to an earlier band would get tedious), bands are
shown a page of up to 8 tabs at a time, with "<" and ">" arrow buttons
either side of the tab strip to move between pages, and a "Bands X-Y of Z"
indicator underneath. Jumping to a band by hotkey or adding/removing one
automatically flips to whichever page contains it. Each band tab has its
own Enabled checkbox and sliders for period, amplitude, phase, and
waveform.

Below that is a **targeted-colour picker and eyedropper**, shared by all
three limits described next — it sits outside and above them, and is always
visible regardless of which limits are currently on, rather than being
buried inside one particular limit's section. Click the swatch to open a
full color picker (a hue wheel with a saturation/value box) and click the
actual color you're seeing the artifact on, or click "Eyedropper" to arm
live picking from **anywhere on your actual screen** instead — not just the
picker's own wheel. With the eyedropper armed, a live swatch previews
whatever's under your cursor as you move it (over any window, game, image,
whatever actually shows the artifact), and clicking anywhere samples that
exact pixel (Esc cancels). Either way, the picked color's hue, saturation,
AND luminance are all written at once into the hue/saturation/luminance
limits below — so you can pick a color first and then turn on whichever
limits you actually need, instead of having to turn a limit on before you
can set where it's centered. Uses circular distance for hue so a range can
wrap around red at the 0°/360° seam.

Create new bands to target specific colours and tones. You can target
individual tones by using the colour picker and sliders to select the
specific offending tone and its adjacent tones.

Below the picker is a **"Limit by luminance"** checkbox — on by default,
matching every prior version's behavior — with its target luminance
(center) and Luminance Span sliders shown only while it's checked, the same
way the hue/saturation limits below only show their own sliders while
enabled. With the limit on, set these independently per band so, say, Band 1
can be tuned against midtones while Band 2 handles a saturated color range,
without one undoing the other; with it off, that band applies at its full
amplitude across every brightness level equally, which is what you want for
a band that should only be restricted by hue and/or saturation (below)
rather than by brightness at all. A newly added band is a full copy of
whichever band was active when you added it — period, amplitude, phase,
waveform, the luminance limit's on/off state and center/span, and any
hue/saturation limits all carry over (forced enabled, since a disabled
clone would be pointless) — since a new band is almost always a small
variation on the one you just finished tuning rather than something to
build up from scratch.

Below that are two optional extra limits, off by default so adding them
never changes a band you've already tuned on luminance alone:

- **Limit by hue** — restricts the band to a range of colors on the color
  wheel, centered on the targeted-colour picker's hue above. There's a raw
  "Hue (0-360°)" slider here too, for fine nudging once you're close
  without reopening the picker. Its span is set with the "Hue Span" slider.
- **Limit by saturation** — restricts the band to a range from grayscale (0)
  to fully saturated (1), centered on the targeted-colour picker's
  saturation above. Its span is set with the "Saturation Span" slider.

These exist because the real defect plausibly comes from individual R/G/B
channel overdrive rather than pure perceptual brightness, so two colors at
the identical luminance (a saturated red and a saturated cyan, say) could
need different correction — something luminance alone can never
distinguish, since it collapses color down to one number. When more than
one limit is on, a band applies only where all of them overlap, which the
panel reminds you of since narrow limits stacked together can end up
matching very little.

Each slider/checkbox in a band's tab now shows its matching hotkey right
next to it in dim text (e.g. "Period (Distance between scanlines)
(Ctrl+Alt+Up/Down)"), so you don't need to cross-reference the table below
while you're actually tuning. The period, amplitude, phase, and target
luminance/span hotkeys all support press-and-hold: keep the key combo held
down and the value keeps adjusting continuously, using Windows' own
keyboard repeat rate, rather than needing repeated individual presses.

Below the tabs is a master Enabled checkbox that bypasses everything at
once, a "Reset all" button, and a "Remove last band" button with a live
`(N/64 bands)` counter. Below that are "Export Profile..." and "Import
Profile..." buttons — see "Profiles" below. It's a normal window — drag it
by its title bar, close it to quit the whole app (it's the only visible,
clickable window there is, since the overlay itself is invisible and
click-through by design), and it automatically grows to fit whatever's
currently shown in it (see "Panel sizing" below) rather than ever clipping
content outside its edges. Its own titlebar minimize button performs a
normal taskbar minimize (see "Minimize behavior" above for how that differs
from the dedicated tray button). The hotkeys below still work at the same
time and write to the same shared settings (though the limit checkboxes,
hue/saturation limiting, and profiles are GUI-only — there wasn't room left
in the hotkey list, and all three are the kind of thing more naturally done
with a checkbox/dialog/swatch than a keypress); switching tabs in the GUI
also switches which band the hotkeys edit.

## Panel sizing

The control panel resizes itself automatically, both taller AND wider, to
fit its own content instead of using a single fixed size. Opening a band's
hue and/or saturation limits adds rows to that tab; having several bands
each with several limits open used to just run off the bottom of a
fixed-height window with no way to see or reach the cut-off controls. Now
the window measures what it actually needs each frame in both dimensions
and grows the real window (and its rendering surface) to match whenever
that's more than its current size — it only ever grows, never shrinks back
down on its own, so it doesn't visibly resize itself every time you switch
to a tab with less in it.

The targeted-colour picker/eyedropper row (the widest thing in the panel) is
what the width measurement is based on, and it renders unconditionally on
every band's tab rather than only appearing once you open "Limit by hue" —
so the window sizes itself to its full width immediately, the first time
you open any band's tab, rather than only growing wider later once you
happen to click into a section that used to contain it. In practice that
means the panel is effectively always at its maximum needed width already,
not something that visibly grows while you're clicking around.

Growth is capped to comfortably fit your monitor's height and width; in the
unlikely case you have enough bands with enough limits open that even that
isn't enough, the window scrolls internally past that point (note the
scrollbars) rather than trying to grow off-screen.

## Profiles

"Export Profile..." writes every currently-active band's full settings
(period, amplitude, phase, waveform, luminance center/span, hue/saturation
limits) plus the master-enabled flag out to a JSON file you name via a
normal Windows save dialog. "Import Profile..." reads one back via an open
dialog and REPLACES all current bands with what's in the file — it's a full
swap, not a merge, since merging two unrelated tunings together would
rarely make sense. A status line under the buttons confirms what happened
(how many bands, which file) or explains why an import/export failed (bad
path, unreadable/malformed JSON) rather than failing silently.

The file is plain, human-readable JSON — open it in a text editor if you
want to hand-tweak a value or diff two profiles. This is the natural way to
keep a separate profile per refresh rate (see step 9 under "How to tune it"
below): tune once at 120Hz, export as `120hz.json`, switch refresh rates,
tune again, export as `144hz.json`, and import whichever one matches
whenever you change modes. A profile with more bands than the current
build's cap (64) is truncated to the first 64 on import rather than
rejected outright. The underlying JSON keys (`ampCenter`, `ampWidth`,
`useLuma`, and so on) haven't changed even though their on-screen labels
have, so old profile files exported by earlier versions still import
correctly.

## Controls (global hotkeys, work without focusing any window)

| Hotkey                    | Effect                                   |
|---------------------------|-------------------------------------------|
| Ctrl+Alt+PageUp/PageDown  | Cycle through however many bands exist    |
| Ctrl+Alt+= / -            | Add a new band / remove the last band (1-64) |
| Ctrl+Alt+Up/Down          | Increase/decrease that band's period (distance between scanlines) — hold to adjust continuously |
| Ctrl+Alt+Right/Left       | Increase/decrease that band's amplitude (scanline intensity) — hold to adjust continuously |
| Ctrl+Alt+, / .            | Shift that band's phase left/right — hold to adjust continuously |
| Ctrl+Alt+W                | Cycle that band's waveform: sine → square → sawtooth |
| Ctrl+Alt+E                | Toggle that band on/off                   |
| Ctrl+Alt+[ / ]            | Move that band's targeted luminance toward black/white (ampCenter, only has a visible effect while "Limit by luminance" is on) — hold to adjust continuously |
| Ctrl+Alt+; / '            | Narrow/widen that band's luminance span (ampWidth, same caveat) — hold to adjust continuously |
| Ctrl+Alt+Q                | Quit                                      |
| Ctrl+Alt+M                | Minimize the GUI panel to the system tray |

All of the continuously-adjustable hotkeys above use Windows' own keyboard
repeat rate while held down, rather than requiring one press per step.

Amplitude is a band's own peak correction strength, applied at its own
`ampCenter` and tapering off across its own `ampWidth`. All enabled bands'
results are added together and applied once (with a single headroom taper
near pure black/white, so overlapping bands can't each clip independently
and compound) — this is what lets you target midtones and saturated colors
as two separate problems instead of one setting compromising between them.

## How to tune it

1. Open a full-screen solid gray or slow-gradient image (this is the same
   "flat is a stress test" idea — scanlines are most visible on uniform
   content) so you have a clean reference to judge against.
2. Start with just Band 1. Toggle it off (its Enabled checkbox, or Ctrl+Alt+E
   with band 1 active) and confirm you can see the real scanline pattern and
   roughly how many rows apart it repeats.
3. Turn Band 1 on, set its period close to your estimate, and start with a
   small amplitude (the default 0.01 is deliberately conservative — that's
   roughly a 1% luminance nudge). Leave its target luminance span wide for
   now (high ampWidth) so it applies fairly evenly while you find
   period/phase.
4. Nudge phase left/right in small steps. You're looking for the point where
   the pattern visually disappears rather than getting *worse* (worse means
   you're 180° out of phase — keep nudging past it, it'll pass through a
   minimum).
5. Once phase is close, fine-tune period by 0.5-row steps, then bump
   amplitude up slightly if the pattern is still faintly visible, or down if
   you've started to see the *opposite* pattern (a sign you've overshot).
6. Try sine vs. square vs. sawtooth — whichever matches the real artifact's
   shape best will cancel most completely.
7. Now check different tones: a saturated color, a bright highlight, a dark
   shadow, mid-gray. If Band 1 alone can't fully cancel all of them with one
   amplitude, that's the real artifact's strength varying by brightness —
   narrow Band 1's span and center it (ampCenter) on whichever tone needs
   the most correction, then click the "+" tab (or Ctrl+Alt+=) to add Band
   2, which starts as a full copy of Band 1, and adjust its amplitude and
   center/span for a different tone from there. Add further bands the same
   way for as many tones as you actually need — there's no reason to add a
   band you don't have a use for, but the cap is 64 if you do.
   (If instead the artifact turns out to be the same strength at every
   brightness and you only need one correction everywhere, turn that band's
   "Limit by luminance" checkbox off — it'll apply at full amplitude across
   every tone rather than fading out toward whichever end of the span it
   isn't centered on, which is what you want for a genuinely flat defect.)
8. If two colors at the same brightness need different treatment (e.g. a
   saturated red still shows the pattern after a band that fully cancels it
   on saturated cyan), that's the hue/saturation limiting this was built
   for. Use that band's targeted-colour picker/eyedropper to pick the color
   it's already working for (the eyedropper is the easy way — click it,
   then click the actual pixel on screen), turn on "Limit by hue" and
   narrow the Hue Span so it stops affecting the other color, then set up a
   separate band for the color that still needs it — same period/phase, its
   own amplitude, and its own eyedropper pick for a hue limit centered on
   that color instead. "Limit by saturation" works the same way for
   washed-out vs. vivid colors at the same hue and brightness — and since
   the eyedropper sets luminance, hue, AND saturation from one click, you
   often don't need to pick twice.
9. This will very likely need separate tuning per refresh rate, since the
   real artifact's magnitude is refresh-rate dependent. Use "Export
   Profile..." to save each refresh rate's tuning to its own JSON file (see
   "Profiles" above) once you're happy with it, and "Import Profile..." to
   snap back to it after changing modes, instead of re-tuning from scratch
   every time you switch.

## Known limitations — please read before filing these as bugs

- **Primary monitor only.** Multi-monitor needs one duplication + one
  overlay window per output; the capture/render functions are written so
  that's an extension, not a rewrite, but it isn't done here. (This is
  separate from the per-monitor DPI awareness fix above, which is about the
  control panel's own window correctly rescaling when dragged between
  monitors — the corrective overlay itself still only targets the primary
  monitor.)
- **No cursor rendering.** Desktop Duplication generally excludes the
  hardware cursor from the captured image (Windows composites it
  separately for performance). `AcquireNextFrame` does return pointer
  shape/position data that a fuller version would draw back in each frame
  — marked as a TODO in `CaptureFrame()` — but it's left out here.
- **DRM-protected content** (video playback, some games) will show as black
  or be excluded from the captured frame. This is a Windows content-
  protection feature and can't be bypassed here, nor should it be.
- **Exclusive-fullscreen games** will typically break the duplication
  handle. Quit this app (Ctrl+Alt+Q) before launching one.
- **Adds at least one frame of latency** versus the real display, since
  every frame is captured → processed → re-presented rather than passing
  through untouched. Worth weighing against using the earlier ReShade-shader
  approach for games specifically, and reserving this for desktop/static
  content where the extra latency doesn't matter.
- **The correction is a guessed periodic pattern, not a measurement.** If
  the real defect isn't a clean fixed ripple (e.g. it varies with motion via
  the panel's overdrive), this will mostly help static/flat content and
  less on fast-changing scenes.
