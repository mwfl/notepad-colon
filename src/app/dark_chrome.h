#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string_view>

// Dark-mode painting for the native chrome that Windows does not darken by
// itself: the menu bar, the document tab strip, the status bar and dialogs.
// Every hook checks the window's current appearance on each message, so the
// same windows keep their stock light look and follow live theme changes.
namespace notepad_colon::app {

// Paints the frame's menu bar from the dark palette.
void InstallDarkMenuBar(HWND frame) noexcept;
// Paints a SysTabControl32 strip from the dark palette.
void InstallDarkTabStrip(HWND tabs) noexcept;
// Paints a status bar from the dark palette.
void InstallDarkStatusBar(HWND status) noexcept;
// Gives a dialog a dark title bar, background and control colours. Call it
// first thing when the dialog initialises; controls added later are covered.
void DarkenDialog(HWND dialog) noexcept;
// Replaces the sunken 3D edges of the edit, list and tree controls under
// `parent` with thin flat borders (the 3D edge is always drawn light), or
// restores them.
void FlattenControlBorders(HWND parent, bool flat) noexcept;
// Rebuilds a 32-bit image list from the toolbar strip (one ink colour drawn on
// magenta), tinting the glyphs so they stay visible on either theme.
bool FillToolbarImages(HIMAGELIST images, HBITMAP strip, COLORREF glyph) noexcept;
// Fills a 32-bit image list of `size`-pixel squares with one icon-font glyph
// per character, so the icons stay sharp at any display scale. Fails when the
// system has no icon font.
bool FillToolbarGlyphs(HIMAGELIST images, std::wstring_view glyphs, int size, COLORREF colour) noexcept;

}  // namespace notepad_colon::app
