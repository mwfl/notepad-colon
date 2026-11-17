#include "dark_chrome.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <mwfl/appearance.h>

#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace notepad_colon::app {
namespace {

constexpr UINT_PTR kMenuBarSubclass = 0x4E43'0001;
constexpr UINT_PTR kTabStripSubclass = 0x4E43'0002;
constexpr UINT_PTR kStatusBarSubclass = 0x4E43'0003;
constexpr UINT_PTR kDialogSubclass = 0x4E43'0004;

// Undocumented but stable messages Windows sends so a window can draw its own
// menu bar (they are what the themed menu bar itself is drawn from).
constexpr UINT kUahDrawMenu = 0x0091;
constexpr UINT kUahDrawMenuItem = 0x0092;

struct UahMenu {
    HMENU menu;
    HDC dc;
    DWORD flags;
};
struct UahMenuItemMetrics {
    DWORD sizes[8];
};
struct UahMenuPopupMetrics {
    DWORD widths[4];
    DWORD update_maximum_widths : 2;
};
struct UahMenuItem {
    int position;
    UahMenuItemMetrics item_metrics;
    UahMenuPopupMetrics popup_metrics;
};
struct UahDrawMenuItem {
    DRAWITEMSTRUCT draw;
    UahMenu menu;
    UahMenuItem item;
};

constexpr COLORREF kBarBackground = RGB(32, 32, 32);
constexpr COLORREF kBarHot = RGB(56, 56, 56);
constexpr COLORREF kBarSelected = RGB(70, 70, 70);
constexpr COLORREF kBarEdge = RGB(60, 60, 60);
constexpr COLORREF kDimText = RGB(190, 190, 190);

bool IsDark(HWND window) noexcept { return mwfl::GetWindowAppearance(window).IsDark(); }

void Fill(HDC dc, const RECT& area, COLORREF colour) noexcept {
    ::SetBkColor(dc, colour);
    ::ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &area, nullptr, 0, nullptr);
}

int Scale(HWND window, int value) noexcept {
    return ::MulDiv(value, static_cast<int>(::GetDpiForWindow(window)), 96);
}

HFONT CreateMenuFont(HWND window) noexcept {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (!::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                                      ::GetDpiForWindow(window)))
        return nullptr;
    return ::CreateFontIndirectW(&metrics.lfMenuFont);
}

RECT MenuBarRect(HWND frame) noexcept {
    MENUBARINFO information{sizeof(information)};
    RECT window{};
    if (!::GetMenuBarInfo(frame, OBJID_MENU, 0, &information) || !::GetWindowRect(frame, &window))
        return {};
    RECT bar = information.rcBar;
    ::OffsetRect(&bar, -window.left, -window.top);
    return bar;
}

// The themed frame leaves a light one-pixel line between the menu bar and the
// client area; cover it.
void PaintMenuBarEdge(HWND frame) noexcept {
    RECT client{};
    RECT window{};
    if (!::GetMenu(frame) || !::GetClientRect(frame, &client) || !::GetWindowRect(frame, &window)) return;
    ::MapWindowPoints(frame, nullptr, reinterpret_cast<POINT*>(&client), 2);
    ::OffsetRect(&client, -window.left, -window.top);
    RECT line{client.left, client.top - 1, client.right, client.top};
    if (HDC dc = ::GetWindowDC(frame)) {
        Fill(dc, line, kBarBackground);
        ::ReleaseDC(frame, dc);
    }
}

LRESULT CALLBACK MenuBarProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                  UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(window, MenuBarProcedure, id);
    if ((message != kUahDrawMenu && message != kUahDrawMenuItem && message != WM_NCPAINT &&
         message != WM_NCACTIVATE) || !IsDark(window))
        return ::DefSubclassProc(window, message, wparam, lparam);

    if (message == kUahDrawMenu) {
        const auto* menu = reinterpret_cast<const UahMenu*>(lparam);
        Fill(menu->dc, MenuBarRect(window), kBarBackground);
        return TRUE;
    }
    if (message == kUahDrawMenuItem) {
        const auto* item = reinterpret_cast<const UahDrawMenuItem*>(lparam);
        wchar_t text[256]{};
        MENUITEMINFOW information{sizeof(information)};
        information.fMask = MIIM_STRING;
        information.dwTypeData = text;
        information.cch = static_cast<UINT>(std::size(text) - 1);
        ::GetMenuItemInfoW(item->menu.menu, static_cast<UINT>(item->item.position), TRUE, &information);

        const UINT state = item->draw.itemState;
        const bool disabled = (state & (ODS_INACTIVE | ODS_GRAYED | ODS_DISABLED)) != 0;
        const COLORREF background = (state & ODS_SELECTED) ? kBarSelected
            : (state & ODS_HOTLIGHT) ? kBarHot : kBarBackground;
        const auto palette = mwfl::GetWindowAppearance(window).palette;
        HDC dc = item->draw.hDC;
        Fill(dc, item->draw.rcItem, background);
        const HFONT font = CreateMenuFont(window);
        const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, disabled ? palette.disabled_text : palette.text);
        UINT format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        if (state & ODS_NOACCEL) format |= DT_HIDEPREFIX;
        RECT area = item->draw.rcItem;
        ::DrawTextW(dc, text, -1, &area, format);
        if (font) {
            ::SelectObject(dc, previous);
            ::DeleteObject(font);
        }
        return TRUE;
    }
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    PaintMenuBarEdge(window);
    return result;
}

constexpr wchar_t kHotTabProperty[] = L"NotepadColon.HotTab";

int HotTab(HWND tabs) noexcept {
    return static_cast<int>(reinterpret_cast<INT_PTR>(::GetPropW(tabs, kHotTabProperty))) - 1;
}

void SetHotTab(HWND tabs, int index) noexcept {
    if (index == HotTab(tabs)) return;
    ::SetPropW(tabs, kHotTabProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(index + 1)));
    ::InvalidateRect(tabs, nullptr, FALSE);
}

void PaintTabStrip(HWND tabs, HDC dc, const RECT& client) noexcept {
    const auto palette = mwfl::GetWindowAppearance(tabs).palette;
    Fill(dc, client, kBarBackground);
    // The control also owns the page area under the strip; only the strip
    // above it holds tabs.
    RECT page = client;
    TabCtrl_AdjustRect(tabs, FALSE, &page);
    const LONG strip_bottom = page.top > client.top && page.top < client.bottom ? page.top : client.bottom;
    RECT edge{client.left, strip_bottom - 1, client.right, strip_bottom};
    Fill(dc, edge, kBarEdge);

    const auto font = reinterpret_cast<HFONT>(::SendMessageW(tabs, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
    ::SetBkMode(dc, TRANSPARENT);
    const int count = TabCtrl_GetItemCount(tabs);
    const int selected = TabCtrl_GetCurSel(tabs);
    const int hot = HotTab(tabs);
    const int accent = Scale(tabs, 2);
    for (int index = 0; index < count; ++index) {
        RECT item{};
        if (!TabCtrl_GetItemRect(tabs, index, &item)) continue;
        item.bottom = strip_bottom;
        if (index == selected) {
            Fill(dc, item, kBarSelected);
            RECT line{item.left, item.bottom - accent, item.right, item.bottom};
            Fill(dc, line, palette.accent);
        } else if (index == hot) {
            Fill(dc, item, kBarHot);
        }
        wchar_t text[260]{};
        TCITEMW information{};
        information.mask = TCIF_TEXT;
        information.pszText = text;
        information.cchTextMax = static_cast<int>(std::size(text));
        if (!TabCtrl_GetItem(tabs, index, &information)) continue;
        ::SetTextColor(dc, index == selected ? palette.text : kDimText);
        RECT label = item;
        ::InflateRect(&label, -Scale(tabs, 6), 0);
        ::DrawTextW(dc, text, -1, &label,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (font) ::SelectObject(dc, previous);
}

// Paints through a memory bitmap so the strip does not flicker while tabs are
// opened, closed and hovered.
template <typename Painter>
void PaintBuffered(HWND window, Painter&& painter) noexcept {
    PAINTSTRUCT paint{};
    HDC target = ::BeginPaint(window, &paint);
    RECT client{};
    ::GetClientRect(window, &client);
    HDC memory = ::CreateCompatibleDC(target);
    HBITMAP bitmap = ::CreateCompatibleBitmap(target, client.right, client.bottom);
    if (memory && bitmap) {
        const HGDIOBJ previous = ::SelectObject(memory, bitmap);
        painter(memory, client);
        ::BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
        ::SelectObject(memory, previous);
    } else {
        painter(target, client);
    }
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    ::EndPaint(window, &paint);
}

LRESULT CALLBACK TabStripProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                   UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) {
        ::RemovePropW(window, kHotTabProperty);
        ::RemoveWindowSubclass(window, TabStripProcedure, id);
    }
    if (!IsDark(window)) return ::DefSubclassProc(window, message, wparam, lparam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(window, [window](HDC dc, const RECT& client) { PaintTabStrip(window, dc, client); });
        return 0;
    case WM_MOUSEMOVE: {
        TCHITTESTINFO hit{};
        hit.pt = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        SetHotTab(window, TabCtrl_HitTest(window, &hit));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        ::TrackMouseEvent(&track);
        break;
    }
    case WM_MOUSELEAVE:
        SetHotTab(window, -1);
        break;
    default:
        break;
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

void PaintStatusBar(HWND status, HDC dc, const RECT& client) noexcept {
    const auto palette = mwfl::GetWindowAppearance(status).palette;
    Fill(dc, client, kBarBackground);
    RECT edge{client.left, client.top, client.right, client.top + 1};
    Fill(dc, edge, kBarEdge);

    const auto font = reinterpret_cast<HFONT>(::SendMessageW(status, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, palette.text);
    const bool simple = ::SendMessageW(status, SB_ISSIMPLE, 0, 0) != 0;
    const int parts = simple ? 1 : static_cast<int>(::SendMessageW(status, SB_GETPARTS, 0, 0));
    for (int part = 0; part < parts; ++part) {
        const WPARAM index = simple ? SB_SIMPLEID : static_cast<WPARAM>(part);
        RECT area = client;
        if (!simple && !::SendMessageW(status, SB_GETRECT, index, reinterpret_cast<LPARAM>(&area))) continue;
        const auto length = LOWORD(::SendMessageW(status, SB_GETTEXTLENGTHW, index, 0));
        std::vector<wchar_t> text(static_cast<std::size_t>(length) + 1);
        ::SendMessageW(status, SB_GETTEXTW, index, reinterpret_cast<LPARAM>(text.data()));
        area.left += Scale(status, 6);
        area.top += 1;
        ::DrawTextW(dc, text.data(), -1, &area,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (font) ::SelectObject(dc, previous);
}

LRESULT CALLBACK StatusBarProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(window, StatusBarProcedure, id);
    if (!IsDark(window)) return ::DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PaintBuffered(window, [window](HDC dc, const RECT& client) { PaintStatusBar(window, dc, client); });
        return 0;
    }
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    // The control draws changed text straight away, bypassing WM_PAINT.
    if (message == SB_SETTEXTW || message == SB_SETTEXTA || message == WM_SETTEXT ||
        message == SB_SETPARTS || message == SB_SIMPLE)
        ::InvalidateRect(window, nullptr, FALSE);
    return result;
}

struct DialogColours {
    mwfl::AppearancePalette palette;
    HBRUSH window = nullptr;
    HBRUSH control = nullptr;
};

const UINT kThemeDialogChildren = ::RegisterWindowMessageW(L"NotepadColon.ThemeDialogChildren");

constexpr wchar_t kFlatBorderProperty[] = L"NotepadColon.FlatBorder";

// Swaps a control's sunken 3D edge, which Windows draws in light colours, for
// a thin flat border, and back again.
void SetFlatBorder(HWND control, bool flat) noexcept {
    const auto extended = ::GetWindowLongPtrW(control, GWL_EXSTYLE);
    const auto style = ::GetWindowLongPtrW(control, GWL_STYLE);
    if (flat) {
        if (!(extended & WS_EX_CLIENTEDGE)) return;
        ::SetPropW(control, kFlatBorderProperty,
                   reinterpret_cast<HANDLE>(static_cast<INT_PTR>((style & WS_BORDER) ? 2 : 1)));
        ::SetWindowLongPtrW(control, GWL_EXSTYLE, extended & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
        ::SetWindowLongPtrW(control, GWL_STYLE, style | WS_BORDER);
    } else {
        const auto saved = reinterpret_cast<INT_PTR>(::GetPropW(control, kFlatBorderProperty));
        if (saved == 0) return;
        ::RemovePropW(control, kFlatBorderProperty);
        ::SetWindowLongPtrW(control, GWL_EXSTYLE, extended | WS_EX_CLIENTEDGE);
        if (saved == 1) ::SetWindowLongPtrW(control, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_BORDER));
    }
    ::SetWindowPos(control, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool HasFlatBorderClass(HWND control) noexcept {
    wchar_t name[64]{};
    ::GetClassNameW(control, name, static_cast<int>(std::size(name)));
    for (const wchar_t* candidate : {L"Edit", L"ListBox", WC_LISTVIEWW, WC_TREEVIEWW})
        if (::_wcsicmp(name, candidate) == 0) return true;
    return false;
}

BOOL CALLBACK FlattenChild(HWND child, LPARAM flat) noexcept {
    if (HasFlatBorderClass(child)) SetFlatBorder(child, flat != 0);
    return TRUE;
}

BOOL CALLBACK ThemeDialogChild(HWND child, LPARAM parameter) noexcept {
    const auto& colours = *reinterpret_cast<const DialogColours*>(parameter);
    wchar_t name[64]{};
    ::GetClassNameW(child, name, static_cast<int>(std::size(name)));
    FlattenChild(child, TRUE);
    if (::_wcsicmp(name, WC_LISTVIEWW) == 0) {
        ListView_SetBkColor(child, colours.palette.control_background);
        ListView_SetTextBkColor(child, colours.palette.control_background);
        ListView_SetTextColor(child, colours.palette.text);
    } else if (::_wcsicmp(name, WC_TREEVIEWW) == 0) {
        TreeView_SetBkColor(child, colours.palette.control_background);
        TreeView_SetTextColor(child, colours.palette.text);
    }
    return TRUE;
}

LRESULT CALLBACK DialogProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                 UINT_PTR id, DWORD_PTR data) noexcept {
    auto* colours = reinterpret_cast<DialogColours*>(data);
    switch (message) {
    case WM_NCDESTROY:
        ::RemoveWindowSubclass(window, DialogProcedure, id);
        if (colours->window) ::DeleteObject(colours->window);
        if (colours->control) ::DeleteObject(colours->control);
        delete colours;
        break;
    case WM_ERASEBKGND: {
        RECT client{};
        ::GetClientRect(window, &client);
        ::FillRect(reinterpret_cast<HDC>(wparam), &client, colours->window);
        return 1;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        const bool enabled = ::IsWindowEnabled(reinterpret_cast<HWND>(lparam)) != FALSE;
        ::SetTextColor(dc, enabled ? colours->palette.text : colours->palette.disabled_text);
        ::SetBkColor(dc, colours->palette.window_background);
        return reinterpret_cast<LRESULT>(colours->window);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        ::SetTextColor(dc, colours->palette.text);
        ::SetBkColor(dc, colours->palette.control_background);
        return reinterpret_cast<LRESULT>(colours->control);
    }
    default:
        if (message == kThemeDialogChildren) {
            ::EnumChildWindows(window, ThemeDialogChild, reinterpret_cast<LPARAM>(colours));
            ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;
        }
        break;
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace

bool FillToolbarImages(HIMAGELIST images, HBITMAP strip, COLORREF glyph) noexcept {
    BITMAP source{};
    if (!images || !strip || ::GetObjectW(strip, sizeof(source), &source) != sizeof(source)) return false;
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(format.bmiHeader);
    format.bmiHeader.biWidth = source.bmWidth;
    format.bmiHeader.biHeight = -source.bmHeight;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(static_cast<std::size_t>(source.bmWidth) * source.bmHeight);
    HDC screen = ::GetDC(nullptr);
    const int copied = ::GetDIBits(screen, strip, 0, static_cast<UINT>(source.bmHeight),
                                   pixels.data(), &format, DIB_RGB_COLORS);
    if (copied != source.bmHeight) {
        ::ReleaseDC(nullptr, screen);
        return false;
    }
    // The strip is one dark ink colour anti-aliased onto magenta. Magenta has
    // no green, so the green channel alone says how much ink a pixel holds.
    constexpr int kInkGreen = 55;
    const DWORD colour = (static_cast<DWORD>(GetRValue(glyph)) << 16) |
        (static_cast<DWORD>(GetGValue(glyph)) << 8) | GetBValue(glyph);
    for (auto& pixel : pixels) {
        const int green = static_cast<int>((pixel >> 8) & 0xFF);
        const DWORD alpha = static_cast<DWORD>(green >= kInkGreen ? 255 : green * 255 / kInkGreen);
        pixel = (alpha << 24) | colour;
    }
    void* bits = nullptr;
    HBITMAP image = ::CreateDIBSection(screen, &format, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (!image || !bits) return false;
    std::memcpy(bits, pixels.data(), pixels.size() * sizeof(DWORD));
    ImageList_RemoveAll(images);
    const bool added = ImageList_Add(images, image, nullptr) == 0;
    ::DeleteObject(image);
    return added;
}

bool FillToolbarGlyphs(HIMAGELIST images, std::wstring_view glyphs, int size, COLORREF colour) noexcept {
    if (!images || glyphs.empty() || size <= 0) return false;
    // Windows 11 ships Segoe Fluent Icons; Windows 10 has the older set with
    // the same code points.
    HFONT font = nullptr;
    for (const wchar_t* face : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        HFONT candidate = ::CreateFontW(-(size * 5 / 6), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        if (!candidate) continue;
        HDC probe = ::GetDC(nullptr);
        const HGDIOBJ previous = ::SelectObject(probe, candidate);
        wchar_t actual[LF_FACESIZE]{};
        ::GetTextFaceW(probe, LF_FACESIZE, actual);
        ::SelectObject(probe, previous);
        ::ReleaseDC(nullptr, probe);
        if (::_wcsicmp(actual, face) == 0) {
            font = candidate;
            break;
        }
        ::DeleteObject(candidate);
    }
    if (!font) return false;

    const int width = size * static_cast<int>(glyphs.size());
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(format.bmiHeader);
    format.bmiHeader.biWidth = width;
    format.bmiHeader.biHeight = -size;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = ::GetDC(nullptr);
    HDC memory = ::CreateCompatibleDC(screen);
    HBITMAP image = ::CreateDIBSection(screen, &format, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    bool added = false;
    if (memory && image && bits) {
        // Draw white on black, then read the brightness back as coverage.
        const HGDIOBJ previous_bitmap = ::SelectObject(memory, image);
        const HGDIOBJ previous_font = ::SelectObject(memory, font);
        RECT all{0, 0, width, size};
        Fill(memory, all, RGB(0, 0, 0));
        ::SetBkMode(memory, TRANSPARENT);
        ::SetTextColor(memory, RGB(255, 255, 255));
        for (std::size_t index = 0; index < glyphs.size(); ++index) {
            RECT cell{static_cast<LONG>(index) * size, 0, static_cast<LONG>(index + 1) * size, size};
            ::DrawTextW(memory, &glyphs[index], 1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        ::GdiFlush();
        ::SelectObject(memory, previous_font);
        ::SelectObject(memory, previous_bitmap);
        const DWORD ink = (static_cast<DWORD>(GetRValue(colour)) << 16) |
            (static_cast<DWORD>(GetGValue(colour)) << 8) | GetBValue(colour);
        auto* pixels = static_cast<DWORD*>(bits);
        for (std::size_t index = 0; index < static_cast<std::size_t>(width) * size; ++index)
            pixels[index] = (((pixels[index] >> 8) & 0xFF) << 24) | ink;
        ImageList_RemoveAll(images);
        added = ImageList_Add(images, image, nullptr) == 0;
    }
    if (image) ::DeleteObject(image);
    if (memory) ::DeleteDC(memory);
    ::DeleteObject(font);
    return added;
}

void FlattenControlBorders(HWND parent, bool flat) noexcept {
    if (parent) ::EnumChildWindows(parent, FlattenChild, flat ? TRUE : FALSE);
}

void InstallDarkMenuBar(HWND frame) noexcept {
    if (frame) ::SetWindowSubclass(frame, MenuBarProcedure, kMenuBarSubclass, 0);
}

void InstallDarkTabStrip(HWND tabs) noexcept {
    if (tabs) ::SetWindowSubclass(tabs, TabStripProcedure, kTabStripSubclass, 0);
}

void InstallDarkStatusBar(HWND status) noexcept {
    if (status) ::SetWindowSubclass(status, StatusBarProcedure, kStatusBarSubclass, 0);
}

void DarkenDialog(HWND dialog) noexcept {
    if (!dialog) return;
    const auto state = mwfl::GetWindowAppearance(dialog);
    if (!state.IsDark()) return;
    auto* colours = new (std::nothrow) DialogColours{state.palette};
    if (!colours) return;
    colours->window = ::CreateSolidBrush(state.palette.window_background);
    colours->control = ::CreateSolidBrush(state.palette.control_background);
    if (!::SetWindowSubclass(dialog, DialogProcedure, kDialogSubclass,
                             reinterpret_cast<DWORD_PTR>(colours))) {
        if (colours->window) ::DeleteObject(colours->window);
        if (colours->control) ::DeleteObject(colours->control);
        delete colours;
        return;
    }
    const BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE
    static_cast<void>(::DwmSetWindowAttribute(dialog, 20, &dark, sizeof(dark)));
    // List and tree views are created after this call; colour them once the
    // dialog has finished building itself.
    ::PostMessageW(dialog, kThemeDialogChildren, 0, 0);
}

}  // namespace notepad_colon::app
