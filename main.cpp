// InverseScanlineUtility
//
// Desktop-wide row-banding correction overlay.
//
// WHAT THIS DOES
//   Captures the primary monitor every frame via DXGI Desktop Duplication,
//   runs a pixel shader that adds up to 64 independent, user-tunable
//   periodic luminance ripples ("bands" — starting from just 1 and grown as
//   needed via the GUI's "+" tab or Ctrl+Alt+= — each with its own period /
//   amplitude / phase / waveform / OPTIONAL target luminance range (this
//   luminance limit itself can be turned off so a band applies uniformly
//   across all brightness), and OPTIONALLY an additional hue and/or
//   saturation limit on top of that) summed together, and re-presents the result through a
//   borderless, click-through,
//   always-on-top window that covers the whole screen. If a band's period
//   and phase line up with the panel's real row-banding artifact on the
//   tones it targets, and the sign is inverted, the two should visually
//   cancel. Multiple bands exist because the real artifact's own strength
//   usually isn't flat across brightness (or even across hue/saturation, if
//   it's rooted in per-channel R/G/B overdrive rather than pure luminance) — one
//   band tuned for midtones and another
//   for saturated colors can each be inverted separately, which a single
//   global setting can't do at once.
//
// HOW TO BUILD (Visual Studio, "Open Folder" with CMake support, or manual):
//   Needs the Windows SDK (d3d11/dxgi/d3dcompiler, all part of it) plus
//   Dear ImGui for the GUI panel, fetched automatically by CMakeLists.txt
//   via FetchContent — see that file. There's no longer a single-command
//   `cl` compile line since ImGui's sources need to be part of the build.
//
// CONTROLS (global hotkeys — work even though the window can't take focus):
//   Ctrl+Alt+PageUp/PageDown : cycle through however many bands exist
//   Ctrl+Alt+= / -        : add a new band / remove the last band (1..64)
//   Ctrl+Alt+Up/Down      : increase/decrease that band's period (distance between scanlines) -- hold to adjust continuously
//   Ctrl+Alt+Right/Left   : increase/decrease that band's amplitude (scanline intensity) -- hold to adjust continuously
//   Ctrl+Alt+,/.          : shift that band's phase left/right -- hold to adjust continuously
//   Ctrl+Alt+W            : cycle that band's waveform (sine / square / sawtooth)
//   Ctrl+Alt+E            : toggle that band on/off
//   Ctrl+Alt+[ / ]        : move that band's targeted luminance (ampCenter) toward black/white -- hold to adjust continuously
//   Ctrl+Alt+; / '        : narrow/widen that band's luminance span (ampWidth) -- hold to adjust continuously
//   Ctrl+Alt+Q            : quit
//   Ctrl+Alt+M            : minimize the GUI panel to the system tray (double-click
//                           the tray icon to bring it back, or right-click it for
//                           Quit -- all hotkeys are suspended while minimized to
//                           the tray, since there's no visible panel to reflect
//                           what they'd change, and re-arm on restore)
//   Amplitude is a band's PEAK strength at its own ampCenter, tapering off
//   per its own ampWidth — this is how each band targets midtones,
//   shadows, highlights, etc. independently, and all enabled bands' results
//   are added together before being applied once.
//   A console window prints every band's current values after each change.
//
//   There is also a small always-on-top GUI panel (built with Dear ImGui)
//   with a tab per band (shown a page of up to 8 at a time, with "<"/">"
//   arrows to move between pages) and sliders for everything above — the
//   hotkeys still work alongside it, both just write to the same shared
//   state. The panel is a second, normal (non-click-through) window so you
//   can actually click its sliders; the fullscreen corrective overlay
//   itself stays click-through as before. It also has "Export Profile..."
//   / "Import Profile..." buttons that save/load every current band's
//   settings as a JSON file via a normal Windows file dialog, and a
//   "Minimize to tray" button (or Ctrl+Alt+M) that hides the panel to the
//   notification area instead of the taskbar -- the window's own titlebar
//   minimize button is a separate, normal taskbar minimize, unaffected by
//   this. Double-click the tray icon to bring it back, or right-click it
//   for Quit. All global hotkeys are suspended for as long as the panel is
//   minimized to the tray, and re-armed the moment it's restored. There's
//   no visible console window on startup either (Log() still writes to
//   one, it's just hidden by default). The panel window is a fixed width,
//   generous enough to comfortably fit its widest row, and grows itself
//   automatically TALLER (never wider) to fit whatever's currently in it
//   (opening hue+saturation limits on several bands, say), so nothing ever
//   gets clipped outside its visible area or needs the scrollbar to reach;
//   it's capped at the monitor's height and scrolls internally past that
//   point rather than growing off-screen. Bands are shown a page (of up to 8) at a time, with
//   "<"/">" arrows either side of the tab strip, rather than every band
//   becoming its own tab in one long scrolling row. Each band's
//   target-luminance sliders can also be turned off entirely ("Limit by
//   luminance") so that band applies uniformly across all brightness. A
//   single targeted-colour picker/eyedropper per band, always visible above
//   all three limits (not nested inside any one of them), sets luminance
//   AND hue AND saturation all at once from whichever color you pick or
//   eyedrop -- whichever limits you actually have turned on are the only
//   ones that end up mattering.
//
// KNOWN LIMITATIONS (read this before assuming something is broken):
//   - Primary monitor only in this version. Multi-monitor support means
//     enumerating every IDXGIOutput and running one duplication+window per
//     output — the capture/shader loop below is written so that's a
//     straightforward extension (see EnumerateAndCapturePrimary), not a
//     structural rewrite.
//   - The hardware cursor is NOT drawn by this version. Windows composites
//     the cursor separately from the desktop image for performance reasons,
//     so DXGI Desktop Duplication frequently does not include it in the
//     captured frame. Windows sometimes falls back to a software-composited
//     cursor baked directly into the desktop bitmap instead (a documented
//     DXGI quirk, seen most reliably while dragging a window), which DOES
//     get captured and can show up as a second, one-frame-delayed cursor
//     sitting under the real one -- a punch-a-hole-in-the-overlay fix for
//     that was tried and reverted (it left an ugly square gap in the
//     corrected scanlines instead), so for now that artifact is a known,
//     accepted limitation rather than something actively worked around.
//   - DRM-protected content (video playback, some games/DRM'd apps) will
//     appear black or be excluded from the duplicated frame — this is a
//     Windows security feature, not a bug in this app, and cannot be
//     worked around.
//   - Fullscreen apps (exclusive OR borderless) are detected automatically
//     (see DetectForegroundFullscreen/UpdateOverlayVisibility) and the
//     overlay hides itself for as long as one is in the foreground, so no
//     manual quitting is needed. Exclusive fullscreen additionally
//     invalidates the desktop-duplication handle outright
//     (DXGI_ERROR_ACCESS_LOST) -- handled the same way (see CaptureFrame /
//     RecoverCaptureIfLost), reacquiring it automatically once the display
//     is free again, rather than leaving a frozen last-captured frame
//     showing after you tab out.
//   - This adds at least one full frame of latency versus the real display,
//     because every frame is captured, processed, and re-presented rather
//     than passing through. On a monitor bought specifically for low
//     latency this is a real cost — worth weighing against just using the
//     ReShade shader for games and this only for desktop/flat content.
//   - The correction is a *guess pattern* you tune by eye, not a measured
//     calibration. If the real panel defect isn't a clean periodic ripple
//     (e.g. it's content/motion dependent via overdrive), this will only
//     ever partially cancel it, and mostly on static/flat content.

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib") // Shell_NotifyIcon, for the tray icon

// Forward declare ImGui's Win32 message handler (declared in imgui_impl_win32.h
// too, but spelling it out here makes the dependency obvious at a glance).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Tunable correction parameters, adjusted live via hotkeys/GUI.
//
// Multiple independent BANDS, each targeting its own luminance range, are summed
// together before being applied once. This is what lets you fully cancel
// the real artifact on midtones AND on saturated colors at the same time,
// when a single global amplitude couldn't do both (the real defect's own
// strength isn't flat across brightness, so neither should the correction
// be — one band per "zone" that needs its own period/amplitude/phase).
// ---------------------------------------------------------------------------
static const int kMaxBands = 256; // hard cap on how many bands can ever exist

struct Band {
    float periodRows = 4.0f;     // rows per cycle of this band's ripple
    float amplitude = 0.01f;     // PEAK luminance offset magnitude, at ampCenter
    float phase = 0.0f;          // phase shift in rows
    int   waveform = 0;          // 0 = sine, 1 = square, 2 = sawtooth
    float ampCenter = 0.5f;      // luminance (0=black..1=white) where this band peaks
    float ampWidth = 0.5f;       // how wide that peak is (its span); smaller = narrower band
    int   enabled = 0;           // this band contributes nothing until turned on

    // Whether the luminance limit above (ampCenter/ampWidth) actually restricts
    // anything. On by default (matches every band's prior behavior, and
    // every existing profile — a profile saved before this existed has no
    // "useLuma" key, and BandFromJson defaults a missing one to true). Turn
    // it off to make a band apply at its full amplitude across ALL
    // brightness levels uniformly, e.g. when you only want to limit by
    // hue/saturation below and don't want luminance narrowing it further.
    int   useLuma = 1;

    // Optional additional limiting by hue/saturation, on top of the luminance
    // limit above — off by default so adding this never changes a band you've
    // already tuned purely on luminance. The real defect is plausibly tied to
    // per-channel (R/G/B) overdrive rather than pure perceptual brightness,
    // so two colors at the same luminance (e.g. saturated red vs. saturated
    // cyan) could need different correction — this is how you'd separate
    // them into different bands instead of only being able to slice by
    // brightness.
    int   useHue = 0;
    float hueCenter = 0.0f;      // 0..1, mapped from hue 0..360 degrees
    float hueWidth = 0.1f;       // circular distance span
    int   useSat = 0;
    float satCenter = 0.5f;      // 0 = grayscale, 1 = fully saturated
    float satWidth = 0.3f;
};

static Band g_bands[kMaxBands];
static int  g_numActiveBands = 1; // starts at 1; grows/shrinks via GUI "+"/hotkeys, up to kMaxBands
static int  g_activeBand = 0;   // which band the hotkeys/GUI are currently editing
static int  g_masterEnabled = 1; // global bypass, independent of each band's own enabled
static bool g_running = true;

// ImGui's tab bar tracks "which tab is selected" as its OWN internal state,
// separate from g_activeBand -- so just changing g_activeBand (as AddBand,
// RemoveLastBand, and the PageUp/PageDown hotkey all do) does not actually
// switch which tab is showing. Left alone, the previously-selected tab keeps
// rendering every frame regardless, and since its own BeginTabItem block
// unconditionally does `g_activeBand = b`, it immediately stomps
// g_activeBand right back to the OLD band -- which is exactly what made a
// freshly-added band's sliders seem to "not work" (you were still editing
// the band you were on before, not the new one you were looking at).
//
// Setting g_forceSelectBand to a band index tells the tab loop in
// RenderControlPanel to pass ImGuiTabItemFlags_SetSelected for that one
// band's tab THIS frame, which makes ImGui's own selection state actually
// follow it; the loop resets this back to -1 once it's been applied.
static int g_forceSelectBand = -1;

// Bands are shown a page at a time in the tab strip (see RenderControlPanel);
// these three are moved up here, ahead of ImportProfile below, so import can
// reset the page view back to page 0 -- otherwise, importing a profile with
// fewer bands than whatever page you'd scrolled to left the tab strip
// pointing at a page beyond the new (smaller) band count, which looked like
// the trailing "+" button had vanished (it's only ever drawn on the actual
// last page).
static int g_bandPageStart = 0;
// How many band tabs fit on one page is computed fresh each frame in
// RenderControlPanel now (see bandsPerPage there), based on the window's
// actual width, rather than a fixed count -- so it's no longer a constant
// here. Nothing outside RenderControlPanel needs to know the page size.
static int g_lastActiveBandForPaging = -1; // see the page-follow guard in RenderControlPanel

// Appends a new band (up to kMaxBands) as a full copy of the currently
// active band — period, amplitude, phase, waveform, luminance center/span, and
// the hue/saturation limits all carry over. In practice a new band is almost
// always a small variation on the one you just finished tuning (a
// different target tone, maybe a tweaked amplitude), so starting from an
// exact clone is more useful than starting over with defaults each time.
static void AddBand()
{
    if (g_numActiveBands >= kMaxBands) return;
    g_bands[g_numActiveBands] = g_bands[g_activeBand]; // full copy, including enabled
    g_bands[g_numActiveBands].enabled = 1; // a disabled clone would be pointless to add
    g_activeBand = g_numActiveBands;
    g_forceSelectBand = g_activeBand; // actually switch the panel to the new band -- see comment above
    g_numActiveBands++;
}

// Removes the last band (bands are a simple ordered stack — no per-tab
// delete — so "last" is unambiguous and nothing needs reindexing).
static void RemoveLastBand()
{
    if (g_numActiveBands <= 1) return;
    g_numActiveBands--;
    if (g_activeBand >= g_numActiveBands) {
        g_activeBand = g_numActiveBands - 1;
        g_forceSelectBand = g_activeBand; // see AddBand's comment above
    }
}

// Deletes any one band by index -- not just the last one. Used by "Remove
// selected band" to get rid of whichever band you're currently on, not only
// the one at the end of the list. Safe to call directly (unlike from inside
// the tab-bar loop itself) since the button that calls this lives in the
// controls row below the tab bar, after it's already finished rendering for
// the frame.
static void DeleteBand(int idx)
{
    if (g_numActiveBands <= 1 || idx < 0 || idx >= g_numActiveBands) return;
    for (int i = idx; i < g_numActiveBands - 1; i++) g_bands[i] = g_bands[i + 1];
    g_numActiveBands--;
    if (g_activeBand >= g_numActiveBands) {
        g_activeBand = g_numActiveBands - 1; // deleted band was the last one -- fall back to the new last
    } else if (g_activeBand > idx) {
        g_activeBand--; // everything after the deleted slot shifted down one
    }
    // else g_activeBand < idx (untouched) or == idx (now correctly pointing
    // at whatever shifted into that slot) -- nothing to adjust either way.
    g_forceSelectBand = g_activeBand; // see AddBand's comment above
}

// ---------------------------------------------------------------------------
// Profile export/import (JSON via nlohmann/json).
//
// A "profile" is just the master-enabled flag plus every currently-active
// band, so you can save a full tuning session (e.g. "144Hz mid-gray pass")
// and get back to it exactly, or hand a working profile to someone else with
// the same panel. Loading a profile REPLACES all current bands outright
// (rather than merging) since a partial merge of two unrelated tunings would
// almost never make sense.
// ---------------------------------------------------------------------------

// Serializes one Band's full settings. Field names are spelled out (rather
// than, say, a plain array) so a profile JSON file is actually readable/
// editable by hand, and so adding a field later doesn't shift meaning for
// older files.
static json BandToJson(const Band& b)
{
    json j;
    j["periodRows"] = b.periodRows;
    j["amplitude"]  = b.amplitude;
    j["phase"]      = b.phase;
    j["waveform"]   = b.waveform;
    j["ampCenter"]  = b.ampCenter;
    j["ampWidth"]   = b.ampWidth;
    j["enabled"]    = b.enabled;
    j["useLuma"]    = b.useLuma;
    j["useHue"]     = b.useHue;
    j["hueCenter"]  = b.hueCenter;
    j["hueWidth"]   = b.hueWidth;
    j["useSat"]     = b.useSat;
    j["satCenter"]  = b.satCenter;
    j["satWidth"]   = b.satWidth;
    return j;
}

// Deserializes one band. Every field is read via .value(key, currentDefault)
// rather than j.at(key), so a profile saved by an older/newer version of
// this app (missing or with extra fields) loads instead of throwing --
// missing fields just fall back to whatever a freshly-constructed Band
// already has for that field.
static Band BandFromJson(const json& j)
{
    Band b; // defaults
    b.periodRows = j.value("periodRows", b.periodRows);
    b.amplitude  = j.value("amplitude",  b.amplitude);
    b.phase      = j.value("phase",      b.phase);
    b.waveform   = j.value("waveform",   b.waveform);
    b.ampCenter  = j.value("ampCenter",  b.ampCenter);
    b.ampWidth   = j.value("ampWidth",   b.ampWidth);
    b.enabled    = j.value("enabled",    b.enabled);
    b.useLuma    = j.value("useLuma",    b.useLuma); // missing key (older profile) defaults to true, matching prior behavior
    b.useHue     = j.value("useHue",     b.useHue);
    b.hueCenter  = j.value("hueCenter",  b.hueCenter);
    b.hueWidth   = j.value("hueWidth",   b.hueWidth);
    b.useSat     = j.value("useSat",     b.useSat);
    b.satCenter  = j.value("satCenter",  b.satCenter);
    b.satWidth   = j.value("satWidth",   b.satWidth);
    return b;
}

// Global status message shown in the GUI after an export/import attempt
// (success or failure) -- file dialogs and disk I/O are the kind of thing
// that silently fails in ways worth telling the user about, rather than
// only printing to the console window they might not be watching.
static std::string g_profileStatus;

// ---------------------------------------------------------------------------
// Eyedropper: pick a hue directly off the screen instead of guessing it on
// a color wheel. Since the overlay window is deliberately click-through and
// borderless, there's no window to "click on" to sample a pixel -- instead,
// clicking "Eyedropper" arms a mode that's polled once per main-loop frame
// (see the WM_LBUTTONDOWN-free polling in wWinMain) using GetAsyncKeyState,
// which works no matter which window (if any) has focus or is under the
// cursor. Samples the real screen via GetPixel on the desktop DC, not the
// captured/corrected texture, so the color you pick is what's actually
// showing, unaffected by whatever correction is already applied.
// ---------------------------------------------------------------------------
static HWND g_controlHwnd = nullptr; // moved up -- the eyedropper code right below needs it to check
                                      // clicks against the control window; the later declaration was removed
static bool g_eyedropperActive = false;
static int  g_eyedropperBand = -1;   // which band's hueCenter to write on click
static COLORREF g_eyedropperPreview = RGB(128, 128, 128); // updated live while active

// Set directly by the hook below when it sees the actual click that should
// count as a pick (i.e. not one landing on our own control panel). The main
// loop's PollEyedropper() (NOT the hook) does the real work -- sampling the
// pixel, converting to HSV, writing the band -- because doing that inside the
// hook risks tripping Windows' LowLevelHooksTimeout. This flag is the only
// thing that crosses from the hook back to the main loop, and it's safe to
// touch from both: WH_MOUSE_LL hooks run on the very thread that installed
// them (the same thread running the main loop here), never a separate one.
static bool g_eyedropperPickRequested = false;

// While the eyedropper is armed, this hook swallows every mouse BUTTON
// event (left/right/middle/X, single or double click) and every wheel
// event, system-wide, before it reaches whatever window is actually under
// the cursor. Without it, a click meant to sample a color would ALSO land
// on whatever's underneath (a game, the taskbar, another app's button).
// Mouse MOVEMENT is deliberately left alone -- only clicks/wheel are
// blocked, so the cursor still moves normally and the live preview swatch
// keeps following it. It also detects the actual pick click here (on the
// real WM_LBUTTONDOWN event, not by polling GetAsyncKeyState afterward --
// polling was found to miss/duplicate clicks unreliably).
static HHOOK g_eyedropperMouseHook = nullptr;

static LRESULT CALLBACK EyedropperMouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_eyedropperActive) {
        if (wParam == WM_LBUTTONDOWN) {
            g_eyedropperPickRequested = true; // consumed next frame by PollEyedropper
        }
        switch (wParam) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 1; // swallow it -- nothing else on the desktop sees this input
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// Installs/removes the hook above. Safe to call redundantly (e.g. disabling
// twice in a row is a no-op) since it checks the current handle first.
static void SetEyedropperClickBlocking(bool enable)
{
    if (enable && !g_eyedropperMouseHook) {
        g_eyedropperMouseHook = SetWindowsHookEx(WH_MOUSE_LL, EyedropperMouseHookProc, GetModuleHandle(nullptr), 0);
    } else if (!enable && g_eyedropperMouseHook) {
        UnhookWindowsHookEx(g_eyedropperMouseHook);
        g_eyedropperMouseHook = nullptr;
    }
}

static void StartEyedropper(int band)
{
    g_eyedropperActive = true;
    g_eyedropperBand = band;
    g_eyedropperPickRequested = false; // ignore whatever's left over from a previous session
    SetEyedropperClickBlocking(true);
}

// Called once per frame from the main loop while g_eyedropperActive. Always
// updates the live preview swatch; when the hook above has flagged a real
// click, samples the pixel under the cursor at that moment, converts it to
// hue, writes it into the target band, and deactivates. Escape cancels
// without writing anything.
static void PollEyedropper()
{
    if (!g_eyedropperActive) return;

    POINT pt;
    GetCursorPos(&pt);
    HDC screenDC = GetDC(nullptr);
    COLORREF px = GetPixel(screenDC, pt.x, pt.y);
    ReleaseDC(nullptr, screenDC);
    g_eyedropperPreview = px;

    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        g_eyedropperActive = false;
        SetEyedropperClickBlocking(false);
        g_eyedropperPickRequested = false;
        g_profileStatus = "Eyedropper cancelled.";
        return;
    }

    if (!g_eyedropperPickRequested) return;
    g_eyedropperPickRequested = false;

    // Any click counts as a pick now, including one that lands on the app's
    // own window -- clicks are blocked system-wide while the eyedropper is
    // armed anyway (see EyedropperMouseHookProc), so there's no risk of a
    // click "leaking through" to a button underneath; sampling the app's own
    // rendered pixels (e.g. to grab a color off the picker swatch itself) is
    // a legitimate use, not a bug.
    if (g_eyedropperBand >= 0 && g_eyedropperBand < g_numActiveBands) {
        float r = GetRValue(px) / 255.0f;
        float g = GetGValue(px) / 255.0f;
        float bl = GetBValue(px) / 255.0f;
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(r, g, bl, h, s, v);
        // Same weights as the shader's own luminance calculation (see PSMain),
        // not HSV's "V" (which is just max(r,g,b) and reads noticeably
        // brighter than perceptual luminance) -- so the picked band's luminance
        // target actually matches what the correction shader sees.
        float luminance = 0.299f * r + 0.587f * g + 0.114f * bl;
        g_bands[g_eyedropperBand].hueCenter = h;
        g_bands[g_eyedropperBand].satCenter = s; // the same picked pixel's saturation feeds the saturation limit too
        g_bands[g_eyedropperBand].ampCenter = luminance; // ...and its luminance feeds the luminance limit too
        g_profileStatus = "Eyedropper picked hue " + std::to_string((int)(h * 360.0f)) +
                           "°, saturation " + std::to_string((int)(s * 100.0f)) +
                           "%, luminance " + std::to_string((int)(luminance * 100.0f)) + "%.";
        g_eyedropperActive = false;
        SetEyedropperClickBlocking(false);
    }
}

// Wraps GetSaveFileNameW. Returns an empty string if the user cancelled.
// The buffer is pre-seeded with ".json" so Explorer's dialog offers that as
// the default extension without the user having to type it.
static std::wstring ShowSaveFileDialog(HWND owner)
{
    wchar_t path[MAX_PATH] = L"profile.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"JSON profile (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) return path;
    return L"";
}

// Wraps GetOpenFileNameW. Returns an empty string if the user cancelled.
static std::wstring ShowOpenFileDialog(HWND owner)
{
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"JSON profile (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return path;
    return L"";
}

// Narrow-string helper purely for status messages / std::ofstream's path
// argument on the toolchains where it only takes narrow paths -- profile
// paths from the save/open dialogs are treated as plain filesystem paths
// here, never displayed as UI text beyond the status line, so a lossy
// conversion (WideCharToMultiByte with CP_UTF8) is fine.
static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

// Opens a save dialog and writes the current master-enabled flag plus every
// active band out as a JSON file. Called from the GUI's "Export Profile..."
// button.
static void ExportProfile(HWND owner)
{
    std::wstring pathW = ShowSaveFileDialog(owner);
    if (pathW.empty()) return; // user cancelled -- not an error, say nothing

    json root;
    root["formatVersion"] = 1;
    root["masterEnabled"] = g_masterEnabled;
    json bandsArr = json::array();
    for (int i = 0; i < g_numActiveBands; i++) bandsArr.push_back(BandToJson(g_bands[i]));
    root["bands"] = bandsArr;

    std::string pathUtf8 = WideToUtf8(pathW);
    std::ofstream out(pathUtf8, std::ios::binary);
    if (!out) {
        g_profileStatus = "Export failed: couldn't open file for writing.";
        return;
    }
    out << root.dump(2);
    if (!out) {
        g_profileStatus = "Export failed: error writing file.";
        return;
    }
    g_profileStatus = "Exported " + std::to_string(g_numActiveBands) + " band(s) to " + pathUtf8;
}

// Opens an open dialog and REPLACES all current bands with whatever the
// chosen JSON file contains. Called from the GUI's "Import Profile..."
// button. A malformed/unreadable file leaves the current bands untouched
// and reports why in g_profileStatus, rather than partially applying a
// broken profile.
static void ImportProfile(HWND owner)
{
    std::wstring pathW = ShowOpenFileDialog(owner);
    if (pathW.empty()) return; // user cancelled

    std::string pathUtf8 = WideToUtf8(pathW);
    std::ifstream in(pathUtf8, std::ios::binary);
    if (!in) {
        g_profileStatus = "Import failed: couldn't open " + pathUtf8;
        return;
    }

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        g_profileStatus = std::string("Import failed: not valid JSON (") + e.what() + ")";
        return;
    }

    if (!root.contains("bands") || !root["bands"].is_array() || root["bands"].empty()) {
        g_profileStatus = "Import failed: file has no bands array.";
        return;
    }

    int count = (int)root["bands"].size();
    if (count > kMaxBands) count = kMaxBands; // clamp rather than reject -- still usable

    for (int i = 0; i < count; i++) g_bands[i] = BandFromJson(root["bands"][i]);
    g_numActiveBands = count;
    g_masterEnabled = root.value("masterEnabled", 1);
    g_activeBand = 0;
    g_forceSelectBand = 0;        // actually switch the tab strip to band 0, not just the variable
    g_bandPageStart = 0;          // jump the page view back to page 0 -- fixes the "+ button vanished"
    g_lastActiveBandForPaging = 0; // report the page as already synced, so the guard doesn't immediately fight this reset

    g_profileStatus = "Imported " + std::to_string(count) + " band(s) from " + pathUtf8;
}

// Must match the cbuffer layout in the pixel shader exactly (16-byte packed,
// each array element a full float4 regardless of how many fields it holds —
// simplest way to guarantee the memcpy below lines up with HLSL's layout).
struct CBData {
    int   masterEnabled;
    int   numBands;
    float pad0, pad1;
    // Per band: (periodRows, amplitude, phase, waveform)
    float bandA[kMaxBands][4];
    // Per band: (ampCenter, ampWidth, enabled, useLuma)
    float bandB[kMaxBands][4];
    // Per band: (hueCenter, hueWidth, useHue, unused)
    float bandC[kMaxBands][4];
    // Per band: (satCenter, satWidth, useSat, unused)
    float bandD[kMaxBands][4];
};

// ---------------------------------------------------------------------------
// Shaders (compiled at runtime with D3DCompile — no offline .hlsl needed).
// MAX_BANDS is spliced in from kMaxBands below (via kShaderSrcBody, prefixed
// with a generated #define) rather than hardcoded here a second time, so the
// C++ and HLSL band counts can never silently drift apart.
// ---------------------------------------------------------------------------
static const char* kShaderSrcBody = R"(
Texture2D    tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer Params : register(b0)
{
    int    masterEnabled;
    int    numBands;
    float2 pad0;
    // Per band: (periodRows, amplitude, phase, waveform)
    float4 bandA[MAX_BANDS];
    // Per band: (ampCenter, ampWidth, enabled, useLuma)
    float4 bandB[MAX_BANDS];
    // Per band: (hueCenter [0..1], hueWidth, useHue, unused)
    float4 bandC[MAX_BANDS];
    // Per band: (satCenter [0..1], satWidth, useSat, unused)
    float4 bandD[MAX_BANDS];
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Full-screen triangle strip generated from vertex id — no vertex buffer needed.
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

float Waveform(float x, int type)
{
    // x is in cycles (0..1 repeating)
    float twoPi = 6.2831853f;
    if (type == 0) {
        // sine
        return sin(x * twoPi);
    } else if (type == 1) {
        // square
        return (frac(x) < 0.5f) ? 1.0f : -1.0f;
    } else {
        // sawtooth
        return frac(x) * 2.0f - 1.0f;
    }
}

// Standard RGB -> HSV, hue/sat/val all returned in 0..1 (hue as fraction of
// the color wheel rather than degrees, to match hueCenter's units).
float3 RGBtoHSV(float3 c)
{
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float delta = mx - mn;

    float h = 0.0f;
    if (delta > 1e-6f) {
        if (mx == c.r)      h = fmod((c.g - c.b) / delta, 6.0f);
        else if (mx == c.g) h = (c.b - c.r) / delta + 2.0f;
        else                h = (c.r - c.g) / delta + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }

    float s = (mx <= 1e-6f) ? 0.0f : delta / mx;
    float v = mx;
    return float3(h, s, v);
}

// Circular distance between two hues in 0..1 space (so hue 0.98 and 0.02 are
// close together, wrapping around red at the top/bottom of the wheel).
float HueDistance(float a, float b)
{
    float d = abs(a - b);
    return min(d, 1.0f - d);
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float4 c = tex0.Sample(samp0, i.uv);

    if (masterEnabled != 0)
    {
        float luminance = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));
        float3 hsv = RGBtoHSV(c.rgb);

        // Taper the COMBINED correction down near pure black/white. A fixed
        // additive ripple has nowhere to go on the "brighten" half of its
        // cycle once a pixel is already at 1.0 (or the "darken" half once
        // it's at 0.0) — that half just clips instead of applying evenly,
        // turning a subtle symmetric wave into an obvious lopsided one.
        // Applied once to the sum of all bands (not per-band) so multiple
        // overlapping bands can't each clip independently and compound.
        float headroom = saturate(1.0f - abs(luminance * 2.0f - 1.0f));

        float totalRipple = 0.0f;

        // [loop] instead of [unroll]: with MAX_BANDS now as high as 256,
        // fully unrolling this (several transcendental calls and branches
        // per iteration) at shader-compile time is what made the app take
        // ages to even open -- D3DCompile runs once at startup, and
        // compiling a 256x-unrolled version of this loop is dramatically
        // slower than compiling a real loop. ps_5_0 supports a
        // runtime-varying index into a cbuffer array just fine, so nothing
        // about correctness changes here.
        [loop]
        for (int b = 0; b < MAX_BANDS; b++)
        {
            if (b >= numBands) break;

            float periodRows = bandA[b].x;
            float amplitude  = bandA[b].y;
            float phase      = bandA[b].z;
            int   waveform   = (int)round(bandA[b].w);
            float ampCenter  = bandB[b].x;
            float ampWidth   = bandB[b].y;
            bool  bandOn     = bandB[b].z > 0.5f;
            bool  useLuma    = bandB[b].w > 0.5f;

            if (!bandOn || periodRows <= 0.0f) continue;

            // i.pos.y is the pixel's row coordinate in screen space.
            float cycles = (i.pos.y + phase) / periodRows;

            // Shape this band's strength over luminance with a bell curve so it
            // concentrates on whichever tones it's meant for (e.g. one band
            // for midtones, another for saturated highlights) instead of
            // applying at the same strength everywhere. ampWidth near 0 = a
            // narrow band right at ampCenter; larger ampWidth = broader.
            // Turning the luminance limit off (useLuma) skips this entirely and
            // leaves shape at full strength (1.0) everywhere, for a band
            // that should apply uniformly across brightness -- e.g. one
            // that's only meant to be restricted by hue/saturation below.
            float shape = 1.0f;
            if (useLuma) {
                float d = luminance - ampCenter;
                shape = exp(-(d * d) / (2.0f * ampWidth * ampWidth + 1e-5f));
            }

            // Optional additional limits by hue and/or saturation, on top of
            // luminance — off by default (factor stays 1) so a band tuned purely
            // on luminance is unaffected. This is what separates "this shade of
            // red" from "this shade of cyan" at the same brightness, since
            // the real defect plausibly depends on individual R/G/B channel
            // transitions rather than perceptual luminance alone.
            bool useHue = bandC[b].z > 0.5f;
            if (useHue) {
                float hueCenter = bandC[b].x;
                float hueWidth = bandC[b].y;
                float hd = HueDistance(hsv.x, hueCenter);
                shape *= exp(-(hd * hd) / (2.0f * hueWidth * hueWidth + 1e-5f));
            }

            bool useSat = bandD[b].z > 0.5f;
            if (useSat) {
                float satCenter = bandD[b].x;
                float satWidth = bandD[b].y;
                float sd = hsv.y - satCenter;
                shape *= exp(-(sd * sd) / (2.0f * satWidth * satWidth + 1e-5f));
            }

            totalRipple += Waveform(cycles, waveform) * amplitude * shape;
        }

        c.rgb = saturate(c.rgb + totalRipple * headroom);
    }

    return c;
}
)";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static ID3D11Device*            g_device = nullptr;
static ID3D11DeviceContext*      g_context = nullptr;
static IDXGISwapChain1*          g_swapChain = nullptr;
static ID3D11RenderTargetView*   g_rtv = nullptr;
static ID3D11VertexShader*       g_vs = nullptr;
static ID3D11PixelShader*        g_ps = nullptr;
static ID3D11Buffer*             g_cbuffer = nullptr;
static ID3D11SamplerState*       g_sampler = nullptr;
static IDXGIOutputDuplication*   g_duplication = nullptr;
static ID3D11Texture2D*          g_stagingTex = nullptr;   // shader-readable copy of desktop
static ID3D11ShaderResourceView* g_stagingSRV = nullptr;
static int                       g_screenW = 0, g_screenH = 0;
static RECT                      g_monitorRect{}; // primary monitor's on-screen rect, for fullscreen detection

// The fullscreen click-through overlay's own window -- stored globally (as
// opposed to just the local `hwnd` in wWinMain) so it can be hidden/shown
// from the fullscreen-detection/capture-recovery logic and so hotkeys can be
// (un)registered against it when the control panel is minimized to the tray.
static HWND                      g_overlayHwnd = nullptr;

// GUI control-panel window (separate from the fullscreen click-through
// overlay above, so it can actually receive mouse/keyboard input).
// (g_controlHwnd itself is declared earlier, near the eyedropper code, which needs it sooner)
static IDXGISwapChain1*          g_controlSwapChain = nullptr;
static ID3D11RenderTargetView*   g_controlRtv = nullptr;
// kControlW is a FIXED width (see ResizeControlWindowIfNeeded's comment for
// why it no longer grows dynamically) -- 820 logical pixels is generous
// enough to comfortably fit the widest row in the panel (a slider using its
// fixed kSliderWidth -- see below -- plus its full label plus its
// "(Ctrl+Alt+...)" hotkey hint, all on one line) with room to spare, at
// every DPI scale, so the horizontal scrollbar should never actually be
// needed in practice -- it stays on only as a last-resort safety net.
// kControlH is only a STARTING height; it grows taller automatically as
// content needs it (see ResizeControlWindowIfNeeded), so this just needs to
// be roughly right for the common case (one band, no limits open) to avoid
// an initial resize flicker.
static const int                 kControlW = 820, kControlH = 620;

// Fixed pixel width (before DPI scaling) for every slider/combo in the
// panel, applied via ImGui::SetNextItemWidth(kSliderWidth * g_uiScale)
// rather than letting ImGui's default CalcItemWidth() size them relative to
// the window's own width. A slider whose width scales with the window means
// a long label plus its "(Ctrl+Alt+...)" hotkey hint has to fight the slider
// for room in an unpredictable way as the window grows -- pinning the
// slider's own width keeps that arithmetic simple and kControlW's "fits
// everything" comment actually verifiable at a glance.
static const float               kSliderWidth = 220.0f;

// Credit/version line shown at the bottom of the panel -- kRepoUrl is what's
// displayed and clicked, kRepoUrlW is the same string as a wide literal for
// ShellExecuteW (which only takes wide strings).
static const char*  kRepoUrl  = "https://github.com/kazipex/InverseScanlineUtility";
static const wchar_t* kRepoUrlW = L"https://github.com/kazipex/InverseScanlineUtility";

static void Log()
{
    printf("[master %s]  %d band(s)  editing band %d\n",
        g_masterEnabled ? "ON" : "BYPASS", g_numActiveBands, g_activeBand + 1);
    for (int b = 0; b < g_numActiveBands; b++) {
        Band& p = g_bands[b];
        printf("  band %d%s: period=%.1f  amplitude=%.4f  phase=%.1f  waveform=%s  luminance=%s%.2f/%.2f  %s\n",
            b + 1, b == g_activeBand ? " <-- editing" : "",
            p.periodRows, p.amplitude, p.phase,
            p.waveform == 0 ? "sine" : p.waveform == 1 ? "square" : "sawtooth",
            p.useLuma ? "" : "[limit off] ", p.ampCenter, p.ampWidth,
            p.enabled ? "[on]" : "[off]");
        if (p.useHue) printf("           hue limit: %.0f°/%.0f°\n", p.hueCenter * 360.0f, p.hueWidth * 360.0f);
        if (p.useSat) printf("           sat limit: %.2f/%.2f\n", p.satCenter, p.satWidth);
    }
}

// Hotkey IDs. All of the shape/tone hotkeys act on whichever band is
// currently "active" -- Ctrl+Alt+PageUp/PageDown cycles through however many
// bands actually exist -- rather than on a single global setting.
enum {
    HK_PERIOD_UP = 1, HK_PERIOD_DOWN, HK_AMP_UP, HK_AMP_DOWN,
    HK_PHASE_LEFT, HK_PHASE_RIGHT, HK_WAVEFORM, HK_TOGGLE, HK_QUIT,
    HK_CENTER_UP, HK_CENTER_DOWN, HK_WIDTH_UP, HK_WIDTH_DOWN,
    HK_BAND_NEXT, HK_BAND_PREV, HK_ADD_BAND, HK_REMOVE_BAND, HK_MINIMIZE_TO_TRAY
};

// Defined further down, alongside the rest of the tray-icon plumbing --
// forward-declared here so HandleHotkey (which comes first) can call it.
static void MinimizeToTray(HWND hwnd);

static bool g_hotkeysRegistered = false;

static void RegisterHotkeys(HWND hwnd)
{
    if (g_hotkeysRegistered) return; // already active -- avoid re-registering on top of ourselves
    // Two modifier sets: MOD_NOREPEAT (mod) for one-shot actions, where
    // holding the key down should still only fire once (toggling a band,
    // cycling the waveform, jumping to a band, adding/removing a band,
    // minimizing) -- and a repeat-enabled set (modRepeat) for the sliders
    // you'd actually want to hold down and watch move continuously (period,
    // amplitude, phase, and the luminance center/span). Without
    // MOD_NOREPEAT, Windows delivers WM_HOTKEY repeatedly for as long as
    // the key stays down, at the OS's own configured keyboard repeat
    // delay/rate, which is what gives press-and-hold its continuous feel
    // here -- there's no polling loop needed for it.
    UINT mod = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    UINT modRepeat = MOD_CONTROL | MOD_ALT;
    RegisterHotKey(hwnd, HK_PERIOD_UP,   modRepeat, VK_UP);
    RegisterHotKey(hwnd, HK_PERIOD_DOWN, modRepeat, VK_DOWN);
    RegisterHotKey(hwnd, HK_AMP_UP,      modRepeat, VK_RIGHT);
    RegisterHotKey(hwnd, HK_AMP_DOWN,    modRepeat, VK_LEFT);
    RegisterHotKey(hwnd, HK_PHASE_LEFT,  modRepeat, VK_OEM_COMMA);
    RegisterHotKey(hwnd, HK_PHASE_RIGHT, modRepeat, VK_OEM_PERIOD);
    RegisterHotKey(hwnd, HK_WAVEFORM,    mod, 'W');
    RegisterHotKey(hwnd, HK_TOGGLE,      mod, 'E');
    RegisterHotKey(hwnd, HK_QUIT,        mod, 'Q');
    // Shape which tones the ACTIVE band corrects: [ / ] slide its target
    // luminance (ampCenter) toward black/white; ; / ' widen or narrow it
    // (ampWidth). Combined with amplitude (that band's peak strength) these
    // let each band concentrate on midtones, shadows, highlights, or a
    // saturated-color range, independently of the other bands.
    RegisterHotKey(hwnd, HK_CENTER_DOWN, modRepeat, VK_OEM_4); // [
    RegisterHotKey(hwnd, HK_CENTER_UP,   modRepeat, VK_OEM_6); // ]
    RegisterHotKey(hwnd, HK_WIDTH_DOWN,  modRepeat, VK_OEM_1); // ;
    RegisterHotKey(hwnd, HK_WIDTH_UP,    modRepeat, VK_OEM_7); // '
    // Cycle through whatever number of bands actually exist with PageUp/PageDown.
    RegisterHotKey(hwnd, HK_BAND_NEXT, mod, VK_NEXT);  // Page Down
    RegisterHotKey(hwnd, HK_BAND_PREV, mod, VK_PRIOR); // Page Up
    // Add/remove bands (up to kMaxBands; always at least 1 remains).
    RegisterHotKey(hwnd, HK_ADD_BAND,    mod, VK_OEM_PLUS);
    RegisterHotKey(hwnd, HK_REMOVE_BAND, mod, VK_OEM_MINUS);
    RegisterHotKey(hwnd, HK_MINIMIZE_TO_TRAY, mod, 'M');
    g_hotkeysRegistered = true;
}

// Reverses RegisterHotkeys -- called when the control panel is sent to the
// tray, so the global hotkeys stop firing while there's no visible panel to
// reflect their effect (see MinimizeToTray/RestoreFromTray below). Harmless
// to call if nothing is currently registered.
static void UnregisterHotkeys(HWND hwnd)
{
    if (!g_hotkeysRegistered) return;
    UnregisterHotKey(hwnd, HK_PERIOD_UP);
    UnregisterHotKey(hwnd, HK_PERIOD_DOWN);
    UnregisterHotKey(hwnd, HK_AMP_UP);
    UnregisterHotKey(hwnd, HK_AMP_DOWN);
    UnregisterHotKey(hwnd, HK_PHASE_LEFT);
    UnregisterHotKey(hwnd, HK_PHASE_RIGHT);
    UnregisterHotKey(hwnd, HK_WAVEFORM);
    UnregisterHotKey(hwnd, HK_TOGGLE);
    UnregisterHotKey(hwnd, HK_QUIT);
    UnregisterHotKey(hwnd, HK_CENTER_DOWN);
    UnregisterHotKey(hwnd, HK_CENTER_UP);
    UnregisterHotKey(hwnd, HK_WIDTH_DOWN);
    UnregisterHotKey(hwnd, HK_WIDTH_UP);
    UnregisterHotKey(hwnd, HK_BAND_NEXT);
    UnregisterHotKey(hwnd, HK_BAND_PREV);
    UnregisterHotKey(hwnd, HK_ADD_BAND);
    UnregisterHotKey(hwnd, HK_REMOVE_BAND);
    UnregisterHotKey(hwnd, HK_MINIMIZE_TO_TRAY);
    g_hotkeysRegistered = false;
}

static void HandleHotkey(int id)
{
    // A couple of ids (add/remove band) change g_activeBand themselves and
    // don't want a stale reference to the band that was active before that
    // change, so handle them before taking the `p` reference below.
    if (id == HK_ADD_BAND)    { AddBand(); Log(); return; }
    if (id == HK_REMOVE_BAND) { RemoveLastBand(); Log(); return; }
    if (id == HK_MINIMIZE_TO_TRAY) { if (g_controlHwnd) MinimizeToTray(g_controlHwnd); return; }

    Band& p = g_bands[g_activeBand]; // remaining hotkeys always edit the active band

    switch (id) {
    case HK_PERIOD_UP:   p.periodRows += 0.5f; break;
    case HK_PERIOD_DOWN: p.periodRows = max(0.5f, p.periodRows - 0.5f); break;
    case HK_AMP_UP:      p.amplitude += 0.002f; break;
    case HK_AMP_DOWN:    p.amplitude = max(0.0f, p.amplitude - 0.002f); break;
    case HK_PHASE_LEFT:  p.phase -= 0.25f; break;
    case HK_PHASE_RIGHT: p.phase += 0.25f; break;
    case HK_WAVEFORM:    p.waveform = (p.waveform + 1) % 3; break;
    case HK_TOGGLE:      p.enabled = !p.enabled; break; // toggles the ACTIVE band; master stays on
    case HK_QUIT:         g_running = false; break;
    case HK_CENTER_UP:    p.ampCenter = min(1.0f, p.ampCenter + 0.05f); break;
    case HK_CENTER_DOWN:  p.ampCenter = max(0.0f, p.ampCenter - 0.05f); break;
    case HK_WIDTH_UP:     p.ampWidth += 0.05f; break;
    case HK_WIDTH_DOWN:   p.ampWidth = max(0.02f, p.ampWidth - 0.05f); break;
    case HK_BAND_NEXT: g_activeBand = (g_activeBand + 1) % g_numActiveBands; g_forceSelectBand = g_activeBand; break;
    case HK_BAND_PREV: g_activeBand = (g_activeBand - 1 + g_numActiveBands) % g_numActiveBands; g_forceSelectBand = g_activeBand; break;
    }
    Log();
}

// ---------------------------------------------------------------------------
// System tray icon.
//
// The GUI panel's own titlebar minimize button now minimizes it to the
// taskbar like any other normal window -- that's standard OS behavior
// (WM_SYSCOMMAND/SC_MINIMIZE is left to DefWindowProc, unhandled here), and
// there's already a SEPARATE, dedicated way to send it to the tray instead
// ("Minimize to tray" in the GUI, or Ctrl+Alt+M -- see MinimizeToTray below),
// so the titlebar button doesn't need to double as that too. The overlay
// itself keeps running unaffected either way (it has no titlebar/taskbar
// presence to begin with) -- minimizing only affects the GUI panel's own
// visibility.
// ---------------------------------------------------------------------------
static const UINT WM_TRAYICON = WM_APP + 1;
static const UINT_PTR kTrayIconID = 1;
// Restore and Show/Hide Debug Console used to be separate menu items here;
// removed on request so the right-click menu is just Quit -- double-clicking
// the tray icon (see WM_TRAYICON below) still restores the panel.
static const UINT kTrayMenuQuit = 2;
static bool g_trayIconAdded = false;

static void AddTrayIcon(HWND hwnd)
{
    NOTIFYICONDATA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = (HICON)LoadImage(nullptr, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
    wcscpy_s(nid.szTip, L"InverseScanlineUtility (running)");
    if (Shell_NotifyIcon(NIM_ADD, &nid)) g_trayIconAdded = true;
}

static void RemoveTrayIcon(HWND hwnd)
{
    if (!g_trayIconAdded) return;
    NOTIFYICONDATA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconID;
    Shell_NotifyIcon(NIM_DELETE, &nid);
    g_trayIconAdded = false;
}

// Hides the control panel window (and its taskbar button) and leaves only
// the tray icon behind. Called from the GUI's "Minimize to tray" button and
// from the HK_MINIMIZE_TO_TRAY hotkey -- deliberately NOT from the window's
// own titlebar minimize button, which now performs a normal taskbar
// minimize instead (see the comment above the tray-icon section).
static void MinimizeToTray(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
    // No panel visible to reflect what a hotkey just changed, and no way to
    // bring the panel back via Ctrl+Alt+M specifically (only the tray icon
    // itself can restore it) -- so all global hotkeys are suspended while
    // minimized to the tray, and re-armed in RestoreFromTray below.
    if (g_overlayHwnd) UnregisterHotkeys(g_overlayHwnd);
}

static void RestoreFromTray(HWND hwnd)
{
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    if (g_overlayHwnd) RegisterHotkeys(g_overlayHwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_HOTKEY:
        HandleHotkey((int)wp);
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Defined further down alongside the rest of the per-monitor DPI handling
// -- forward-declared here so ControlWndProc's WM_DPICHANGED handler (which
// comes first in the file) can call it.
static int g_controlClientW = 0, g_controlClientH = 0; // moved up from below -- ControlWndProc's
                                                         // WM_DPICHANGED handler needs these, and it's
                                                         // defined textually before their old declaration
static float g_uiScale = 1.0f; // same story -- WM_DPICHANGED reads the OLD scale before overwriting it
static void ApplyUiScale(float scale);

// WndProc for the GUI control panel — a normal, focusable, non-click-through
// window, separate from the fullscreen overlay above. ImGui needs first
// crack at every message (it tracks mouse/keyboard state itself) before our
// own handling runs.
LRESULT CALLBACK ControlWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

    switch (msg) {
    case WM_DPICHANGED: {
        // Fired when this window moves to a monitor with a different DPI
        // setting (per-monitor-v2 awareness, declared in wWinMain, is what
        // makes Windows send this instead of silently bitmap-stretching the
        // window). wp's high word is the new DPI; lp points at Windows'
        // suggested new window rect for that DPI.
        UINT newDpi = HIWORD(wp);
        float newScale = newDpi / 96.0f;
        float oldScale = g_uiScale; // still the OLD scale here -- ApplyUiScale(newScale) hasn't run yet
        RECT* suggested = (RECT*)lp;

        // Position comes from Windows' suggestion (it knows where to put
        // the window so it stays roughly where you dragged it onto the new
        // monitor) -- but SIZE is recomputed here from scratch, the same
        // way CreateControlWindow computed it originally, rather than
        // trusting the suggested rect's width/height. Windows derives that
        // suggestion by scaling the OLD OUTER window rect (border and all)
        // by the raw DPI ratio, and border metrics aren't perfectly linear
        // across every DPI step -- a small drift there was enough to leave
        // the panel very slightly narrower than kControlW actually calls
        // for on some monitors, which is exactly what was forcing the
        // horizontal scrollbar to kick in after dragging to a different-
        // resolution/DPI second monitor. Recomputing from kControlW
        // directly guarantees the same width-to-content ratio every time,
        // on every monitor, rather than depending on how well the OS's
        // generic scaling happened to line up on this specific one. Height
        // is carried over in LOGICAL (DPI-independent) pixels so whatever
        // it had already grown to (more bands, more limits open) survives
        // the move instead of being reset or drifting the same way.
        float logicalClientH = g_controlClientH / oldScale;
        int newClientW = (int)(kControlW * newScale);
        int newClientH = (int)(logicalClientH * newScale);

        const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX; // must match CreateControlWindow
        const DWORD exStyle = WS_EX_TOPMOST;
        RECT rect = { 0, 0, newClientW, newClientH };
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        int outerW = rect.right - rect.left;
        int outerH = rect.bottom - rect.top;

        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, outerW, outerH,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        // The window's client area is a different size now -- refresh our
        // cached size and resize the swap chain to match, the same dance
        // ResizeControlWindowIfNeeded does elsewhere for the same reason
        // (a swap chain left at the old size just stretches/crops into the
        // new window rect instead of actually gaining/losing usable space).
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        g_controlClientW = clientRect.right - clientRect.left;
        g_controlClientH = clientRect.bottom - clientRect.top;
        if (g_controlSwapChain) {
            if (g_controlRtv) { g_controlRtv->Release(); g_controlRtv = nullptr; }
            g_controlSwapChain->ResizeBuffers(0, g_controlClientW, g_controlClientH, DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* backBuffer = nullptr;
            g_controlSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
            g_device->CreateRenderTargetView(backBuffer, nullptr, &g_controlRtv);
            backBuffer->Release();
        }

        // Only rescale ImGui's style/font if ImGui is actually up and
        // running yet -- WM_DPICHANGED can theoretically arrive very early
        // (e.g. from the initial CreateWindowEx placement itself) before
        // CreateContext() has run.
        if (ImGui::GetCurrentContext() != nullptr) ApplyUiScale(newScale);
        return 0;
    }
    case WM_TRAYICON:
        // lp carries which mouse event landed on the tray icon.
        if (lp == WM_LBUTTONDBLCLK || lp == NIN_SELECT) {
            RestoreFromTray(hwnd);
        } else if (lp == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenu(menu, MF_STRING, kTrayMenuQuit, L"Quit");
            // Required so the menu dismisses correctly if you click away
            // from it without picking anything.
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == kTrayMenuQuit) { g_running = false; PostQuitMessage(0); return 0; }
        break;
    case WM_DESTROY:
        // Closing the control panel closes the whole app — it's the only
        // visible window there is to close, since the overlay is invisible
        // and click-through by design.
        RemoveTrayIcon(hwnd);
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
static HWND CreateOverlayWindow(int x, int y, int w, int h)
{
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"InverseScanlineUtilityWnd";
    RegisterClassEx(&wc);

    // WS_POPUP: no border/titlebar. WS_EX_TOPMOST: stays above everything.
    // WS_EX_TRANSPARENT + WS_EX_LAYERED: mouse clicks pass through to the
    // real desktop/windows underneath. WS_EX_NOACTIVATE: never steals focus,
    // which is why we rely on RegisterHotKey (global) instead of WM_KEYDOWN.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"InverseScanlineUtility",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, wc.hInstance, nullptr);

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA); // fully opaque, just needed for LAYERED

    // CRITICAL: without this, our own rendered output becomes part of the
    // desktop image that the NEXT AcquireNextFrame call captures — since
    // this window is topmost and covers the screen, we'd be feeding our own
    // corrected frame back in as the "source" every frame, compounding the
    // ripple correction over and over until affected rows saturate to solid
    // black or white. Excluding this window from capture breaks that loop
    // so every captured frame is always the genuine desktop content beneath
    // us, exactly once.
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

// Desktop Duplication reports frame sizes in real physical pixels. If this
// process isn't marked DPI-aware, GetSystemMetrics/CreateWindowEx silently
// work in DPI-virtualized (scaled) coordinates instead, so the window and
// capture texture end up sized differently from the actual monitor — this
// is what produces a black/undersized region that doesn't cover the full
// screen. Querying the output's DesktopCoordinates directly sidesteps the
// mismatch entirely, but declaring DPI awareness (SetProcessDpiAwarenessContext,
// called in wWinMain before any of this runs) is what stops window *placement* from also getting
// virtualized.
static bool GetPrimaryOutputRect(RECT& rect)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return false;

    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    bool ok = false;
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)) &&
        SUCCEEDED(adapter->EnumOutputs(0, &output))) {
        DXGI_OUTPUT_DESC desc;
        if (SUCCEEDED(output->GetDesc(&desc))) {
            rect = desc.DesktopCoordinates;
            ok = true;
        }
    }
    if (output) output->Release();
    if (adapter) adapter->Release();
    factory->Release();
    return ok;
}

// Normal, focusable, titled window for the GUI — deliberately NOT
// WS_EX_TRANSPARENT/WS_EX_NOACTIVATE like the overlay, so you can actually
// click its sliders. Fixed size (no WS_THICKFRAME/WS_MAXIMIZEBOX) to avoid
// needing swap-chain resize handling. It's WS_EX_TOPMOST for the same
// reason the overlay is: without that, the fullscreen overlay (also
// topmost, created first) would paint over it, since WS_EX_TRANSPARENT only
// affects hit-testing, not paint order.
// Real, final client-area size of the control window, filled in by
// CreateControlWindow — this is what the swap chain and ImGui are actually
// sized to, NOT the logical kControlW/kControlH request. Requesting a
// window size gives you the OUTER size (title bar + borders included); the
// client area — what actually gets drawn into and what mouse coordinates
// are measured against — is smaller than that by whatever the title bar
// and border metrics are. The previous version fed kControlW/kControlH
// (a size that assumed no title bar) straight to the swap chain while the
// real client rect was smaller, so the rendered image was scaled to fit a
// differently-sized area than ImGui's own mouse-hit-testing assumed —
// exactly the "have to point a bit lower than the control actually is"
// symptom. Always reading GetClientRect() back after creation and using
// THAT everywhere sidesteps the mismatch regardless of DPI, title bar
// height, or border thickness on a given Windows theme.
// ---------------------------------------------------------------------------
// Per-monitor DPI handling.
//
// Dragging the control window from one monitor to another with a different
// display-scaling setting used to leave text/sliders the wrong size and
// cut content off outside the window: the app was only SYSTEM-DPI-aware
// (SetProcessDPIAware(), a single scale queried once at startup), so
// Windows would just bitmap-stretch the whole window to approximate the
// new monitor's scale instead of the app re-laying-out at the real DPI --
// stretched UI doesn't reflow, so anything near an edge falls outside the
// window's actual (unchanged) pixel dimensions. The real fix is declaring
// PER-MONITOR-V2 awareness (see wWinMain) and handling WM_DPICHANGED
// ourselves (see ControlWndProc) to resize the window and rescale ImGui's
// style/font for the monitor it just landed on, which is what this section
// supports.
//
// ScaleAllSizes() multiplies the CURRENT style in place, so calling it
// again on top of an already-scaled style would compound (e.g. 125% then
// 150% would not land on 150%, it'd land on 125%*150%). g_baseStyle is a
// snapshot of the UNSCALED style taken once, right after StyleColorsDark(),
// so ApplyUiScale() can always rescale from that same starting point no
// matter how many times the window changes monitors.
static ImGuiStyle g_baseStyle;

static void ApplyUiScale(float scale)
{
    g_uiScale = scale;
    ImGui::GetStyle() = g_baseStyle;
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}

static HWND CreateControlWindow(int screenX, int screenY, int screenW, float uiScale)
{
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = ControlWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"InverseScanlineUtilityControlWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassEx(&wc);

    // Top-right corner of the target monitor, clear of the taskbar area.
    int x = screenX + 40;
    int y = screenY + 40;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX; // fixed-size, but minimizable to the taskbar as usual (see the tray-icon section for the separate "send to tray" path)
    const DWORD exStyle = WS_EX_TOPMOST;

    // Ask for an outer size that yields the desired CLIENT size (scaled for
    // DPI) once the title bar/borders are subtracted — AdjustWindowRectEx
    // computes exactly how much bigger the outer rect needs to be. This
    // gets us close on the first try; GetClientRect() after creation (see
    // below) is still what we actually trust and build the swap chain from.
    RECT rect = { 0, 0, (LONG)(kControlW * uiScale), (LONG)(kControlH * uiScale) };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    int outerW = rect.right - rect.left;
    int outerH = rect.bottom - rect.top;

    HWND hwnd = CreateWindowEx(
        exStyle,
        wc.lpszClassName, L"InverseScanlineUtility Controls",
        style,
        x, y, outerW, outerH,
        nullptr, nullptr, wc.hInstance, nullptr);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    g_controlClientW = clientRect.right - clientRect.left;
    g_controlClientH = clientRect.bottom - clientRect.top;

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

static bool InitControlSwapChain(HWND hwnd, int w, int h)
{
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = w;
    scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGIDevice* dxgiDevice = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    HRESULT hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &scd, nullptr, nullptr, &g_controlSwapChain);

    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    if (FAILED(hr)) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_controlSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_controlRtv);
    backBuffer->Release();
    return true;
}

static bool InitD3D(HWND hwnd, int w, int h)
{
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = w;
    scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // NOTE: deliberately NOT setting scd.AlphaMode to PREMULTIPLIED/STRAIGHT
    // here. Those are only valid for a swap chain created with
    // CreateSwapChainForComposition -- using either one with
    // CreateSwapChainForHwnd (what this window uses) makes swap chain
    // creation itself fail with DXGI_ERROR_INVALID_CALL, which is what made
    // the app fail to start at all the one time this was tried.

    D3D_FEATURE_LEVEL fl;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION, &g_device, &fl, &g_context);
    if (FAILED(hr)) return false;

    IDXGIDevice* dxgiDevice = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &scd, nullptr, nullptr, &g_swapChain);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    // Compile shaders from the embedded source string. MAX_BANDS is
    // generated from kMaxBands here rather than typed into the HLSL a
    // second time, so the C++ and shader band counts can't drift apart.
    std::string shaderSrc = "#define MAX_BANDS " + std::to_string(kMaxBands) + "\n" + kShaderSrcBody;

    ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *err = nullptr;
    hr = D3DCompile(shaderSrc.c_str(), shaderSrc.size(), nullptr, nullptr, nullptr,
        "VSMain", "vs_5_0", 0, 0, &vsBlob, &err);
    if (FAILED(hr)) {
        if (err) fprintf(stderr, "VS compile error: %s\n", (char*)err->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(shaderSrc.c_str(), shaderSrc.size(), nullptr, nullptr, nullptr,
        "PSMain", "ps_5_0", 0, 0, &psBlob, &err);
    if (FAILED(hr)) {
        if (err) fprintf(stderr, "PS compile error: %s\n", (char*)err->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);
    vsBlob->Release();
    psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBData);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&cbd, nullptr, &g_cbuffer);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_device->CreateSamplerState(&sd, &g_sampler);

    // Shader-readable texture we copy each captured desktop frame into
    // (the duplication surface itself typically isn't bindable as an SRV).
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    g_device->CreateTexture2D(&td, nullptr, &g_stagingTex);
    g_device->CreateShaderResourceView(g_stagingTex, nullptr, &g_stagingSRV);

    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    return true;
}

static bool InitDuplication()
{
    IDXGIDevice* dxgiDevice = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);

    IDXGIOutput* output = nullptr;
    adapter->EnumOutputs(0, &output); // primary output only — see limitations
    IDXGIOutput1* output1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);

    HRESULT hr = output1->DuplicateOutput(g_device, &g_duplication);

    output1->Release();
    output->Release();
    adapter->Release();
    dxgiDevice->Release();

    return SUCCEEDED(hr);
}

// True once an exclusive-fullscreen app has invalidated the duplication
// handle (see CaptureFrame below) -- checked by the main loop to skip
// capturing/hide the overlay until RecoverCaptureIfLost() gets a fresh
// duplication handle back.
static bool g_captureLost = false;

// Captures one frame; returns true if a new frame was copied into g_stagingTex.
// If no new frame arrived within the timeout, keeps the previous contents
// (desktop duplication only signals on changes, which is fine — we just
// redraw the same corrected frame in that case).
static bool CaptureFrame()
{
    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false; // nothing changed — not an error
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // An exclusive-fullscreen app (a game, typically) just grabbed the
        // display and invalidated this duplication handle outright. Without
        // this, g_stagingTex simply keeps whatever it last held and
        // RenderFrame() happily keeps redrawing that same now-stale frame
        // forever -- which is exactly the "frozen ghost image after tabbing
        // out of a fullscreen game, until you close the app" symptom this is
        // here to fix. Dropping the handle and flagging g_captureLost lets
        // the main loop hide the overlay (see UpdateOverlayVisibility) and
        // periodically retry InitDuplication() until the display is free
        // again (see RecoverCaptureIfLost).
        g_duplication->Release();
        g_duplication = nullptr;
        g_captureLost = true;
        return false;
    }
    if (FAILED(hr)) {
        return false; // some other transient failure -- just skip this frame
    }

    ID3D11Texture2D* desktopTex = nullptr;
    desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTex);
    g_context->CopyResource(g_stagingTex, desktopTex);
    desktopTex->Release();
    desktopResource->Release();
    g_duplication->ReleaseFrame();
    return true;
}

// Retries InitDuplication() once per main-loop iteration while capture is
// lost (see CaptureFrame's DXGI_ERROR_ACCESS_LOST handling above). Cheap to
// call every frame -- it either succeeds immediately once the fullscreen app
// that grabbed the display has released it, or fails instantly and costs
// nothing. No backoff/retry limit: there's nothing else useful to do while
// lost, and once it succeeds g_captureLost clears and normal capture resumes
// on the very next frame.
static void RecoverCaptureIfLost()
{
    if (!g_captureLost) return;
    if (InitDuplication()) {
        g_captureLost = false;
        printf("Desktop capture recovered -- resuming normal correction.\n");
    }
}


// True while something OTHER than this app's own windows currently fills
// the entire primary monitor with no border -- the shared signature of both
// exclusive-fullscreen and borderless-fullscreen windows. Checked every
// frame from the main loop (see UpdateOverlayVisibility) so the overlay can
// be hidden proactively, before any actual capture problem occurs -- unlike
// exclusive fullscreen, borderless fullscreen never triggers
// DXGI_ERROR_ACCESS_LOST, so relying on that alone would miss it entirely.
static bool DetectForegroundFullscreen()
{
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_overlayHwnd || fg == g_controlHwnd) return false;

    RECT r;
    if (!GetWindowRect(fg, &r)) return false;

    return r.left <= g_monitorRect.left && r.top <= g_monitorRect.top &&
           r.right >= g_monitorRect.right && r.bottom >= g_monitorRect.bottom;
}

// Hides/shows the corrective overlay to match whether it SHOULD currently be
// hidden -- either because something else is running fullscreen
// (DetectForegroundFullscreen) or because desktop duplication was just
// invalidated (g_captureLost) -- only touching the actual window state (and
// logging) on a real transition, not every single frame regardless of
// change.
static void UpdateOverlayVisibility()
{
    static bool wasHidden = false;
    bool shouldHide = DetectForegroundFullscreen() || g_captureLost;
    if (shouldHide == wasHidden) return;
    if (g_overlayHwnd) ShowWindow(g_overlayHwnd, shouldHide ? SW_HIDE : SW_SHOW);
    printf(shouldHide ? "Fullscreen app detected -- correction overlay hidden.\n"
                       : "No longer fullscreen -- correction overlay restored.\n");
    wasHidden = shouldHide;
}

static void RenderFrame()
{
    CBData cb{};
    cb.masterEnabled = g_masterEnabled;
    cb.numBands = g_numActiveBands; // only the bands that actually exist, not the full kMaxBands cap
    for (int b = 0; b < g_numActiveBands; b++) {
        Band& p = g_bands[b];
        cb.bandA[b][0] = p.periodRows;
        cb.bandA[b][1] = p.amplitude;
        cb.bandA[b][2] = p.phase;
        cb.bandA[b][3] = (float)p.waveform;
        cb.bandB[b][0] = p.ampCenter;
        cb.bandB[b][1] = p.ampWidth;
        cb.bandB[b][2] = p.enabled ? 1.0f : 0.0f;
        cb.bandB[b][3] = p.useLuma ? 1.0f : 0.0f;
        cb.bandC[b][0] = p.hueCenter;
        cb.bandC[b][1] = p.hueWidth;
        cb.bandC[b][2] = p.useHue ? 1.0f : 0.0f;
        cb.bandC[b][3] = 0.0f;
        cb.bandD[b][0] = p.satCenter;
        cb.bandD[b][1] = p.satWidth;
        cb.bandD[b][2] = p.useSat ? 1.0f : 0.0f;
        cb.bandD[b][3] = 0.0f;
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    g_context->Unmap(g_cbuffer, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)g_screenW, (float)g_screenH, 0, 1 };
    g_context->RSSetViewports(1, &vp);
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->VSSetShader(g_vs, nullptr, 0);
    g_context->PSSetShader(g_ps, nullptr, 0);
    g_context->PSSetShaderResources(0, 1, &g_stagingSRV);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    g_context->PSSetConstantBuffers(0, 1, &g_cbuffer);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_context->Draw(4, 0); // full-screen triangle strip, no vertex/index buffers needed

    g_swapChain->Present(1, 0);
}

static const char* WaveformName(int w) { return w == 0 ? "Sine" : w == 1 ? "Square" : "Sawtooth"; }

// Draws a small dim "(hotkey)" label right after whatever widget was just
// placed, on the same line, so you don't have to cross-reference the
// hotkey table in the README to know a slider has a keyboard shortcut too.
static void HotkeyHint(const char* keys)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", keys);
}

// Grows the real Win32 control window (and its swap chain) TALLER to at
// least neededH client pixels, if it isn't already that tall. Never shrinks
// automatically -- once the window has grown for, say, a band with both hue
// and saturation limits open, it stays that size even if you switch to a
// simpler tab, rather than visibly resizing itself on every tab change.
// Capped to fit on the monitor (minus a margin for the taskbar/title bar)
// since a build with all 64 bands maxed out on content could otherwise ask
// for a window taller than the screen; ImGui's own vertical scrollbar takes
// over for whatever doesn't fit past that cap, so content is always at
// least reachable, never silently cut off outside the window.
//
// WIDTH is deliberately NOT part of this anymore. It used to grow the same
// way, based on a measurement of the widest row rendered each frame -- but
// that measurement only ever covered ONE particular row (the colour-picker
// row), so any OTHER row that happened to be wider on a given frame (a long
// slider label plus its hotkey hint, or a long status line) would ask
// CalcItemWidth() to lay out content for a window wider than what the real
// Win32 window/swap chain had actually grown to that frame, which is what
// caused sliders and text to visually compress and overlap onto each other
// -- not a cosmetic glitch, an actual mismatch between what ImGui laid
// content out for and what the GPU had to squeeze it into. A single fixed,
// generously-sized width (see kControlW) sidesteps the whole class of bug:
// nothing ever needs to renegotiate the window's width mid-session.
static void ResizeControlWindowIfNeeded(HWND hwnd, int neededH)
{
    int maxH = g_screenH > 160 ? g_screenH - 120 : g_screenH; // leave room for the taskbar and the window's own titlebar/margin
    if (neededH > maxH) neededH = maxH;

    if (neededH <= g_controlClientH) return;

    int newClientH = max(neededH, g_controlClientH);

    // Must match the style/exStyle CreateControlWindow used, or
    // AdjustWindowRectEx will compute the wrong outer size for this
    // window's actual title bar/border metrics.
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_TOPMOST;
    RECT rect = { 0, 0, g_controlClientW, newClientH }; // width stays exactly as it is
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    int outerW = rect.right - rect.left;
    int outerH = rect.bottom - rect.top;

    SetWindowPos(hwnd, nullptr, 0, 0, outerW, outerH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    g_controlClientW = clientRect.right - clientRect.left;
    g_controlClientH = clientRect.bottom - clientRect.top;

    // The swap chain's buffers are still the OLD size -- resize them to
    // match, or the rendered image would just get stretched/cropped into
    // the new window rect instead of actually gaining usable space. The
    // existing RTV holds a reference to the old buffer, which blocks
    // ResizeBuffers, so it has to be released and recreated around the call.
    if (g_controlRtv) { g_controlRtv->Release(); g_controlRtv = nullptr; }
    g_controlSwapChain->ResizeBuffers(0, g_controlClientW, g_controlClientH, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* backBuffer = nullptr;
    g_controlSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_controlRtv);
    backBuffer->Release();
}

// Bands are shown a page at a time (as many tabs as actually fit across the
// window -- see bandsPerPage below) with dedicated "<"/">" arrow buttons on
// either side of the tab strip, rather than rendering every band as a tab
// and relying on ImGui's own built-in tab-bar scroll arrows. With up to 256
// bands, that built-in scrolling meant reaching a band near the end took
// many small clicks/scroll-wheel nudges through a long horizontal strip, and
// there was no way to jump back to the visible "rest of the window" (the
// buttons/graph below the tabs) without first scrolling the tab strip back
// -- paging in fixed-width chunks is far fewer clicks to get anywhere, and
// the row of tabs never grows wider than one page's worth regardless of how
// many bands exist.
// (g_bandPageStart and g_lastActiveBandForPaging are declared earlier now,
// near g_forceSelectBand, so ImportProfile can reset them too.)

static void RenderControlPanel()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    // Use the window's REAL client size (queried once at creation via
    // GetClientRect, not the logical kControlW/kControlH request) so the
    // ImGui window, the swap chain, and the OS's own mouse-hit-testing all
    // agree on the same dimensions — this is what fixes the "have to point
    // a bit lower than the slider actually is" offset.
    ImGui::SetNextWindowSize(ImVec2((float)g_controlClientW, (float)g_controlClientH));
    ImGui::Begin("Inverse Scanline Utility", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_HorizontalScrollbar);

    // g_masterEnabled is an int (to match the HLSL cbuffer layout, which has
    // no bool type), so bounce it through a real bool for the checkbox.
    bool masterEnabled = g_masterEnabled != 0;
    ImGui::Checkbox("Master enable (bypass all bands)", &masterEnabled);
    g_masterEnabled = masterEnabled ? 1 : 0;

    ImGui::Separator();
    ImGui::TextWrapped("Create new bands to target specific colours and tones. You can target "
                        "individual tones by using the colour picker and sliders to select the "
                        "specific offending tone and its adjacent tones.");
    ImGui::TextDisabled("Ctrl+Alt+PageUp/PageDown cycles through all of them.");

    // How many band tabs actually fit across the window, recomputed every
    // frame from the real available width instead of a fixed count -- a
    // window this wide (widened earlier to fit the sliders without
    // scrolling) was otherwise leaving most of its width empty past a
    // hardcoded 8 tabs per page, no matter how many bands existed.
    // "Band 256 (off)" is the longest label this app can ever produce, used
    // here as a conservative per-tab width estimate.
    float tabAvailWidth = ImGui::GetContentRegionAvail().x - 40.0f; // leaves room for the prev/next arrow buttons
    ImVec2 sampleLabelSize = ImGui::CalcTextSize("Band 256 (off)");
    float estTabWidth = sampleLabelSize.x + ImGui::GetStyle().FramePadding.x * 2.0f
                       + ImGui::GetStyle().ItemInnerSpacing.x + 6.0f;
    int bandsPerPage = (int)(tabAvailWidth / estTabWidth);
    if (bandsPerPage < 1) bandsPerPage = 1;

    // Keep whichever band is currently active on-screen: jump the page to
    // contain it whenever a hotkey, "+", or band removal moves it outside
    // the page currently being shown, rather than leaving you looking at an
    // empty page while the active band is silently off to one side.
    //
    // Gated on g_activeBand having actually CHANGED since last frame -- this
    // is what makes the "<"/">" page arrows work at all. Without the gate,
    // clicking a page arrow changes g_bandPageStart but not g_activeBand
    // (paging doesn't select a different band, just shows a different page
    // of tabs), so this same check would immediately see the still-active
    // band sitting outside the just-changed page and snap g_bandPageStart
    // right back to wherever that band already was -- silently undoing every
    // click on the arrow buttons.
    if (g_activeBand != g_lastActiveBandForPaging) {
        if (g_activeBand < g_bandPageStart || g_activeBand >= g_bandPageStart + bandsPerPage) {
            g_bandPageStart = (g_activeBand / bandsPerPage) * bandsPerPage;
        }
        g_lastActiveBandForPaging = g_activeBand;
    }
    if (g_bandPageStart > g_numActiveBands - 1) {
        g_bandPageStart = max(0, ((g_numActiveBands - 1) / bandsPerPage) * bandsPerPage);
    }
    if (g_bandPageStart < 0) g_bandPageStart = 0;
    int bandPageEnd = min(g_bandPageStart + bandsPerPage, g_numActiveBands);

    ImGui::BeginDisabled(g_bandPageStart <= 0);
    if (ImGui::ArrowButton("##bandsPagePrev", ImGuiDir_Left)) {
        g_bandPageStart = max(0, g_bandPageStart - bandsPerPage);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    // ImGuiTabBarFlags_NoTabListScrollingButtons: without this, clicking the
    // last tab visible on a page (right at the trailing edge of the strip)
    // could make ImGui think the tab list needs its OWN built-in scroll
    // arrows, which then competed for the same horizontal space as our
    // custom page-arrow drawn right after EndTabBar() below -- pushing it
    // out of the visible area so it looked like it had simply vanished,
    // until selecting an earlier (non-edge) tab let the built-in arrows
    // disappear again and freed the space back up.
    if (ImGui::BeginTabBar("Bands", ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int b = g_bandPageStart; b < bandPageEnd; b++) {
            // The "###bandN" suffix gives this tab a stable identity that
            // ImGui tracks independently of the visible label text. Without
            // it, ImGui identifies tabs BY their label string — so the
            // moment a band's "(off)" suffix appeared/disappeared (e.g.
            // from Ctrl+Alt+E), ImGui saw a "new" tab, lost track of which
            // one had been selected, and silently fell back to the first
            // tab. That was a real bug (switching to Band 1 without asking
            // you), not just a label cosmetic — everything after ### is
            // never displayed, only used for identity, so the label can
            // change freely now without disturbing which tab is selected.
            char label[32]; // wide enough for "Band 256 (off)###band255" now that kMaxBands is 256
            snprintf(label, sizeof(label), "Band %d%s###band%d", b + 1, g_bands[b].enabled ? "" : " (off)", b);
            // See g_forceSelectBand's own comment: without this, adding a
            // band (or PageUp/PageDown-ing to one) changes g_activeBand but
            // NOT which tab ImGui itself considers selected, so the
            // previously-selected tab keeps rendering and silently stomps
            // g_activeBand right back to itself every frame.
            ImGuiTabItemFlags tabFlags = (b == g_forceSelectBand) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label, nullptr, tabFlags)) {
                g_activeBand = b; // hotkeys follow whichever tab you're looking at
                Band& p = g_bands[b];

                bool bandEnabled = p.enabled != 0;
                ImGui::Checkbox("Enabled", &bandEnabled);
                p.enabled = bandEnabled ? 1 : 0;
                HotkeyHint("Ctrl+Alt+E");

                ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                ImGui::SliderFloat("Period (Distance between scanlines)", &p.periodRows, 0.5f, 32.0f, "%.1f");
                HotkeyHint("Ctrl+Alt+Up/Down");
                ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                ImGui::SliderFloat("Amplitude (Scanline intensity)", &p.amplitude, 0.0f, 0.15f, "%.4f");
                HotkeyHint("Ctrl+Alt+Right/Left");
                ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                ImGui::SliderFloat("Phase", &p.phase, -32.0f, 32.0f, "%.2f");
                HotkeyHint("Ctrl+Alt+,/.");

                ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                if (ImGui::BeginCombo("Waveform", WaveformName(p.waveform))) {
                    for (int i = 0; i < 3; i++) {
                        bool selected = (p.waveform == i);
                        if (ImGui::Selectable(WaveformName(i), selected)) p.waveform = i;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                HotkeyHint("Ctrl+Alt+W");

                ImGui::Separator();
                ImGui::TextUnformatted("Which tones this band targets");

                // Targeted-colour picker + eyedropper, shared by all three
                // limits below -- deliberately OUTSIDE any of their
                // collapsible sections (and rendered unconditionally, every
                // frame, regardless of which limits are on) so it's always
                // visible and always available no matter which limits you're
                // currently using, rather than being buried inside "Limit
                // by hue" the way it used to be. Picking a color here
                // (via the wheel, or the eyedropper sampling any pixel on
                // your screen) writes its hue AND saturation AND luminance into
                // ampCenter/hueCenter/satCenter all at once -- whichever
                // limits are actually turned on below are the only ones that
                // end up mattering, but there's no need to turn a limit on
                // first just to set where it should be centered.
                ImVec4 pickerColor = ImColor::HSV(p.hueCenter, p.satCenter, 1.0f);
                char pickerId[32]; // wide enough for "Targeted Colour##ref255"
                snprintf(pickerId, sizeof(pickerId), "Targeted Colour##ref%d", b);
                if (ImGui::ColorEdit3(pickerId, (float*)&pickerColor,
                                      ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueWheel)) {
                    float h, s, v;
                    ImGui::ColorConvertRGBtoHSV(pickerColor.x, pickerColor.y, pickerColor.z, h, s, v);
                    p.hueCenter = h;
                    p.satCenter = s;
                }
                ImGui::SameLine();
                if (g_eyedropperActive && g_eyedropperBand == b) {
                    // Live preview of whatever's currently under the
                    // cursor, so you can see what you're about to pick
                    // before you click.
                    ImVec4 live(GetRValue(g_eyedropperPreview) / 255.0f,
                                GetGValue(g_eyedropperPreview) / 255.0f,
                                GetBValue(g_eyedropperPreview) / 255.0f, 1.0f);
                    ImGui::ColorButton("##eyedropperPreview", live, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24));
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Click anywhere on screen (Esc to cancel)...");
                } else {
                    if (ImGui::Button("Eyedropper")) StartEyedropper(b);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Pick a color directly from any pixel on your screen,\n"
                                           "including outside this app -- sets luminance, hue, AND\n"
                                           "saturation all at once from that one pixel.");
                    }
                }
                ImGui::Spacing();
                bool useLuma = p.useLuma != 0;
                ImGui::Checkbox("Limit by luminance", &useLuma);
                p.useLuma = useLuma ? 1 : 0;
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Off = this band applies at full amplitude across ALL "
                                       "brightness levels equally, ignoring the sliders below.\n"
                                       "Turn it off if you only want hue/saturation (below) to "
                                       "decide where this band applies.");
                }
                if (useLuma) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Target Luminance", &p.ampCenter, 0.0f, 1.0f, "%.2f");
                    HotkeyHint("Ctrl+Alt+[/]");
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Luminance Span", &p.ampWidth, 0.02f, 1.0f, "%.2f");
                    HotkeyHint("Ctrl+Alt+;/'");
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                bool useHue = p.useHue != 0;
                ImGui::Checkbox("Limit by hue", &useHue);
                p.useHue = useHue ? 1 : 0;
                if (useHue) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Target Hue (0-360°)", &p.hueCenter, 0.0f, 1.0f, "%.3f");
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Target Hue Span", &p.hueWidth, 0.01f, 0.5f, "%.3f");
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                bool useSat = p.useSat != 0;
                ImGui::Checkbox("Limit by saturation", &useSat);
                p.useSat = useSat ? 1 : 0;
                if (useSat) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Target Saturation", &p.satCenter, 0.0f, 1.0f, "%.2f");
                    ImGui::SetNextItemWidth(kSliderWidth * g_uiScale);
                    ImGui::SliderFloat("Target Saturation Span", &p.satWidth, 0.02f, 1.0f, "%.2f");
                    ImGui::Unindent();
                }

                if ((useLuma && (useHue || useSat)) || (useHue && useSat)) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("Note: this band now applies only where %s overlap — narrow "
                                        "limits combined can end up matching very little.",
                                        useLuma ? "the luminance limit above AND the hue/saturation limits here"
                                                : "the hue AND saturation limits here");
                }

                ImGui::EndTabItem();
            }
        }

        // Trailing "+" tab button to add a band, standard ImGui idiom for
        // an "add tab" control — only shown on the page that actually
        // contains the last existing band (otherwise it would appear to
        // "add" a band into the middle of an earlier page).
        if (g_numActiveBands < kMaxBands && bandPageEnd >= g_numActiveBands) {
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                AddBand();
            }
        }

        // The "next page" arrow, as ANOTHER trailing tab-bar item, right
        // here inside the tab bar -- not a separate ArrowButton drawn after
        // EndTabBar(). That was the actual bug behind it showing up next to
        // "Limit by saturation" (or wherever): a plain widget placed after
        // EndTabBar() has no idea where the tab strip's row visually is and
        // is at the mercy of ImGui's "last item" bookkeeping, no matter how
        // carefully the cursor position is captured/restored around it. A
        // trailing tab-bar item, by contrast, is laid out BY the tab bar
        // itself, on its own header row, guaranteed, exactly like "+" above
        // and the "<" arrow before it.
        ImGui::BeginDisabled(bandPageEnd >= g_numActiveBands);
        if (ImGui::TabItemButton(">##bandsPageNext", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            g_bandPageStart = min(g_bandPageStart + bandsPerPage, max(0, g_numActiveBands - 1));
        }
        ImGui::EndDisabled();

        ImGui::EndTabBar();
    }
    g_forceSelectBand = -1; // consumed for this frame -- see its own comment above

    ImGui::TextDisabled("Bands %d-%d of %d", g_bandPageStart + 1, bandPageEnd, g_numActiveBands);

    ImGui::Separator();
    if (ImGui::Button("Reset all")) {
        g_numActiveBands = 1;
        for (int b = 0; b < kMaxBands; b++) g_bands[b] = Band{};
        g_bands[0].enabled = 1; // matches first-run state: exactly one active band
        g_activeBand = 0;
        g_masterEnabled = 1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_numActiveBands <= 1);
    if (ImGui::Button("Remove last band")) RemoveLastBand();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_numActiveBands <= 1);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Remove selected band")) DeleteBand(g_activeBand); // whichever band's tab is open, not just the last
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::TextDisabled("(%d/%d bands, Ctrl+Alt+=/- to add/remove)", g_numActiveBands, kMaxBands);

    ImGui::Separator();
    if (ImGui::Button("Export Profile...")) ExportProfile(g_controlHwnd);
    ImGui::SameLine();
    if (ImGui::Button("Import Profile...")) ImportProfile(g_controlHwnd);
    if (!g_profileStatus.empty()) ImGui::TextWrapped("%s", g_profileStatus.c_str());

    ImGui::Separator();
    if (ImGui::Button("Minimize to tray")) MinimizeToTray(g_controlHwnd);
    HotkeyHint("Ctrl+Alt+M");

    // Small credit/version line, always last, so it sits below whatever else
    // is currently showing rather than at a fixed pixel offset that could
    // collide with taller content on a page with more bands/limits open.
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted("InverseScanlineUtility v1.0.7 by kazipex |");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.65f, 1.0f, 1.0f)); // link-blue, so it reads as clickable
    ImGui::TextUnformatted(kRepoUrl);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) ShellExecuteW(nullptr, L"open", kRepoUrlW, nullptr, nullptr, SW_SHOWNORMAL);
    }

    // Measure how much VERTICAL room the content actually just used -- the
    // cursor position right after the last widget, plus the window's own
    // padding -- and grow the real Win32 window (and swap chain) to fit if
    // it's taller than what we've got. This is what used to overflow
    // silently: hue/saturation sections add rows per band, up to 64 bands,
    // and the window never got any taller to compensate, so anything past
    // the fixed original size was clipped and unreachable. One frame of lag
    // (grows on the frame AFTER content first needs more room) is an
    // acceptable tradeoff for not needing a resize border the user could
    // accidentally shrink it with.
    //
    // Width is NOT grown here -- see kControlW's own comment for why it's a
    // fixed constant instead. ImGuiWindowFlags_HorizontalScrollbar stays set
    // as a safety net regardless, in case some future row is ever wider than
    // that constant allows for.
    {
        ImVec2 pad = ImGui::GetStyle().WindowPadding;
        int neededH = (int)(ImGui::GetCursorPosY() + pad.y);
        ResizeControlWindowIfNeeded(g_controlHwnd, neededH);
    }

    ImGui::End();
    ImGui::Render();

    g_context->OMSetRenderTargets(1, &g_controlRtv, nullptr);
    float clearColor[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
    g_context->ClearRenderTargetView(g_controlRtv, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_controlSwapChain->Present(1, 0);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // A console is still allocated (Log() below writes every band's current
    // values to it after each change, which is handy if you're capturing
    // output or scripting around this) but immediately hidden -- there's no
    // need for a visible command-line window cluttering the taskbar/alt-tab
    // list alongside the GUI panel. Right-click the tray icon for a "Show
    // Debug Console" option if you ever want it back.
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    printf("InverseScanlineUtility starting.\n");
    printf("Ctrl+Alt+Up/Down=period  Left/Right=amplitude  ,/.=phase  W=waveform  E=toggle band  Q=quit\n");
    printf("Ctrl+Alt+[/]=target luminance  ;/'=band span  PgUp/PgDn=switch band  +/-=add/remove band\n");
    printf("Ctrl+Alt+M=minimize to tray\n");
    printf("(or just use the GUI panel — every control above has a slider/button there too)\n\n");

    g_bands[0].enabled = 1; // exactly one active band by default, matching the original single-band behavior

    // Per-monitor-v2 DPI awareness (rather than the older, single-scale
    // SetProcessDPIAware()) is what lets Windows send WM_DPICHANGED and
    // hand this window's real content to us to re-layout at the new DPI
    // when it's dragged to a different-scaling monitor, instead of just
    // bitmap-stretching the old content to approximate it -- stretching is
    // exactly what caused elements to get cut off/misaligned after a drag,
    // since a stretched window's actual pixel dimensions never change to
    // fit the new scale. Must happen before any GetSystemMetrics/window
    // sizing below.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    RECT outRect;
    int screenX = 0, screenY = 0;
    if (GetPrimaryOutputRect(outRect)) {
        screenX = outRect.left;
        screenY = outRect.top;
        g_screenW = outRect.right - outRect.left;
        g_screenH = outRect.bottom - outRect.top;
    } else {
        // Fallback if output enumeration fails for some reason — less
        // reliable under DPI scaling, but better than not starting at all.
        fprintf(stderr, "Could not query monitor geometry via DXGI, falling back to "
                         "GetSystemMetrics (may be wrong if display scaling isn't 100%%).\n");
        g_screenW = GetSystemMetrics(SM_CXSCREEN);
        g_screenH = GetSystemMetrics(SM_CYSCREEN);
    }
    printf("Targeting monitor at (%d,%d), %dx%d\n", screenX, screenY, g_screenW, g_screenH);
    g_monitorRect = { screenX, screenY, screenX + g_screenW, screenY + g_screenH }; // for fullscreen detection

    HWND hwnd = CreateOverlayWindow(screenX, screenY, g_screenW, g_screenH);
    g_overlayHwnd = hwnd;
    if (!InitD3D(hwnd, g_screenW, g_screenH)) {
        fprintf(stderr, "D3D init failed.\n");
        return 1;
    }
    if (!InitDuplication()) {
        fprintf(stderr, "Desktop Duplication init failed. Common causes: running under Remote "
                         "Desktop, a display switching event just happened, or another duplication "
                         "app already has an exclusive handle. Try again after a normal login.\n");
        return 1;
    }
    RegisterHotkeys(hwnd);

    // GUI control panel: a second, normal window sharing the same D3D11
    // device, created after the overlay so it stacks above it (both are
    // topmost; last-created wins the tie).
    //
    // uiScale accounts for display scaling (125%/150%/etc.) so the panel's
    // physical size and font stay readable on a high-DPI monitor instead of
    // rendering at a fixed pixel size that was only ever sized for 100%.
    // GetDpiForSystem() is a reasonable choice for the STARTING scale (the
    // window is about to be created on the primary monitor); if it later
    // moves to a monitor with a different scale, WM_DPICHANGED in
    // ControlWndProc takes over from there.
    float uiScale = GetDpiForSystem() / 96.0f;

    g_controlHwnd = CreateControlWindow(screenX, screenY, g_screenW, uiScale);
    AddTrayIcon(g_controlHwnd);
    if (!InitControlSwapChain(g_controlHwnd, g_controlClientW, g_controlClientH)) {
        fprintf(stderr, "Control panel swap chain failed to init — continuing without a GUI; "
                         "hotkeys still work.\n");
    } else {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't clutter the exe's folder with imgui.ini
        ImGui::StyleColorsDark();
        g_baseStyle = ImGui::GetStyle(); // UNSCALED snapshot -- see ApplyUiScale's comment
        ApplyUiScale(uiScale); // readable text/controls at the display's actual DPI; rescaled again on WM_DPICHANGED
        ImGui_ImplWin32_Init(g_controlHwnd);
        ImGui_ImplDX11_Init(g_device, g_context);
    }

    Log();

    MSG msg;
    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        UpdateOverlayVisibility(); // hides the overlay while something's fullscreen or capture is lost
        PollEyedropper();          // no-op unless the eyedropper is currently armed
        // RenderControlPanel() -- which is what actually applies a slider
        // drag, a checkbox click, or a freshly-added band to g_bands -- runs
        // BEFORE RenderFrame() now, not after. It used to run after, which
        // meant every edit took one extra loop iteration to actually show
        // up on screen (RenderFrame() would draw with the band values as of
        // BEFORE this frame's edits were processed) -- not a huge delay in
        // absolute terms, but a real, avoidable one-frame lag between
        // touching a slider and seeing it take effect. Processing the UI
        // first means RenderFrame() always draws with this same frame's
        // up-to-date values.
        if (g_controlRtv) RenderControlPanel();
        if (g_captureLost) RecoverCaptureIfLost();
        else CaptureFrame();       // updates g_stagingTex if the desktop changed
        RenderFrame();             // always redraws — cheap, and keeps Present cadence smooth
    }

    if (g_controlRtv) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // Deliberately skipping exhaustive COM Release() calls on shutdown —
    // the process is exiting and the OS reclaims everything; add them if
    // you turn this into something that starts/stops without exiting.
    return 0;
}
