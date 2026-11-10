#include "main_frame.hpp"
#include "file_metadata.hpp"
#include "app_root.hpp"

#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/dirdlg.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/srchctrl.h>
#include <wx/stdpaths.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/listctrl.h>
#include <wx/listbox.h>
#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/choicdlg.h>
#include <wx/event.h>
#include <wx/artprov.h>
#include <wx/aui/tabart.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/settings.h>
#include <wx/display.h>
#ifdef __WXMSW__
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText
#endif
#endif

#include <fstream>
#include <iterator>
#include <limits>
#include <cmath>
#include <cstring>
#include <charconv>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <regex>
#include <thread>
#include <chrono>
#include <cerrno>
#include <optional>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>
#ifndef __WXMSW__
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using rbl::wxui::FileIdentity;
using rbl::wxui::SameFileIdentity;
#ifndef __WXMSW__
using rbl::wxui::CopyPosixMetadataAt;
using rbl::wxui::QueryFileIdentityAt;
using rbl::wxui::ReadFileAt;

int PosixErrorOrIoError() noexcept {
    const int code = errno;
    // Windows-backed WSL filesystems can fail a dirfd operation without
    // setting errno. Never report error_code{} for a failed syscall.
    return code != 0 ? code : EIO;
}
#endif

bool HasSymlinkOrReparseComponent(const std::filesystem::path& path,
                                  bool& found,
                                  std::error_code& error) {
    found = false;
    error.clear();
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        current /= component;
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                error.clear();
                return true;
            }
            error = std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            found = true;
            return true;
        }
#else
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(current, status_error);
        if (status_error == std::errc::no_such_file_or_directory) return true;
        if (status_error) {
            error = status_error;
            return false;
        }
        if (std::filesystem::is_symlink(status)) {
            found = true;
            return true;
        }
#endif
    }
    return true;
}

#ifdef _WIN32
struct WinDirectoryChainGuard {
    std::vector<HANDLE> handles;
    ~WinDirectoryChainGuard() {
        for (const HANDLE handle : handles) {
            if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
        }
    }
};

bool LockWindowsDirectoryChain(const std::filesystem::path& directory,
                               WinDirectoryChainGuard& guard,
                               std::error_code& error) {
    auto current = directory.root_path();
    if (current.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const auto open_directory = [&](const std::filesystem::path& path) {
        const HANDLE handle = ::CreateFileW(path.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!::GetFileInformationByHandle(handle, &info)) {
            const DWORD code = ::GetLastError();
            ::CloseHandle(handle);
            error = std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            ::CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        guard.handles.push_back(handle);
        return true;
    };
    if (!open_directory(current)) return false;
    for (const auto& component : directory.relative_path()) {
        current /= component;
        if (!open_directory(current)) return false;
    }
    error.clear();
    return true;
}

std::filesystem::path UniqueWindowsSiblingPath(const std::filesystem::path& target,
                                                const wchar_t* suffix,
                                                std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        auto candidate = target;
        candidate += suffix;
        candidate += std::to_wstring(::GetCurrentProcessId());
        candidate += L"-";
        candidate += std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        if (::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            error.clear();
            return candidate;
        }
        error = std::error_code(static_cast<int>(code), std::system_category());
        return {};
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}
#endif

#ifndef _WIN32
struct PosixDirectoryHandle {
    explicit PosixDirectoryHandle(int value) : fd(value) {}
    PosixDirectoryHandle(const PosixDirectoryHandle&) = delete;
    PosixDirectoryHandle& operator=(const PosixDirectoryHandle&) = delete;
    ~PosixDirectoryHandle() { if (fd >= 0) ::close(fd); }
    int fd{-1};
};

struct PosixSearchDirectory {
    std::filesystem::path path;
    std::size_t depth{};
    std::shared_ptr<PosixDirectoryHandle> parent;
    std::string entry_name;
    dev_t expected_device{};
    ino_t expected_inode{};
    std::shared_ptr<PosixDirectoryHandle> handle;
};

int OpenDirectoryPathNoFollow(const std::filesystem::path& path) {
    auto current = path.root_path();
    if (current.empty()) { errno = EINVAL; return -1; }
    int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = ::open(current.c_str(), flags);
    if (fd < 0) return -1;
    for (const auto& component : path.relative_path()) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            ::close(fd);
            errno = EINVAL;
            return -1;
        }
        const auto name = component.native();
        const int child = ::openat(fd, name.c_str(), flags);
        const int saved_errno = PosixErrorOrIoError();
        ::close(fd);
        if (child < 0) { errno = saved_errno; return -1; }
        fd = child;
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0) {
        const int saved_errno = PosixErrorOrIoError();
        ::close(fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        ::close(fd);
        errno = ENOTDIR;
        return -1;
    }
    return fd;
}
#endif

struct SearchFileHandleGuard {
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~SearchFileHandleGuard() {
        if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    }
#else
    int fd{-1};
    ~SearchFileHandleGuard() {
        if (fd >= 0) ::close(fd);
    }
#endif
};

constexpr std::size_t MaxOpenTabs = 32;
constexpr std::size_t MaxOpenDocumentBytes = 64U * 1024U * 1024U;
constexpr std::size_t MaxEditableFileBytes = 16U * 1024U * 1024U;
constexpr std::size_t MaxUndoHistoryBytes = 32U * 1024U * 1024U;
constexpr std::size_t MaxProjectTreeNodes = 50000;
constexpr int MaxFoldLines = 50000;
enum : int {
    ID_New = wxID_HIGHEST + 1,
    ID_Open,
    ID_Save,
    ID_SaveAs,
    ID_CloseTab,
    ID_Run,
    ID_Check,
    ID_Build,
    ID_Stop,
    ID_Stdin,
    ID_SendStdin,
    ID_StdinEof,
    ID_WorkerTimer,
    ID_WorkerProcess,
    ID_Find,
    ID_Replace,
    ID_FoldTimer,
    ID_CloseTabMenu,
    ID_OpenProject,
    ID_Assembly,
    ID_Tests,
    ID_Benchmarks,
    ID_Doctor,
    ID_Diagnostics,
    ID_Settings,
    ID_FindInFiles,
    ID_SearchResults,
    ID_ProjectSearchDone,
    ID_GoToLine,
    ID_QuickOpen,
    ID_CommandPalette,
    ID_QuickReference,
    ID_About,
    ID_AutoCheckTimer,
    ID_CancelSearch,
    ID_SaveAll,
    ID_CloseAll,
    ID_CloseOtherTabs,
    ID_ReopenClosedTab,
    ID_SearchResultsTimer,
    ID_DiagnosticsTimer,
    ID_NewText,
    ID_ToggleProjectPane,
    ID_ToggleOutputPane,
    ID_ResetLayout,
    ID_FindNext,
    ID_FindPrevious,
    ID_ReplaceOne,
    ID_ReplaceAll,
    ID_FindClose,
    ID_FindNextCommand,
    ID_FindPreviousCommand,
};

enum RblStyle : int {
    RblDefault = 0,
    RblKeyword = 1,
    RblString = 2,
    RblComment = 3,
    RblNumber = 4,
    RblFunction = 5,
    RblBoolean = 6,
    RblOperator = 7,
};

class DarkToolBarArt final : public wxAuiGenericToolBarArt {
public:
    explicit DarkToolBarArt(const rbl::wxui::ThemePalette& palette, bool compact = false) : palette_(palette) {
        m_baseColour = palette_.surface;
        m_highlightColour = palette_.selected;
        m_font = wxFontInfo(compact ? 8 : 9).FaceName("Segoe UI");
    }

    wxAuiToolBarArt* Clone() override {
        return new DarkToolBarArt(palette_, m_font.GetPointSize() <= 8);
    }

    void DrawBackground(wxDC& dc, wxWindow*, const wxRect& rect) override {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette_.surface));
        dc.DrawRectangle(rect);
    }

    void DrawPlainBackground(wxDC& dc, wxWindow*, const wxRect& rect) override {
        DrawBackground(dc, nullptr, rect);
    }

    void DrawLabel(wxDC& dc, wxWindow*, const wxAuiToolBarItem& item,
                   const wxRect& rect) override {
        dc.SetTextForeground(palette_.text);
        dc.SetFont(m_font);
        dc.DrawLabel(item.GetLabel(), wxNullBitmap, rect, wxALIGN_CENTER);
    }
private:
    rbl::wxui::ThemePalette palette_;
};

class FlatDockArt final : public wxAuiDefaultDockArt {
public:
    explicit FlatDockArt(const rbl::wxui::ThemePalette& palette) : palette_(palette) {
        SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, palette_.window);
        SetColour(wxAUI_DOCKART_SASH_COLOUR, palette_.border);
        SetColour(wxAUI_DOCKART_BORDER_COLOUR, palette_.border);
        SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, palette_.surface_alt);
        SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, palette_.surface);
        SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, palette_.text);
        SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, palette_.muted);
        SetMetric(wxAUI_DOCKART_SASH_SIZE, 3);
        SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
        SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
    }

    wxAuiDockArt* Clone() override { return new FlatDockArt(palette_); }

    void DrawSash(wxDC& dc, wxWindow*, int, const wxRect& rect) override {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette_.border));
        dc.DrawRectangle(rect);
    }

    void DrawBackground(wxDC& dc, wxWindow*, int, const wxRect& rect) override {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette_.window));
        dc.DrawRectangle(rect);
    }

    void DrawBorder(wxDC& dc, wxWindow*, const wxRect& rect, wxAuiPaneInfo&) override {
        if (rect.IsEmpty()) return;
        dc.SetPen(wxPen(palette_.border));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(rect);
    }

private:
    rbl::wxui::ThemePalette palette_;
};

class ThemeTabArt final : public wxAuiSimpleTabArt {
public:
    explicit ThemeTabArt(const rbl::wxui::ThemePalette& palette, bool compact = false)
        : palette_(palette) {
        const wxFont normal(wxFontInfo(compact ? 8 : 9).FaceName("Segoe UI"));
        const wxFont selected(wxFontInfo(compact ? 8 : 9).Bold().FaceName("Segoe UI"));
        SetColour(palette_.surface);
        SetActiveColour(palette_.selected);
        SetNormalFont(normal);
        SetSelectedFont(selected);
        SetMeasuringFont(normal);
    }

    wxAuiTabArt* Clone() override {
        auto* copy = new ThemeTabArt(palette_, m_normalFont.GetPointSize() <= 8);
        copy->SetFlags(m_flags);
        copy->SetNormalFont(m_normalFont);
        copy->SetSelectedFont(m_selectedFont);
        copy->SetMeasuringFont(m_measuringFont);
        return copy;
    }

    void DrawBorder(wxDC& dc, wxWindow* wnd, const wxRect& rect) override {
        if (rect.IsEmpty()) return;
        const int width = wnd ? std::max(1, wnd->FromDIP(1)) : 1;
        dc.SetPen(wxPen(palette_.border, width));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLine(rect.x, rect.GetBottom(), rect.GetRight(), rect.GetBottom());
    }

    void DrawBackground(wxDC& dc, wxWindow*, const wxRect& rect) override {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette_.surface));
        dc.DrawRectangle(rect);
    }

    void DrawTab(wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& page,
                 const wxRect& inRect, int closeButtonState, wxRect* outTabRect,
                 wxRect* outButtonRect, int* xExtent) override {
        const bool closeOnAllTabs = (m_flags & wxAUI_NB_CLOSE_ON_ALL_TABS) != 0;
        const bool closeOnActiveTab = (m_flags & wxAUI_NB_CLOSE_ON_ACTIVE_TAB) != 0 && page.active;
        const bool showCloseButton = closeOnAllTabs || closeOnActiveTab;
        // Keep the close-button state stable for every page selected by the
        // notebook style. The stock tab art leaves a 10-DIP overlap at the
        // trailing edge, which otherwise lets the next tab cover this glyph.
        const int visibleCloseState = showCloseButton
            ? closeButtonState & ~wxAUI_BUTTON_STATE_HIDDEN : closeButtonState;
        wxAuiSimpleTabArt::DrawTab(dc, wnd, page, inRect, visibleCloseState,
                                   outTabRect, outButtonRect, xExtent);

        const wxRect tabRect = outTabRect && !outTabRect->IsEmpty() ? *outTabRect : inRect;
        if (showCloseButton && outButtonRect && outButtonRect->IsEmpty()) {
            // The simple art provider leaves this empty for inactive pages on
            // some wxWidgets builds. Supply the same stable close-button slot
            // ourselves so every tab has both visible artwork and a hit area.
            const int closeSize = wnd ? wnd->FromDIP(16) : 16;
            const int closeRight = wnd ? wnd->FromDIP(4) : 4;
            *outButtonRect = wxRect(tabRect.GetRight() + 1 - closeRight - closeSize,
                tabRect.y + (tabRect.height - closeSize) / 2, closeSize, closeSize);
        }
        if (showCloseButton && outButtonRect && !outButtonRect->IsEmpty()) {
            // Simple tabs overlap the following tab by 10 DIP. Move the
            // inactive-tab close slot inside the visible portion, and do the
            // same for active tabs so the glyph never jumps when selection
            // changes.
            outButtonRect->Offset(-(wnd ? wnd->FromDIP(10) : 10), 0);
        } else if (outButtonRect) {
            *outButtonRect = wxRect();
        }
        // Paint over the stock bevel/gradient. The rest of the IDE uses flat,
        // theme-token surfaces; leaving the native AUI tab bevel visible looks
        // like a legacy raised Windows control.
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(page.active ? palette_.selected : palette_.surface));
        dc.DrawRectangle(tabRect);
        dc.SetPen(wxPen(palette_.border, wnd ? std::max(1, wnd->FromDIP(1)) : 1));
        dc.DrawLine(tabRect.x, tabRect.GetBottom(), tabRect.GetRight(), tabRect.GetBottom());

        const int padding = wnd ? wnd->FromDIP(8) : 8;
        const int gap = wnd ? wnd->FromDIP(5) : 5;
        const int inset = wnd ? wnd->FromDIP(1) : 1;
        int textX = tabRect.x + padding;
        if (page.bitmap.IsOk())
            textX += page.bitmap.GetPreferredLogicalSizeFor(wnd).GetWidth() + gap;

        int textRight = tabRect.GetRight() + 1 - padding;
        if (showCloseButton && outButtonRect && !outButtonRect->IsEmpty()) {
            const int buttonGap = wnd ? wnd->FromDIP(3) : 3;
            textRight = std::min(textRight, outButtonRect->x - buttonGap);
        }
        if (textRight <= textX) return;

        const wxRect textRect(textX, tabRect.y + inset,
            textRight - textX, std::max(0, tabRect.height - inset * 2));
        if (textRect.IsEmpty()) return;

        dc.SetClippingRegion(textRect);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetTextForeground(page.active ? palette_.text : palette_.muted);
        dc.SetFont(page.active ? m_selectedFont : m_normalFont);
        wxCoord textHeight = 0;
        dc.GetTextExtent(page.caption, nullptr, &textHeight);
        const int textY = tabRect.y + (tabRect.height - textHeight) / 2;
        dc.DrawText(page.caption, textX, textY);
        dc.DestroyClippingRegion();

        // Render the close glyph here as well as through DrawButton: some AUI
        // builds skip the latter callback when the tab's transient state is
        // HIDDEN, even though the hit-test rectangle is reserved below.
        if (showCloseButton && outButtonRect && !outButtonRect->IsEmpty()) {
            const bool hovered = (closeButtonState & wxAUI_BUTTON_STATE_HOVER) != 0;
            const bool pressed = (closeButtonState & wxAUI_BUTTON_STATE_PRESSED) != 0;
            if (hovered || pressed) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(pressed ? palette_.selected : palette_.surface_alt));
                dc.DrawRoundedRectangle(*outButtonRect, wnd ? wnd->FromDIP(3) : 3);
            }
            const int inset = wnd ? wnd->FromDIP(5) : 5;
            const int left = outButtonRect->x + inset;
            const int right = outButtonRect->GetRight() - inset;
            const int top = outButtonRect->y + inset;
            const int bottom = outButtonRect->GetBottom() - inset;
            if (right > left && bottom > top) {
                dc.SetPen(wxPen(hovered || pressed ? palette_.text : palette_.muted,
                    wnd ? std::max(1, wnd->FromDIP(1)) : 1));
                dc.DrawLine(left, top, right, bottom);
                dc.DrawLine(right, top, left, bottom);
            }
        }
    }

    wxSize GetTabSize(wxDC& dc, wxWindow* wnd, const wxString& caption,
                      const wxBitmapBundle& bitmap, bool active,
                      int closeButtonState, int* xExtent) override {
        const bool showCloseButton =
            (m_flags & wxAUI_NB_CLOSE_ON_ALL_TABS) != 0 ||
            ((m_flags & wxAUI_NB_CLOSE_ON_ACTIVE_TAB) != 0 && active);
        return wxAuiSimpleTabArt::GetTabSize(dc, wnd, caption, bitmap, active,
            showCloseButton ? closeButtonState & ~wxAUI_BUTTON_STATE_HIDDEN
                            : closeButtonState, xExtent);
    }

    void DrawButton(wxDC& dc, wxWindow* wnd, const wxRect& inRect, int bitmapId,
                    int buttonState, int orientation, wxRect* outRect) override {
        if (bitmapId != wxAUI_BUTTON_CLOSE) {
            wxAuiSimpleTabArt::DrawButton(dc, wnd, inRect, bitmapId,
                                          buttonState, orientation, outRect);
            return;
        }
        // The notebook may still pass HIDDEN to DrawButton even though
        // DrawTab reserved the close-button slot for every page. Ignore only
        // that visibility bit; hover/pressed states remain intact.
        buttonState &= ~wxAUI_BUTTON_STATE_HIDDEN;
        if (outRect) *outRect = inRect;

        const bool hovered = (buttonState & wxAUI_BUTTON_STATE_HOVER) != 0;
        const bool pressed = (buttonState & wxAUI_BUTTON_STATE_PRESSED) != 0;
        if (hovered || pressed) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(pressed ? palette_.selected : palette_.surface_alt));
            const int radius = wnd ? wnd->FromDIP(3) : 3;
            dc.DrawRoundedRectangle(inRect, radius);
        }

        const int inset = wnd ? wnd->FromDIP(5) : 5;
        const int left = inRect.x + inset;
        const int right = inRect.GetRight() - inset;
        const int top = inRect.y + inset;
        const int bottom = inRect.GetBottom() - inset;
        if (right <= left || bottom <= top) return;
        const int penWidth = wnd ? std::max(1, wnd->FromDIP(1)) : 1;
        dc.SetPen(wxPen(hovered || pressed ? palette_.text : palette_.muted, penWidth));
        dc.DrawLine(left, top, right, bottom);
        dc.DrawLine(right, top, left, bottom);
    }

private:
    rbl::wxui::ThemePalette palette_;
};

wxBitmap MakeProjectGlyph(int kind, const rbl::wxui::ThemePalette& palette,
                          const wxSize& bitmap_size) {
    wxBitmap bitmap(bitmap_size.GetWidth(), bitmap_size.GetHeight(), 32);
    wxMemoryDC dc(bitmap);
    const double scale_x = static_cast<double>(bitmap_size.GetWidth()) / 16.0;
    const double scale_y = static_cast<double>(bitmap_size.GetHeight()) / 16.0;
    dc.SetUserScale(scale_x, scale_y);
    dc.SetBackground(wxBrush(palette.surface));
    dc.Clear();
    if (kind == 0 || kind == 1) {
        const wxColour folder = kind == 0 ? wxColour(105, 151, 199) : wxColour(91, 130, 172);
        dc.SetPen(wxPen(wxColour(49, 72, 98), 1));
        dc.SetBrush(wxBrush(folder));
        dc.DrawRoundedRectangle(1, 5, 14, 9, 2);
        dc.DrawRoundedRectangle(2, 3, 7, 4, 1);
        if (kind == 0) {
            dc.SetPen(wxPen(wxColour(143, 183, 220), 1));
            dc.DrawLine(2, 7, 13, 7);
        }
    } else if (kind == 2) {
        dc.SetPen(wxPen(wxColour(92, 72, 166), 1));
        dc.SetBrush(wxBrush(wxColour(111, 91, 190)));
        dc.DrawRoundedRectangle(2, 1, 12, 14, 2);
        dc.SetPen(wxPen(wxColour(190, 178, 245), 1));
        dc.DrawLine(10, 1, 14, 5);
        dc.SetTextForeground(*wxWHITE);
        dc.SetFont(wxFontInfo(9).Bold().FaceName("Segoe UI"));
        dc.DrawText("R", 4, 3);
    } else {
        dc.SetPen(wxPen(wxColour(91, 103, 120), 1));
        dc.SetBrush(wxBrush(wxColour(145, 157, 174)));
        dc.DrawRoundedRectangle(3, 1, 10, 14, 1);
        dc.SetPen(wxPen(wxColour(65, 77, 94), 1));
        dc.DrawLine(5, 6, 11, 6);
        dc.DrawLine(5, 9, 11, 9);
        dc.DrawLine(5, 12, 9, 12);
    }
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

wxIcon MakeApplicationIcon(const wxSize& bitmap_size) {
    wxBitmap bitmap(bitmap_size.GetWidth(), bitmap_size.GetHeight(), 32);
    wxMemoryDC dc(bitmap);
    const double scale_x = static_cast<double>(bitmap_size.GetWidth()) / 32.0;
    const double scale_y = static_cast<double>(bitmap_size.GetHeight()) / 32.0;
    dc.SetUserScale(scale_x, scale_y);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();
    dc.SetPen(wxPen(wxColour(201, 168, 240), 1));
    dc.SetBrush(wxBrush(wxColour(79, 50, 116)));
    dc.DrawEllipse(1, 1, 30, 30);
    dc.SetTextForeground(*wxWHITE);
    dc.SetFont(wxFontInfo(23).Bold().FaceName("Arial"));
    wxCoord text_width = 0;
    wxCoord text_height = 0;
    dc.GetTextExtent("R", &text_width, &text_height);
    dc.DrawText("R", (32 - text_width) / 2, (32 - text_height) / 2 - 1);
    dc.SelectObject(wxNullBitmap);
    wxIcon icon;
    icon.CopyFromBitmap(bitmap);
    return icon;
}

void ClampFrameToWorkArea(wxFrame* frame, const wxSize& requested_size) {
    wxDisplay display(frame);
    if (!display.IsOk()) {
        frame->SetSize(requested_size);
        return;
    }

    const wxRect work_area = display.GetClientArea();
    if (work_area.IsEmpty()) {
        frame->SetSize(requested_size);
        return;
    }
    const wxSize bounded_size(
        std::min(requested_size.GetWidth(), work_area.GetWidth()),
        std::min(requested_size.GetHeight(), work_area.GetHeight()));
    frame->SetSize(bounded_size);

    const int max_x = work_area.GetRight() - bounded_size.GetWidth() + 1;
    const int max_y = work_area.GetBottom() - bounded_size.GetHeight() + 1;
    const wxPoint current = frame->GetPosition();
    frame->SetPosition(wxPoint(
        std::clamp(current.x, work_area.GetLeft(), std::max(work_area.GetLeft(), max_x)),
        std::clamp(current.y, work_area.GetTop(), std::max(work_area.GetTop(), max_y))));
}

std::optional<std::string> ScaleAuiPerspectiveSizes(std::string_view perspective,
                                                     double scale_ratio) {
    if (!std::isfinite(scale_ratio) || scale_ratio < 0.25 || scale_ratio > 4.0)
        return std::nullopt;
    std::string result(perspective);
    const auto scale_value = [&](std::size_t begin, std::size_t& end) -> bool {
        if (begin >= end) return false;
        int value = 0;
        const auto parsed = std::from_chars(result.data() + begin, result.data() + end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != result.data() + end) return false;
        // Negative AUI sentinel values (notably -1 for an unconstrained size)
        // are semantic flags, not dimensions.
        if (value <= 0) return true;
        const double scaled = std::round(static_cast<double>(value) * scale_ratio);
        if (!std::isfinite(scaled) || scaled > std::numeric_limits<int>::max()) return false;
        const auto replacement = std::to_string(static_cast<int>(scaled));
        result.replace(begin, end - begin, replacement);
        end = begin + replacement.size();
        return true;
    };

    static constexpr std::string_view size_keys[] = {
        "bestw=", "besth=", "minw=", "minh=", "maxw=", "maxh=", "floatw=", "floath="
    };
    for (const auto key : size_keys) {
        std::size_t search = 0;
        while ((search = result.find(key, search)) != std::string::npos) {
            const std::size_t value_begin = search + key.size();
            std::size_t value_end = value_begin;
            if (value_end < result.size() && result[value_end] == '-') ++value_end;
            while (value_end < result.size() && result[value_end] >= '0' && result[value_end] <= '9')
                ++value_end;
            if (value_end == value_begin || (value_end == value_begin + 1 && result[value_begin] == '-'))
                return std::nullopt;
            if (!scale_value(value_begin, value_end)) return std::nullopt;
            search = value_end;
        }
    }

    // wxWidgets 3.2.11 serializes dock_size(dir,layer,row)=pixel_size.
    // Validate but preserve the three topology integers; scale only the
    // integer after ')='. Floating x/y coordinates are handled by monitor
    // clamping, not by DPI scaling.
    std::size_t dock = 0;
    while ((dock = result.find("dock_size(", dock)) != std::string::npos) {
        std::size_t cursor = dock + std::string_view("dock_size(").size();
        for (int component = 0; component < 3; ++component) {
            std::size_t end = cursor;
            if (end < result.size() && result[end] == '-') ++end;
            while (end < result.size() && result[end] >= '0' && result[end] <= '9') ++end;
            if (end == cursor || (end == cursor + 1 && result[cursor] == '-')) return std::nullopt;
            int topology_value = 0;
            const auto parsed = std::from_chars(result.data() + cursor, result.data() + end,
                                                topology_value);
            if (parsed.ec != std::errc{} || parsed.ptr != result.data() + end)
                return std::nullopt;
            cursor = end;
            if (component != 2) {
                if (cursor >= result.size() || result[cursor] != ',') return std::nullopt;
                ++cursor;
            }
        }
        if (cursor + 1 >= result.size() || result[cursor] != ')' || result[cursor + 1] != '=')
            return std::nullopt;
        cursor += 2;
        std::size_t size_end = cursor;
        if (size_end < result.size() && result[size_end] == '-') ++size_end;
        while (size_end < result.size() && result[size_end] >= '0' && result[size_end] <= '9')
            ++size_end;
        if (size_end == cursor || (size_end == cursor + 1 && result[cursor] == '-'))
            return std::nullopt;
        if (!scale_value(cursor, size_end)) return std::nullopt;
        dock = size_end;
    }
    return result;
}

void ClampFloatingPanesToDisplays(wxAuiManager& manager, wxWindow* owner) {
    const int owner_display = wxDisplay::GetFromWindow(owner);
    auto& panes = manager.GetAllPanes();
    for (std::size_t index = 0; index < panes.GetCount(); ++index) {
        auto& pane = panes.Item(index);
        if (!pane.IsOk() || !pane.IsFloating()) continue;
        int display_index = wxDisplay::GetFromPoint(pane.floating_pos);
        if (display_index == wxNOT_FOUND) display_index = owner_display;
        if (display_index == wxNOT_FOUND) continue;
        wxDisplay display(static_cast<unsigned int>(display_index));
        if (!display.IsOk()) continue;
        const wxRect area = display.GetClientArea();
        if (area.IsEmpty()) continue;
        wxSize size = pane.floating_size;
        if (size.GetWidth() <= 0 || size.GetHeight() <= 0) continue;
        size.SetWidth(std::min(size.GetWidth(), area.GetWidth()));
        size.SetHeight(std::min(size.GetHeight(), area.GetHeight()));
        const int max_x = area.GetRight() - size.GetWidth() + 1;
        const int max_y = area.GetBottom() - size.GetHeight() + 1;
        const wxPoint pos(
            std::clamp(pane.floating_pos.x, area.GetLeft(), std::max(area.GetLeft(), max_x)),
            std::clamp(pane.floating_pos.y, area.GetTop(), std::max(area.GetTop(), max_y)));
        pane.FloatingPosition(pos).FloatingSize(size);
    }
}

bool IsIdentStart(unsigned char ch) {
    return ch == '_' || (ch < 0x80 && std::isalpha(ch)) || ch >= 0x80;
}
bool IsIdentContinue(unsigned char ch) {
    return ch == '_' || (ch < 0x80 && std::isalnum(ch)) || ch >= 0x80;
}
bool IsKeyword(std::string_view word) {
    static constexpr std::string_view keywords[] = {
        "set", "let", "func", "if", "elif", "else", "return", "and", "or",
        "not", "is", "for", "in", "break", "continue", "struct", "switch",
        "case", "default"
    };
    return std::find(std::begin(keywords), std::end(keywords), word) != std::end(keywords);
}
bool IsBoolean(std::string_view word) {
    return word == "true" || word == "false" || word == "null";
}
std::size_t HashBytes(std::string_view bytes) {
    return std::hash<std::string_view>{}(bytes);
}
bool IsValidUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            if (lead < 0x20 && lead != '\t' && lead != '\n' && lead != '\r') return false;
            ++i;
            continue;
        }
        std::size_t count = 0;
        unsigned char second_min = 0x80, second_max = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) count = 2;
        else if (lead >= 0xE0 && lead <= 0xEF) {
            count = 3;
            if (lead == 0xE0) second_min = 0xA0;
            if (lead == 0xED) second_max = 0x9F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            count = 4;
            if (lead == 0xF0) second_min = 0x90;
            if (lead == 0xF4) second_max = 0x8F;
        } else return false;
        if (i + count > text.size()) return false;
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if (second < second_min || second > second_max) return false;
        for (std::size_t j = 2; j < count; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if (continuation < 0x80 || continuation > 0xBF) return false;
        }
        i += count;
    }
    return true;
}

void AppendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool DecodeUtf16(std::string_view bytes, bool little_endian, std::string& utf8) {
    if ((bytes.size() & 1U) != 0) return false;
    utf8.clear();
    utf8.reserve(bytes.size());
    auto unit_at = [&](std::size_t offset) {
        const auto a = static_cast<unsigned char>(bytes[offset]);
        const auto b = static_cast<unsigned char>(bytes[offset + 1]);
        return static_cast<std::uint16_t>(little_endian ? (a | (b << 8)) : ((a << 8) | b));
    };
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
        std::uint32_t codepoint = unit_at(offset);
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (offset + 3 >= bytes.size()) return false;
            const auto low = unit_at(offset + 2);
            if (low < 0xDC00 || low > 0xDFFF) return false;
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            offset += 2;
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return false;
        }
        if (codepoint == 0) return false;
        AppendUtf8(utf8, codepoint);
    }
    return true;
}

std::string EncodeUtf16(std::string_view utf8, bool little_endian) {
    std::string bytes;
    bytes.reserve(utf8.size() * 2);
    auto append_unit = [&](std::uint16_t unit) {
        const char first = static_cast<char>(little_endian ? (unit & 0xFF) : (unit >> 8));
        const char second = static_cast<char>(little_endian ? (unit >> 8) : (unit & 0xFF));
        bytes.push_back(first);
        bytes.push_back(second);
    };
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i++]);
        std::uint32_t cp = lead;
        int continuation = 0;
        if (lead >= 0xF0) { cp = lead & 0x07; continuation = 3; }
        else if (lead >= 0xE0) { cp = lead & 0x0F; continuation = 2; }
        else if (lead >= 0xC0) { cp = lead & 0x1F; continuation = 1; }
        for (int j = 0; j < continuation && i < utf8.size(); ++j) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i++]) & 0x3F);
        }
        if (cp <= 0xFFFF) append_unit(static_cast<std::uint16_t>(cp));
        else {
            cp -= 0x10000;
            append_unit(static_cast<std::uint16_t>(0xD800 | (cp >> 10)));
            append_unit(static_cast<std::uint16_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    return bytes;
}

std::string ToUtf8(const wxString& value) {
    const auto bytes = value.ToUTF8();
    return std::string(bytes.data(), bytes.length());
}

wxString FromUtf8(const std::string& value) {
    return wxString::FromUTF8(value.data(), value.size());
}

std::string JsonEscape(const wxString& value) {
    const auto input = ToUtf8(value);
    std::string output;
    output.reserve(input.size() + 8);
    for (const unsigned char ch : input) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) {
                static constexpr char hex[] = "0123456789ABCDEF";
                output += "\\u00";
                output.push_back(hex[ch >> 4]);
                output.push_back(hex[ch & 0x0F]);
            } else {
                output += static_cast<char>(ch);
            }
        }
    }
    return output;
}

wxString JsonStringField(const wxString& json, const wxString& key) {
    const wxString needle = "\"" + key + "\"";
    auto position = json.find(needle);
    if (position == wxString::npos) return {};
    position = json.find(':', position + needle.length());
    if (position == wxString::npos) return {};
    position = json.find_first_not_of(" \t\r\n", position + 1);
    if (position == wxString::npos || json[position] != '"') return {};
    ++position;
    wxString value;
    auto read_hex4 = [&](std::size_t& offset) -> std::uint32_t {
        if (offset + 4 > json.length()) return 0xFFFD;
        std::uint32_t codepoint = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const wxChar hex = json[offset++];
            codepoint <<= 4;
            if (hex >= '0' && hex <= '9') codepoint |= static_cast<std::uint32_t>(hex - '0');
            else if (hex >= 'a' && hex <= 'f') codepoint |= static_cast<std::uint32_t>(hex - 'a' + 10);
            else if (hex >= 'A' && hex <= 'F') codepoint |= static_cast<std::uint32_t>(hex - 'A' + 10);
            else return 0xFFFD;
        }
        return codepoint;
    };
    while (position < json.length()) {
        const wxChar ch = json[position++];
        if (ch == '"') break;
        if (ch == '\\' && position < json.length()) {
            const wxChar escaped = json[position++];
            switch (escaped) {
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'u': {
                auto codepoint = read_hex4(position);
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                    position + 6 <= json.length() && json[position] == '\\' && json[position + 1] == 'u') {
                    position += 2;
                    const auto low = read_hex4(position);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        codepoint = 0xFFFD;
                    }
                }
                value += wxUniChar(codepoint);
                break;
            }
            default: value += escaped; break;
            }
        } else {
            value += ch;
        }
    }
    return value;
}

bool JsonBoolField(const wxString& json, const wxString& key, bool fallback = false) {
    const wxString needle = "\"" + key + "\"";
    auto position = json.find(needle);
    if (position == wxString::npos) return fallback;
    position = json.find(':', position + needle.length());
    if (position == wxString::npos) return fallback;
    position = json.find_first_not_of(" \t\r\n", position + 1);
    if (position == wxString::npos) return fallback;
    if (json.Mid(position, 4) == "true") return true;
    if (json.Mid(position, 5) == "false") return false;
    return fallback;
}

wxString JsonNumberField(const wxString& json, const wxString& key) {
    const wxString needle = "\"" + key + "\"";
    auto position = json.find(needle);
    if (position == wxString::npos) return {};
    position = json.find(':', position + needle.length());
    if (position == wxString::npos) return {};
    position = json.find_first_not_of(" \t\r\n", position + 1);
    if (position == wxString::npos) return {};
    auto end = json.find_first_not_of("0123456789-", position);
    return json.Mid(position, end == wxString::npos ? wxString::npos : end - position);
}

wxString PageTitle(const std::filesystem::path& path) {
    if (path.empty()) return "Untitled";
    const auto name = path.filename().u8string();
    return wxString::FromUTF8(reinterpret_cast<const char*>(name.data()), name.size());
}

#ifndef _WIN32
struct PosixProjectRoot {
    PosixProjectRoot(int descriptor, FileIdentity value)
        : fd(descriptor), identity(value) {}
    ~PosixProjectRoot() {
        if (fd >= 0) ::close(fd);
    }
    PosixProjectRoot(const PosixProjectRoot&) = delete;
    PosixProjectRoot& operator=(const PosixProjectRoot&) = delete;

    int fd{-1};
    FileIdentity identity;
};

class ScopedProjectFd {
public:
    explicit ScopedProjectFd(int value = -1) : fd_(value) {}
    ~ScopedProjectFd() { if (fd_ >= 0) ::close(fd_); }
    ScopedProjectFd(const ScopedProjectFd&) = delete;
    ScopedProjectFd& operator=(const ScopedProjectFd&) = delete;
    ScopedProjectFd(ScopedProjectFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    ScopedProjectFd& operator=(ScopedProjectFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }
private:
    int fd_{-1};
};

bool SameProjectIdentity(const FileIdentity& expected, const struct stat& actual) {
    return expected.valid && expected.first == static_cast<std::uint64_t>(actual.st_dev) &&
        expected.second == static_cast<std::uint64_t>(actual.st_ino);
}

std::shared_ptr<PosixProjectRoot> OpenProjectRoot(const std::filesystem::path& path,
                                                  std::error_code& error) {
    const int descriptor = rbl::wxui::OpenDirectoryNoFollow(path, error);
    if (descriptor < 0) return {};
    struct stat info{};
    if (::fstat(descriptor, &info) != 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        ::close(descriptor);
        return {};
    }
    if (!S_ISDIR(info.st_mode)) {
        error = std::make_error_code(std::errc::not_a_directory);
        ::close(descriptor);
        return {};
    }
    error.clear();
    return std::make_shared<PosixProjectRoot>(descriptor,
        FileIdentity{static_cast<std::uint64_t>(info.st_dev),
                     static_cast<std::uint64_t>(info.st_ino), true,
                     static_cast<std::uint64_t>(info.st_nlink)});
}
#endif

class ProjectPathData final : public wxTreeItemData {
public:
    explicit ProjectPathData(std::filesystem::path value) : path(std::move(value)) {
#ifndef _WIN32
        std::error_code error;
        root = OpenProjectRoot(path, error);
        if (root) expected_identity = root->identity;
#endif
    }
#ifndef _WIN32
    ProjectPathData(std::filesystem::path value,
                    std::shared_ptr<PosixProjectRoot> root_value,
                    std::filesystem::path relative_value,
                    std::vector<FileIdentity> ancestors_value,
                    FileIdentity identity_value)
        : root(std::move(root_value)), relative_path(std::move(relative_value)),
          ancestor_identities(std::move(ancestors_value)),
          expected_identity(identity_value), path(std::move(value)) {}

    std::shared_ptr<PosixProjectRoot> root;
    std::filesystem::path relative_path;
    std::vector<FileIdentity> ancestor_identities;
    FileIdentity expected_identity;
#endif
    std::filesystem::path path;
    bool loaded{};
};

struct ProjectSearchHit {
    std::filesystem::path path;
    std::size_t line{};
    std::size_t column{};
    std::string snippet;
    FileIdentity identity;
};

struct ProjectSearchResults {
    std::vector<ProjectSearchHit> hits;
    std::vector<std::string> skipped_paths;
    std::size_t files_scanned{};
    bool truncated{};
    bool cancelled{};
};

wxString PathLabel(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    return wxString::FromUTF8(reinterpret_cast<const char*>(name.data()), name.size());
}

#ifdef __WXMSW__
bool DeleteFileByIdentity(const std::filesystem::path& path,
                          const FileIdentity& expected,
                          std::error_code& error) {
    if (!expected.valid) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    HANDLE file = ::CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(file, &info)) {
        const DWORD code = ::GetLastError();
        ::CloseHandle(file);
        error = std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
        ::CloseHandle(file);
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const FileIdentity actual{
        info.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
        true, 0};
    if (!SameFileIdentity(expected, actual)) {
        ::CloseHandle(file);
        error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!::SetFileInformationByHandle(file, FileDispositionInfo,
            &disposition, sizeof(disposition))) {
        const DWORD code = ::GetLastError();
        ::CloseHandle(file);
        error = std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    if (!::CloseHandle(file)) {
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return true;
}

// Create a same-directory temporary file without following or truncating an
// existing path. The old ofstream(path, trunc) sequence was vulnerable to a
// pre-created symlink (or a stale-name collision) redirecting the write.
bool WriteExclusiveTemporary(const std::filesystem::path& target,
                             std::string_view bytes,
                             std::filesystem::path& temporary,
                             FileIdentity& identity,
                             std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporary = target;
        temporary += ".rblstudio-tmp-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
#ifdef __WXMSW__
        HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
            error = std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        bool ok = true;
        if (!::GetFileInformationByHandle(file, &info)) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            ok = false;
        } else {
            identity.first = info.dwVolumeSerialNumber;
            identity.second = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
                              info.nFileIndexLow;
            identity.valid = true;
        }
        std::size_t offset = 0;
        while (ok && offset < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset, static_cast<std::size_t>(MAXDWORD)));
            DWORD written = 0;
            if (!::WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
                error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
                ok = false;
                break;
            }
            offset += written;
        }
        if (ok && !::FlushFileBuffers(file)) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            ok = false;
        }
        if (!::CloseHandle(file) && ok) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            ok = false;
        }
        if (!ok) {
            std::error_code cleanup_error;
            if (!DeleteFileByIdentity(temporary, identity, cleanup_error) && cleanup_error) {
                error = cleanup_error;
            }
            return false;
        }
        error.clear();
        return true;
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int fd = ::open(temporary.c_str(), flags, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            return false;
        }
        std::size_t offset = 0;
        bool ok = true;
        while (offset < bytes.size()) {
            const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                error = std::error_code(written < 0 ? PosixErrorOrIoError() : EIO, std::generic_category());
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (ok && ::fsync(fd) != 0) {
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            ok = false;
        }
        if (ok) {
            struct stat info{};
            if (::fstat(fd, &info) != 0) {
                error = std::error_code(PosixErrorOrIoError(), std::generic_category());
                ok = false;
            } else {
                identity.first = static_cast<std::uint64_t>(info.st_dev);
                identity.second = static_cast<std::uint64_t>(info.st_ino);
                identity.valid = true;
            }
        }
        if (::close(fd) != 0 && ok) {
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            ok = false;
        }
        if (!ok) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        error.clear();
        return true;
#endif
    }
    error = std::make_error_code(std::errc::file_exists);
    return false;
}
#endif

#ifndef __WXMSW__
bool UnlinkLeafIfIdentityAt(int parent_fd, const std::string& leaf,
                            const FileIdentity& expected, std::string& retained_leaf,
                            std::error_code& error);

bool WriteExclusiveTemporaryAt(int parent_fd,
                               const std::string& target_leaf,
                               std::string_view bytes,
                               std::string& temporary_leaf,
                               FileIdentity& identity,
                               std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary_leaf = target_leaf + ".rblstudio-tmp-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
        flags |= O_NONBLOCK;
#endif
        const int fd = ::openat(parent_fd, temporary_leaf.c_str(), flags, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            temporary_leaf.clear();
            return false;
        }
        bool ok = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                error = std::error_code(written < 0 ? PosixErrorOrIoError() : EIO, std::generic_category());
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (ok && ::fsync(fd) != 0) {
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            ok = false;
        }
        struct stat info{};
        const int stat_result = ::fstat(fd, &info);
        const bool have_identity = stat_result == 0 && S_ISREG(info.st_mode);
        if (!have_identity && ok) {
            error = std::error_code(stat_result != 0 ? PosixErrorOrIoError() : EINVAL, std::generic_category());
            ok = false;
        } else if (have_identity) {
            identity = {static_cast<std::uint64_t>(info.st_dev),
                        static_cast<std::uint64_t>(info.st_ino), true,
                        static_cast<std::uint64_t>(info.st_nlink)};
        }
        if (::close(fd) != 0 && ok) {
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            ok = false;
        }
        if (!ok) {
            if (have_identity) {
                const FileIdentity created_identity{
                    static_cast<std::uint64_t>(info.st_dev),
                    static_cast<std::uint64_t>(info.st_ino), true,
                    static_cast<std::uint64_t>(info.st_nlink)};
                std::string retained_leaf;
                std::error_code cleanup_error;
                if (UnlinkLeafIfIdentityAt(parent_fd, temporary_leaf, created_identity,
                                           retained_leaf, cleanup_error)) {
                    temporary_leaf.clear();
                } else if (!retained_leaf.empty()) {
                    temporary_leaf = retained_leaf;
                }
            }
            return false;
        }
        error.clear();
        return true;
    }
    temporary_leaf.clear();
    error = std::make_error_code(std::errc::file_exists);
    return false;
}

bool CreateExclusiveSiblingHardLinkAt(int parent_fd,
                                      const std::string& source_leaf,
                                      const char* suffix,
                                      std::string& link_leaf,
                                      std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        link_leaf = std::string(suffix) +
            std::to_string(static_cast<unsigned long long>(::getpid())) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        if (::linkat(parent_fd, source_leaf.c_str(), parent_fd, link_leaf.c_str(), 0) == 0) {
            error.clear();
            return true;
        }
        if (errno == EEXIST) continue;
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        link_leaf.clear();
        return false;
    }
    error = std::make_error_code(std::errc::file_exists);
    link_leaf.clear();
    return false;
}

bool UnlinkLeafIfIdentityAt(int parent_fd, const std::string& leaf,
                            const FileIdentity& expected, std::string& retained_leaf,
                            std::error_code& error) {
    // First move the name into a freshly-created owner-only directory. The
    // identity check is performed on the quarantined entry, never on the
    // mutable source name. If the source was swapped before renameat, retain
    // the moved object rather than unlinking an unexpected file.
    static std::atomic_uint64_t sequence{0};
    retained_leaf.clear();
    std::string quarantine_directory;
    bool created = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        quarantine_directory = ".rblstudio-quarantine-" +
            std::to_string(static_cast<unsigned long long>(::getpid())) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        if (::mkdirat(parent_fd, quarantine_directory.c_str(), 0700) == 0) {
            created = true;
            break;
        }
        if (errno != EEXIST) {
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            return false;
        }
    }
    if (!created) {
        error = std::make_error_code(std::errc::file_exists);
        return false;
    }
    if (::fsync(parent_fd) != 0) {
        const int code = PosixErrorOrIoError();
        retained_leaf = quarantine_directory;
        error = std::error_code(code, std::generic_category());
        return false;
    }

    int quarantine_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
    quarantine_flags |= O_CLOEXEC;
#endif
    int quarantine_fd = ::openat(parent_fd, quarantine_directory.c_str(), quarantine_flags);
    if (quarantine_fd < 0) {
        const int code = PosixErrorOrIoError();
        retained_leaf = quarantine_directory;
        error = std::error_code(code, std::generic_category());
        return false;
    }
    if (::renameat(parent_fd, leaf.c_str(), quarantine_fd, "entry") != 0) {
        const int code = PosixErrorOrIoError();
        ::close(quarantine_fd);
        const int remove_result = ::unlinkat(parent_fd, quarantine_directory.c_str(), AT_REMOVEDIR);
        if (remove_result != 0 && code != ENOENT) {
            retained_leaf = quarantine_directory;
        } else if (remove_result == 0 && ::fsync(parent_fd) != 0) {
            retained_leaf = quarantine_directory;
            error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            return false;
        }
        if (code == ENOENT && remove_result == 0) { error.clear(); return true; }
        error = std::error_code(code, std::generic_category());
        return false;
    }
    // Persist both sides of the quarantine move before considering deletion.
    const int parent_sync_result = ::fsync(parent_fd);
    const int parent_sync_error = parent_sync_result != 0 ? PosixErrorOrIoError() : 0;
    const int quarantine_sync_result = ::fsync(quarantine_fd);
    const int quarantine_sync_error = quarantine_sync_result != 0 ? PosixErrorOrIoError() : 0;
    if (parent_sync_result != 0 || quarantine_sync_result != 0) {
        ::close(quarantine_fd);
        retained_leaf = quarantine_directory + "/entry";
        error = std::error_code(parent_sync_result != 0
            ? parent_sync_error : quarantine_sync_error, std::generic_category());
        return false;
    }

    struct stat quarantined{};
    const int stat_result = ::fstatat(quarantine_fd, "entry", &quarantined, AT_SYMLINK_NOFOLLOW);
    if (stat_result != 0 || !S_ISREG(quarantined.st_mode) ||
        !SameProjectIdentity(expected, quarantined)) {
        const int code = stat_result != 0 ? PosixErrorOrIoError() : 0;
        ::close(quarantine_fd);
        retained_leaf = quarantine_directory + "/entry";
        error = code != 0 && code != ENOENT
            ? std::error_code(code, std::generic_category())
            : std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    if (::unlinkat(quarantine_fd, "entry", 0) != 0) {
        const int code = PosixErrorOrIoError();
        ::close(quarantine_fd);
        retained_leaf = quarantine_directory + "/entry";
        error = std::error_code(code, std::generic_category());
        return false;
    }
    if (::fsync(quarantine_fd) != 0) {
        const int code = PosixErrorOrIoError();
        ::close(quarantine_fd);
        retained_leaf = quarantine_directory;
        error = std::error_code(code, std::generic_category());
        return false;
    }
    ::close(quarantine_fd);
    if (::unlinkat(parent_fd, quarantine_directory.c_str(), AT_REMOVEDIR) != 0) {
        retained_leaf = quarantine_directory;
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    if (::fsync(parent_fd) != 0) {
        retained_leaf = quarantine_directory;
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    error.clear();
    return true;
}

#endif

#ifdef __WXMSW__
bool QueryFileIdentity(const std::filesystem::path& path, FileIdentity& identity,
                       std::error_code& error) {
    HANDLE file = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = ::GetFileInformationByHandle(file, &info);
    const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
    ::CloseHandle(file);
    if (!ok) {
        error = std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    identity.first = info.dwVolumeSerialNumber;
    identity.second = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
                      info.nFileIndexLow;
    identity.link_count = info.nNumberOfLinks;
    identity.valid = true;
    error.clear();
    return true;
}
#endif

} // namespace

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_New, MainFrame::OnNew)
    EVT_MENU(ID_NewText, MainFrame::OnNewText)
    EVT_MENU(ID_Open, MainFrame::OnOpen)
    EVT_MENU(ID_OpenProject, MainFrame::OnOpenProject)
    EVT_MENU(ID_Save, MainFrame::OnSave)
    EVT_MENU(ID_SaveAs, MainFrame::OnSaveAs)
    EVT_MENU(ID_SaveAll, MainFrame::OnSaveAll)
    EVT_MENU(ID_CloseAll, MainFrame::OnCloseAll)
    EVT_MENU(ID_CloseOtherTabs, MainFrame::OnCloseOtherTabs)
    EVT_MENU(ID_ReopenClosedTab, MainFrame::OnReopenClosedTab)
    EVT_MENU(wxID_EXIT, MainFrame::OnExit)
    EVT_MENU(ID_Run, MainFrame::OnRun)
    EVT_MENU(ID_Check, MainFrame::OnCheck)
    EVT_MENU(ID_Build, MainFrame::OnBuild)
    EVT_MENU(ID_Assembly, MainFrame::OnAssembly)
    EVT_MENU(ID_Tests, MainFrame::OnTests)
    EVT_MENU(ID_Benchmarks, MainFrame::OnBenchmarks)
    EVT_MENU(ID_Doctor, MainFrame::OnDoctor)
    EVT_MENU(ID_Stop, MainFrame::OnStop)
    EVT_MENU(ID_Find, MainFrame::OnFind)
    EVT_MENU(ID_Replace, MainFrame::OnReplace)
    EVT_MENU(ID_FindNextCommand, MainFrame::OnFindNext)
    EVT_MENU(ID_FindPreviousCommand, MainFrame::OnFindPrevious)
    EVT_MENU(ID_CloseTabMenu, MainFrame::OnCloseCurrentMenu)
    EVT_TEXT_ENTER(ID_Stdin, MainFrame::OnSendStdin)
    EVT_BUTTON(ID_SendStdin, MainFrame::OnSendStdin)
    EVT_BUTTON(ID_StdinEof, MainFrame::OnStdinEof)
    EVT_TIMER(ID_WorkerTimer, MainFrame::OnWorkerTimer)
    EVT_TIMER(ID_FoldTimer, MainFrame::OnFoldTimer)
    EVT_TIMER(ID_AutoCheckTimer, MainFrame::OnAutoCheckTimer)
    EVT_END_PROCESS(ID_WorkerProcess, MainFrame::OnWorkerExit)
    EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, MainFrame::OnCloseTab)
    EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, MainFrame::OnPageChanged)
    EVT_TREE_ITEM_EXPANDING(wxID_ANY, MainFrame::OnProjectItemExpanding)
    EVT_TREE_ITEM_COLLAPSED(wxID_ANY, MainFrame::OnProjectItemCollapsed)
    EVT_TREE_ITEM_ACTIVATED(wxID_ANY, MainFrame::OnProjectFileActivated)
    EVT_LIST_ITEM_ACTIVATED(ID_Diagnostics, MainFrame::OnDiagnosticActivated)
    EVT_MENU(ID_Settings, MainFrame::OnSettings)
    EVT_MENU(ID_ToggleProjectPane, MainFrame::OnToggleProjectPane)
    EVT_MENU(ID_ToggleOutputPane, MainFrame::OnToggleOutputPane)
    EVT_MENU(ID_ResetLayout, MainFrame::OnResetLayout)
    EVT_MENU(ID_FindInFiles, MainFrame::OnFindInFiles)
    EVT_MENU(ID_CancelSearch, MainFrame::OnCancelSearch)
    EVT_THREAD(ID_ProjectSearchDone, MainFrame::OnProjectSearchDone)
    EVT_TIMER(ID_SearchResultsTimer, MainFrame::OnSearchResultsTimer)
    EVT_TIMER(ID_DiagnosticsTimer, MainFrame::OnDiagnosticsTimer)
    EVT_LIST_ITEM_ACTIVATED(ID_SearchResults, MainFrame::OnSearchResultActivated)
    EVT_MENU(ID_GoToLine, MainFrame::OnGoToLine)
    EVT_MENU(ID_QuickOpen, MainFrame::OnQuickOpen)
    EVT_MENU(ID_CommandPalette, MainFrame::OnCommandPalette)
    EVT_MENU(ID_QuickReference, MainFrame::OnQuickReference)
    EVT_MENU(ID_About, MainFrame::OnAbout)
    EVT_CLOSE(MainFrame::OnFrameClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "RBL Studio"),
      manager_(this), worker_timer_(this, ID_WorkerTimer), fold_timer_(this, ID_FoldTimer),
      auto_check_timer_(this, ID_AutoCheckTimer),
      search_results_timer_(this, ID_SearchResultsTimer),
      diagnostics_timer_(this, ID_DiagnosticsTimer) {
    wxString configured_root;
    std::optional<std::filesystem::path> explicit_root;
    if (wxGetEnv("RBL_STUDIO_ROOT", &configured_root) && !configured_root.empty()) {
        explicit_root = std::filesystem::path(configured_root.ToStdWstring());
    }
    const auto app_root = rbl::wxui::ResolveApplicationRoot(
        explicit_root,
        std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()));
    if (app_root) {
        application_root_ = app_root.root;
        workspace_root_ = application_root_;
        wxSetEnv("RBL_STUDIO_ROOT", wxString(application_root_.wstring()));
    } else {
        application_root_error_ = app_root.error;
        workspace_root_ = std::filesystem::current_path();
    }
#ifdef __WXMSW__
    wxIconBundle application_icons;
    if (!application_root_.empty()) {
        const auto icon_path = application_root_ / "assets" / "rblstudio.ico";
        application_icons.AddIcon(wxString(icon_path.wstring()), wxBITMAP_TYPE_ICO);
    }
    if (!application_icons.IsEmpty()) SetIcons(application_icons);
    else SetIcon(MakeApplicationIcon(FromDIP(wxSize(32, 32))));
#else
    SetIcon(MakeApplicationIcon(FromDIP(wxSize(32, 32))));
#endif
    wxDisplay initial_display(this);
    wxSize minimum_size = FromDIP(wxSize(900, 600));
    if (initial_display.IsOk()) {
        const wxRect work_area = initial_display.GetClientArea();
        if (!work_area.IsEmpty()) {
            minimum_size.SetWidth(std::min(minimum_size.GetWidth(), work_area.GetWidth()));
            minimum_size.SetHeight(std::min(minimum_size.GetHeight(), work_area.GetHeight()));
        }
    }
    SetMinSize(minimum_size);
    SetSize(FromDIP(wxSize(1440, 900)));
    auto* status = CreateStatusBar(4);
    const int status_widths[] = {FromDIP(165), -1, FromDIP(112), FromDIP(150)};
    status->SetStatusWidths(4, status_widths);
    status->SetBackgroundColour(wxColour(30, 33, 40));
    status->SetForegroundColour(wxColour(205, 211, 220));
    SetStatusText("RBL Studio 0.8.0", 0);
    SetStatusText(PathLabel(workspace_root_), 1);
    SetStatusText("Ready", 2);
    SetStatusText("Ln 1, Col 1", 3);
    BuildMenus();
    BuildLayout();
    Bind(wxEVT_DPI_CHANGED, &MainFrame::OnDpiChanged, this);
    CallAfter(&MainFrame::LoadUiState);
    NewDocument();
    Centre();
    ClampFrameToWorkArea(this, GetSize());
    // Start the redirected worker only after the frame has entered the wx
    // event loop. Starting its polling timer from the constructor can leave
    // the Windows timer without a live owner HWND; the child then runs but its
    // `worker_started`/`ready` frames are never drained until another message
    // happens to wake the frame.
    CallAfter(&MainFrame::StartWorker);
}

MainFrame::~MainFrame() {
    search_cancel_.store(true, std::memory_order_relaxed);
    if (search_thread_.joinable()) search_thread_.join();
    SaveUiState();
    search_results_timer_.Stop();
    diagnostics_timer_.Stop();
    if (worker_) {
        SendWorkerRequest("{\"command\":\"shutdown\"}");
        if (worker_->GetOutputStream()) worker_->GetOutputStream()->Close();
        // Keep draining both redirected pipes while the worker cancels its
        // active child and emits Finished/bye. Stopping the timer before this
        // wait used to fill stdout, trip the worker's backpressure failsafe,
        // and lose the terminal protocol frames during an otherwise graceful
        // close.
        const auto graceful_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (worker_pid_ > 0 && wxProcess::Exists(worker_pid_) &&
               std::chrono::steady_clock::now() < graceful_deadline) {
            PollWorker();
            wxMilliSleep(25);
        }
        PollWorker();
        if (worker_pid_ > 0 && wxProcess::Exists(worker_pid_)) {
            // wxSIGTERM is not a reliable termination signal for a Windows
            // console worker (it has no top-level window for WM_QUIT). Use the
            // hard-kill signal for the bounded fallback on both platforms.
            wxProcess::Kill(worker_pid_, wxSIGKILL, wxKILL_CHILDREN);
            const auto kill_deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(1);
            while (worker_pid_ > 0 && wxProcess::Exists(worker_pid_) &&
                   std::chrono::steady_clock::now() < kill_deadline) {
                PollWorker();
                wxMilliSleep(25);
            }
        }
        worker_timer_.Stop();
        diagnostics_timer_.Stop();
        delete worker_;
        worker_ = nullptr;
    } else {
        worker_timer_.Stop();
    }
    manager_.UnInit();
}

void MainFrame::OpenStartupFile(const wxString& path) {
    if (path.empty()) return;
    CallAfter([this, path] { OpenDocumentPath(path); });
}

void MainFrame::OpenStartupFiles(const wxArrayString& paths) {
    if (paths.empty()) return;
    CallAfter([this, paths] {
        // The constructor creates one empty editor for a no-argument launch.
        // When files were explicitly supplied, reuse that startup slot rather
        // than consuming one of the user's 32 document tabs.
        if (notebook_->GetPageCount() == 1) {
            auto* page = notebook_->GetPage(0);
            const auto found = documents_.find(page);
            auto* editor = dynamic_cast<wxStyledTextCtrl*>(page);
            if (found != documents_.end() && found->second.path.empty() &&
                editor && editor->GetTextLength() == 0) {
                documents_.erase(found);
                notebook_->DeletePage(0);
            }
        }
        for (const auto& path : paths) OpenDocumentPath(path);
    });
}

void MainFrame::LoadUiState() {
    wxString directory;
    if (!wxGetEnv("RBL_STUDIO_CONFIG_DIR", &directory) || directory.empty()) {
        directory = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + "RBLStudio";
    }
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const auto file = directory + wxFILE_SEP_PATH + "ui.ini";
    ui_config_ = std::make_unique<wxFileConfig>("RBLStudio", wxEmptyString, file,
        wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    ui_theme_ = ui_config_->Read("Appearance/Theme", "RBL Dark");
    if (ui_theme_ == "Dark") ui_theme_ = "RBL Dark";
    if (ui_theme_ == "Light") ui_theme_ = "RBL Light";
    if (ui_theme_ != "RBL Dark" && ui_theme_ != "RBL Light" && ui_theme_ != "Nord" &&
        ui_theme_ != "Monokai" && ui_theme_ != "High Contrast") ui_theme_ = "RBL Dark";
    editor_color_scheme_ = ui_config_->Read("Appearance/EditorColorScheme", ui_theme_);
    if (editor_color_scheme_ != "RBL Dark" && editor_color_scheme_ != "RBL Light" &&
        editor_color_scheme_ != "Nord" && editor_color_scheme_ != "Monokai" &&
        editor_color_scheme_ != "High Contrast") editor_color_scheme_ = ui_theme_;
    const auto saved_root = ui_config_->Read("Workspace/Root", wxEmptyString);
    if (!saved_root.empty()) {
        std::error_code root_error;
        const std::filesystem::path candidate(saved_root.ToStdWstring());
        if (std::filesystem::is_directory(candidate, root_error) && !root_error) {
            workspace_root_ = candidate;
            project_->DeleteAllItems();
            const auto root = project_->AddRoot(PathLabel(workspace_root_), 0, 0,
                new ProjectPathData(workspace_root_));
            project_->SetItemBold(root, true);
            project_->SetItemTextColour(root, wxColour(105, 164, 235));
            project_->AppendItem(root, "...", 1, 1);
            // Workspace roots are sent with individual task requests, so
            // restoring a project never requires restarting the worker.
        }
    }
    const auto project_root = project_->GetRootItem();
    if (project_root.IsOk()) {
        PopulateProjectNode(project_root);
        project_->Expand(project_root);
    }
    SetStatusText(PathLabel(workspace_root_), 1);
    const auto saved_width_dip = ui_config_->ReadLong("Window/WidthDIP", -1);
    const auto saved_height_dip = ui_config_->ReadLong("Window/HeightDIP", -1);
    if (saved_width_dip >= 900 && saved_height_dip >= 600) {
        ClampFrameToWorkArea(this, FromDIP(wxSize(
            static_cast<int>(saved_width_dip), static_cast<int>(saved_height_dip))));
    } else if (!ui_config_->HasEntry("Window/WidthDIP") &&
               !ui_config_->HasEntry("Window/HeightDIP")) {
        // Previous releases stored 96-DPI logical dimensions as raw pixels.
        // Preserve those values as DIP on the first DPI-aware launch.
        const auto legacy_width = ui_config_->ReadLong("Window/Width", 1440);
        const auto legacy_height = ui_config_->ReadLong("Window/Height", 900);
        if (legacy_width >= 900 && legacy_height >= 600) {
            ClampFrameToWorkArea(this, FromDIP(wxSize(
                static_cast<int>(legacy_width), static_cast<int>(legacy_height))));
        }
    }
    const auto perspective = ui_config_->Read("AUI/Perspective", wxEmptyString);
    const double saved_aui_scale = ui_config_->ReadDouble("AUI/DpiScale", 1.0);
    const double current_aui_scale = GetDPIScaleFactor();
    const double perspective_scale = saved_aui_scale > 0.0
        ? current_aui_scale / saved_aui_scale : 1.0;
    const auto scaled_perspective = ScaleAuiPerspectiveSizes(
        perspective.ToStdString(), perspective_scale);
    bool perspective_loaded = true;
    if (!perspective.empty()) {
        perspective_loaded = scaled_perspective.has_value() &&
            manager_.LoadPerspective(wxString::FromUTF8(scaled_perspective->c_str()), true);
    }
    if (perspective_loaded && !perspective.empty()) {
        ClampFloatingPanesToDisplays(manager_, this);
        manager_.Update();
    }
    // Perspectives saved before the native toolbar was introduced can hide or
    // omit the new core panes. Recover the complete default workspace once;
    // newer layouts retain the user's own pane visibility choices.
    const auto& editor_pane = manager_.GetPane(notebook_);
    const bool old_layout = !perspective.empty() && perspective.Find("main-toolbar") == wxNOT_FOUND;
    if (!perspective_loaded || old_layout || !editor_pane.IsOk() || !editor_pane.IsShown()) {
        manager_.GetPane("main-toolbar").Show(true);
        manager_.GetPane("project-pane").Show(true);
        manager_.GetPane("editor-pane").Show(true);
        manager_.GetPane("output-pane").Show(true);
        manager_.Update();
    }
    editor_font_size_ = static_cast<int>(ui_config_->ReadLong("Editor/FontSize", 11));
    editor_tab_size_ = static_cast<int>(ui_config_->ReadLong("Editor/TabSize", 4));
    editor_line_numbers_ = ui_config_->ReadBool("Editor/LineNumbers", true);
    editor_soft_wrap_ = ui_config_->ReadBool("Editor/SoftWrap", false);
    editor_indent_guides_ = ui_config_->ReadBool("Editor/IndentGuides", true);
    editor_auto_check_ = ui_config_->ReadBool("Editor/AutoCheck", true);
    editor_auto_save_ = ui_config_->ReadBool("Editor/AutoSave", false);
    editor_syntax_highlighting_ = ui_config_->ReadBool("Editor/SyntaxHighlighting", true);
    compact_mode_ = ui_config_->ReadBool("Appearance/CompactMode", false);
    toolchain_mode_ = ui_config_->Read("Toolchain/Mode", "auto");
    if (toolchain_mode_ != "auto" && toolchain_mode_ != "native" && toolchain_mode_ != "wsl")
        toolchain_mode_ = "auto";
    wsl_distribution_ = ui_config_->Read("Toolchain/WslDistribution", "Ubuntu-24.04");
    if (wsl_distribution_.Trim(true).Trim(false).empty()) wsl_distribution_ = "Ubuntu-24.04";
    if (!ui_config_->ReadBool("Migration/LegacySettingsImported", false)) {
        const auto legacy_path = directory + wxFILE_SEP_PATH + "settings.json";
        std::ifstream legacy_file(std::filesystem::path(legacy_path.ToStdWstring()), std::ios::binary);
        if (legacy_file) {
            const std::string legacy((std::istreambuf_iterator<char>(legacy_file)), std::istreambuf_iterator<char>());
            std::smatch match;
            if (std::regex_search(legacy, match, std::regex(R"("font_size"[[:space:]]*:[[:space:]]*([0-9]+([.][0-9]+)?))"))) {
                const auto value = match[1].str();
                const auto integer_end = value.find('.');
                int parsed = 0;
                const auto result = std::from_chars(value.data(),
                    value.data() + (integer_end == std::string::npos ? value.size() : integer_end), parsed);
                if (result.ec == std::errc{} && result.ptr == value.data() +
                    (integer_end == std::string::npos ? value.size() : integer_end))
                    editor_font_size_ = std::clamp(parsed, 9, 24);
            }
            if (std::regex_search(legacy, match, std::regex(R"("tab_size"[[:space:]]*:[[:space:]]*([0-9]+))"))) {
                const auto value = match[1].str();
                int parsed = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
                    editor_tab_size_ = std::clamp(parsed, 2, 8);
            }
            auto read_bool = [&](const char* key, bool& target) {
                const std::regex expression(std::string("\"") + key + "\"[[:space:]]*:[[:space:]]*(true|false)");
                if (std::regex_search(legacy, match, expression)) target = match[1].str() == "true";
            };
            read_bool("show_line_numbers", editor_line_numbers_);
            read_bool("soft_wrap", editor_soft_wrap_);
            read_bool("indent_guides", editor_indent_guides_);
            read_bool("auto_check", editor_auto_check_);
            read_bool("auto_save", editor_auto_save_);
            read_bool("syntax_highlighting", editor_syntax_highlighting_);
            if (std::regex_search(legacy, match,
                std::regex(R"theme("wsl_mode"[[:space:]]*:[[:space:]]*"(auto|native|wsl)")theme")))
                toolchain_mode_ = wxString::FromUTF8(match[1].str());
            if (std::regex_search(legacy, match,
                std::regex(R"theme("wsl_distro"[[:space:]]*:[[:space:]]*"([^"\\]{1,128})")theme")))
                wsl_distribution_ = wxString::FromUTF8(match[1].str());
            if (std::regex_search(legacy, match,
                std::regex(R"theme("theme"[[:space:]]*:[[:space:]]*"(Dark|Light|Nord|Monokai)")theme"))) {
                const auto legacy_theme = match[1].str();
                ui_theme_ = legacy_theme == "Dark" ? "RBL Dark" :
                    legacy_theme == "Light" ? "RBL Light" : wxString::FromUTF8(legacy_theme);
                if (!ui_config_->HasEntry("Appearance/EditorColorScheme"))
                    editor_color_scheme_ = ui_theme_;
            }
            ui_config_->Write("Migration/LegacySettingsImported", true);
            SaveUiState();
        }
    }
    editor_font_size_ = std::clamp(editor_font_size_, 9, 24);
    editor_tab_size_ = std::clamp(editor_tab_size_, 2, 8);
    for (const auto& [page, document] : documents_) {
        (void)document;
        if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page)) ApplyEditorSettings(editor);
    }
    ApplyUiTheme();
    if (GetMenuBar()) {
        const auto& project_pane = manager_.GetPane("project-pane");
        const auto& output_pane = manager_.GetPane("output-pane");
        if (auto* item = GetMenuBar()->FindItem(ID_ToggleProjectPane)) item->Check(project_pane.IsShown());
        if (auto* item = GetMenuBar()->FindItem(ID_ToggleOutputPane)) item->Check(output_pane.IsShown());
    }
}

void MainFrame::SaveUiState() {
    if (!ui_config_) return;
    ui_config_->Write("AUI/Perspective", manager_.SavePerspective());
    ui_config_->Write("AUI/DpiScale", GetDPIScaleFactor());
    ui_config_->Write("Workspace/Root", wxString(workspace_root_.wstring()));
    ui_config_->Write("Appearance/Theme", ui_theme_);
    ui_config_->Write("Appearance/EditorColorScheme", editor_color_scheme_);
    ui_config_->Write("Appearance/CompactMode", compact_mode_);
    const auto size_dip = ToDIP(GetSize());
    ui_config_->Write("Window/WidthDIP", size_dip.GetWidth());
    ui_config_->Write("Window/HeightDIP", size_dip.GetHeight());
    ui_config_->Write("Editor/FontSize", editor_font_size_);
    ui_config_->Write("Editor/TabSize", editor_tab_size_);
    ui_config_->Write("Editor/LineNumbers", editor_line_numbers_);
    ui_config_->Write("Editor/SoftWrap", editor_soft_wrap_);
    ui_config_->Write("Editor/IndentGuides", editor_indent_guides_);
    ui_config_->Write("Editor/AutoCheck", editor_auto_check_);
    ui_config_->Write("Editor/AutoSave", editor_auto_save_);
    ui_config_->Write("Editor/SyntaxHighlighting", editor_syntax_highlighting_);
    ui_config_->Write("Toolchain/Mode", toolchain_mode_);
    ui_config_->Write("Toolchain/WslDistribution", wsl_distribution_);
    ui_config_->Flush();
}

void MainFrame::OnDpiChanged(wxDPIChangedEvent& event) {
    const wxSize old_dpi = event.GetOldDPI();
    const wxSize new_dpi = event.GetNewDPI();
    double ratio = 1.0;
    if (old_dpi.GetWidth() > 0 && new_dpi.GetWidth() > 0)
        ratio = static_cast<double>(new_dpi.GetWidth()) / old_dpi.GetWidth();
    event.Skip();
    // Let wxWidgets finish its native DPI transition and child-event
    // propagation before touching AUI's internal pane state.
    CallAfter([this, ratio] { ApplyDpiMetrics(ratio); });
}

void MainFrame::ApplyDpiMetrics(double scale_ratio) {
    if (!IsBeingDeleted()) {
        const auto current_perspective = ToUtf8(manager_.SavePerspective());
        const auto perspective = ScaleAuiPerspectiveSizes(
            current_perspective, scale_ratio);
        if (perspective) {
            if (manager_.LoadPerspective(wxString::FromUTF8(perspective->c_str()), true)) {
                ClampFloatingPanesToDisplays(manager_, this);
            }
        }

        if (auto* art = manager_.GetArtProvider()) {
            art->SetMetric(wxAUI_DOCKART_SASH_SIZE, FromDIP(5));
            art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, FromDIP(1));
        }
        RebuildDpiAssets();
#ifndef __WXMSW__
        SetIcon(MakeApplicationIcon(FromDIP(wxSize(32, 32))));
#endif
        notebook_->SetTabCtrlHeight(FromDIP(compact_mode_ ? 26 : 30));
        project_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        project_->SetIndent(FromDIP(compact_mode_ ? 16 : 18));
        project_->SetSpacing(FromDIP(compact_mode_ ? 2 : 4));
        toolbar_->SetMargins(FromDIP(compact_mode_ ? 7 : 10), FromDIP(compact_mode_ ? 2 : 4));
        problems_->SetColumnWidth(0, FromDIP(220));
        problems_->SetColumnWidth(1, FromDIP(65));
        problems_->SetColumnWidth(2, FromDIP(75));
        problems_->SetColumnWidth(3, FromDIP(85));
        problems_->SetColumnWidth(4, FromDIP(420));
        search_results_->SetColumnWidth(0, FromDIP(220));
        search_results_->SetColumnWidth(1, FromDIP(65));
        search_results_->SetColumnWidth(2, FromDIP(75));
        search_results_->SetColumnWidth(3, FromDIP(85));
        search_results_->SetColumnWidth(4, FromDIP(420));
        for (const auto& [page, document] : documents_) {
            (void)document;
            if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page)) {
                editor->SetMarginWidth(0, editor_line_numbers_ ? FromDIP(48) : 0);
                editor->SetMarginWidth(1, FromDIP(16));
            }
        }
        if (auto* status = GetStatusBar()) {
            const int widths[] = {FromDIP(165), -1, FromDIP(112), FromDIP(150)};
            status->SetStatusWidths(4, widths);
        }
        ApplyUiTheme();
        Layout();
        ClampFrameToWorkArea(this, GetSize());
    }
}

void MainFrame::RebuildDpiAssets() {
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    const wxSize project_bitmap_size = FromDIP(wxSize(16, 16));
    if (project_) {
        auto* images = new wxImageList(project_bitmap_size.GetWidth(),
            project_bitmap_size.GetHeight(), true, 4);
        images->Add(MakeProjectGlyph(0, palette, project_bitmap_size));
        images->Add(MakeProjectGlyph(1, palette, project_bitmap_size));
        images->Add(MakeProjectGlyph(2, palette, project_bitmap_size));
        images->Add(MakeProjectGlyph(3, palette, project_bitmap_size));
        project_->AssignImageList(images);
        project_images_ = images;
    }

    const wxSize list_bitmap_size = FromDIP(wxSize(16, 16));
    if (problems_) {
        auto* images = new wxImageList(list_bitmap_size.GetWidth(),
            list_bitmap_size.GetHeight(), true, 2);
        images->Add(wxArtProvider::GetBitmapBundle(
            wxART_ERROR, wxART_OTHER, list_bitmap_size).GetBitmap(list_bitmap_size));
        images->Add(wxArtProvider::GetBitmapBundle(
            wxART_WARNING, wxART_OTHER, list_bitmap_size).GetBitmap(list_bitmap_size));
        problems_->AssignImageList(images, wxIMAGE_LIST_SMALL);
    }

    if (toolbar_) {
        const wxSize toolbar_bitmap_size = FromDIP(wxSize(
            compact_mode_ ? 16 : 18, compact_mode_ ? 16 : 18));
        toolbar_->SetToolBitmapSize(toolbar_bitmap_size);
        toolbar_->SetToolBitmap(ID_New, wxArtProvider::GetBitmapBundle(wxART_NEW, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Open, wxArtProvider::GetBitmapBundle(wxART_FILE_OPEN, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Save, wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Run, wxArtProvider::GetBitmapBundle(wxART_GO_FORWARD, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Stop, wxArtProvider::GetBitmapBundle(wxART_STOP, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Check, wxArtProvider::GetBitmapBundle(wxART_TICK_MARK, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Build, wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Assembly, wxArtProvider::GetBitmapBundle(wxART_REPORT_VIEW, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Tests, wxArtProvider::GetBitmapBundle(wxART_LIST_VIEW, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_Benchmarks, wxArtProvider::GetBitmapBundle(wxART_TIP, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_FindInFiles, wxArtProvider::GetBitmapBundle(wxART_FIND, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_QuickOpen, wxArtProvider::GetBitmapBundle(wxART_FOLDER_OPEN, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->SetToolBitmap(ID_CommandPalette, wxArtProvider::GetBitmapBundle(wxART_HELP, wxART_TOOLBAR, toolbar_bitmap_size));
        toolbar_->Realize();
    }
}

void MainFrame::BuildMenus() {
    auto* file = new wxMenu();
    file->Append(ID_New, "&New\tCtrl+N");
    file->Append(ID_NewText, "New &Text File");
    file->Append(ID_Open, "&Open...\tCtrl+O");
    file->AppendSeparator();
    file->Append(ID_Save, "&Save\tCtrl+S");
    file->Append(ID_SaveAs, "Save &As...");
    file->Append(ID_SaveAll, "Save A&ll\tCtrl+Shift+S");
    file->Append(ID_ReopenClosedTab, "Reopen Closed &Tab");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto* menu_bar = new wxMenuBar();
    menu_bar->Append(file, "&File");
    auto* project = new wxMenu();
    project->Append(ID_OpenProject, "Open &Folder...\tCtrl+Shift+O");
    menu_bar->Append(project, "&Project");
    auto* edit = new wxMenu();
    edit->Append(ID_Find, "&Find...\tCtrl+F");
    edit->Append(ID_Replace, "&Replace...\tCtrl+H");
    edit->Append(ID_FindNextCommand, "Find &Next\tF3");
    edit->Append(ID_FindPreviousCommand, "Find Pre&vious\tShift+F3");
    edit->Append(ID_FindInFiles, "Find in &Files...\tCtrl+Shift+F");
    edit->Append(ID_CancelSearch, "Cancel Find in Files");
    edit->Append(ID_GoToLine, "Go to &Line...\tCtrl+G");
    edit->Append(ID_QuickOpen, "Quick &Open...\tCtrl+P");
    edit->Append(ID_CloseTabMenu, "Close &Tab\tCtrl+W");
    edit->Append(ID_CloseOtherTabs, "Close Other Tabs");
    edit->Append(ID_CloseAll, "Close All Tabs");
    menu_bar->Append(edit, "&Edit");
    auto* run = new wxMenu();
    run->Append(ID_Run, "&Run\tF5");
    run->Append(ID_Check, "&Check\tF7");
    run->Append(ID_Build, "&Build\tCtrl+B");
    run->Append(ID_Assembly, "Show generated &ASM");
    run->AppendSeparator();
    run->Append(ID_Tests, "Run &Tests\tCtrl+T");
    run->Append(ID_Benchmarks, "Run &Benchmarks\tCtrl+Shift+T");
    run->Append(ID_Doctor, "Toolchain &Doctor");
    run->Append(ID_Stop, "S&top\tShift+F5");
    menu_bar->Append(run, "&Run");
    auto* view = new wxMenu();
    view->AppendCheckItem(ID_ToggleProjectPane, "Project Tool Window");
    view->AppendCheckItem(ID_ToggleOutputPane, "Run / Output Tool Window");
    view->AppendSeparator();
    view->Append(ID_ResetLayout, "Restore Default Layout");
    menu_bar->Append(view, "&View");
    auto* settings = new wxMenu();
    settings->Append(ID_Settings, "&Settings...");
    menu_bar->Append(settings, "&Settings");
    auto* help = new wxMenu();
    help->Append(ID_CommandPalette, "Command &Palette...\tCtrl+Shift+P");
    help->Append(ID_QuickReference, "RBL Quick &Reference\tF1");
    help->AppendSeparator();
    help->Append(ID_About, "&About RBL Studio");
    menu_bar->Append(help, "&Help");
    SetMenuBar(menu_bar);
}

void MainFrame::BuildLayout() {
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    const auto& background = palette.window;
    const auto& panel = palette.surface;
    const auto& panel_alt = palette.surface_alt;
    const auto& foreground = palette.text;
    const auto& muted = palette.muted;
    const auto& accent = palette.accent;
    const auto& border = palette.border;
    SetBackgroundColour(background);
    SetForegroundColour(foreground);

    manager_.SetArtProvider(new FlatDockArt(palette));
    auto* dock_art = manager_.GetArtProvider();
    dock_art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, background);
    dock_art->SetColour(wxAUI_DOCKART_SASH_COLOUR, border);
    dock_art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, border);
    dock_art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, panel_alt);
    dock_art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, panel);
    dock_art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, foreground);
    dock_art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, muted);
    dock_art->SetMetric(wxAUI_DOCKART_SASH_SIZE, FromDIP(4));
    dock_art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
    dock_art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);

    notebook_ = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE |
        wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_CLOSE_ON_ALL_TABS |
        wxAUI_NB_MIDDLE_CLICK_CLOSE | wxBORDER_NONE);
    notebook_->SetArtProvider(new ThemeTabArt(palette, compact_mode_));
    notebook_->SetTabCtrlHeight(FromDIP(compact_mode_ ? 26 : 30));
    notebook_->SetBackgroundColour(panel);
    notebook_->SetForegroundColour(foreground);

    project_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxBORDER_NONE);
    project_->SetBackgroundColour(panel);
    project_->SetForegroundColour(foreground);
    project_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    project_->SetIndent(FromDIP(18));
    project_->SetSpacing(FromDIP(4));
    const auto root_path = workspace_root_;
    const auto root_item = project_->AddRoot(PathLabel(root_path), 0, 0,
        new ProjectPathData(root_path));
    project_->SetItemBold(root_item, true);
    project_->SetItemTextColour(root_item, accent);
    project_->AppendItem(root_item, "...", 1, 1);

    toolbar_ = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_TEXT | wxAUI_TB_OVERFLOW);
    const wxSize toolbar_bitmap_size = FromDIP(wxSize(18, 18));
    toolbar_->SetToolBitmapSize(toolbar_bitmap_size);
    toolbar_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    toolbar_->SetBackgroundColour(panel);
    toolbar_->SetForegroundColour(foreground);
    toolbar_->SetArtProvider(new DarkToolBarArt(palette));
    toolbar_->SetMargins(FromDIP(10), FromDIP(4));
    const auto add_tool = [this, toolbar_bitmap_size](int id, const wxString& label, const wxArtID& art,
                                 const wxString& help) {
        toolbar_->AddTool(id, label,
            wxArtProvider::GetBitmapBundle(art, wxART_TOOLBAR, toolbar_bitmap_size), help);
    };
    add_tool(ID_New, "New", wxART_NEW, "New file (Ctrl+N)");
    add_tool(ID_Open, "Open", wxART_FILE_OPEN, "Open file (Ctrl+O)");
    add_tool(ID_Save, "Save", wxART_FILE_SAVE, "Save current file (Ctrl+S)");
    toolbar_->AddSeparator();
    add_tool(ID_Run, "Run", wxART_GO_FORWARD, "Run current RBL file (F5)");
    add_tool(ID_Stop, "Stop", wxART_STOP, "Stop active task");
    add_tool(ID_Check, "Check", wxART_TICK_MARK, "Check RBL syntax");
    add_tool(ID_Build, "Build", wxART_EXECUTABLE_FILE, "Build current RBL file");
    toolbar_->AddSeparator();
    add_tool(ID_Assembly, "ASM", wxART_REPORT_VIEW, "View generated assembly");
    add_tool(ID_Tests, "Tests", wxART_LIST_VIEW, "Run RBL test suite");
    add_tool(ID_Benchmarks, "Bench", wxART_TIP, "Run benchmarks");
    toolbar_->AddSeparator();
    add_tool(ID_FindInFiles, "Search", wxART_FIND, "Find in project files");
    add_tool(ID_QuickOpen, "Quick Open", wxART_FOLDER_OPEN, "Quickly open a project file");
    add_tool(ID_CommandPalette, "Commands", wxART_HELP, "Open command palette");
    toolbar_->AddStretchSpacer();
        toolbar_->AddLabel(wxID_ANY, "RBL 0.8.0", FromDIP(88));
    toolbar_->Realize();

    auto* output_panel = new wxPanel(this);
    output_panel->SetBackgroundColour(panel);
    auto* output_sizer = new wxBoxSizer(wxVERTICAL);
    output_tabs_ = new wxAuiNotebook(output_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxAUI_NB_TOP | wxAUI_NB_SCROLL_BUTTONS | wxBORDER_NONE);
    output_tabs_->SetArtProvider(new ThemeTabArt(palette, compact_mode_));
    output_tabs_->SetTabCtrlHeight(FromDIP(compact_mode_ ? 24 : 26));
    output_tabs_->SetBackgroundColour(panel);
    output_tabs_->SetForegroundColour(foreground);
    output_tabs_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    auto* console_panel = new wxPanel(output_tabs_);
    console_panel->SetBackgroundColour(panel);
    auto* console_sizer = new wxBoxSizer(wxVERTICAL);
    output_ = new wxTextCtrl(console_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL | wxBORDER_NONE);
    output_->SetBackgroundColour(palette.output);
    output_->SetForegroundColour(foreground);
    output_->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    output_->SetDefaultStyle(wxTextAttr(foreground, palette.output,
        wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    auto* input_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_ = new wxTextCtrl(console_panel, ID_Stdin, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
    input_->SetHint("Program input - type a line and press Enter");
    input_->SetBackgroundColour(panel_alt);
    input_->SetForegroundColour(foreground);
    input_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    send_button_ = new wxButton(console_panel, ID_SendStdin, "Send");
    eof_button_ = new wxButton(console_panel, ID_StdinEof, "EOF");
    send_button_->SetBackgroundColour(palette.accent);
    send_button_->SetForegroundColour(*wxWHITE);
    eof_button_->SetBackgroundColour(panel_alt);
    eof_button_->SetForegroundColour(foreground);
    input_->Disable();
    send_button_->Disable();
    eof_button_->Disable();
    input_sizer->Add(input_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    input_sizer->Add(send_button_, 0, wxEXPAND | wxRIGHT, FromDIP(4));
    input_sizer->Add(eof_button_, 0, wxEXPAND);
    console_sizer->Add(output_, 1, wxEXPAND | wxALL, FromDIP(4));
    console_sizer->Add(input_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
    console_panel->SetSizer(console_sizer);
    problems_ = new wxListCtrl(output_tabs_, ID_Diagnostics, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    problems_->SetBackgroundColour(palette.output);
    problems_->SetTextColour(foreground);
    problems_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    problems_->InsertColumn(0, "File", wxLIST_FORMAT_LEFT, FromDIP(220));
    problems_->InsertColumn(1, "Line", wxLIST_FORMAT_RIGHT, FromDIP(65));
    problems_->InsertColumn(2, "Column", wxLIST_FORMAT_RIGHT, FromDIP(75));
    problems_->InsertColumn(3, "Severity", wxLIST_FORMAT_LEFT, FromDIP(85));
    problems_->InsertColumn(4, "Message", wxLIST_FORMAT_LEFT, FromDIP(420));
    output_tabs_->AddPage(console_panel, "Console", true);
    output_tabs_->AddPage(problems_, "Problems", false);
    search_results_ = new wxListCtrl(output_tabs_, ID_SearchResults, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    search_results_->SetBackgroundColour(palette.output);
    search_results_->SetTextColour(foreground);
    search_results_->SetFont(wxFontInfo(9).FaceName("Segoe UI"));
    search_results_->InsertColumn(0, "File", wxLIST_FORMAT_LEFT, FromDIP(220));
    search_results_->InsertColumn(1, "Line", wxLIST_FORMAT_RIGHT, FromDIP(65));
    search_results_->InsertColumn(2, "Column", wxLIST_FORMAT_RIGHT, FromDIP(75));
    search_results_->InsertColumn(3, "Type", wxLIST_FORMAT_LEFT, FromDIP(85));
    search_results_->InsertColumn(4, "Match", wxLIST_FORMAT_LEFT, FromDIP(420));
    const auto fit_result_column = [this](wxListCtrl* list) {
        if (!list || list->GetColumnCount() < 5) return;
        int fixed_width = 0;
        for (int column = 0; column < 4; ++column) {
            wxListItem info;
            info.SetMask(wxLIST_MASK_WIDTH);
            if (list->GetColumn(column, info)) fixed_width += info.GetWidth();
        }
        const int available = list->GetClientSize().GetWidth();
        list->SetColumnWidth(4, std::max(FromDIP(180), available - fixed_width - FromDIP(8)));
    };
    problems_->Bind(wxEVT_SIZE, [fit_result_column, list = problems_](wxSizeEvent& event) {
        event.Skip();
        fit_result_column(list);
    });
    search_results_->Bind(wxEVT_SIZE, [fit_result_column, list = search_results_](wxSizeEvent& event) {
        event.Skip();
        fit_result_column(list);
    });
    output_tabs_->AddPage(search_results_, "Search", false);
    output_sizer->Add(output_tabs_, 1, wxEXPAND | wxALL, FromDIP(2));
    output_panel->SetSizer(output_sizer);

    find_panel_ = new wxPanel(this, wxID_ANY);
    find_panel_->SetBackgroundColour(panel);
    auto* find_layout = new wxBoxSizer(wxVERTICAL);
    auto* find_row = new wxBoxSizer(wxHORIZONTAL);
    find_row->Add(new wxStaticText(find_panel_, wxID_ANY, "Find"), 0,
        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    find_query_ = new wxTextCtrl(find_panel_, wxID_ANY, wxEmptyString,
        wxDefaultPosition, FromDIP(wxSize(180, -1)), wxTE_PROCESS_ENTER);
    find_query_->SetHint("Search in current file");
    find_row->Add(find_query_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    auto* previous_button = new wxButton(find_panel_, ID_FindPrevious, "Previous");
    auto* next_button = new wxButton(find_panel_, ID_FindNext, "Next");
    find_row->Add(previous_button, 0, wxRIGHT, FromDIP(4));
    find_row->Add(next_button, 0, wxRIGHT, FromDIP(10));
    find_status_ = new wxStaticText(find_panel_, wxID_ANY, wxEmptyString);
    find_row->Add(find_status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    auto* close_button = new wxButton(find_panel_, ID_FindClose, "Hide");
    close_button->SetToolTip("Close Find / Replace");
    find_row->Add(close_button, 0, wxALIGN_CENTER_VERTICAL);
    find_layout->Add(find_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    auto* replace_row = new wxBoxSizer(wxHORIZONTAL);
    replace_row->Add(new wxStaticText(find_panel_, wxID_ANY, "Replace"), 0,
        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    replace_query_ = new wxTextCtrl(find_panel_, wxID_ANY, wxEmptyString,
        wxDefaultPosition, FromDIP(wxSize(180, -1)), wxTE_PROCESS_ENTER);
    replace_query_->SetHint("Replace with");
    replace_row->Add(replace_query_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    auto* replace_one_button = new wxButton(find_panel_, ID_ReplaceOne, "Replace");
    auto* replace_all_button = new wxButton(find_panel_, ID_ReplaceAll, "Replace All");
    replace_row->Add(replace_one_button, 0, wxRIGHT, FromDIP(4));
    replace_row->Add(replace_all_button, 0, wxRIGHT, FromDIP(10));
    find_match_case_ = new wxCheckBox(find_panel_, wxID_ANY, "Match case");
    replace_row->Add(find_match_case_, 0, wxALIGN_CENTER_VERTICAL);
    find_layout->Add(replace_row, 0,
        wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(8));
    find_panel_->SetSizer(find_layout);

    find_query_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { FindInCurrentEditor(false); });
    replace_query_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ReplaceInCurrentEditor(false); });
    find_query_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        find_status_->SetLabel(wxEmptyString);
        event.Skip();
    });
    previous_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindInCurrentEditor(true); });
    next_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindInCurrentEditor(false); });
    replace_one_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceInCurrentEditor(false); });
    replace_all_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceInCurrentEditor(true); });
    close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        manager_.GetPane("find-pane").Hide();
        manager_.Update();
        if (auto* editor = CurrentEditor()) editor->SetFocus();
    });

    manager_.AddPane(toolbar_, wxAuiPaneInfo().Name("main-toolbar").ToolbarPane()
        .Top().Row(0).Position(0).Gripper(false).DockFixed(true)
        .CloseButton(false).MaximizeButton(false));
    manager_.AddPane(project_, wxAuiPaneInfo().Name("project-pane").Left().Caption("PROJECT")
        .MinSize(FromDIP(wxSize(190, 180))).BestSize(FromDIP(wxSize(260, 500)))
        .CloseButton(false).MaximizeButton(true));
    manager_.AddPane(notebook_, wxAuiPaneInfo().Name("editor-pane").CenterPane().PaneBorder(false));
    manager_.AddPane(output_panel, wxAuiPaneInfo().Name("output-pane").Bottom().Caption("RUN / OUTPUT")
        .MinSize(FromDIP(wxSize(300, 100))).BestSize(FromDIP(wxSize(-1, 190)))
        .CloseButton(true).MaximizeButton(true));
    manager_.AddPane(find_panel_, wxAuiPaneInfo().Name("find-pane").Bottom().Row(1).Caption("FIND / REPLACE")
        .MinSize(FromDIP(wxSize(650, 76))).BestSize(FromDIP(wxSize(-1, 112)))
        .Hide().CloseButton(true).MaximizeButton(false));
    manager_.Update();
    default_layout_ = manager_.SavePerspective();
    RebuildDpiAssets();
}

void MainFrame::ConfigureEditor(wxStyledTextCtrl* editor) {
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    const auto scheme = rbl::wxui::ColorSchemeForName(editor_color_scheme_);
    editor->SetLexer(wxSTC_LEX_CONTAINER);
    editor->SetCodePage(wxSTC_CP_UTF8);
    editor->SetTabWidth(editor_tab_size_);
    editor->SetIndent(editor_tab_size_);
    editor->SetUseTabs(false);
    editor->SetTabIndents(true);
    editor->SetBackSpaceUnIndents(true);
    editor->SetIndentationGuides(editor_indent_guides_ ? wxSTC_IV_LOOKBOTH : wxSTC_IV_NONE);
    editor->SetViewEOL(false);
    editor->SetViewWhiteSpace(wxSTC_WS_INVISIBLE);
    editor->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    editor->SetMarginWidth(0, editor_line_numbers_ ? FromDIP(48) : 0);
    editor->SetMarginType(1, wxSTC_MARGIN_SYMBOL);
    editor->SetMarginMask(1, wxSTC_MASK_FOLDERS);
    editor->SetMarginWidth(1, FromDIP(16));
    editor->SetMarginSensitive(1, true);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_VLINE, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_LCORNER, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUS, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUS, palette.muted, palette.surface_alt);
    editor->MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER, palette.muted, palette.surface_alt);
    editor->SetProperty("fold", "1");
    editor->SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED |
                         wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);
    editor->SetFoldMarginColour(true, palette.editor);
    editor->SetFoldMarginHiColour(true, palette.editor);
    editor->SetCaretLineVisible(true);
    editor->SetCaretLineBackground(palette.caret_line);
    editor->StyleSetFont(wxSTC_STYLE_DEFAULT, wxFontInfo(editor_font_size_).Family(wxFONTFAMILY_TELETYPE));
    editor->StyleSetForeground(wxSTC_STYLE_DEFAULT, palette.text);
    editor->StyleSetBackground(wxSTC_STYLE_DEFAULT, palette.editor);
    editor->StyleClearAll();
    editor->StyleSetForeground(wxSTC_STYLE_LINENUMBER, palette.muted);
    editor->StyleSetBackground(wxSTC_STYLE_LINENUMBER, palette.editor);
    editor->StyleSetFont(wxSTC_STYLE_LINENUMBER,
        wxFontInfo(std::max(8, editor_font_size_ - 1)).Family(wxFONTFAMILY_TELETYPE));
    editor->StyleSetForeground(RblKeyword, scheme.keyword);
    editor->StyleSetBold(RblKeyword, true);
    editor->StyleSetForeground(RblString, scheme.string);
    editor->StyleSetForeground(RblComment, scheme.comment);
    editor->StyleSetItalic(RblComment, true);
    editor->StyleSetForeground(RblNumber, scheme.number);
    editor->StyleSetForeground(RblFunction, scheme.function);
    editor->StyleSetForeground(RblBoolean, scheme.boolean);
    editor->StyleSetBold(RblBoolean, true);
    editor->StyleSetForeground(RblOperator, scheme.op);
    editor->SetSelForeground(true, palette.selection_text);
    editor->SetSelBackground(true, palette.selection);
    editor->SetCaretForeground(palette.text);
    editor->SetWhitespaceForeground(true, palette.muted);
    editor->SetFoldMarginColour(true, palette.editor);
    editor->SetFoldMarginHiColour(true, palette.editor);
    editor->SetScrollWidthTracking(true);
    editor->SetScrollWidth(1);
    editor->SetUndoCollection(true);
    editor->AutoCompSetIgnoreCase(false);
    editor->AutoCompSetAutoHide(true);
    editor->AutoCompSetChooseSingle(false);
    editor->AutoCompSetMaxHeight(12);
    editor->AutoCompSetFillUps(" (");
    editor->SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT |
                            wxSTC_PERFORMED_USER | wxSTC_PERFORMED_UNDO |
                            wxSTC_PERFORMED_REDO);
    editor->Bind(wxEVT_STC_STYLENEEDED, &MainFrame::OnEditorStyleNeeded, this);
    editor->Bind(wxEVT_STC_MODIFIED, &MainFrame::OnEditorModified, this);
    editor->Bind(wxEVT_STC_SAVEPOINTLEFT, &MainFrame::OnEditorSavePoint, this);
    editor->Bind(wxEVT_STC_SAVEPOINTREACHED, &MainFrame::OnEditorSavePoint, this);
    editor->Bind(wxEVT_STC_UPDATEUI, &MainFrame::OnEditorUpdateUi, this);
    editor->Bind(wxEVT_STC_CHARADDED, &MainFrame::OnEditorCharAdded, this);
    editor->SetWrapMode(editor_soft_wrap_ ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
}

void MainFrame::ApplyEditorSettings(wxStyledTextCtrl* editor) {
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    const auto scheme = rbl::wxui::ColorSchemeForName(editor_color_scheme_);
    editor->SetTabWidth(editor_tab_size_);
    editor->SetIndent(editor_tab_size_);
    editor->SetMarginWidth(0, editor_line_numbers_ ? FromDIP(48) : 0);
    editor->SetIndentationGuides(editor_indent_guides_ ? wxSTC_IV_LOOKBOTH : wxSTC_IV_NONE);
    editor->SetWrapMode(editor_soft_wrap_ ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
    editor->StyleSetFont(wxSTC_STYLE_DEFAULT, wxFontInfo(editor_font_size_).Family(wxFONTFAMILY_TELETYPE));
    for (int style = 0; style <= RblOperator; ++style) {
        editor->StyleSetFont(style, wxFontInfo(editor_font_size_).Family(wxFONTFAMILY_TELETYPE));
    }
    editor->StyleSetFont(wxSTC_STYLE_LINENUMBER,
        wxFontInfo(std::max(8, editor_font_size_ - 1)).Family(wxFONTFAMILY_TELETYPE));
    editor->StyleSetForeground(wxSTC_STYLE_DEFAULT, palette.text);
    editor->StyleSetBackground(wxSTC_STYLE_DEFAULT, palette.editor);
    editor->StyleClearAll();
    editor->StyleSetForeground(wxSTC_STYLE_LINENUMBER, palette.muted);
    editor->StyleSetBackground(wxSTC_STYLE_LINENUMBER, palette.editor);
    if (editor_syntax_highlighting_) {
        editor->StyleSetForeground(RblKeyword, scheme.keyword);
        editor->StyleSetBold(RblKeyword, true);
        editor->StyleSetForeground(RblString, scheme.string);
        editor->StyleSetForeground(RblComment, scheme.comment);
        editor->StyleSetItalic(RblComment, true);
        editor->StyleSetForeground(RblNumber, scheme.number);
        editor->StyleSetForeground(RblFunction, scheme.function);
        editor->StyleSetForeground(RblBoolean, scheme.boolean);
        editor->StyleSetBold(RblBoolean, true);
        editor->StyleSetForeground(RblOperator, scheme.op);
    } else {
        for (int style = RblKeyword; style <= RblOperator; ++style) {
            editor->StyleSetForeground(style, palette.text);
            editor->StyleSetBold(style, false);
            editor->StyleSetItalic(style, false);
        }
    }
    editor->SetCaretLineBackground(palette.caret_line);
    editor->SetCaretForeground(palette.text);
    editor->SetSelForeground(true, palette.selection_text);
    editor->SetSelBackground(true, palette.selection);
    editor->SetFoldMarginColour(true, palette.editor);
    editor->SetFoldMarginHiColour(true, palette.editor);
    editor->SetWhitespaceForeground(true, palette.muted);
    editor->StyleSetFont(wxSTC_STYLE_DEFAULT,
        wxFontInfo(editor_font_size_).Family(wxFONTFAMILY_TELETYPE));
    editor->StyleSetFont(wxSTC_STYLE_LINENUMBER,
        wxFontInfo(std::max(8, editor_font_size_ - 1)).Family(wxFONTFAMILY_TELETYPE));
    for (int style = 0; style <= RblOperator; ++style)
        editor->StyleSetFont(style, wxFontInfo(editor_font_size_).Family(wxFONTFAMILY_TELETYPE));
}

void MainFrame::ApplyUiTheme() {
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    SetBackgroundColour(palette.window);
    SetForegroundColour(palette.text);
    if (auto* menu_bar = GetMenuBar()) {
        menu_bar->SetBackgroundColour(palette.surface);
        menu_bar->SetForegroundColour(palette.text);
        menu_bar->Refresh();
    }
    if (auto* status = GetStatusBar()) {
        status->SetBackgroundColour(palette.surface);
        status->SetForegroundColour(palette.text);
        status->Refresh();
    }
    manager_.SetArtProvider(new FlatDockArt(palette));
    if (auto* art = manager_.GetArtProvider()) {
        art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, palette.window);
        art->SetColour(wxAUI_DOCKART_SASH_COLOUR, palette.border);
        art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, palette.border);
        art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, palette.surface_alt);
        art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, palette.surface);
        art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, palette.text);
        art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, palette.muted);
        art->SetMetric(wxAUI_DOCKART_SASH_SIZE, FromDIP(4));
        art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
        art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
    }
    if (notebook_) {
        notebook_->SetBackgroundColour(palette.surface);
        notebook_->SetForegroundColour(palette.text);
        notebook_->SetArtProvider(new ThemeTabArt(palette, compact_mode_));
        notebook_->SetTabCtrlHeight(FromDIP(compact_mode_ ? 26 : 30));
    }
    if (project_) {
        project_->SetBackgroundColour(palette.surface);
        project_->SetForegroundColour(palette.text);
        project_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        project_->SetIndent(FromDIP(compact_mode_ ? 16 : 18));
        project_->SetSpacing(FromDIP(compact_mode_ ? 2 : 4));
        const auto root = project_->GetRootItem();
        if (root.IsOk()) project_->SetItemTextColour(root, palette.accent);
        project_->Refresh();
    }
    if (toolbar_) {
        toolbar_->SetBackgroundColour(palette.surface);
        toolbar_->SetForegroundColour(palette.text);
        toolbar_->SetArtProvider(new DarkToolBarArt(palette, compact_mode_));
        toolbar_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        toolbar_->SetMargins(FromDIP(compact_mode_ ? 7 : 10), FromDIP(compact_mode_ ? 2 : 4));
        const auto toolbar_size = FromDIP(wxSize(compact_mode_ ? 16 : 18, compact_mode_ ? 16 : 18));
        toolbar_->SetToolBitmapSize(toolbar_size);
        toolbar_->Realize();
    }
    if (output_tabs_) {
        output_tabs_->SetBackgroundColour(palette.surface);
        output_tabs_->SetForegroundColour(palette.text);
        output_tabs_->SetArtProvider(new ThemeTabArt(palette, compact_mode_));
        output_tabs_->SetTabCtrlHeight(FromDIP(compact_mode_ ? 24 : 26));
        output_tabs_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
    }
    if (output_) {
        output_->SetBackgroundColour(palette.output);
        output_->SetForegroundColour(palette.text);
        output_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).Family(wxFONTFAMILY_TELETYPE));
        output_->SetDefaultStyle(wxTextAttr(palette.text, palette.output,
            wxFontInfo(compact_mode_ ? 8 : 9).Family(wxFONTFAMILY_TELETYPE)));
        if (output_->GetLastPosition() > 0)
            output_->SetStyle(0, output_->GetLastPosition(), wxTextAttr(palette.text, palette.output));
    }
    if (input_) {
        input_->SetBackgroundColour(palette.surface_alt);
        input_->SetForegroundColour(palette.text);
        input_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
    }
    if (send_button_) {
        send_button_->SetBackgroundColour(palette.accent);
        send_button_->SetForegroundColour(palette.accent_text);
    }
    if (eof_button_) {
        eof_button_->SetBackgroundColour(palette.surface_alt);
        eof_button_->SetForegroundColour(palette.text);
    }
    if (problems_) {
        problems_->SetBackgroundColour(palette.output);
        problems_->SetTextColour(palette.text);
        problems_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        for (long row = 0; row < problems_->GetItemCount(); ++row) {
            const bool is_error = problems_->GetItemText(row, 3).CmpNoCase("error") == 0;
            problems_->SetItemTextColour(row, is_error ? palette.error : palette.warning);
        }
        problems_->Refresh();
    }
    if (search_results_) {
        search_results_->SetBackgroundColour(palette.output);
        search_results_->SetTextColour(palette.text);
        search_results_->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        search_results_->Refresh();
    }
    if (find_panel_) {
        find_panel_->SetBackgroundColour(palette.surface);
        for (auto* child : find_panel_->GetChildren()) {
            child->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
            if (auto* text = dynamic_cast<wxStaticText*>(child))
                text->SetForegroundColour(palette.text);
            else if (auto* input = dynamic_cast<wxTextCtrl*>(child)) {
                input->SetBackgroundColour(palette.surface_alt);
                input->SetForegroundColour(palette.text);
            } else if (auto* button = dynamic_cast<wxButton*>(child)) {
                button->SetBackgroundColour(palette.surface_alt);
                button->SetForegroundColour(palette.text);
            } else if (auto* check = dynamic_cast<wxCheckBox*>(child)) {
                check->SetForegroundColour(palette.text);
            }
        }
        find_panel_->Refresh();
    }
    for (const auto& [page, document] : documents_) {
        (void)document;
        if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page)) ApplyEditorSettings(editor);
    }
    RebuildDpiAssets();
    manager_.GetPane("project-pane").MinSize(FromDIP(wxSize(compact_mode_ ? 160 : 190,
        compact_mode_ ? 150 : 180)));
    manager_.GetPane("output-pane").MinSize(FromDIP(wxSize(compact_mode_ ? 260 : 300, 100)));
    manager_.Update();
    Refresh();
    UpdateToolbarState();
}

void MainFrame::UpdateToolbarState() {
    if (!toolbar_) return;
    auto* editor = CurrentEditor();
    const auto found = editor ? documents_.find(editor) : documents_.end();
    const bool has_document = found != documents_.end();
    const bool is_rbl = has_document && found->second.is_rbl;
    const bool can_start = worker_ready_ && !task_running_ && has_document;
    const bool can_compile = can_start && is_rbl;
    toolbar_->EnableTool(ID_Run, can_compile);
    toolbar_->EnableTool(ID_Check, can_compile);
    toolbar_->EnableTool(ID_Build, can_compile);
    toolbar_->EnableTool(ID_Assembly, can_compile);
    toolbar_->EnableTool(ID_Tests, can_start);
    toolbar_->EnableTool(ID_Benchmarks, can_start);
    toolbar_->EnableTool(ID_Doctor, can_start);
    toolbar_->EnableTool(ID_Stop, task_running_);
}

void MainFrame::UpdateDocumentLimits() {
    std::size_t total = 0;
    for (const auto& [page, document] : documents_) {
        (void)document;
        if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page))
            total += static_cast<std::size_t>(editor->GetTextLength());
    }
    for (const auto& [page, document] : documents_) {
        (void)document;
        if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page)) {
            const auto current = static_cast<std::size_t>(editor->GetTextLength());
            const auto other = total - current;
            const auto available = MaxOpenDocumentBytes > other ? MaxOpenDocumentBytes - other : current;
            const auto limit = std::min(MaxEditableFileBytes, std::max(current, available));
            editor->SetMaxLength(static_cast<int>(limit));
        }
    }
}

void MainFrame::StyleRbl(wxStyledTextCtrl* editor, int requested_end) {
    // GetTextRaw() returns an owning wxScopedCharTypeBuffer. Keep it alive for
    // the entire styling pass; assigning its temporary conversion to `char*`
    // leaves a dangling pointer and crashes on large documents.
    const auto text_buffer = editor->GetTextRaw();
    const char* text = text_buffer.data();
    if (!text) return;
    // Scintilla positions are UTF-8 byte offsets. Defensively bound all
    // positions by both the control's length and the actual owned buffer size:
    // STYLE_NEEDED can be delivered while a large document is being replaced.
    const auto buffer_length = text_buffer.length();
    const int length = std::min(editor->GetTextLength(), static_cast<int>(
        std::min<std::size_t>(buffer_length, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    const int end = std::clamp(requested_end, 0, length);
    int cursor = std::clamp(editor->GetEndStyled(), 0, length);
    if (cursor >= end) return;
    int line = editor->LineFromPosition(cursor);
    if (line < 0) return;
    cursor = std::clamp(editor->PositionFromLine(line), 0, end);
    if (!editor_syntax_highlighting_) {
        editor->StartStyling(cursor);
        editor->SetStyling(end - cursor, RblDefault);
        for (int position = cursor; position < end; ++position) {
            if (text[position] == '\n') editor->SetLineState(line++, 0);
        }
        return;
    }
    bool in_triple_string = line > 0 && editor->GetLineState(line - 1) != 0;
    editor->StartStyling(cursor);
    int styled = cursor;
    auto style = [&](int finish, int value) {
        for (int position = styled; position < finish; ++position) {
            if (text[position] == '\n') editor->SetLineState(line++, in_triple_string ? 1 : 0);
        }
        const int count = finish - styled;
        if (count > 0) editor->SetStyling(count, value);
        styled = finish;
    };
    while (cursor < end) {
        const int start = cursor;
        const unsigned char ch = static_cast<unsigned char>(text[cursor]);
        if (std::isspace(ch)) { ++cursor; style(cursor, RblDefault); continue; }
        if (ch == '#' || (ch == '/' && cursor + 1 < end && text[cursor + 1] == '/')) {
            cursor += ch == '#' ? 1 : 2;
            while (cursor < end && text[cursor] != '\n' && text[cursor] != '\r') ++cursor;
            style(cursor, RblComment);
            continue;
        }
        if (in_triple_string || (ch == 'f' && cursor + 1 < end && text[cursor + 1] == '"') || ch == '"') {
            const bool prefixed = !in_triple_string && ch == 'f';
            const int quote = cursor + (prefixed ? 1 : 0);
            const bool triple = in_triple_string || (quote + 2 < end && text[quote] == '"' &&
                text[quote + 1] == '"' && text[quote + 2] == '"');
            if (!in_triple_string) cursor = quote + (triple ? 3 : 1);
            if (triple && !in_triple_string) in_triple_string = true;
            while (cursor < end) {
                if (text[cursor] == '\n') {
                    ++cursor;
                    style(cursor, RblString);
                    continue;
                }
                if (text[cursor] == '\\') { cursor = std::min(end, cursor + 2); continue; }
                if (triple) {
                    if (cursor + 2 < end && text[cursor] == '"' &&
                        text[cursor + 1] == '"' && text[cursor + 2] == '"') {
                        cursor += 3;
                        in_triple_string = false;
                        break;
                    }
                } else if (text[cursor] == '"') { ++cursor; break; }
                ++cursor;
            }
            style(cursor, RblString);
            continue;
        }
        if (std::isdigit(ch)) {
            ++cursor;
            while (cursor < end) {
                const unsigned char next = static_cast<unsigned char>(text[cursor]);
                if (std::isalnum(next) || next == '_') ++cursor;
                else if (next == '.' && cursor + 1 < end && text[cursor + 1] != '.' &&
                         std::isdigit(static_cast<unsigned char>(text[cursor + 1]))) ++cursor;
                else if ((next == '+' || next == '-') && cursor > start &&
                         (text[cursor - 1] == 'e' || text[cursor - 1] == 'E')) ++cursor;
                else break;
            }
            style(cursor, RblNumber);
            continue;
        }
        if (IsIdentStart(ch)) {
            ++cursor;
            while (cursor < end && IsIdentContinue(static_cast<unsigned char>(text[cursor]))) ++cursor;
            const std::string_view word(text + start, static_cast<std::size_t>(cursor - start));
            int token_style = RblDefault;
            if (IsKeyword(word)) token_style = RblKeyword;
            else if (IsBoolean(word)) token_style = RblBoolean;
            else {
                int lookahead = cursor;
                while (lookahead < end && (text[lookahead] == ' ' || text[lookahead] == '\t')) ++lookahead;
                if (lookahead < end && text[lookahead] == '(') token_style = RblFunction;
            }
            style(cursor, token_style);
            continue;
        }
        ++cursor;
        style(cursor, RblOperator);
    }
    if (styled < end) style(end, RblDefault);
}

void MainFrame::OnEditorStyleNeeded(wxStyledTextEvent& event) {
    if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject())) {
        StyleRbl(editor, event.GetPosition());
    }
}

void MainFrame::OnEditorModified(wxStyledTextEvent& event) {
    if ((event.GetModificationType() & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) == 0) {
        event.Skip();
        return;
    }
    if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject())) {
        fold_pending_.insert(editor);
        if (editor == active_snapshot_editor_ && !diagnostics_stale_ &&
            (task_running_ || problems_->GetItemCount() != 0 ||
             !pending_diagnostics_.empty() || !diagnostic_buffer_.empty())) {
            diagnostics_stale_ = true;
            diagnostics_timer_.Stop();
            problems_->DeleteAllItems();
            pending_diagnostics_.clear();
            pending_diagnostic_render_index_ = 0;
            diagnostic_buffer_.clear();
            diagnostic_discarding_oversized_line_ = false;
            AppendOutput("[problems] diagnostics cleared because the checked source buffer changed; run Check again\n");
        }
        const auto found = documents_.find(editor);
        if (found != documents_.end()) {
            found->second.fold_scan_active = false;
            found->second.fold_next_line = 0;
            found->second.fold_depth = 0;
            found->second.fold_in_triple_string = false;
            found->second.undo_bytes += static_cast<std::size_t>(std::max(0, event.GetLength()));
            std::size_t undo_total = 0;
            for (const auto& [page, document] : documents_) {
                (void)page;
                undo_total += document.undo_bytes;
            }
            if (undo_total > MaxUndoHistoryBytes) {
                for (auto& [page, document] : documents_) {
                    if (auto* open_editor = dynamic_cast<wxStyledTextCtrl*>(page)) open_editor->EmptyUndoBuffer();
                    document.undo_bytes = 0;
                }
                AppendOutput("[editor] undo history was cleared to stay within the 32 MiB history budget\n");
            }
        }
        UpdateDocumentLimits();
    }
    fold_timer_.StartOnce(180);
    if (editor_auto_check_) {
        auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject());
        const auto found = editor ? documents_.find(editor) : documents_.end();
        if (editor && found != documents_.end() && (found->second.path.empty() ||
            wxFileName(wxString(found->second.path.wstring())).GetExt().CmpNoCase("rbl") == 0)) {
            auto_check_editor_ = editor;
            auto_check_timer_.StartOnce(750);
        }
    }
    event.Skip();
}

void MainFrame::OnEditorSavePoint(wxStyledTextEvent& event) {
    if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject())) UpdateDocumentTitle(editor);
    event.Skip();
}

void MainFrame::OnEditorUpdateUi(wxStyledTextEvent& event) {
    if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject())) {
    SetStatusText(wxString::Format("Ln %d, Col %d", editor->GetCurrentLine() + 1,
            editor->GetColumn(editor->GetCurrentPos()) + 1), 3);
        const int length = editor->GetTextLength();
        int brace = editor->GetCurrentPos();
        auto is_brace = [&](int position) {
            if (position < 0 || position >= length) return false;
            const int ch = editor->GetCharAt(position);
            return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
        };
        if (!is_brace(brace) && is_brace(brace - 1)) --brace;
        if (is_brace(brace)) {
            const int match = editor->BraceMatch(brace);
            if (match >= 0) editor->BraceHighlight(brace, match);
            else editor->BraceBadLight(brace);
        } else {
            editor->BraceHighlight(-1, -1);
        }
    }
    event.Skip();
}

void MainFrame::OnEditorCharAdded(wxStyledTextEvent& event) {
    auto* editor = dynamic_cast<wxStyledTextCtrl*>(event.GetEventObject());
    if (!editor) { event.Skip(); return; }
    const int added = event.GetKey();
    const int line = editor->GetCurrentLine();
    // Scintilla reports the inserted newline byte: LF for LF/CRLF documents,
    // CR for CR-only documents. Handle both so indentation remains consistent
    // when editing legacy CR files on any host platform.
    if ((added == '\n' || added == '\r') && line > 0) {
        const int previous_position = editor->GetCurrentPos() - 2;
        const int previous_style = previous_position >= 0 ? editor->GetStyleAt(previous_position) : RblDefault;
        if (previous_style == RblString || previous_style == RblComment) {
            event.Skip();
            return;
        }
        const auto previous = ToUtf8(editor->GetLine(line - 1));
        int indentation = editor->GetLineIndentation(line - 1);
        auto trimmed = std::string_view(previous);
        while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' ||
            trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.remove_suffix(1);
        const auto first = trimmed.find_first_not_of(" \t");
        const auto header = first == std::string_view::npos ? std::string_view{} : trimmed.substr(first);
        auto clause = header;
        if (clause.starts_with(")")) {
            clause.remove_prefix(1);
            while (!clause.empty() && (clause.front() == ' ' || clause.front() == '\t')) clause.remove_prefix(1);
        }
        const bool opens_block = (clause.starts_with("func ") || clause.starts_with("if ") ||
            clause.starts_with("elif ") || clause == "else(" || clause.starts_with("else (") ||
            clause.starts_with("for ") || clause.starts_with("struct ") ||
            clause.starts_with("switch ") || clause.starts_with("case ") ||
            clause.starts_with("default")) &&
            !trimmed.empty() && trimmed.back() == '(';
        if (opens_block) indentation += editor_tab_size_;
        editor->SetLineIndentation(line, indentation);
        editor->GotoPos(editor->PositionFromLine(line) + editor->GetLineIndentation(line));
    } else if (added == ')') {
        const int previous_position = editor->GetCurrentPos() - 1;
        const int current_style = previous_position >= 0 ? editor->GetStyleAt(previous_position) : RblDefault;
        if (current_style == RblString || current_style == RblComment) {
            event.Skip();
            return;
        }
        const int current_line = editor->GetCurrentLine();
        const auto current = ToUtf8(editor->GetLine(current_line));
        const auto first = current.find_first_not_of(" \t");
        if (first != std::string::npos && current[first] == ')' && current_line > 0) {
            const int indentation = std::max(0, editor->GetLineIndentation(current_line) - editor_tab_size_);
            editor->SetLineIndentation(current_line, indentation);
            editor->GotoPos(editor->PositionFromLine(current_line) + editor->GetLineIndentation(current_line));
        }
    }
    if (added >= 0 && added < 128 && (std::isalnum(static_cast<unsigned char>(added)) || added == '_')) {
        const int position = editor->GetCurrentPos();
        if (position > 0) {
            const int style = editor->GetStyleAt(position - 1);
            if (style != RblString && style != RblComment) {
                const int word_start = editor->WordStartPosition(position, true);
                const int entered = position - word_start;
                if (entered >= 2) {
                    static const wxString completions =
                        "set let func if elif else return and or not is for in break continue struct switch case default "
                        "true false null print input read_file write_file len abs sqrt min max int float str bool "
                        "range enumerate sum round floor ceil sin cos tan asin acos atan atan2 hypot log log10 exp "
                        "rbl rbl.io rbl.math rbl.string rbl.fs rbl.time rbl.random";
                    editor->AutoCompShow(entered, completions);
                }
            }
        }
    }
    event.Skip();
}

void MainFrame::UpdateDocumentTitle(wxStyledTextCtrl* editor) {
    const auto found = documents_.find(editor);
    if (found == documents_.end()) return;
    const int index = notebook_->GetPageIndex(editor);
    if (index == wxNOT_FOUND) return;
    auto title = PageTitle(found->second.path);
    if (editor->GetModify()) title += " *";
    notebook_->SetPageText(static_cast<std::size_t>(index), title);
}

void MainFrame::OnFoldTimer(wxTimerEvent&) {
    if (fold_pending_.empty()) return;
    const auto item = fold_pending_.begin();
    auto* editor = *item;
    fold_pending_.erase(item);
    if (documents_.contains(editor)) UpdateFolding(editor);
    if (!fold_pending_.empty()) fold_timer_.StartOnce(60);
}

void MainFrame::OnAutoCheckTimer(wxTimerEvent&) {
    auto* editor = CurrentEditor();
    if (editor && editor == auto_check_editor_ && !task_running_) StartTask("check");
    auto_check_editor_ = nullptr;
}

void MainFrame::UpdateFolding(wxStyledTextCtrl* editor) {
    const int line_count = editor->GetLineCount();
    auto document = documents_.find(editor);
    if (document == documents_.end()) return;
    if (line_count > MaxFoldLines) {
        document->second.fold_scan_active = false;
        fold_pending_.erase(editor);
        if (!document->second.folding_disabled) {
            editor->FoldAll(wxSTC_FOLDACTION_EXPAND);
            editor->SetMarginWidth(1, 0);
            document->second.folding_disabled = true;
        }
        return;
    }
    if (document->second.folding_disabled) {
        editor->SetMarginWidth(1, FromDIP(16));
        document->second.folding_disabled = false;
    }
    auto& scan = document->second;
    if (!scan.fold_scan_active || scan.fold_line_count != line_count) {
        scan.fold_scan_active = true;
        scan.fold_next_line = 0;
        scan.fold_line_count = line_count;
        scan.fold_depth = 0;
        scan.fold_in_triple_string = false;
    }
    constexpr int FoldChunkLines = 2048;
    int processed = 0;
    int line = scan.fold_next_line;
    for (; line < line_count && processed < FoldChunkLines; ++line, ++processed) {
        const auto line_text = ToUtf8(editor->GetLine(line));
        std::string_view view(line_text);
        while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
        const bool was_in_string = scan.fold_in_triple_string;
        bool closes = false;
        bool opens = false;
        const auto has_block_opener = [](std::string_view candidate) {
            while (!candidate.empty() && (candidate.front() == ' ' || candidate.front() == '\t'))
                candidate.remove_prefix(1);
            const auto word_end = candidate.find_first_of(" (\t\r\n");
            const auto word = candidate.substr(0, word_end);
            const bool block_keyword = word == "func" || word == "if" || word == "elif" ||
                word == "else" || word == "for" || word == "struct" ||
                word == "switch" || word == "case" || word == "default";
            auto tail = candidate;
            while (!tail.empty() && (tail.back() == ' ' || tail.back() == '\t' || tail.back() == '\r'))
                tail.remove_suffix(1);
            return block_keyword && !tail.empty() && tail.back() == '(';
        };
        if (!was_in_string && !view.empty()) {
            if (view.front() == ')') {
                closes = true;
                view.remove_prefix(1);
                while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
                // Multiline conditions may close and open their body on one line: `) (`.
                opens = !view.empty() && (view.front() == '(' || has_block_opener(view));
            }
            if (!closes) opens = has_block_opener(view);
        }
        if (closes && scan.fold_depth > 0) --scan.fold_depth;
        int level = wxSTC_FOLDLEVELBASE + scan.fold_depth;
        if (opens) level |= wxSTC_FOLDLEVELHEADERFLAG;
        editor->SetFoldLevel(line, level);
        if (opens) ++scan.fold_depth;

        // Carry triple-quoted string state across lines so text that resembles
        // a block keyword inside a multiline literal is never foldable.
        for (std::size_t i = 0; i < line_text.size(); ++i) {
            if (scan.fold_in_triple_string) {
                if (i + 2 < line_text.size() && line_text[i] == '"' &&
                    line_text[i + 1] == '"' && line_text[i + 2] == '"') {
                    scan.fold_in_triple_string = false;
                    i += 2;
                } else if (line_text[i] == '\\' && i + 1 < line_text.size()) {
                    ++i;
                }
                continue;
            }
            if (line_text[i] == '#' || (line_text[i] == '/' && i + 1 < line_text.size() &&
                line_text[i + 1] == '/')) break;
            if (line_text[i] == '"') {
                if (i + 2 < line_text.size() && line_text[i + 1] == '"' && line_text[i + 2] == '"') {
                    scan.fold_in_triple_string = true;
                    i += 2;
                } else {
                    ++i;
                    while (i < line_text.size()) {
                        if (line_text[i] == '\\' && i + 1 < line_text.size()) ++i;
                        else if (line_text[i] == '"') break;
                        ++i;
                    }
                }
            }
        }
    }
    scan.fold_next_line = line;
    if (scan.fold_next_line < line_count) {
        fold_pending_.insert(editor);
        if (!fold_timer_.IsRunning()) fold_timer_.StartOnce(60);
    } else {
        scan.fold_scan_active = false;
    }
}

void MainFrame::NewDocument() {
    if (notebook_->GetPageCount() >= MaxOpenTabs) {
        AppendOutput("Tab limit reached (32). Close a document first.\n");
        return;
    }
    auto* editor = new wxStyledTextCtrl(notebook_, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxBORDER_NONE);
    ConfigureEditor(editor);
    notebook_->AddPage(editor, "Untitled", true);
    Document document{};
    document.editor = editor;
    documents_.emplace(editor, std::move(document));
    UpdateDocumentLimits();
    UpdateFolding(editor);
    editor->SetFocus();
}

void MainFrame::NewTextDocument() {
    if (notebook_->GetPageCount() >= MaxOpenTabs) {
        AppendOutput("Tab limit reached (32). Close a document first.\n");
        return;
    }
    auto* editor = new wxStyledTextCtrl(notebook_, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxBORDER_NONE);
    ConfigureEditor(editor);
    notebook_->AddPage(editor, "Untitled.txt", true);
    Document document{};
    document.editor = editor;
    document.is_rbl = false;
    documents_.emplace(editor, std::move(document));
    UpdateDocumentLimits();
    UpdateFolding(editor);
    editor->SetFocus();
}

void MainFrame::OpenDocument() {
    wxFileDialog dialog(this, "Open source file", wxEmptyString, wxEmptyString,
        "RBL and text files (*.rbl;*.txt)|*.rbl;*.txt|All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;

    OpenDocumentPath(dialog.GetPath());
}

void MainFrame::OpenDocumentPath(const wxString& selected_path,
                                const FileIdentity* expected_identity) {
    std::error_code path_error;
    auto path = std::filesystem::absolute(
        std::filesystem::path(selected_path.ToStdWstring()), path_error).lexically_normal();
    if (path_error) {
        wxMessageBox("Could not resolve the selected file path.", "Open failed",
            wxOK | wxICON_ERROR, this);
        return;
    }
    bool path_contains_reparse_point = false;
    std::error_code reparse_error;
    if (!HasSymlinkOrReparseComponent(path, path_contains_reparse_point, reparse_error) ||
        path_contains_reparse_point) {
        const wxString message = path_contains_reparse_point
            ? wxString("Symbolic links and filesystem reparse points are not opened as source files.")
            : wxString("The selected path could not be checked safely: ") +
                wxString::FromUTF8(reparse_error.message());
        wxMessageBox(message, "Open failed", wxOK | wxICON_WARNING, this);
        return;
    }
    std::string bytes;
    FileIdentity opened_identity;
    std::error_code read_error;
    if (!ReadFileNoFollow(path, MaxEditableFileBytes, bytes, opened_identity, read_error)) {
        const wxString message = read_error == std::errc::file_too_large
            ? wxString("Files larger than 16 MiB are not supported by the editor.")
            : wxString("The file could not be read consistently: ") +
                wxString::FromUTF8(read_error.message());
        const wxString title = read_error == std::errc::file_too_large
            ? wxString("File too large") : wxString("Open failed");
        wxMessageBox(message, title,
            read_error == std::errc::file_too_large ? wxOK | wxICON_WARNING : wxOK | wxICON_ERROR,
            this);
        return;
    }
    if (expected_identity &&
        !SameFileIdentity(*expected_identity, opened_identity)) {
        wxMessageBox("The selected project entry changed since it was listed. Expand the directory again before opening it.",
            "Open failed", wxOK | wxICON_WARNING, this);
        return;
    }
    for (const auto& [page, document] : documents_) {
        if (document.path == path) {
            if (expected_identity &&
                (!document.file_identity.valid ||
                 !SameFileIdentity(*expected_identity, document.file_identity))) {
                wxMessageBox("The already-open document does not match the selected file identity. It was not activated.",
                    "Open failed", wxOK | wxICON_WARNING, this);
                return;
            }
            notebook_->SetSelection(notebook_->GetPageIndex(page));
            return;
        }
    }
    if (notebook_->GetPageCount() >= MaxOpenTabs) {
        AppendOutput("Tab limit reached (32). Close a document first.\n");
        return;
    }
    auto encoding = Document::Encoding::Utf8;
    std::string_view text_bytes(bytes);
    std::string decoded_utf16;
    if (text_bytes.size() >= 2 && static_cast<unsigned char>(text_bytes[0]) == 0xFF &&
        static_cast<unsigned char>(text_bytes[1]) == 0xFE) {
        encoding = Document::Encoding::Utf16Le;
        text_bytes.remove_prefix(2);
        if (!DecodeUtf16(text_bytes, true, decoded_utf16)) {
            wxMessageBox("The UTF-16 LE file contains invalid code units.", "Unsupported encoding", wxOK | wxICON_WARNING, this);
            return;
        }
        text_bytes = decoded_utf16;
    } else if (text_bytes.size() >= 2 && static_cast<unsigned char>(text_bytes[0]) == 0xFE &&
        static_cast<unsigned char>(text_bytes[1]) == 0xFF) {
        encoding = Document::Encoding::Utf16Be;
        text_bytes.remove_prefix(2);
        if (!DecodeUtf16(text_bytes, false, decoded_utf16)) {
            wxMessageBox("The UTF-16 BE file contains invalid code units.", "Unsupported encoding", wxOK | wxICON_WARNING, this);
            return;
        }
        text_bytes = decoded_utf16;
    } else if (text_bytes.size() >= 3 && static_cast<unsigned char>(text_bytes[0]) == 0xEF &&
        static_cast<unsigned char>(text_bytes[1]) == 0xBB &&
        static_cast<unsigned char>(text_bytes[2]) == 0xBF) {
        encoding = Document::Encoding::Utf8Bom;
        text_bytes.remove_prefix(3);
    }
    // Scintilla keeps the line endings already present in the document, but
    // uses its configured EOL mode for newly inserted lines. Match the first
    // source line ending so editing an LF/CR file on Windows does not silently
    // introduce CRLF when the user presses Enter or pastes a newline.
    int eol_mode = wxSTC_EOL_LF;
    for (std::size_t offset = 0; offset < text_bytes.size(); ++offset) {
        if (text_bytes[offset] == '\r') {
            eol_mode = offset + 1 < text_bytes.size() && text_bytes[offset + 1] == '\n'
                ? wxSTC_EOL_CRLF : wxSTC_EOL_CR;
            break;
        }
        if (text_bytes[offset] == '\n') {
            eol_mode = wxSTC_EOL_LF;
            break;
        }
    }
    if (text_bytes.size() > MaxEditableFileBytes) {
        wxMessageBox("The decoded document exceeds the 16 MiB editor limit.",
            "File too large", wxOK | wxICON_WARNING, this);
        return;
    }
    if (text_bytes.find('\0') != std::string_view::npos) {
        wxMessageBox("Files containing NUL bytes are not supported by this editor.",
                     "Unsupported file", wxOK | wxICON_WARNING, this);
        return;
    }
    if (!IsValidUtf8(text_bytes)) {
        wxMessageBox("The selected file is not valid UTF-8. It was not opened or modified.",
                     "Unsupported encoding", wxOK | wxICON_WARNING, this);
        return;
    }
    std::size_t open_bytes = 0;
    for (const auto& [page, document] : documents_) {
        (void)document;
        if (auto* open_editor = dynamic_cast<wxStyledTextCtrl*>(page))
            open_bytes += static_cast<std::size_t>(open_editor->GetTextLength());
    }
    if (open_bytes + text_bytes.size() > MaxOpenDocumentBytes) {
        wxMessageBox("The 64 MiB aggregate open-document budget has been reached. Close documents first.",
            "Document memory limit", wxOK | wxICON_WARNING, this);
        return;
    }
    auto* editor = new wxStyledTextCtrl(notebook_, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxBORDER_NONE);
    ConfigureEditor(editor);
    editor->SetEOLMode(eol_mode);
    editor->SetText(wxString::FromUTF8(text_bytes.data(), text_bytes.size()));
    editor->EmptyUndoBuffer();
    editor->SetSavePoint();
    notebook_->AddPage(editor, PageTitle(path), true);
    Document document{};
    document.editor = editor;
    document.path = path;
    document.is_rbl = wxFileName(wxString(path.wstring())).GetExt().CmpNoCase("rbl") == 0;
    document.saved_hash = HashBytes(bytes);
    document.encoding = encoding;
    document.file_identity = opened_identity;
    document.retained_original_backup_links = 0;
    documents_.emplace(editor, std::move(document));
    UpdateDocumentLimits();
    UpdateFolding(editor);
}

bool MainFrame::SaveDocument(wxWindow* page, bool save_as) {
    auto found = documents_.find(page);
    if (found == documents_.end()) return false;
    auto& document = found->second;
    auto target = document.path;
    if (document.path.empty() || save_as) {
        wxFileDialog dialog(this, "Save source file", wxEmptyString,
            document.is_rbl ? "untitled.rbl" : "untitled.txt",
            "RBL files (*.rbl)|*.rbl|Text files (*.txt)|*.txt|All files (*.*)|*.*",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return false;
        target = std::filesystem::path(dialog.GetPath().ToStdWstring());
    }
    std::error_code canonical_error;
    auto canonical_target = std::filesystem::absolute(target, canonical_error).lexically_normal();
    if (canonical_error) {
        wxMessageBox("Could not resolve the destination path.", "Save failed",
            wxOK | wxICON_ERROR, this);
        return false;
    }
    std::error_code current_path_error;
    const auto canonical_current = document.path.empty()
        ? std::filesystem::path{}
        : std::filesystem::absolute(document.path, current_path_error).lexically_normal();
    std::error_code equivalent_error;
    const bool same_file = !document.path.empty() &&
        std::filesystem::equivalent(document.path, canonical_target, equivalent_error);
    const bool same_document_path = !document.path.empty() &&
        (same_file || (!current_path_error && canonical_current == canonical_target));
    for (const auto& [other_page, other_document] : documents_) {
        if (other_page != page && !other_document.path.empty()) {
            std::error_code other_error;
            auto other_path = std::filesystem::absolute(
                other_document.path, other_error).lexically_normal();
            if (other_error) other_path = other_document.path;
            if (other_path == canonical_target) {
                wxMessageBox("That file is already open in another tab. Save As to a different path.",
                    "Duplicate document", wxOK | wxICON_WARNING, this);
                return false;
            }
        }
    }
    target = canonical_target;
    std::error_code reparse_error;
#ifdef _WIN32
    WinDirectoryChainGuard save_path_guard;
    if (!LockWindowsDirectoryChain(target.parent_path(), save_path_guard, reparse_error)) {
        wxMessageBox("The destination directory could not be locked and verified safely: " +
                     FromUtf8(reparse_error.message()),
            "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
#endif
    bool path_contains_reparse_point = false;
    if (!HasSymlinkOrReparseComponent(target, path_contains_reparse_point, reparse_error) ||
        path_contains_reparse_point) {
        const wxString message = path_contains_reparse_point
            ? wxString("Saving through symbolic links or filesystem reparse points is disabled. Select a regular destination path directly.")
            : wxString("The destination path could not be checked safely: ") +
                wxString::FromUTF8(reparse_error.message());
        wxMessageBox(message, "Save failed", wxOK | wxICON_WARNING, this);
        return false;
    }
#ifndef __WXMSW__
    std::error_code directory_open_error;
    PosixDirectoryHandle save_directory_guard(
        rbl::wxui::OpenDirectoryNoFollow(target.parent_path(), directory_open_error));
    std::vector<std::filesystem::path> retained_quarantine_paths;
    const std::string target_leaf = target.filename().string();
    if (save_directory_guard.fd < 0 || target_leaf.empty() || target_leaf == "." ||
        target_leaf == ".." || target_leaf.find('/') != std::string::npos) {
        if (!directory_open_error)
            directory_open_error = std::make_error_code(std::errc::invalid_argument);
        wxMessageBox("The destination directory could not be pinned safely: " +
                     FromUtf8(directory_open_error.message()),
            "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
    const auto read_target = [&](std::string& bytes, FileIdentity& identity,
                                 std::error_code& error) {
        return ReadFileAt(save_directory_guard.fd, target_leaf,
                          MaxEditableFileBytes, bytes, identity, error);
    };
    const auto query_save_leaf = [&](const std::string& leaf, FileIdentity& identity,
                                     std::error_code& error) {
        return QueryFileIdentityAt(save_directory_guard.fd, leaf, identity, error);
    };
    const auto unlink_save_leaf = [&](const std::string& leaf, const FileIdentity& identity,
                                      std::error_code& error) {
        std::string retained_leaf;
        const bool removed = UnlinkLeafIfIdentityAt(
            save_directory_guard.fd, leaf, identity, retained_leaf, error);
        if (!retained_leaf.empty())
            retained_quarantine_paths.push_back(target.parent_path() / retained_leaf);
        return removed;
    };
    const auto append_quarantine_paths = [&](wxString& message) {
        for (const auto& path : retained_quarantine_paths) {
            message += "\nUnexpected or unremoved artifact quarantined at: ";
            message += wxString::FromUTF8(path.string());
        }
    };
#endif
    std::error_code exists_error;
#ifdef __WXMSW__
    const bool target_exists = std::filesystem::exists(target, exists_error);
#else
    struct stat initial_target_info{};
    const int initial_stat = ::fstatat(save_directory_guard.fd, target_leaf.c_str(),
                                      &initial_target_info, AT_SYMLINK_NOFOLLOW);
    const bool target_exists = initial_stat == 0;
    if (initial_stat != 0 && errno != ENOENT)
        exists_error = std::error_code(PosixErrorOrIoError(), std::generic_category());
    else if (target_exists && !S_ISREG(initial_target_info.st_mode))
        exists_error = std::make_error_code(std::errc::invalid_argument);
#endif
    if (exists_error) {
        wxMessageBox("Could not inspect the destination file before saving.", "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
    FileIdentity original_identity;
#ifdef __WXMSW__
    if (target_exists && !QueryFileIdentity(target, original_identity, exists_error)) {
#else
    if (target_exists && !QueryFileIdentityAt(save_directory_guard.fd, target_leaf,
                                               original_identity, exists_error)) {
#endif
        wxMessageBox("The destination is not a regular file or its identity cannot be verified. Saving was cancelled.",
            "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
    const std::uint64_t retained_backup_links = same_document_path
        ? document.retained_original_backup_links : 0U;
    if (target_exists && original_identity.link_count > 1U + retained_backup_links) {
        wxString message;
        message.Printf(
            "This file has %llu hard links. Saving atomically replaces only this pathname; "
            "the other hard-link names will continue to contain the previous bytes. Continue?",
            static_cast<unsigned long long>(original_identity.link_count - retained_backup_links));
        if (wxMessageBox(message, "Hard-linked destination",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
            return false;
        }
    }
    std::uint64_t disk_hash_at_check = 0;
    if (same_document_path && document.saved_hash != 0 && !target_exists && !exists_error) {
        const auto answer = wxMessageBox("This file was deleted outside RBL Studio. Recreate it?",
            "File deleted", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES) return false;
    }
    if (target_exists) {
        std::string disk_bytes;
        FileIdentity disk_identity;
        std::error_code disk_read_error;
#ifdef __WXMSW__
        if (!ReadFileNoFollow(target, MaxEditableFileBytes, disk_bytes,
                disk_identity, disk_read_error) ||
#else
        if (!read_target(disk_bytes, disk_identity, disk_read_error) ||
#endif
            !SameFileIdentity(original_identity, disk_identity) ||
            disk_identity.link_count != original_identity.link_count) {
            if (!disk_read_error) disk_read_error = std::make_error_code(std::errc::state_not_recoverable);
            wxMessageBox("Could not read and verify the existing file safely: " +
                         FromUtf8(disk_read_error.message()),
                "Save conflict check failed", wxOK | wxICON_ERROR, this);
            return false;
        }
        disk_hash_at_check = HashBytes(disk_bytes);
        if (same_document_path && document.saved_hash != 0 &&
            disk_hash_at_check != document.saved_hash) {
            const auto answer = wxMessageBox("This file changed outside RBL Studio. Overwrite those changes?",
                "External modification", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
            if (answer != wxYES) return false;
        }
    }
    const auto utf8 = document.editor->GetText().ToUTF8();
    const std::string utf8_text(utf8.data(), utf8.length());
    std::size_t other_document_bytes = 0;
    for (const auto& [other_page, other_document] : documents_) {
        (void)other_document;
        if (other_page != page) {
            if (auto* other_editor = dynamic_cast<wxStyledTextCtrl*>(other_page))
                other_document_bytes += static_cast<std::size_t>(other_editor->GetTextLength());
        }
    }
    if (utf8_text.size() > MaxEditableFileBytes || other_document_bytes + utf8_text.size() > MaxOpenDocumentBytes) {
        wxMessageBox("The document or aggregate 64 MiB open-document limit has been reached.",
            "Document memory limit", wxOK | wxICON_WARNING, this);
        return false;
    }
    std::string output_bytes;
    if (document.encoding == Document::Encoding::Utf16Le || document.encoding == Document::Encoding::Utf16Be) {
        const bool little = document.encoding == Document::Encoding::Utf16Le;
        output_bytes = little ? std::string("\xFF\xFE", 2) : std::string("\xFE\xFF", 2);
        output_bytes += EncodeUtf16(utf8_text, little);
    } else {
        output_bytes = utf8_text;
        if (document.encoding == Document::Encoding::Utf8Bom) output_bytes.insert(0, "\xEF\xBB\xBF", 3);
    }
    std::filesystem::path temporary;
    std::string temporary_leaf;
    FileIdentity temporary_identity;
    std::error_code temporary_error;
#ifdef __WXMSW__
    if (!WriteExclusiveTemporary(target, output_bytes, temporary, temporary_identity, temporary_error)) {
#else
    if (!WriteExclusiveTemporaryAt(save_directory_guard.fd, target_leaf, output_bytes,
                                   temporary_leaf, temporary_identity, temporary_error)) {
#endif
        wxString message = "Could not create and durably write a unique temporary file: " +
            FromUtf8(temporary_error.message());
#ifndef __WXMSW__
        if (!temporary_leaf.empty()) {
            message += "\nUnfinished temporary artifact retained at: ";
            message += wxString::FromUTF8((target.parent_path() / temporary_leaf).string());
        }
        append_quarantine_paths(message);
#endif
        wxMessageBox(message, "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
#ifndef __WXMSW__
    temporary = target.parent_path() / temporary_leaf;
#endif
    // Revalidate after the temporary file has been fully written. This narrows
    // the external-writer race window: a file changed while serialization or
    // disk I/O was in progress must trigger a second explicit confirmation.
    // Existing POSIX destinations are replaced only through atomic name
    // exchange; unsupported filesystems fail closed. First saves use linkat
    // with exclusive destination creation.
    if (target_exists) {
        std::string disk_bytes;
        FileIdentity current_disk_identity;
        std::error_code disk_read_error;
#ifdef __WXMSW__
        if (!ReadFileNoFollow(target, MaxEditableFileBytes, disk_bytes,
                current_disk_identity, disk_read_error) ||
#else
        if (!read_target(disk_bytes, current_disk_identity, disk_read_error) ||
#endif
            !SameFileIdentity(original_identity, current_disk_identity) ||
            current_disk_identity.link_count != original_identity.link_count) {
#ifdef __WXMSW__
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
#else
            std::error_code ignored;
            unlink_save_leaf(temporary_leaf, temporary_identity, ignored);
#endif
            if (!disk_read_error) disk_read_error = std::make_error_code(std::errc::state_not_recoverable);
            wxString message = "The destination could not be safely revalidated after writing: " +
                FromUtf8(disk_read_error.message());
#ifndef __WXMSW__
            append_quarantine_paths(message);
#endif
            wxMessageBox(message, "Save conflict check failed", wxOK | wxICON_ERROR, this);
            return false;
        }
        const auto current_disk_hash = HashBytes(disk_bytes);
        if (current_disk_hash != disk_hash_at_check) {
            const auto answer = wxMessageBox(
                "This file changed again while RBL Studio was saving. Overwrite the newer changes?",
                "External modification", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
            if (answer != wxYES) {
#ifdef __WXMSW__
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
#else
                std::error_code ignored;
                unlink_save_leaf(temporary_leaf, temporary_identity, ignored);
                if (!retained_quarantine_paths.empty()) {
                    wxString message("The temporary file was not accepted; a cleanup artifact was retained:");
                    append_quarantine_paths(message);
                    wxMessageBox(message, "Save cleanup warning", wxOK | wxICON_WARNING, this);
                }
#endif
                return false;
            }
            disk_hash_at_check = current_disk_hash;
        }
    }
    std::error_code replace_error;
    bool directory_sync_failed = false;
    bool temporary_cleanup_failed = false;
    FileIdentity current_temporary_identity;
#ifdef __WXMSW__
    if (!QueryFileIdentity(temporary, current_temporary_identity, replace_error) ||
#else
    if (!query_save_leaf(temporary_leaf, current_temporary_identity, replace_error) ||
#endif
        !SameFileIdentity(temporary_identity, current_temporary_identity)) {
        if (!replace_error) replace_error = std::make_error_code(std::errc::state_not_recoverable);
    }
    // Recheck content after any modal conflict prompt. The user may have left
    // the dialog open while another process continued writing the same inode.
    // If it changed again, keep the editor dirty and leave the newer bytes in
    // place rather than asking an unbounded chain of overwrite confirmations.
    if (!replace_error && target_exists) {
        std::string disk_bytes;
        FileIdentity current_disk_identity;
        std::error_code disk_read_error;
#ifdef __WXMSW__
        if (!ReadFileNoFollow(target, MaxEditableFileBytes, disk_bytes,
                current_disk_identity, disk_read_error) ||
#else
        if (!read_target(disk_bytes, current_disk_identity, disk_read_error) ||
#endif
            !SameFileIdentity(original_identity, current_disk_identity) ||
            current_disk_identity.link_count != original_identity.link_count ||
            HashBytes(disk_bytes) != disk_hash_at_check) {
            replace_error = disk_read_error
                ? disk_read_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error && target_exists) {
        FileIdentity current_identity;
#ifdef __WXMSW__
        if (!QueryFileIdentity(target, current_identity, replace_error) ||
#else
        if (!query_save_leaf(target_leaf, current_identity, replace_error) ||
#endif
            !SameFileIdentity(original_identity, current_identity) ||
            current_identity.link_count != original_identity.link_count) {
            if (!replace_error) replace_error = std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (replace_error) {
#ifdef __WXMSW__
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
#else
        std::error_code ignored;
        unlink_save_leaf(temporary_leaf, temporary_identity, ignored);
#endif
        wxString message("The destination changed again or could not be revalidated immediately before replacement. "
                         "The newer file was left untouched.");
#ifndef __WXMSW__
        append_quarantine_paths(message);
#endif
        wxMessageBox(message, "Save conflict check failed", wxOK | wxICON_ERROR, this);
        return false;
    }
    bool metadata_warning = false;
    std::filesystem::path replacement_backup_path;
    std::filesystem::path original_backup_path;
    std::filesystem::path recovery_path;
    bool backup_cleanup_failed = false;
#ifdef __WXMSW__
    bool original_backup_cleanup_failed = false;
#endif
    bool recovery_cleanup_failed = false;
#ifndef __WXMSW__
    bool save_directory_synced_before_replace = false;
    std::string replacement_backup_leaf;
    std::string recovery_leaf;
    bool temporary_recovery_retained = false;
    bool atomic_exchange_used = false;
    bool displaced_cleanup_failed = false;
#endif
#ifdef __WXMSW__
    bool recovery_link_created = false;
    bool recovery_link_unavailable = false;
    bool recovery_link_creation_failed = false;
    DWORD recovery_link_error = ERROR_SUCCESS;
    bool original_backup_link_created = false;
    DWORD original_backup_link_error = ERROR_SUCCESS;
    FileIdentity backup_identity;
    FileIdentity original_backup_identity;
    FileIdentity recovery_identity;
#else
    bool backup_link_created = false;
    bool recovery_link_created = false;
#endif
#ifdef __WXMSW__
    recovery_path = UniqueWindowsSiblingPath(target, L".rblstudio-recovery-", replace_error);
    if (!replace_error && !recovery_path.empty()) {
        recovery_link_created = ::CreateHardLinkW(
            recovery_path.c_str(), temporary.c_str(), nullptr) != FALSE;
        recovery_link_unavailable = !recovery_link_created;
        if (!recovery_link_created) {
            recovery_link_error = ::GetLastError();
            recovery_link_creation_failed = true;
            recovery_path.clear();
            // Existing destinations must have a second name for the new
            // bytes before ReplaceFileW consumes the temporary pathname.
            // First-time saves retain the historical best-effort behavior.
            if (target_exists) {
                replace_error = std::error_code(
                    static_cast<int>(recovery_link_error), std::system_category());
            }
        }
    }
    if (target_exists && !replace_error) {
        original_backup_path = UniqueWindowsSiblingPath(
            target, L".rblstudio-original-", replace_error);
        if (!replace_error && !original_backup_path.empty()) {
            original_backup_link_created = ::CreateHardLinkW(
                original_backup_path.c_str(), target.c_str(), nullptr) != FALSE;
            if (!original_backup_link_created) {
                original_backup_link_error = ::GetLastError();
                replace_error = std::error_code(
                    static_cast<int>(original_backup_link_error), std::system_category());
            }
        }
    }
    if (target_exists && !replace_error) {
        replacement_backup_path = UniqueWindowsSiblingPath(
            target, L".rblstudio-backup-", replace_error);
    }
    if (target_exists && !replace_error) {
        // Revalidate every path-backed input and recovery name immediately
        // before ReplaceFileW. The original hard link remains as a durable
        // copy if the destination leaf races in the remaining pathname gap.
        FileIdentity current_target_identity;
        FileIdentity current_temporary_identity;
        FileIdentity current_original_backup_identity;
        FileIdentity current_recovery_identity;
        std::string current_target_bytes;
        std::error_code validation_error;
        if (!ReadFileNoFollow(target, MaxEditableFileBytes, current_target_bytes,
                current_target_identity, validation_error) ||
            !SameFileIdentity(original_identity, current_target_identity) ||
            HashBytes(current_target_bytes) != disk_hash_at_check ||
            !QueryFileIdentity(temporary, current_temporary_identity, validation_error) ||
            !SameFileIdentity(temporary_identity, current_temporary_identity) ||
            !QueryFileIdentity(original_backup_path, current_original_backup_identity, validation_error) ||
            !SameFileIdentity(original_identity, current_original_backup_identity) ||
            (recovery_link_created &&
                (!QueryFileIdentity(recovery_path, current_recovery_identity, validation_error) ||
                 !SameFileIdentity(temporary_identity, current_recovery_identity)))) {
            replace_error = validation_error
                ? validation_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error) {
        // ReplaceFileW's WRITE_THROUGH flag is documented as unsupported.
        // Its backup path preserves the actual object displaced by the
        // atomic replacement, allowing identity verification before cleanup.
        const BOOL replaced = target_exists
            ? ::ReplaceFileW(target.c_str(), temporary.c_str(), replacement_backup_path.c_str(),
                0, nullptr, nullptr)
            : ::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!replaced) {
            replace_error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        }
    }
#else
    if (target_exists && !CopyPosixMetadataAt(save_directory_guard.fd, target_leaf,
            temporary_leaf, original_identity, temporary_identity,
            metadata_warning, replace_error)) {
        // CopyPosixMetadataAt leaves the original destination untouched on error.
    }
    if (!replace_error && target_exists) {
        recovery_link_created = CreateExclusiveSiblingHardLinkAt(
            save_directory_guard.fd, temporary_leaf,
            ".rblstudio-recovery-", recovery_leaf, replace_error);
        if (recovery_link_created) recovery_path = target.parent_path() / recovery_leaf;
    }
    if (!replace_error && target_exists) {
        backup_link_created = CreateExclusiveSiblingHardLinkAt(
            save_directory_guard.fd, target_leaf,
            ".rblstudio-backup-", replacement_backup_leaf, replace_error);
        if (backup_link_created)
            replacement_backup_path = target.parent_path() / replacement_backup_leaf;
    }
    if (!replace_error && target_exists) {
        // Hard links retain the original inode and the new output as recovery
        // artifacts until the replacement identity checks have completed.
        // Prefer renameat2(RENAME_EXCHANGE): unlike renameat(), it preserves
        // whichever object actually occupied the destination at the instant
        // of replacement under the temporary name, so a raced leaf is not
        // silently unlinked. Filesystems such as DrvFS may not support it.
        std::string disk_bytes;
        FileIdentity current_disk_identity;
        FileIdentity current_temporary_identity;
        FileIdentity backup_identity;
        FileIdentity recovery_identity;
        std::error_code validation_error;
        if (!read_target(disk_bytes, current_disk_identity, validation_error) ||
            !SameFileIdentity(original_identity, current_disk_identity) ||
            current_disk_identity.link_count != original_identity.link_count + 1U ||
            HashBytes(disk_bytes) != disk_hash_at_check ||
            !query_save_leaf(temporary_leaf, current_temporary_identity, validation_error) ||
            !SameFileIdentity(temporary_identity, current_temporary_identity) ||
            !query_save_leaf(replacement_backup_leaf, backup_identity, validation_error) ||
            !SameFileIdentity(original_identity, backup_identity) ||
            !query_save_leaf(recovery_leaf, recovery_identity, validation_error) ||
            !SameFileIdentity(temporary_identity, recovery_identity)) {
            replace_error = validation_error
                ? validation_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error && !target_exists) {
        FileIdentity current_temporary;
        if (!query_save_leaf(temporary_leaf, current_temporary, replace_error) ||
            !SameFileIdentity(temporary_identity, current_temporary)) {
            if (!replace_error) replace_error = std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error) {
        // Stage 1: make the temporary name and (for replacement saves) both
        // recovery hard-links durable before changing the target namespace.
        if (::fsync(save_directory_guard.fd) != 0) {
            replace_error = std::error_code(PosixErrorOrIoError(), std::generic_category());
            directory_sync_failed = true;
        } else {
            save_directory_synced_before_replace = true;
        }
    }
    if (!replace_error && target_exists) {
        std::error_code exchange_error;
        if (rbl::wxui::ExchangeFileNamesAt(save_directory_guard.fd, temporary_leaf,
                                target_leaf, exchange_error)) {
            atomic_exchange_used = true;
        } else {
            // Replacing an existing leaf requires an atomic exchange so the
            // object displaced at the instant of replacement can be verified.
            // renameat() is unconditional and could discard a third-party leaf
            // installed after preflight; unsupported filesystems must fail closed.
            replace_error = exchange_error;
        }
    }
    if (!replace_error && atomic_exchange_used) {
        std::string displaced_bytes;
        FileIdentity displaced_identity;
        std::error_code displaced_error;
        const bool displaced_read = ReadFileAt(save_directory_guard.fd, temporary_leaf,
            MaxEditableFileBytes, displaced_bytes, displaced_identity, displaced_error);
        if (!displaced_read) {
            // Reading may fail after a concurrent permission change even when
            // the displaced regular-file identity is still queryable. Use that
            // identity to attempt a safe exchange-back; never require content
            // readability to restore the namespace entry we just displaced.
            std::error_code identity_error;
            if (!query_save_leaf(temporary_leaf, displaced_identity, identity_error) &&
                identity_error) {
                displaced_error = identity_error;
            }
        }
        const bool displaced_is_original = displaced_read &&
            SameFileIdentity(original_identity, displaced_identity) &&
            displaced_identity.link_count == original_identity.link_count + 1U &&
            HashBytes(displaced_bytes) == disk_hash_at_check;
        if (!displaced_is_original) {
            // Roll the exchange back only while both names still refer to the
            // exact objects observed after our exchange. If either name raced
            // again, preserve the recovery set instead of overwriting it.
            FileIdentity current_target_identity;
            FileIdentity current_displaced_identity;
            std::error_code rollback_error;
            if (query_save_leaf(target_leaf, current_target_identity, rollback_error) &&
                SameFileIdentity(temporary_identity, current_target_identity) &&
                query_save_leaf(temporary_leaf, current_displaced_identity, rollback_error) &&
                SameFileIdentity(displaced_identity, current_displaced_identity)) {
                if (!rbl::wxui::ExchangeFileNamesAt(save_directory_guard.fd, temporary_leaf,
                                         target_leaf, rollback_error)) {
                    displaced_error = rollback_error;
                }
            }
            replace_error = displaced_error
                ? displaced_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    else if (!replace_error && !target_exists) {
        // A first save uses an exclusive hard-link so a destination created
        // after the initial absence check is never overwritten.
        if (::linkat(save_directory_guard.fd, temporary_leaf.c_str(),
                     save_directory_guard.fd, target_leaf.c_str(), 0) != 0)
            replace_error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        // Keep the temporary name as the recovery name until the installed
        // target has passed its post-replacement directory sync.
    }
#endif
#ifdef __WXMSW__
    if (!replace_error && target_exists) {
        FileIdentity backup_identity;
        std::error_code backup_error;
        if (!QueryFileIdentity(replacement_backup_path, backup_identity, backup_error) ||
            !SameFileIdentity(original_identity, backup_identity)) {
            replace_error = backup_error
                ? backup_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
#else
    if (!replace_error && recovery_link_created) {
        FileIdentity recovery_identity;
        std::error_code recovery_error;
        if (!query_save_leaf(recovery_leaf, recovery_identity, recovery_error) ||
            !SameFileIdentity(temporary_identity, recovery_identity)) {
            replace_error = recovery_error
                ? recovery_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error && atomic_exchange_used) {
        FileIdentity displaced_identity;
        std::error_code displaced_error;
        if (!query_save_leaf(temporary_leaf, displaced_identity, displaced_error) ||
            !SameFileIdentity(original_identity, displaced_identity)) {
            replace_error = displaced_error
                ? displaced_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
    if (!replace_error && backup_link_created) {
        FileIdentity backup_identity;
        std::error_code backup_error;
        if (!query_save_leaf(replacement_backup_leaf, backup_identity, backup_error) ||
            !SameFileIdentity(original_identity, backup_identity)) {
            replace_error = backup_error
                ? backup_error
                : std::make_error_code(std::errc::state_not_recoverable);
        }
    }
#endif
    if (replace_error) {
#ifdef __WXMSW__
        // A failed ReplaceFileW may leave the original destination in place
        // together with our own hard-link recovery name(s). Keep those names
        // as recovery artifacts, but account for their link count in this
        // open document so a retry does not mistake our own backup for an
        // external hard link or a concurrent destination change.
        if (same_document_path && target_exists) {
            std::string unchanged_bytes;
            FileIdentity unchanged_target;
            std::error_code cleanup_validation_error;
            bool snapshot_valid = ReadFileNoFollow(target, MaxEditableFileBytes,
                    unchanged_bytes, unchanged_target, cleanup_validation_error) &&
                SameFileIdentity(original_identity, unchanged_target) &&
                HashBytes(unchanged_bytes) == disk_hash_at_check;

            std::uint64_t newly_retained_backup_links = 0;
            if (snapshot_valid && original_backup_link_created) {
                FileIdentity backup_identity;
                snapshot_valid = QueryFileIdentity(
                    original_backup_path, backup_identity, cleanup_validation_error) &&
                    SameFileIdentity(original_identity, backup_identity);
                if (snapshot_valid) ++newly_retained_backup_links;
            }
            if (snapshot_valid && !replacement_backup_path.empty()) {
                const DWORD attributes = ::GetFileAttributesW(replacement_backup_path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES) {
                    FileIdentity backup_identity;
                    snapshot_valid = QueryFileIdentity(
                        replacement_backup_path, backup_identity, cleanup_validation_error) &&
                        SameFileIdentity(original_identity, backup_identity);
                    if (snapshot_valid) ++newly_retained_backup_links;
                } else {
                    const DWORD code = ::GetLastError();
                    snapshot_valid = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
                }
            }
            const std::uint64_t expected_link_count =
                original_identity.link_count + newly_retained_backup_links;
            if (snapshot_valid && unchanged_target.link_count == expected_link_count) {
                document.file_identity = unchanged_target;
                document.retained_original_backup_links += newly_retained_backup_links;
            }
        }
#endif
        wxString message("Could not safely replace the destination. The editor remains dirty.");
#ifndef __WXMSW__
        if (replace_error == std::errc::operation_not_supported) {
            message = "This filesystem does not support the atomic name exchange required to "
                      "safely replace an existing file. The destination was not replaced and "
                      "the editor remains dirty. Save As to a new filename or use a filesystem "
                      "that supports atomic name exchange.";
        }
#endif
#ifdef __WXMSW__
        if (target_exists) {
            message += recovery_link_creation_failed
                ? "\nDestination left unchanged at: "
                : "\nDestination path: ";
            message += wxString(target.c_str());
        }
        if (::GetFileAttributesW(temporary.c_str()) != INVALID_FILE_ATTRIBUTES) {
            message += "\nNew contents preserved at: ";
            message += wxString(temporary.c_str());
        }
        if (!replacement_backup_path.empty()) {
            message += "\nDisplaced-file backup path: ";
            message += wxString(replacement_backup_path.c_str());
        }
        if (original_backup_link_created && !original_backup_path.empty()) {
            message += "\nOriginal-file recovery hard link: ";
            message += wxString(original_backup_path.c_str());
        }
        if (recovery_link_created && !recovery_path.empty()) {
            message += "\nNew-content recovery link: ";
            message += wxString(recovery_path.c_str());
        }
        if (original_backup_link_error != ERROR_SUCCESS) {
            message += "\nCreateHardLinkW for original backup failed with Win32 error ";
            message += wxString::Format("%lu", static_cast<unsigned long>(original_backup_link_error));
            message += ": ";
            const std::string backup_error_text =
                std::system_category().message(static_cast<int>(original_backup_link_error));
            message += wxString::FromUTF8(backup_error_text.c_str());
        }
        if (recovery_link_creation_failed) {
            message += "\nCreateHardLinkW failed with Win32 error ";
            message += wxString::Format("%lu", static_cast<unsigned long>(recovery_link_error));
            message += ": ";
            message += wxString::FromUTF8(
                std::system_category().message(static_cast<int>(recovery_link_error)));
        }
#else
        struct stat temporary_status{};
        if (::fstatat(save_directory_guard.fd, temporary_leaf.c_str(),
                      &temporary_status, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(temporary_status.st_mode)) {
            message += "\nNew contents preserved at: ";
            message += wxString::FromUTF8(temporary.string());
        }
        if (backup_link_created && !replacement_backup_path.empty()) {
            message += "\nOriginal file recovery link: ";
            message += wxString::FromUTF8(replacement_backup_path.string());
        }
        if (recovery_link_created && !recovery_path.empty()) {
            message += "\nNew-content recovery link: ";
            message += wxString::FromUTF8(recovery_path.string());
        }
        append_quarantine_paths(message);
#endif
        wxMessageBox(message, "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }
    FileIdentity installed_identity;
#ifdef __WXMSW__
    if (!QueryFileIdentity(target, installed_identity, replace_error) ||
#else
    if (!query_save_leaf(target_leaf, installed_identity, replace_error) ||
#endif
        !SameFileIdentity(temporary_identity, installed_identity)) {
        wxString message("The destination no longer refers to the temporary file created by this save. "
                         "The editor remains dirty; inspect the destination before retrying.");
#ifdef __WXMSW__
        message += "\nDestination path: ";
        message += wxString(target.c_str());
        if (!replacement_backup_path.empty()) {
            message += "\nDisplaced-file backup path (preserve; verify identity): ";
            message += wxString(replacement_backup_path.c_str());
        }
        if (original_backup_link_created && !original_backup_path.empty()) {
            message += "\nOriginal-file recovery hard link (preserve): ";
            message += wxString(original_backup_path.c_str());
        }
        if (recovery_link_created && !recovery_path.empty()) {
            message += "\nNew-content recovery link (preserve): ";
            message += wxString(recovery_path.c_str());
        }
#else
        if (backup_link_created && !replacement_backup_path.empty()) {
            message += "\nOriginal file recovery link: ";
            message += wxString::FromUTF8(replacement_backup_path.string());
        }
        if (recovery_link_created && !recovery_path.empty()) {
            message += "\nNew-content recovery link: ";
            message += wxString::FromUTF8(recovery_path.string());
        }
        append_quarantine_paths(message);
#endif
        wxMessageBox(message, "Save identity verification failed", wxOK | wxICON_ERROR, this);
        return false;
    }
#ifdef __WXMSW__
    // Validate both recovery names before deleting either one. If either
    // identity changed, retain both artifacts for manual recovery/inspection.
    if (target_exists) {
        std::error_code backup_error;
        if (!QueryFileIdentity(replacement_backup_path, backup_identity, backup_error) ||
            !SameFileIdentity(original_identity, backup_identity)) {
            backup_cleanup_failed = true;
        }
    }
    if (original_backup_link_created) {
        std::error_code original_backup_error;
        if (!QueryFileIdentity(original_backup_path, original_backup_identity, original_backup_error) ||
            !SameFileIdentity(original_identity, original_backup_identity)) {
            original_backup_cleanup_failed = true;
        }
    }
    if (recovery_link_created) {
        std::error_code recovery_error;
        if (!QueryFileIdentity(recovery_path, recovery_identity, recovery_error) ||
            !SameFileIdentity(temporary_identity, recovery_identity)) {
            recovery_cleanup_failed = true;
        }
    }
    if (backup_cleanup_failed || original_backup_cleanup_failed || recovery_cleanup_failed) {
        // Keep and report the complete recovery set when either identity is
        // unexpected; deleting the other verified name would reduce options.
        backup_cleanup_failed = target_exists;
        original_backup_cleanup_failed = original_backup_link_created;
        recovery_cleanup_failed = recovery_link_created;
    }
    if (!backup_cleanup_failed && !recovery_cleanup_failed) {
        // Remove the new-content recovery link first. If it fails, retain the
        // old-content backup; deleting that first could discard the only copy
        // of the pre-save file while the new recovery link still exists.
        if (recovery_link_created) {
            std::error_code cleanup_error;
            if (!DeleteFileByIdentity(recovery_path, recovery_identity, cleanup_error)) {
                recovery_cleanup_failed = true;
                backup_cleanup_failed = target_exists;
                original_backup_cleanup_failed = original_backup_link_created;
            }
        }
        if (!recovery_cleanup_failed && target_exists) {
            std::error_code cleanup_error;
            if (!DeleteFileByIdentity(replacement_backup_path, backup_identity, cleanup_error)) {
                backup_cleanup_failed = true;
                original_backup_cleanup_failed = original_backup_link_created;
            }
        }
        if (!recovery_cleanup_failed && !backup_cleanup_failed && original_backup_link_created) {
            std::error_code cleanup_error;
            if (!DeleteFileByIdentity(original_backup_path, original_backup_identity, cleanup_error)) {
                original_backup_cleanup_failed = true;
            }
        }
    }
#else
    // Stage 2: persist the installed target before deleting any recovery name.
    if (!save_directory_synced_before_replace || save_directory_guard.fd < 0 ||
        ::fsync(save_directory_guard.fd) != 0) {
        directory_sync_failed = true;
        backup_cleanup_failed = backup_link_created;
        recovery_cleanup_failed = recovery_link_created;
        if (!target_exists) {
            // The target is installed, but the temp name is deliberately kept
            // as the recovery name until this sync succeeds.
            temporary_cleanup_failed = true;
            temporary_recovery_retained = true;
        }
    } else {
        // Validate the complete recovery set before deleting either member.
        bool recovery_set_valid = true;
        if (backup_link_created) {
            FileIdentity backup_identity;
            std::error_code backup_error;
            if (!query_save_leaf(replacement_backup_leaf, backup_identity, backup_error) ||
                !SameFileIdentity(original_identity, backup_identity))
                recovery_set_valid = false;
        }
        if (recovery_link_created) {
            FileIdentity recovery_identity;
            std::error_code recovery_error;
            if (!query_save_leaf(recovery_leaf, recovery_identity, recovery_error) ||
                !SameFileIdentity(temporary_identity, recovery_identity))
                recovery_set_valid = false;
        }
        if (atomic_exchange_used) {
            std::string displaced_bytes;
            FileIdentity displaced_identity;
            std::error_code displaced_error;
            if (!ReadFileAt(save_directory_guard.fd, temporary_leaf,
                    MaxEditableFileBytes, displaced_bytes, displaced_identity, displaced_error) ||
                !SameFileIdentity(original_identity, displaced_identity) ||
                displaced_identity.link_count != original_identity.link_count + 1U ||
                HashBytes(displaced_bytes) != disk_hash_at_check) {
                recovery_set_valid = false;
            }
        }
        if (!recovery_set_valid) {
            // If either name is unexpected, retain the entire recovery set.
            backup_cleanup_failed = backup_link_created;
            recovery_cleanup_failed = recovery_link_created;
        } else {
            // Remove the new-content recovery name first. If that fails,
            // preserve the original-file backup as the last-resort copy.
            if (recovery_link_created) {
                std::error_code recovery_error;
                if (!unlink_save_leaf(recovery_leaf, temporary_identity, recovery_error)) {
                    recovery_cleanup_failed = true;
                } else {
                    recovery_link_created = false;
                }
            }
            if (recovery_cleanup_failed) {
                backup_cleanup_failed = backup_link_created;
            } else if (atomic_exchange_used) {
                // The exchange leaves the displaced original at the old temp
                // name. Remove that duplicate first while the backup hardlink
                // remains available as the final recovery copy.
                std::error_code displaced_error;
                if (!unlink_save_leaf(temporary_leaf, original_identity, displaced_error)) {
                    displaced_cleanup_failed = true;
                    backup_cleanup_failed = backup_link_created;
                }
            }
            if (!recovery_cleanup_failed && !displaced_cleanup_failed && backup_link_created) {
                std::error_code backup_error;
                if (!unlink_save_leaf(replacement_backup_leaf, original_identity, backup_error)) {
                    backup_cleanup_failed = true;
                } else {
                    backup_link_created = false;
                }
            }
            if (!target_exists) {
                std::error_code temporary_error;
                if (!unlink_save_leaf(temporary_leaf, temporary_identity, temporary_error))
                    temporary_cleanup_failed = true;
            }
        }
        // Stage 3: persist removal/quarantine of all cleanup artifacts.
        if (::fsync(save_directory_guard.fd) != 0) directory_sync_failed = true;
    }
#endif
    document.path = target;
    document.is_rbl = wxFileName(wxString(target.wstring())).GetExt().CmpNoCase("rbl") == 0;
    document.saved_hash = HashBytes(output_bytes);
    document.file_identity = installed_identity;
    document.retained_original_backup_links = 0;
    document.editor->SetSavePoint();
    if (metadata_warning) {
        wxMessageBox("The file was saved, but some extended metadata could not be preserved on this "
                     "filesystem or platform. Review permissions and security labels if they are important.",
            "Save metadata warning", wxOK | wxICON_WARNING, this);
    }
    const int index = notebook_->GetPageIndex(page);
    if (index != wxNOT_FOUND) notebook_->SetPageText(static_cast<std::size_t>(index), PageTitle(target));
    UpdateDocumentLimits();
    if (directory_sync_failed) {
        wxMessageBox("The file was replaced, but the containing directory could not be synchronized. "
                     "The save is complete; crash-durability of the rename is uncertain.",
            "Save durability warning", wxOK | wxICON_WARNING, this);
    }
    if (temporary_cleanup_failed) {
#ifdef __WXMSW__
        wxMessageBox("The file was saved, but its temporary file could not be removed. "
                     "Remove the matching temporary file after closing the IDE.",
            "Save cleanup warning", wxOK | wxICON_WARNING, this);
#else
        wxString message = temporary_recovery_retained
            ? wxString("The file was installed, but its temporary recovery name was retained because the post-replacement directory sync failed: ")
            : wxString("The file was saved, but its temporary file could not be safely removed: ");
        message += wxString::FromUTF8(temporary.string());
        append_quarantine_paths(message);
        wxMessageBox(message, "Save cleanup warning", wxOK | wxICON_WARNING, this);
#endif
    }
#ifdef __WXMSW__
    if (backup_cleanup_failed || original_backup_cleanup_failed || recovery_cleanup_failed || recovery_link_unavailable) {
        wxString message("The file was saved and verified, but recovery-artifact cleanup was incomplete.");
        if (backup_cleanup_failed) {
            message += "\nDisplaced-file backup retained at: ";
            message += wxString(replacement_backup_path.c_str());
        }
        if (original_backup_cleanup_failed && !original_backup_path.empty()) {
            message += "\nOriginal-file recovery hard link retained at: ";
            message += wxString(original_backup_path.c_str());
        }
        if (recovery_cleanup_failed && !recovery_path.empty()) {
            message += "\nNew-content recovery link retained at: ";
            message += wxString(recovery_path.c_str());
        }
        if (recovery_link_unavailable) {
            message += "\nThe filesystem did not allow an additional hard-link recovery copy.";
        }
        wxMessageBox(message, "Save recovery warning", wxOK | wxICON_WARNING, this);
    }
#else
    if (backup_cleanup_failed || recovery_cleanup_failed) {
        wxString message("The file was saved and verified, but recovery-artifact cleanup was incomplete.");
        if (backup_cleanup_failed) {
            message += "\nOriginal file recovery link retained at: ";
            message += wxString::FromUTF8(replacement_backup_path.string());
        }
        if (recovery_cleanup_failed) {
            message += "\nNew-content recovery link retained at: ";
            message += wxString::FromUTF8(recovery_path.string());
        }
        append_quarantine_paths(message);
        wxMessageBox(message, "Save recovery warning", wxOK | wxICON_WARNING, this);
    }
    if (displaced_cleanup_failed) {
        wxString message("The file was saved, but the displaced original remains at: ");
        message += wxString::FromUTF8(temporary.string());
        message += "\nThe separate original-file recovery link is also retained at: ";
        message += wxString::FromUTF8(replacement_backup_path.string());
        wxMessageBox(message, "Save recovery warning", wxOK | wxICON_WARNING, this);
    }
#endif
    return true;
}

bool MainFrame::SaveCurrent() {
    auto* page = notebook_->GetCurrentPage();
    return page && SaveDocument(page, false);
}

void MainFrame::CloseCurrent() {
    const int index = notebook_->GetSelection();
    if (index != wxNOT_FOUND) {
        auto* page = notebook_->GetPage(static_cast<std::size_t>(index));
        if (!CanClosePage(page)) return;
        RememberClosedDocument(page);
        if (page == active_snapshot_editor_) active_snapshot_editor_ = nullptr;
        documents_.erase(page);
        notebook_->DeletePage(static_cast<std::size_t>(index));
        UpdateDocumentLimits();
    }
    if (notebook_->GetPageCount() == 0) NewDocument();
}

void MainFrame::RememberClosedDocument(wxWindow* page) {
    const auto found = documents_.find(page);
    if (found == documents_.end() || found->second.path.empty()) return;
    recently_closed_.push_back(found->second.path);
    if (recently_closed_.size() > 20) recently_closed_.erase(recently_closed_.begin());
}

bool MainFrame::CanClosePage(wxWindow* page) {
    const auto found = documents_.find(page);
    if (found == documents_.end() || !found->second.editor->GetModify()) return true;
    const auto answer = wxMessageBox("Save changes before closing this tab?", "Unsaved changes",
        wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) return false;
    return answer != wxYES || SaveDocument(page, false);
}

void MainFrame::AppendOutput(const wxString& text, bool parse_diagnostics) {
    if (parse_diagnostics) ParseDiagnostics(text);
    constexpr long max_output_chars = 4 * 1024 * 1024;
    const long current = output_->GetLastPosition();
    wxString visible = text;
    const long incoming = static_cast<long>(visible.length());
    if (incoming >= max_output_chars) {
        // Keep only the newest tail before touching the native control. This
        // avoids temporarily inserting a huge burst and then deleting most of
        // it, which is particularly expensive for RichEdit on Windows.
        visible = visible.Right(static_cast<std::size_t>(max_output_chars));
        output_->Clear();
    } else if (current + incoming > max_output_chars) {
        const long remove = current + incoming - (max_output_chars * 3 / 4);
        output_->Remove(0, remove);
        output_->SetInsertionPointEnd();
    }
    output_->AppendText(visible);
}

void MainFrame::ParseDiagnostics(const wxString& text) {
    if (diagnostics_stale_) return;
    diagnostic_buffer_ += ToUtf8(text);
    // Output may arrive in large worker batches. Defer parsing and native list
    // updates to short timer turns; complete lines are never discarded here.
    if (!diagnostics_timer_.IsRunning()) diagnostics_timer_.StartOnce(1);
}

void MainFrame::OnDiagnosticsTimer(wxTimerEvent&) {
    if (diagnostics_stale_) {
        diagnostics_timer_.Stop();
        return;
    }
    static const std::regex pattern(R"(^(.+):([0-9]+):([0-9]+): (error|warning)(.*?): (.*)$)");
    constexpr std::size_t ParseBatch = 200;
    constexpr std::size_t MaxDiagnosticLineBytes = 64U * 1024U;
    std::size_t parsed = 0;
    while (parsed < ParseBatch) {
        const auto newline = diagnostic_buffer_.find('\n');
        if (newline == std::string::npos) {
            if (diagnostic_discarding_oversized_line_) {
                if (diagnostic_input_finished_) {
                    diagnostic_buffer_.clear();
                    diagnostic_discarding_oversized_line_ = false;
                }
                break;
            }
            if (diagnostic_buffer_.size() > MaxDiagnosticLineBytes) {
                diagnostic_buffer_.clear();
                diagnostic_discarding_oversized_line_ = true;
                diagnostics_truncated_ = true;
                continue;
            }
            if (diagnostic_input_finished_ && !diagnostic_buffer_.empty()) {
                auto line = std::move(diagnostic_buffer_);
                diagnostic_buffer_.clear();
                while (!line.empty() && line.back() == '\r') line.pop_back();
                std::smatch match;
                if (line.find(':') != std::string::npos && std::regex_match(line, match, pattern)) {
                    if (pending_diagnostics_.size() < 10000) {
                        const bool is_error = match[4].str() == "error";
                        const auto stage = match[5].str();
                        const auto message = stage.empty() ? match[6].str() : stage + ": " + match[6].str();
                        pending_diagnostics_.push_back({FromUtf8(match[1].str()), FromUtf8(match[2].str()),
                            FromUtf8(match[3].str()), FromUtf8(match[4].str()), FromUtf8(message), is_error});
                    } else diagnostics_truncated_ = true;
                }
                ++parsed;
            }
            break;
        }
        if (diagnostic_discarding_oversized_line_) {
            diagnostic_buffer_.erase(0, newline + 1);
            diagnostic_discarding_oversized_line_ = false;
            ++parsed;
            continue;
        }
        if (newline > MaxDiagnosticLineBytes) {
            diagnostic_buffer_.erase(0, newline + 1);
            diagnostics_truncated_ = true;
            ++parsed;
            continue;
        }
        auto line = diagnostic_buffer_.substr(0, newline);
        diagnostic_buffer_.erase(0, newline + 1);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch match;
        if (line.find(':') != std::string::npos && std::regex_match(line, match, pattern)) {
            if (pending_diagnostics_.size() < 10000) {
                const bool is_error = match[4].str() == "error";
                const auto stage = match[5].str();
                const auto message = stage.empty() ? match[6].str() : stage + ": " + match[6].str();
                pending_diagnostics_.push_back({FromUtf8(match[1].str()), FromUtf8(match[2].str()),
                    FromUtf8(match[3].str()), FromUtf8(match[4].str()), FromUtf8(message), is_error});
            } else diagnostics_truncated_ = true;
        }
        ++parsed;
    }

    if (!diagnostic_input_finished_) {
        if (diagnostic_buffer_.find('\n') != std::string::npos) {
            diagnostics_timer_.StartOnce(1);
        }
        return;
    }
    if (!diagnostic_buffer_.empty() || parsed == ParseBatch) {
        diagnostics_timer_.StartOnce(1);
        return;
    }

    if (!diagnostics_sorted_) {
        const auto number = [](const wxString& value) {
            long result = 0;
            value.ToLong(&result);
            return result;
        };
        std::stable_sort(pending_diagnostics_.begin(), pending_diagnostics_.end(),
            [&](const PendingDiagnostic& left, const PendingDiagnostic& right) {
                const int file_order = left.path.CmpNoCase(right.path);
                if (file_order != 0) return file_order < 0;
                const auto left_line = number(left.line), right_line = number(right.line);
                if (left_line != right_line) return left_line < right_line;
                const auto left_column = number(left.column), right_column = number(right.column);
                if (left_column != right_column) return left_column < right_column;
                if (left.is_error != right.is_error) return left.is_error;
                return left.message.CmpNoCase(right.message) < 0;
            });
        diagnostics_sorted_ = true;
    }

    constexpr std::size_t RenderBatch = 50;
    const auto end = std::min(pending_diagnostic_render_index_ + RenderBatch, pending_diagnostics_.size());
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    problems_->Freeze();
    for (; pending_diagnostic_render_index_ < end; ++pending_diagnostic_render_index_) {
        const auto& diagnostic = pending_diagnostics_[pending_diagnostic_render_index_];
        const auto row = problems_->InsertItem(problems_->GetItemCount(), diagnostic.path,
            diagnostic.is_error ? 0 : 1);
        problems_->SetItem(row, 1, diagnostic.line);
        problems_->SetItem(row, 2, diagnostic.column);
        problems_->SetItem(row, 3, diagnostic.severity);
        problems_->SetItem(row, 4, diagnostic.message);
        problems_->SetItemTextColour(row, diagnostic.is_error ? palette.error : palette.warning);
    }
    problems_->Thaw();
    if (pending_diagnostic_render_index_ < pending_diagnostics_.size()) {
        diagnostics_timer_.StartOnce(1);
        return;
    }
    diagnostics_timer_.Stop();
    pending_diagnostics_.clear();
    pending_diagnostic_render_index_ = 0;
    if (diagnostics_truncated_ && !diagnostics_notice_emitted_) {
        diagnostics_notice_emitted_ = true;
        AppendOutput("[problems] diagnostic list capped at 10,000 entries or an incomplete line exceeded 64 KiB\n");
    }
}

void MainFrame::StartWorker() {
    if (application_root_.empty()) {
        AppendOutput("Cannot locate the RBL Studio installation root: " +
            FromUtf8(application_root_error_) + "\n");
        SetStatusText("Application root missing", 2);
        return;
    }
    // The worker inherits this discovery hint, but its authoritative root is
    // the explicit initialize.root field in the protocol below.
    wxSetEnv("RBL_STUDIO_ROOT", wxString(application_root_.wstring()));
    wxSetEnv("RBL_TOOLCHAIN", toolchain_mode_);
    wxSetEnv("RBL_WSL_DISTRO", wsl_distribution_);
    wxString executable;
    if (wxGetEnv("RBL_STUDIO_WORKER", &executable) && !executable.empty()) {
        // Explicit path is useful for development builds and portable layouts.
    } else {
        const auto app = wxFileName(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
        executable = app.GetPathWithSep() + "rbl-studio-worker.exe";
#else
        executable = app.GetPathWithSep() + "rbl-studio-worker";
#endif
    }
    if (!wxFileExists(executable)) {
        AppendOutput("Backend worker not found: " + executable + "\n"
                     "Set RBL_STUDIO_WORKER to its full path.\n");
        return;
    }
    worker_ = new wxProcess(this, ID_WorkerProcess);
    worker_->Redirect();
    worker_pid_ = wxExecute("\"" + executable + "\"", wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, worker_);
    if (worker_pid_ <= 0) {
        delete worker_;
        worker_ = nullptr;
        AppendOutput("Could not start the Rust backend worker.\n");
        return;
    }
    worker_shutdown_kill_sent_ = false;
    worker_shutdown_deadline_ = {};
    const bool timer_started = worker_timer_.Start(25);
    SetStatusText(timer_started ? "Polling worker" : "Worker timer failed", 2);
    const auto root_bytes = application_root_.u8string();
    const auto root = wxString::FromUTF8(reinterpret_cast<const char*>(root_bytes.data()), root_bytes.size());
    SendWorkerRequest("{\"command\":\"initialize\",\"root\":\"" +
                      FromUtf8(JsonEscape(root)) + "\"}");
}

void MainFrame::RestartWorker() {
    if (task_running_) return;
    if (worker_) {
        restart_worker_pending_ = true;
        worker_ready_ = false;
        UpdateToolbarState();
        worker_shutdown_kill_sent_ = false;
        worker_shutdown_deadline_ = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        SendWorkerRequest("{\"command\":\"shutdown\"}");
        return;
    }
    worker_pid_ = -1;
    worker_shutdown_kill_sent_ = false;
    worker_shutdown_deadline_ = {};
    worker_ready_ = false;
    UpdateToolbarState();
    worker_buffer_.clear();
    worker_error_buffer_.clear();
    StartWorker();
}

bool MainFrame::SendWorkerLine(const wxString& line) {
    if (!worker_) return false;
    auto* stream = worker_->GetOutputStream();
    if (!stream || !stream->IsOk()) return false;
    const auto bytes = ToUtf8(line + "\n");
    // Redirected process pipes can accept fewer bytes than requested when the
    // child has not drained its input pipe yet. A single large unsaved-buffer
    // snapshot (up to 16 MiB plus JSON escaping) must therefore be written in
    // bounded chunks and retried until the complete NDJSON frame is delivered.
    constexpr std::size_t WriteChunkBytes = 4 * 1024;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min(WriteChunkBytes, bytes.size() - offset);
        stream->Write(bytes.data() + offset, count);
        const auto written = stream->LastWrite();
        if (!stream->IsOk() || written == 0 || written > count) {
            AppendOutput(wxString::Format(
                "[backend] worker input pipe write failed at %llu/%llu bytes (last write %llu)\n",
                static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(bytes.size()),
                static_cast<unsigned long long>(written)));
            return false;
        }
        offset += written;
    }
    stream->Sync();
    return stream->IsOk();
}

bool MainFrame::SendWorkerRequest(const wxString& json) {
    return SendWorkerLine(json);
}

void MainFrame::StartTask(const wxString& task) {
    auto_check_timer_.Stop();
    auto_check_editor_ = nullptr;
    if (!worker_ || !worker_ready_) {
        AppendOutput("Backend worker is not ready.\n");
        return;
    }
    if (task_running_) {
        AppendOutput("A task is already running.\n");
        return;
    }
    diagnostic_buffer_.clear();
    diagnostics_timer_.Stop();
    pending_diagnostics_.clear();
    pending_diagnostic_render_index_ = 0;
    diagnostics_stale_ = false;
    diagnostic_input_finished_ = false;
    diagnostic_discarding_oversized_line_ = false;
    diagnostics_sorted_ = false;
    diagnostics_notice_emitted_ = false;
    pending_stdin_.clear();
    pending_stdin_active_ = false;
    stdin_eof_pending_ = false;
    active_task_accepts_stdin_ = false;
    if (task == "check") {
        problems_->DeleteAllItems();
        diagnostics_truncated_ = false;
        output_tabs_->SetSelection(1);
    } else {
        output_tabs_->SetSelection(0);
    }
    auto* editor = CurrentEditor();
    if (!editor) return;
    const auto found = documents_.find(editor);
    if (found == documents_.end()) return;
    const auto& document = found->second;
    if (editor_auto_save_ && (task == "run" || task == "build" || task == "assembly") &&
        (document.path.empty() || editor->GetModify())) {
        if (!SaveCurrent()) {
            AppendOutput("Save cancelled; operation not started.\n");
            return;
        }
    }
    const bool is_rbl = document.is_rbl;
    const bool compiler_task = task == "run" || task == "build" || task == "check" || task == "assembly";
    if (compiler_task) {
        active_snapshot_path_.clear();
        active_source_path_.clear();
        active_snapshot_editor_ = editor;
        problems_->DeleteAllItems();
        diagnostics_truncated_ = false;
    }
    if (compiler_task && !is_rbl) {
        AppendOutput("Compiler actions accept only .rbl source files.\n");
        return;
    }
    active_request_id_ = next_request_id_++;
    task_started_ = false;
    std::filesystem::path snapshot_file_path;
    if (compiler_task) {
        const auto snapshot = ToUtf8(editor->GetText());
        if (snapshot.size() > MaxEditableFileBytes) {
            AppendOutput("Source exceeds the 16 MiB editor limit.\n");
            active_request_id_ = 0;
            return;
        }
        const auto snapshot_directory = document.path.empty()
            ? workspace_root_ / "build" / "ide-check"
            : document.path.parent_path();
        std::error_code directory_error;
        std::filesystem::create_directories(snapshot_directory, directory_error);
        if (directory_error) {
            AppendOutput("Could not create the source snapshot directory: " +
                FromUtf8(directory_error.message()) + "\n");
            active_request_id_ = 0;
            return;
        }
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        snapshot_file_path = snapshot_directory /
            (".rblstudio-snapshot-" + std::to_string(active_request_id_) + "-" +
             std::to_string(unique) + ".rbl");
        std::ofstream snapshot_output(snapshot_file_path, std::ios::binary | std::ios::trunc);
        if (snapshot_output) {
            snapshot_output.write(snapshot.data(), static_cast<std::streamsize>(snapshot.size()));
            snapshot_output.flush();
        }
        if (!snapshot_output) {
            std::error_code ignored;
            std::filesystem::remove(snapshot_file_path, ignored);
            AppendOutput("Could not write the temporary source snapshot.\n");
            active_request_id_ = 0;
            return;
        }
    }
    const auto workspace_bytes = workspace_root_.u8string();
    const auto workspace = wxString::FromUTF8(
        reinterpret_cast<const char*>(workspace_bytes.data()), workspace_bytes.size());
    wxString request = "{\"command\":\"start\",\"request_id\":" +
        wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + ",\"task\":\"" + task +
        "\",\"workspace_root\":\"" + FromUtf8(JsonEscape(workspace)) +
        "\",\"is_rbl\":" + wxString(compiler_task ? "true" : "false") + ",\"source_path\":";
    request += !compiler_task || document.path.empty() ? "null" : "\"" + FromUtf8(JsonEscape(
        wxString(document.path.wstring()))) + "\"";
    request += compiler_task
        ? ",\"snapshot_file\":\"" + FromUtf8(JsonEscape(wxString(snapshot_file_path.wstring()))) +
            "\",\"snapshot\":null}"
        : ",\"snapshot_file\":null,\"snapshot\":null}";
    AppendOutput("\n> " + task + "\n");
    if (!SendWorkerRequest(request)) {
        if (!snapshot_file_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(snapshot_file_path, ignored);
        }
        active_request_id_ = 0;
        AppendOutput("[backend] failed to send task request\n");
        SetStatusText("Backend error", 2);
        RestartWorker();
        return;
    }
    task_running_ = true;
    UpdateToolbarState();
    SetStatusText("Starting " + task, 2);
    active_task_accepts_stdin_ = task == "run";
    input_->Enable(active_task_accepts_stdin_);
    send_button_->Enable(active_task_accepts_stdin_);
    eof_button_->Enable(active_task_accepts_stdin_);
}

void MainFrame::PollWorker() {
    if (!worker_) return;
    auto* stream = worker_->GetInputStream();
    // The worker forwards output in 8 KiB protocol frames. Reading only one
    // 8 KiB buffer per timer turn can cap a busy GUI near 320 KiB/s and trip
    // the worker's bounded stdout backpressure deadline. A 256 KiB read drains
    // several frames per turn without introducing unbounded buffering.
    char buffer[256 * 1024];
    constexpr std::size_t max_worker_frame = 256 * 1024;
    // A worker can produce hundreds of 8 KiB output events in one burst. Do
    // not force the native RichEdit control to reflow after every frame: batch
    // consecutive output events while preserving the order of control events.
    constexpr std::size_t max_output_batch_chars = 1024 * 1024;
    wxString output_batch;
    bool output_batch_parse_diagnostics = false;
    bool output_batch_has_channel = false;
    auto flush_output_batch = [&]() {
        if (!output_batch.empty()) AppendOutput(output_batch, output_batch_parse_diagnostics);
        output_batch.clear();
        output_batch_has_channel = false;
    };
    if (stream) {
        while (stream->CanRead()) {
            stream->Read(buffer, sizeof(buffer));
            const auto count = stream->LastRead();
            if (count == 0) break;
            worker_buffer_.append(buffer, count);
        }
    }
    while (true) {
        const auto newline = worker_buffer_.find('\n');
        if (newline == std::string::npos) break;
        if (newline > max_worker_frame) {
            flush_output_batch();
            AppendOutput("[backend] oversized worker protocol frame; restarting worker\n");
            worker_buffer_.clear();
            if (active_request_id_ != 0) {
                SendWorkerRequest("{\"command\":\"cancel\",\"request_id\":" +
                    wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + "}");
            }
            task_running_ = false;
            RestartWorker();
            return;
        }
        const auto line = worker_buffer_.substr(0, newline);
        worker_buffer_.erase(0, newline + 1);
        if (!line.empty()) {
            const auto event_line = wxString::FromUTF8(line.data(), line.size());
            if (JsonStringField(event_line, "event") == "output") {
                const bool parse_diagnostics = JsonBoolField(event_line, "stderr", false);
                if (output_batch_has_channel &&
                    parse_diagnostics != output_batch_parse_diagnostics) {
                    flush_output_batch();
                }
                if (!output_batch_has_channel) {
                    output_batch_parse_diagnostics = parse_diagnostics;
                    output_batch_has_channel = true;
                }
                output_batch += JsonStringField(event_line, "text");
                if (output_batch.length() >= max_output_batch_chars) flush_output_batch();
            } else {
                flush_output_batch();
                HandleWorkerEvent(event_line);
            }
        }
    }
    if (worker_buffer_.size() > max_worker_frame) {
        flush_output_batch();
        AppendOutput("[backend] oversized or unterminated protocol frame; restarting worker\n");
        worker_buffer_.clear();
        SendWorkerRequest("{\"command\":\"cancel\",\"request_id\":" +
            wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + "}");
        task_running_ = false;
        RestartWorker();
    }
    flush_output_batch();

    // The worker keeps protocol frames on stdout and sends operational errors
    // to stderr. Drain both pipes so diagnostics cannot fill the redirected
    // stderr pipe and block the worker during a failure path.
    auto* error_stream = worker_->GetErrorStream();
    if (error_stream) {
        while (error_stream->CanRead()) {
            error_stream->Read(buffer, sizeof(buffer));
            const auto count = error_stream->LastRead();
            if (count == 0) break;
            worker_error_buffer_.append(buffer, count);
        }
        constexpr std::size_t max_error_frame = 64 * 1024;
        if (worker_error_buffer_.size() > max_error_frame) {
            const auto newline = worker_error_buffer_.find('\n', worker_error_buffer_.size() - max_error_frame);
            if (newline == std::string::npos) worker_error_buffer_.clear();
            else worker_error_buffer_.erase(0, newline + 1);
        }
        while (true) {
            const auto newline = worker_error_buffer_.find('\n');
            if (newline == std::string::npos) break;
            const auto line = worker_error_buffer_.substr(0, newline);
            worker_error_buffer_.erase(0, newline + 1);
            AppendOutput("[worker stderr] " + FromUtf8(line) + "\n");
        }
    }
    if (restart_worker_pending_ && !worker_shutdown_kill_sent_ &&
        worker_pid_ > 0 && wxProcess::Exists(worker_pid_) &&
        worker_shutdown_deadline_ != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() >= worker_shutdown_deadline_) {
        worker_shutdown_kill_sent_ = true;
        AppendOutput("[backend] worker did not stop after restart request; forcing shutdown\n");
        wxProcess::Kill(worker_pid_, wxSIGKILL, wxKILL_CHILDREN);
    }
}

void MainFrame::HandleWorkerEvent(const wxString& line) {
    const auto event = JsonStringField(line, "event");
    if (event == "ready") {
        worker_ready_ = true;
        UpdateToolbarState();
        SetStatusText("Ready", 2);
        AppendOutput("RBL backend connected.\n");
    } else if (event == "output") {
        AppendOutput(JsonStringField(line, "text"), JsonBoolField(line, "stderr", false));
    } else if (event == "snapshot_created") {
        active_snapshot_path_ = std::filesystem::path(JsonStringField(line, "temporary_path").ToStdWstring());
        active_source_path_ = std::filesystem::path(JsonStringField(line, "original_path").ToStdWstring());
    } else if (event == "started") {
        task_started_ = true;
        task_running_ = true;
        UpdateToolbarState();
        SetStatusText("Running", 2);
        input_->Enable(active_task_accepts_stdin_);
        send_button_->Enable(active_task_accepts_stdin_);
        eof_button_->Enable(active_task_accepts_stdin_);
        AppendOutput("[" + JsonStringField(line, "label") + "] started\n");
    } else if (event == "stdin_accepted") {
        pending_stdin_.clear();
        pending_stdin_active_ = false;
        if (!stdin_eof_pending_ && active_task_accepts_stdin_) {
            input_->Enable();
            send_button_->Enable();
        }
    } else if (event == "stdin_eof_accepted") {
        stdin_eof_pending_ = false;
        input_->Disable();
        send_button_->Disable();
        eof_button_->Disable();
    } else if (event == "finished") {
        task_running_ = false;
        task_started_ = false;
        UpdateToolbarState();
        active_task_accepts_stdin_ = false;
        active_request_id_ = 0;
        pending_stdin_.clear();
        pending_stdin_active_ = false;
        stdin_eof_pending_ = false;
        input_->Disable();
        send_button_->Disable();
        eof_button_->Disable();
        const auto exit_code = JsonNumberField(line, "exit_code");
        SetStatusText(exit_code == "0" ? "Finished" : "Exit code " + exit_code, 2);
        AppendOutput("\n[process exited: " + exit_code + "]\n");
        diagnostic_input_finished_ = true;
        if (!diagnostics_stale_) diagnostics_timer_.StartOnce(1);
        else AppendOutput("[problems] diagnostics were discarded because the source changed during the task\n");
        if (close_after_task_) CallAfter([this] { Close(); });
    } else if (event == "error" || event == "protocol_error") {
        if (JsonNumberField(line, "request_id") == wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_))) {
            const auto message = JsonStringField(line, "message");
            if (pending_stdin_active_) {
                if (input_->IsEmpty()) input_->SetValue(pending_stdin_);
                pending_stdin_.clear();
                pending_stdin_active_ = false;
                if (task_running_ && active_task_accepts_stdin_ && !stdin_eof_pending_) {
                    input_->Enable();
                    send_button_->Enable();
                }
            }
            if (stdin_eof_pending_) {
                stdin_eof_pending_ = false;
                if (task_running_ && active_task_accepts_stdin_) {
                    input_->Enable();
                    send_button_->Enable();
                    eof_button_->Enable();
                }
            }
            if (!task_started_) {
                task_running_ = false;
                UpdateToolbarState();
                active_request_id_ = 0;
                input_->Disable();
                send_button_->Disable();
                eof_button_->Disable();
            }
        }
        AppendOutput("[backend] " + JsonStringField(line, "message") + "\n");
    } else if (event == "worker_started") {
        AppendOutput("Starting Rust process supervisor...\n");
    }
    if (worker_restart_after_task_ && !task_running_ && !close_after_task_) {
        worker_restart_after_task_ = false;
        CallAfter([this] { RestartWorker(); });
    }
}

void MainFrame::SendStdin() {
    if (!worker_ || !worker_ready_ || !task_running_ || !active_task_accepts_stdin_ || !input_ || pending_stdin_active_ || stdin_eof_pending_) return;
    const auto line = input_->GetValue();
    const auto request = "{\"command\":\"stdin_line\",\"request_id\":" +
        wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) +
        ",\"line\":\"" + FromUtf8(JsonEscape(line)) + "\"}";
    if (!SendWorkerRequest(request)) {
        AppendOutput("[backend] failed to send stdin line; input was retained\n");
        return;
    }
    pending_stdin_ = line;
    pending_stdin_active_ = true;
    input_->Clear();
    input_->Disable();
    send_button_->Disable();
}

void MainFrame::SendStdinEof() {
    if (!worker_ || !worker_ready_ || !task_running_ || !active_task_accepts_stdin_ || stdin_eof_pending_) return;
    if (!pending_stdin_active_ && !input_->GetValue().empty()) {
        SendStdin();
        if (!pending_stdin_active_) return;
    }
    const auto request = "{\"command\":\"stdin_eof\",\"request_id\":" +
        wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + "}";
    if (!SendWorkerRequest(request)) {
        AppendOutput("[backend] failed to send stdin EOF\n");
        return;
    }
    stdin_eof_pending_ = true;
    input_->Disable();
    send_button_->Disable();
    eof_button_->Disable();
    AppendOutput("[stdin EOF requested]\n");
}

wxStyledTextCtrl* MainFrame::CurrentEditor() const {
    return dynamic_cast<wxStyledTextCtrl*>(notebook_->GetCurrentPage());
}

void MainFrame::OnNew(wxCommandEvent&) { NewDocument(); }
void MainFrame::OnNewText(wxCommandEvent&) { NewTextDocument(); }
void MainFrame::OnOpen(wxCommandEvent&) { OpenDocument(); }
void MainFrame::OnOpenProject(wxCommandEvent&) {
    if (search_running_) {
        wxMessageBox("Wait for the project search to finish or cancel it before changing folders.",
            "Project search is running", wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (task_running_) {
        wxMessageBox("Stop the active task before changing the project folder.",
            "Task is running", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxDirDialog dialog(this, "Open project folder", wxString(workspace_root_.wstring()),
        wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    std::error_code error;
    auto selected = std::filesystem::weakly_canonical(
        std::filesystem::path(dialog.GetPath().ToStdWstring()), error);
    if (error) selected = std::filesystem::path(dialog.GetPath().ToStdWstring());
    workspace_root_ = std::move(selected);
    project_->DeleteAllItems();
    const auto label = PathLabel(workspace_root_);
    const auto root = project_->AddRoot(label, 0, 0, new ProjectPathData(workspace_root_));
    project_->SetItemBold(root, true);
    project_->SetItemTextColour(root, wxColour(105, 164, 235));
    project_->AppendItem(root, "...", 1, 1);
    PopulateProjectNode(root);
    project_->Expand(root);
    SetStatusText(PathLabel(workspace_root_), 1);
    AppendOutput("Project folder: " + dialog.GetPath() + "\n");
    SaveUiState();
}
void MainFrame::OnSave(wxCommandEvent&) { SaveCurrent(); }
void MainFrame::OnSaveAs(wxCommandEvent&) {
    if (auto* page = notebook_->GetCurrentPage()) SaveDocument(page, true);
}
void MainFrame::OnExit(wxCommandEvent&) { Close(true); }
void MainFrame::OnRun(wxCommandEvent&) { StartTask("run"); }
void MainFrame::OnCheck(wxCommandEvent&) { StartTask("check"); }
void MainFrame::OnBuild(wxCommandEvent&) { StartTask("build"); }
void MainFrame::OnAssembly(wxCommandEvent&) { StartTask("assembly"); }
void MainFrame::OnTests(wxCommandEvent&) { StartTask("tests"); }
void MainFrame::OnBenchmarks(wxCommandEvent&) { StartTask("benchmarks"); }
void MainFrame::OnDoctor(wxCommandEvent&) { StartTask("doctor"); }
void MainFrame::OnStop(wxCommandEvent&) {
    if (!task_running_) return;
    SetStatusText("Stopping...", 2);
    const auto request = "{\"command\":\"cancel\",\"request_id\":" +
        wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + "}";
    if (!SendWorkerRequest(request)) {
        AppendOutput("[backend] failed to send cancellation request\n");
        return;
    }
    // Do not accept new stdin after cancellation has been queued. The worker
    // may still acknowledge an already in-flight line, but its process writer
    // is stopping and cannot guarantee delivery to the child.
    active_task_accepts_stdin_ = false;
    pending_stdin_.clear();
    pending_stdin_active_ = false;
    stdin_eof_pending_ = false;
    input_->Disable();
    send_button_->Disable();
    eof_button_->Disable();
}
void MainFrame::OnSendStdin(wxCommandEvent&) { SendStdin(); }
void MainFrame::OnStdinEof(wxCommandEvent&) { SendStdinEof(); }
void MainFrame::OnWorkerTimer(wxTimerEvent&) {
    PollWorker();
}
void MainFrame::OnWorkerExit(wxProcessEvent& event) {
    // The process-termination event can arrive before the 25 ms polling timer
    // gets another turn. Drain both redirected pipes while wxProcess still
    // owns them so a final `finished` frame or worker stderr diagnostic is not
    // silently discarded when the process exits between polls.
    PollWorker();
    if (active_request_id_ != 0) {
        // A crashed or forcibly terminated worker may not send the protocol's
        // `finished` event. Finalize the stderr stream after draining the pipe
        // so complete diagnostics and a final unterminated line are rendered.
        diagnostic_input_finished_ = true;
        if (!diagnostics_stale_) diagnostics_timer_.StartOnce(1);
        AppendOutput("[backend] worker exited before sending task completion; received diagnostics were finalized\n");
    }
    worker_ready_ = false;
    task_running_ = false;
    task_started_ = false;
    active_task_accepts_stdin_ = false;
    active_request_id_ = 0;
    pending_stdin_.clear();
    pending_stdin_active_ = false;
    stdin_eof_pending_ = false;
    input_->Disable();
    send_button_->Disable();
    eof_button_->Disable();
    worker_pid_ = -1;
    worker_shutdown_kill_sent_ = false;
    worker_shutdown_deadline_ = {};
    if (worker_) { delete worker_; worker_ = nullptr; }
    UpdateToolbarState();
    worker_timer_.Stop();
    if (!worker_error_buffer_.empty()) {
        AppendOutput("[worker stderr] " + FromUtf8(worker_error_buffer_) + "\n");
        worker_error_buffer_.clear();
    }
    SetStatusText("Backend offline", 2);
    AppendOutput("Backend worker exited (" + wxString::Format("%d", event.GetExitCode()) + ").\n");
    if (close_after_task_) CallAfter([this] { Close(); });
    if (restart_worker_pending_) {
        restart_worker_pending_ = false;
        StartWorker();
    }
}

void MainFrame::OnFrameClose(wxCloseEvent& event) {
    if (active_request_id_ != 0 && worker_) {
        const auto request = "{\"command\":\"cancel\",\"request_id\":" +
            wxString::Format("%llu", static_cast<unsigned long long>(active_request_id_)) + "}";
        if (SendWorkerRequest(request)) {
            close_after_task_ = true;
            AppendOutput("[backend] cancelling the active task before closing the IDE...\n");
            event.Veto();
            return;
        }
        AppendOutput("[backend] cancellation could not be sent; keeping the IDE open until the active task is known to have stopped\n");
        SetStatusText("Waiting for backend", 2);
        event.Veto();
        return;
    }
    if (search_running_) {
        // Keep the frame and its event target alive until the search worker has
        // observed cancellation and posted its completion event. Joining from
        // the destructor would otherwise freeze the close path while a file
        // read or directory operation is still in progress.
        search_cancel_.store(true, std::memory_order_relaxed);
        close_after_search_ = true;
        AppendOutput("[search] cancelling project search before closing the IDE...\n");
        event.Veto();
        return;
    }
    search_results_timer_.Stop();
    pending_search_results_.clear();
    pending_search_result_index_ = 0;
    for (std::size_t index = 0; index < notebook_->GetPageCount(); ++index) {
        if (!CanClosePage(notebook_->GetPage(index))) {
            event.Veto();
            return;
        }
    }
    event.Skip();
}

void MainFrame::OnFind(wxCommandEvent&) { ShowFindPanel(false); }

void MainFrame::OnReplace(wxCommandEvent&) { ShowFindPanel(true); }

void MainFrame::OnFindNext(wxCommandEvent&) {
    if (!manager_.GetPane("find-pane").IsShown()) ShowFindPanel(false);
    else FindInCurrentEditor(false);
}

void MainFrame::OnFindPrevious(wxCommandEvent&) {
    if (!manager_.GetPane("find-pane").IsShown()) ShowFindPanel(false);
    else FindInCurrentEditor(true);
}

void MainFrame::ShowFindPanel(bool show_replace) {
    if (!CurrentEditor()) return;
    if (find_query_->GetValue().empty()) {
        const auto selected = CurrentEditor()->GetSelectedText();
        if (!selected.empty()) find_query_->SetValue(selected);
    }
    manager_.GetPane("find-pane").Show();
    manager_.Update();
    auto* target = show_replace ? replace_query_ : find_query_;
    target->SetFocus();
    target->SetSelection(-1, -1);
}

void MainFrame::FindInCurrentEditor(bool backwards) {
    auto* editor = CurrentEditor();
    if (!editor) return;
    const auto query = find_query_->GetValue();
    if (query.empty()) {
        find_status_->SetLabel("Enter text to find");
        find_query_->SetFocus();
        return;
    }
    editor->SetSearchFlags(find_match_case_->GetValue() ? wxSTC_FIND_MATCHCASE : 0);
    const int length = editor->GetTextLength();
    const int selection_start = editor->GetSelectionStart();
    const int selection_end = editor->GetSelectionEnd();
    const int current = editor->GetSelectionEmpty() ? editor->GetCurrentPos()
        : (backwards ? selection_start : selection_end);
    editor->SetTargetStart(backwards ? current : current);
    editor->SetTargetEnd(backwards ? 0 : length);
    int found = editor->SearchInTarget(query);
    if (found < 0) {
        editor->SetTargetStart(backwards ? length : 0);
        editor->SetTargetEnd(backwards ? current : length);
        found = editor->SearchInTarget(query);
    }
    if (found >= 0) {
        const int match_start = editor->GetTargetStart();
        const int match_end = editor->GetTargetEnd();
        editor->SetSelection(match_start, match_end);
        editor->EnsureCaretVisible();
        const int line = editor->LineFromPosition(match_start) + 1;
        const int column = editor->GetColumn(match_start) + 1;
        find_status_->SetLabel(wxString::Format("Line %d, Col %d", line, column));
    } else {
        find_status_->SetLabel("No matches");
    }
}

void MainFrame::ReplaceInCurrentEditor(bool all) {
    auto* editor = CurrentEditor();
    if (!editor) return;
    const auto query = find_query_->GetValue();
    if (query.empty()) {
        find_status_->SetLabel("Enter text to find");
        find_query_->SetFocus();
        return;
    }
    editor->SetSearchFlags(find_match_case_->GetValue() ? wxSTC_FIND_MATCHCASE : 0);
    const auto replacement = replace_query_->GetValue();
    int replaced = 0;
    editor->BeginUndoAction();
    if (all) {
        editor->SetTargetStart(0);
        editor->SetTargetEnd(editor->GetTextLength());
        while (editor->SearchInTarget(query) >= 0) {
            editor->ReplaceTarget(replacement);
            ++replaced;
            editor->SetTargetStart(editor->GetTargetEnd());
            editor->SetTargetEnd(editor->GetTextLength());
        }
    } else {
        const int start = editor->GetSelectionEmpty()
            ? editor->GetCurrentPos() : editor->GetSelectionStart();
        const int end = editor->GetSelectionEmpty()
            ? editor->GetTextLength() : editor->GetSelectionEnd();
        editor->SetTargetStart(start);
        editor->SetTargetEnd(end);
        if (editor->SearchInTarget(query) >= 0) {
            editor->ReplaceTarget(replacement);
            ++replaced;
        }
    }
    editor->EndUndoAction();
    find_status_->SetLabel(all
        ? wxString::Format("Replaced %d occurrence(s)", replaced)
        : wxString(replaced ? "Replaced 1 occurrence" : "No match in selection/range"));
    if (replaced && !all) FindInCurrentEditor(false);
}
void MainFrame::OnCloseCurrentMenu(wxCommandEvent&) { CloseCurrent(); }
void MainFrame::OnCloseTab(wxAuiNotebookEvent& event) {
    auto* page = notebook_->GetPage(event.GetSelection());
    if (!CanClosePage(page)) { event.Veto(); return; }
    RememberClosedDocument(page);
    if (page == active_snapshot_editor_) active_snapshot_editor_ = nullptr;
    documents_.erase(page);
    UpdateDocumentLimits();
}
void MainFrame::OnPageChanged(wxAuiNotebookEvent& event) {
    auto_check_timer_.Stop();
    auto_check_editor_ = nullptr;
    event.Skip();
    if (auto* editor = CurrentEditor()) {
        editor->SetFocus();
        SetStatusText(wxString::Format("Ln %d, Col %d", editor->GetCurrentLine() + 1,
            editor->GetColumn(editor->GetCurrentPos()) + 1), 3);
    }
    UpdateToolbarState();
}

void MainFrame::OnProjectFileActivated(wxTreeEvent& event) {
    auto* data = dynamic_cast<ProjectPathData*>(project_->GetItemData(event.GetItem()));
#ifdef _WIN32
    std::error_code status_error;
    if (data && std::filesystem::is_regular_file(data->path, status_error) && !status_error) {
        const auto path = data->path.u8string();
        OpenDocumentPath(wxString::FromUTF8(reinterpret_cast<const char*>(path.data()), path.size()));
    }
#else
    if (data && data->root && data->expected_identity.valid) {
        const auto path = data->path.u8string();
        OpenDocumentPath(
            wxString::FromUTF8(reinterpret_cast<const char*>(path.data()), path.size()),
            &data->expected_identity);
    }
#endif
}

void MainFrame::OnProjectItemExpanding(wxTreeEvent& event) {
    PopulateProjectNode(event.GetItem());
    project_->SetItemImage(event.GetItem(), 0, wxTreeItemIcon_Expanded);
    event.Skip();
}

void MainFrame::OnProjectItemCollapsed(wxTreeEvent& event) {
    project_->SetItemImage(event.GetItem(), 1, wxTreeItemIcon_Normal);
    event.Skip();
}

void MainFrame::OnDiagnosticActivated(wxListEvent& event) {
    const auto row = event.GetIndex();
    if (row < 0) return;
    auto path_text = problems_->GetItemText(row, 0);
    auto path = std::filesystem::path(path_text.ToStdWstring());
    const bool is_snapshot = !active_snapshot_path_.empty() && path == active_snapshot_path_;
    if (is_snapshot && active_source_path_.empty()) {
        if (!active_snapshot_editor_) {
            AppendOutput("[problems] the unsaved source tab for this diagnostic has been closed\n");
            return;
        }
        const int page_index = notebook_->GetPageIndex(active_snapshot_editor_);
        if (page_index == wxNOT_FOUND) {
            active_snapshot_editor_ = nullptr;
            AppendOutput("[problems] the unsaved source tab for this diagnostic has been closed\n");
            return;
        }
        notebook_->SetSelection(static_cast<std::size_t>(page_index));
    } else {
        if (is_snapshot && !active_source_path_.empty()) path = active_source_path_;
        const auto path_bytes = path.u8string();
        OpenDocumentPath(wxString::FromUTF8(reinterpret_cast<const char*>(path_bytes.data()), path_bytes.size()));
        auto* selected = CurrentEditor();
        const auto selected_document = selected ? documents_.find(selected) : documents_.end();
        std::error_code expected_error, selected_error;
        const auto expected_path = std::filesystem::weakly_canonical(path, expected_error);
        const auto selected_path = selected_document != documents_.end()
            ? std::filesystem::weakly_canonical(selected_document->second.path, selected_error)
            : std::filesystem::path{};
        if (selected_document == documents_.end() ||
            (expected_error ? path : expected_path) !=
                (selected_error ? selected_document->second.path : selected_path)) {
            AppendOutput("[problems] could not open the diagnostic source document\n");
            return;
        }
    }
    auto* editor = CurrentEditor();
    if (!editor) return;
    long line = 1, column = 1;
    problems_->GetItemText(row, 1).ToLong(&line);
    problems_->GetItemText(row, 2).ToLong(&column);
    const int target_line = std::clamp(static_cast<int>(line) - 1, 0, editor->GetLineCount() - 1);
    const int target_column = std::max(0, static_cast<int>(column) - 1);
    const auto position = editor->FindColumn(target_line, target_column);
    editor->GotoPos(position);
    editor->SetSelection(position, std::min(position + 1, editor->GetTextLength()));
    editor->EnsureCaretVisible();
    notebook_->SetFocus();
}

void MainFrame::OnSettings(wxCommandEvent&) {
    wxDialog dialog(this, wxID_ANY, "RBL Studio Settings", wxDefaultPosition, wxSize(FromDIP(680), FromDIP(520)),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    const auto dialog_palette = rbl::wxui::PaletteForTheme(ui_theme_);
    const int pad = FromDIP(12);
    const int label_gap = FromDIP(14);
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* pages = new wxAuiNotebook(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxAUI_NB_TOP | wxAUI_NB_SCROLL_BUTTONS);
    pages->SetArtProvider(new ThemeTabArt(dialog_palette, compact_mode_));
    pages->SetTabCtrlHeight(FromDIP(compact_mode_ ? 26 : 28));
    pages->SetBackgroundColour(dialog_palette.surface);
    pages->SetForegroundColour(dialog_palette.text);
    pages->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
    auto* appearance_page = new wxPanel(pages);
    auto* editor_page = new wxPanel(pages);
    auto* behavior_page = new wxPanel(pages);
    auto* toolchain_page = new wxPanel(pages);
    pages->AddPage(appearance_page, "Appearance", true);
    pages->AddPage(editor_page, "Editor");
    pages->AddPage(behavior_page, "Behavior");
    pages->AddPage(toolchain_page, "Toolchain");
    root->Add(pages, 1, wxEXPAND | wxALL, pad);

    auto* appearance_layout = new wxBoxSizer(wxVERTICAL);
    auto* appearance_group = new wxStaticBoxSizer(wxVERTICAL, appearance_page, "Interface");
    auto* theme_row = new wxBoxSizer(wxHORIZONTAL);
    theme_row->Add(new wxStaticText(appearance_page, wxID_ANY, "UI theme"),
        1, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    auto* theme = new wxChoice(appearance_page, wxID_ANY);
    for (const auto& name : {"RBL Dark", "RBL Light", "Nord", "Monokai", "High Contrast"})
        theme->Append(name);
    theme->SetStringSelection(ui_theme_);
    if (theme->GetSelection() == wxNOT_FOUND) theme->SetSelection(0);
    theme_row->Add(theme, 0, wxALIGN_CENTER_VERTICAL);
    appearance_group->Add(theme_row, 0, wxEXPAND | wxALL, pad);
    auto* compact = new wxCheckBox(appearance_page, wxID_ANY,
        "Compact mode (smaller toolbar, controls and spacing)");
    compact->SetValue(compact_mode_);
    appearance_group->Add(compact, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    appearance_layout->Add(appearance_group, 0, wxEXPAND | wxALL, pad);
    appearance_layout->Add(new wxStaticText(appearance_page, wxID_ANY,
        "UI theme controls application surfaces. Editor color scheme controls code syntax colors."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    appearance_page->SetSizer(appearance_layout);

    auto* editor_layout = new wxBoxSizer(wxVERTICAL);
    auto* editor_group = new wxStaticBoxSizer(wxVERTICAL, editor_page, "Editor appearance");
    auto* scheme_row = new wxBoxSizer(wxHORIZONTAL);
    scheme_row->Add(new wxStaticText(editor_page, wxID_ANY, "Color scheme"),
        1, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    auto* color_scheme = new wxChoice(editor_page, wxID_ANY);
    for (const auto& name : {"RBL Dark", "RBL Light", "Nord", "Monokai", "High Contrast"})
        color_scheme->Append(name);
    color_scheme->SetStringSelection(editor_color_scheme_);
    if (color_scheme->GetSelection() == wxNOT_FOUND) color_scheme->SetSelection(0);
    scheme_row->Add(color_scheme, 0, wxALIGN_CENTER_VERTICAL);
    editor_group->Add(scheme_row, 0, wxEXPAND | wxALL, pad);
    auto* font_row = new wxBoxSizer(wxHORIZONTAL);
    font_row->Add(new wxStaticText(editor_page, wxID_ANY, "Font size"),
        1, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    auto* font_size = new wxSpinCtrl(editor_page, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxSP_ARROW_KEYS, 9, 24, editor_font_size_);
    font_row->Add(font_size, 0);
    editor_group->Add(font_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    auto* tab_row = new wxBoxSizer(wxHORIZONTAL);
    tab_row->Add(new wxStaticText(editor_page, wxID_ANY, "Tab width"),
        1, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    auto* tab_size = new wxSpinCtrl(editor_page, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxSP_ARROW_KEYS, 2, 8, editor_tab_size_);
    tab_row->Add(tab_size, 0);
    editor_group->Add(tab_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    auto* line_numbers = new wxCheckBox(editor_page, wxID_ANY, "Show line numbers");
    line_numbers->SetValue(editor_line_numbers_);
    editor_group->Add(line_numbers, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    auto* soft_wrap = new wxCheckBox(editor_page, wxID_ANY, "Soft wrap long lines");
    soft_wrap->SetValue(editor_soft_wrap_);
    editor_group->Add(soft_wrap, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    auto* indent_guides = new wxCheckBox(editor_page, wxID_ANY, "Show indentation guides");
    indent_guides->SetValue(editor_indent_guides_);
    editor_group->Add(indent_guides, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    auto* syntax_highlighting = new wxCheckBox(editor_page, wxID_ANY, "Enable RBL syntax highlighting");
    syntax_highlighting->SetValue(editor_syntax_highlighting_);
    editor_group->Add(syntax_highlighting, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    editor_layout->Add(editor_group, 0, wxEXPAND | wxALL, pad);
    editor_page->SetSizer(editor_layout);

    auto* behavior_layout = new wxBoxSizer(wxVERTICAL);
    auto* behavior_group = new wxStaticBoxSizer(wxVERTICAL, behavior_page, "Editing and run behavior");
    auto* auto_check = new wxCheckBox(behavior_page, wxID_ANY, "Check RBL syntax after edits");
    auto_check->SetValue(editor_auto_check_);
    behavior_group->Add(auto_check, 0, wxALL, pad);
    auto* auto_save = new wxCheckBox(behavior_page, wxID_ANY, "Save before Run / Build / Assembly");
    auto_save->SetValue(editor_auto_save_);
    behavior_group->Add(auto_save, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    behavior_layout->Add(behavior_group, 0, wxEXPAND | wxALL, pad);
    behavior_page->SetSizer(behavior_layout);

    auto* toolchain_layout = new wxBoxSizer(wxVERTICAL);
    auto* toolchain_group = new wxStaticBoxSizer(wxVERTICAL, toolchain_page, "Windows toolchain selection");
    auto* toolchain_mode = new wxChoice(toolchain_page, wxID_ANY);
    toolchain_mode->Append("auto");
    toolchain_mode->Append("native");
    toolchain_mode->Append("wsl");
    toolchain_mode->SetStringSelection(toolchain_mode_);
    if (toolchain_mode->GetSelection() == wxNOT_FOUND) toolchain_mode->SetSelection(0);
    toolchain_group->Add(new wxStaticText(toolchain_page, wxID_ANY, "Mode"), 0, wxLEFT | wxRIGHT | wxTOP, pad);
    toolchain_group->Add(toolchain_mode, 0, wxEXPAND | wxALL, pad);
    auto* distro_row = new wxBoxSizer(wxHORIZONTAL);
    distro_row->Add(new wxStaticText(toolchain_page, wxID_ANY, "WSL distribution"),
        1, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    auto* distro = new wxTextCtrl(toolchain_page, wxID_ANY, wsl_distribution_);
    distro_row->Add(distro, 1);
    toolchain_group->Add(distro_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    toolchain_layout->Add(toolchain_group, 0, wxEXPAND | wxALL, pad);
    toolchain_layout->Add(new wxStaticText(toolchain_page, wxID_ANY,
        "Changes to toolchain mode restart the backend worker when it is idle."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    toolchain_page->SetSizer(toolchain_layout);
#ifndef __WXMSW__
    toolchain_page->Enable(false);
    pages->SetPageText(3, "Toolchain (Windows only)");
#endif

    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    auto* restore = new wxButton(&dialog, wxID_ANY, "Restore Defaults");
    footer->Add(restore, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
    footer->AddStretchSpacer(1);
    auto* buttons = new wxStdDialogButtonSizer();
    auto* ok = new wxButton(&dialog, wxID_OK);
    auto* cancel = new wxButton(&dialog, wxID_CANCEL);
    auto* apply = new wxButton(&dialog, wxID_APPLY, "Apply");
    buttons->AddButton(ok);
    buttons->AddButton(cancel);
    buttons->AddButton(apply);
    buttons->Realize();
    footer->Add(buttons, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    dialog.SetSizer(root);
    dialog.SetMinSize(wxSize(FromDIP(600), FromDIP(460)));
    dialog.SetBackgroundColour(dialog_palette.window);
    const auto apply_dialog_theme = [&](auto&& self, wxWindow* window) -> void {
        window->SetForegroundColour(dialog_palette.text);
        window->SetBackgroundColour(dialog_palette.surface);
        window->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        if (dynamic_cast<wxTextCtrl*>(window) || dynamic_cast<wxChoice*>(window) ||
            dynamic_cast<wxSpinCtrl*>(window) || dynamic_cast<wxButton*>(window))
            window->SetBackgroundColour(dialog_palette.surface_alt);
        for (auto* child : window->GetChildren()) self(self, child);
    };
    for (auto* child : dialog.GetChildren()) apply_dialog_theme(apply_dialog_theme, child);
    dialog.CentreOnParent();

    auto apply_settings = [&] {
        const bool syntax_was_enabled = editor_syntax_highlighting_;
        editor_font_size_ = font_size->GetValue();
        editor_tab_size_ = tab_size->GetValue();
        editor_line_numbers_ = line_numbers->GetValue();
        editor_soft_wrap_ = soft_wrap->GetValue();
        editor_indent_guides_ = indent_guides->GetValue();
        editor_auto_check_ = auto_check->GetValue();
        editor_auto_save_ = auto_save->GetValue();
        editor_syntax_highlighting_ = syntax_highlighting->GetValue();
        compact_mode_ = compact->GetValue();
        ui_theme_ = theme->GetStringSelection();
        editor_color_scheme_ = color_scheme->GetStringSelection();
        const bool toolchain_changed = toolchain_mode_ != toolchain_mode->GetStringSelection() ||
            wsl_distribution_ != distro->GetValue();
        toolchain_mode_ = toolchain_mode->GetStringSelection();
        wsl_distribution_ = distro->GetValue().Trim(true).Trim(false);
        if (wsl_distribution_.empty()) wsl_distribution_ = "Ubuntu-24.04";
        for (const auto& [page, document] : documents_) {
            (void)document;
            if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(page)) {
                ApplyEditorSettings(editor);
                if (!syntax_was_enabled && editor_syntax_highlighting_)
                    editor->Colourise(0, editor->GetTextLength());
            }
        }
        ApplyUiTheme();
        SaveUiState();
        if (toolchain_changed) {
            wxSetEnv("RBL_TOOLCHAIN", toolchain_mode_);
            wxSetEnv("RBL_WSL_DISTRO", wsl_distribution_);
            if (worker_ && !task_running_) RestartWorker();
            else if (worker_) {
                worker_restart_after_task_ = true;
                AppendOutput("Toolchain settings saved; the backend will restart when the active task finishes.\n");
            }
        }
    };
    auto restore_defaults = [&] {
        theme->SetStringSelection("RBL Dark");
        compact->SetValue(false);
        color_scheme->SetStringSelection("RBL Dark");
        font_size->SetValue(11);
        tab_size->SetValue(4);
        line_numbers->SetValue(true);
        soft_wrap->SetValue(false);
        indent_guides->SetValue(true);
        syntax_highlighting->SetValue(true);
        auto_check->SetValue(true);
        auto_save->SetValue(false);
        toolchain_mode->SetStringSelection("auto");
        distro->SetValue("Ubuntu-24.04");
    };
    restore->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { restore_defaults(); });
    apply->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { apply_settings(); });
    ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        apply_settings();
        dialog.EndModal(wxID_OK);
    });
    cancel->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dialog.EndModal(wxID_CANCEL); });
    dialog.ShowModal();
}

void MainFrame::OnToggleProjectPane(wxCommandEvent& event) {
    auto& pane = manager_.GetPane("project-pane");
    if (!pane.IsOk()) return;
    pane.Show(event.IsChecked());
    manager_.Update();
    SaveUiState();
}

void MainFrame::OnToggleOutputPane(wxCommandEvent& event) {
    auto& pane = manager_.GetPane("output-pane");
    if (!pane.IsOk()) return;
    pane.Show(event.IsChecked());
    manager_.Update();
    SaveUiState();
}

void MainFrame::OnResetLayout(wxCommandEvent&) {
    if (default_layout_.empty() || !manager_.LoadPerspective(default_layout_, true)) {
        AppendOutput("[layout] could not restore the default workspace layout\n");
        return;
    }
    manager_.Update();
    if (GetMenuBar()) {
        if (auto* item = GetMenuBar()->FindItem(ID_ToggleProjectPane)) item->Check(true);
        if (auto* item = GetMenuBar()->FindItem(ID_ToggleOutputPane)) item->Check(true);
    }
    SaveUiState();
}

void MainFrame::OnGoToLine(wxCommandEvent&) {
    auto* editor = CurrentEditor();
    if (!editor) return;
    wxTextEntryDialog dialog(this, "Line number:", "Go to Line",
        wxString::Format("%d", editor->GetCurrentLine() + 1));
    if (dialog.ShowModal() != wxID_OK) return;
    long requested = 0;
    if (!dialog.GetValue().ToLong(&requested) || requested < 1) {
        wxMessageBox("Enter a positive line number.", "Go to Line", wxOK | wxICON_WARNING, this);
        return;
    }
    const int line = std::clamp(static_cast<int>(requested - 1), 0, editor->GetLineCount() - 1);
    const auto position = editor->PositionFromLine(line);
    editor->GotoPos(position);
    editor->SetSelection(position, position);
    editor->EnsureCaretVisible();
    editor->SetFocus();
}

void MainFrame::OnQuickOpen(wxCommandEvent&) {
    struct IndexState {
        std::mutex mutex;
        std::vector<std::filesystem::path> files;
        std::atomic_bool cancel{false};
        bool done{};
        bool truncated{};
        std::size_t visited{};
    };
    const auto state = std::make_shared<IndexState>();
    const auto root = workspace_root_;

    wxDialog dialog(this, wxID_ANY, "Quick Open", wxDefaultPosition,
        wxSize(FromDIP(680), FromDIP(540)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* search = new wxSearchCtrl(&dialog, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    search->SetDescriptiveText("Search project files by name or path...");
    layout->Add(search, 0, wxEXPAND | wxALL, FromDIP(12));
    auto* list = new wxListBox(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0,
        nullptr, wxLB_SINGLE | wxLB_HSCROLL);
    layout->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
    auto* status = new wxStaticText(&dialog, wxID_ANY, "Indexing workspace...");
    layout->Add(status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    layout->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0,
        wxEXPAND | wxALL, FromDIP(12));
    dialog.SetSizer(layout);
    dialog.SetMinSize(wxSize(FromDIP(480), FromDIP(360)));
    const auto quick_open_palette = rbl::wxui::PaletteForTheme(ui_theme_);
    dialog.SetBackgroundColour(quick_open_palette.window);
    const auto style_quick_open = [&](auto&& self, wxWindow* window) -> void {
        window->SetForegroundColour(quick_open_palette.text);
        window->SetBackgroundColour(quick_open_palette.surface);
        window->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        if (dynamic_cast<wxTextCtrl*>(window) || dynamic_cast<wxListBox*>(window) ||
            dynamic_cast<wxButton*>(window)) window->SetBackgroundColour(quick_open_palette.surface_alt);
        for (auto* child : window->GetChildren()) self(self, child);
    };
    for (auto* child : dialog.GetChildren()) style_quick_open(style_quick_open, child);

    std::vector<std::filesystem::path> filtered;
    std::size_t displayed_count = std::numeric_limits<std::size_t>::max();
    wxString displayed_query;
    bool displayed_done = false;
    const auto refresh = [&] {
        const wxString query = search->GetValue().Lower().Strip(wxString::both);
        std::vector<std::filesystem::path> snapshot;
        bool done = false;
        bool truncated = false;
        std::size_t visited = 0;
        {
            std::lock_guard lock(state->mutex);
            if (state->files.size() == displayed_count && query == displayed_query &&
                state->done == displayed_done) return;
            snapshot = state->files;
            done = state->done;
            truncated = state->truncated;
            visited = state->visited;
        }
        displayed_count = snapshot.size();
        displayed_query = query;
        displayed_done = done;

        struct RankedPath { int rank{}; std::filesystem::path path; wxString label; };
        std::vector<RankedPath> matches;
        matches.reserve(std::min<std::size_t>(snapshot.size(), 5000));
        for (const auto& path : snapshot) {
            const auto relative = path.lexically_relative(root);
            const wxString label(relative.wstring());
            const wxString lowered = label.Lower();
            int rank = 0;
            if (!query.empty()) {
                const auto filename = wxString(path.filename().wstring()).Lower();
                if (filename.StartsWith(query)) rank = 0;
                else if (filename.Find(query) != wxNOT_FOUND) rank = 1;
                else if (lowered.Find(query) != wxNOT_FOUND) rank = 2;
                else continue;
            }
            matches.push_back({rank, path, label});
        }
        std::stable_sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
            if (left.rank != right.rank) return left.rank < right.rank;
            return left.label.CmpNoCase(right.label) < 0;
        });
        constexpr std::size_t max_visible = 300;
        if (matches.size() > max_visible) matches.resize(max_visible);
        filtered.clear();
        list->Freeze();
        list->Clear();
        for (const auto& match : matches) {
            filtered.push_back(match.path);
            list->Append(match.label);
        }
        if (!filtered.empty()) list->SetSelection(0);
        list->Thaw();
        wxString message = done
            ? wxString::Format("%zu files indexed", snapshot.size())
            : wxString::Format("Indexing... %zu entries visited, %zu files found", visited, snapshot.size());
        if (truncated) message += " (limit reached)";
        status->SetLabel(message);
    };

    std::thread indexer([state, root] {
        constexpr std::size_t max_entries = 50000;
        constexpr std::size_t max_files = 20000;
        constexpr std::size_t max_depth = 32;
        const std::unordered_set<std::string> ignored_dirs = {
            ".git", "target", "build", "dist", "out", "artifacts", ".analysis-tmp",
            ".cache", ".pytest_cache", ".mypy_cache", ".ruff_cache", ".venv", "venv",
            "node_modules", "__pycache__", ".idea"
        };
        static const std::unordered_set<std::string> text_extensions = {
            ".rbl", ".txt", ".md", ".rst", ".json", ".jsonl", ".toml", ".yaml", ".yml",
            ".ini", ".cfg", ".conf", ".xml", ".csv", ".log", ".py", ".pyi", ".rs",
            ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".js", ".jsx", ".ts",
            ".tsx", ".html", ".css", ".scss", ".sql", ".sh", ".bash", ".ps1", ".bat",
            ".cmd", ".go", ".java", ".kt", ".lua", ".s", ".asm", ".make", ".cmake"
        };
        static const std::unordered_set<std::string> text_filenames = {
            "makefile", "dockerfile", "license", "notice", "version", "cmakelists.txt"
        };
#ifdef _WIN32
        std::vector<std::pair<std::filesystem::path, std::size_t>> pending;
        pending.emplace_back(root, 0);
#else
        std::vector<PosixSearchDirectory> pending;
        const int root_fd = OpenDirectoryPathNoFollow(root);
        if (root_fd >= 0) {
            pending.push_back({root, 0, {}, {}, 0, 0,
                std::make_shared<PosixDirectoryHandle>(root_fd)});
        }
#endif
        const auto is_text_file = [&](const std::filesystem::path& path) {
            auto extension_u8 = path.extension().u8string();
            std::string extension(reinterpret_cast<const char*>(extension_u8.data()), extension_u8.size());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            auto filename_u8 = path.filename().u8string();
            std::string filename(reinterpret_cast<const char*>(filename_u8.data()), filename_u8.size());
            std::transform(filename.begin(), filename.end(), filename.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return text_extensions.contains(extension) || text_filenames.contains(filename);
        };
        std::size_t entries = 0;
        while (!pending.empty() && !state->cancel.load(std::memory_order_relaxed)) {
#ifdef _WIN32
            auto [directory, depth] = std::move(pending.back());
            pending.pop_back();
            WinDirectoryChainGuard directory_guard;
            std::error_code directory_lock_error;
            if (!LockWindowsDirectoryChain(directory, directory_guard, directory_lock_error)) continue;
            std::error_code iterator_error;
            std::filesystem::directory_iterator iterator(directory,
                std::filesystem::directory_options::skip_permission_denied, iterator_error);
            const std::filesystem::directory_iterator end;
            if (iterator_error) continue;
            while (iterator != end && !state->cancel.load(std::memory_order_relaxed)) {
                if (++entries > max_entries) {
                    std::lock_guard lock(state->mutex);
                    state->truncated = true;
                    pending.clear();
                    break;
                }
                const auto entry = *iterator;
                const auto path = entry.path();
                std::error_code status_error;
                const auto status = entry.symlink_status(status_error);
                if (!status_error && !std::filesystem::is_symlink(status)) {
                    const DWORD attributes = ::GetFileAttributesW(path.c_str());
                    const bool reparse = attributes == INVALID_FILE_ATTRIBUTES ||
                        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    if (!reparse && std::filesystem::is_directory(status)) {
                        const auto name = path.filename().string();
                        if (depth < max_depth && !ignored_dirs.contains(name))
                            pending.emplace_back(path, depth + 1);
                        else if (depth >= max_depth) {
                            std::lock_guard lock(state->mutex);
                            state->truncated = true;
                        }
                    } else if (!reparse && std::filesystem::is_regular_file(status)) {
                        if (is_text_file(path)) {
                            std::lock_guard lock(state->mutex);
                            if (state->files.size() < max_files) state->files.push_back(path);
                            else state->truncated = true;
                        }
                    }
                }
                {
                    std::lock_guard lock(state->mutex);
                    state->visited = entries;
                }
                std::error_code increment_error;
                iterator.increment(increment_error);
                if (increment_error) break;
            }
#else
            auto directory = std::move(pending.back());
            pending.pop_back();
            if (!directory.handle && directory.parent) {
                int child_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
                child_flags |= O_CLOEXEC;
#endif
                const int child_fd = ::openat(directory.parent->fd,
                    directory.entry_name.c_str(), child_flags);
                struct stat child_info{};
                if (child_fd < 0 || ::fstat(child_fd, &child_info) != 0 ||
                    !S_ISDIR(child_info.st_mode) || child_info.st_dev != directory.expected_device ||
                    child_info.st_ino != directory.expected_inode) {
                    if (child_fd >= 0) ::close(child_fd);
                    continue;
                }
                directory.handle = std::make_shared<PosixDirectoryHandle>(child_fd);
            }
            if (!directory.handle) continue;
            DIR* stream = ::fdopendir(::dup(directory.handle->fd));
            if (!stream) continue;
            while (!state->cancel.load(std::memory_order_relaxed)) {
                errno = 0;
                dirent* dir_entry = ::readdir(stream);
                if (!dir_entry) break;
                const std::string entry_name(dir_entry->d_name);
                if (entry_name == "." || entry_name == "..") continue;
                if (++entries > max_entries) {
                    std::lock_guard lock(state->mutex);
                    state->truncated = true;
                    pending.clear();
                    break;
                }
                const auto path = directory.path / std::filesystem::path(entry_name);
                struct stat entry_info{};
                if (::fstatat(directory.handle->fd, entry_name.c_str(), &entry_info,
                              AT_SYMLINK_NOFOLLOW) == 0) {
                    if (S_ISDIR(entry_info.st_mode)) {
                        if (directory.depth < max_depth && !ignored_dirs.contains(entry_name)) {
                            pending.push_back({path, directory.depth + 1, directory.handle,
                                entry_name, entry_info.st_dev, entry_info.st_ino, {}});
                        } else if (directory.depth >= max_depth) {
                            std::lock_guard lock(state->mutex);
                            state->truncated = true;
                        }
                    } else if (S_ISREG(entry_info.st_mode) && is_text_file(path)) {
                        std::lock_guard lock(state->mutex);
                        if (state->files.size() < max_files) state->files.push_back(path);
                        else state->truncated = true;
                    }
                }
                {
                    std::lock_guard lock(state->mutex);
                    state->visited = entries;
                }
            }
            ::closedir(stream);
#endif
        }
        {
            std::lock_guard lock(state->mutex);
            state->done = true;
        }
    });

    wxTimer refresh_timer(&dialog);
    dialog.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { refresh(); }, refresh_timer.GetId());
    search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { refresh(); });
    search->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
        if (!filtered.empty()) dialog.EndModal(wxID_OK);
    });
    list->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent&) { dialog.EndModal(wxID_OK); });
    dialog.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& event) {
        const int selection = list->GetSelection();
        if (event.GetKeyCode() == WXK_DOWN && !filtered.empty()) {
            list->SetSelection(selection == wxNOT_FOUND ? 0 :
                std::min(selection + 1, static_cast<int>(filtered.size()) - 1));
            return;
        }
        if (event.GetKeyCode() == WXK_UP && !filtered.empty()) {
            list->SetSelection(selection == wxNOT_FOUND ? 0 : std::max(selection - 1, 0));
            return;
        }
        if (event.GetKeyCode() == WXK_RETURN && !filtered.empty()) {
            dialog.EndModal(wxID_OK);
            return;
        }
        event.Skip();
    });
    refresh_timer.Start(100);
    search->SetFocus();
    const int result = dialog.ShowModal();
    refresh_timer.Stop();
    state->cancel.store(true, std::memory_order_relaxed);
    if (indexer.joinable()) indexer.join();
    if (result != wxID_OK || list->GetSelection() == wxNOT_FOUND) return;
    const auto selected = filtered[static_cast<std::size_t>(list->GetSelection())];
    OpenDocumentPath(wxString(selected.wstring()));
}

void MainFrame::OnCommandPalette(wxCommandEvent&) {
    const wxArrayString commands = {
        "New RBL File", "New Text File", "Open File", "Open Project Folder", "Save", "Save All", "Save As",
        "Close Tab", "Close Other Tabs", "Close All Tabs", "Reopen Closed Tab",
        "Run", "Build", "Check", "Show Assembly", "Run Tests", "Run Benchmarks",
        "Toolchain Doctor", "Stop Process", "Find", "Find Next", "Find Previous", "Replace", "Find in Files", "Cancel Search",
        "Go to Line", "Settings", "Quick Reference", "About"
    };
    wxDialog dialog(this, wxID_ANY, "Command Palette", wxDefaultPosition,
        wxSize(FromDIP(560), FromDIP(480)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* search = new wxSearchCtrl(&dialog, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    search->SetDescriptiveText("Type a command to filter...");
    layout->Add(search, 0, wxEXPAND | wxALL, FromDIP(10));
    auto* list = new wxListBox(&dialog, wxID_ANY);
    layout->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    auto* buttons = dialog.CreateButtonSizer(wxOK | wxCANCEL);
    layout->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(10));
    dialog.SetSizer(layout);
    dialog.SetMinSize(wxSize(FromDIP(420), FromDIP(320)));
    const auto palette = rbl::wxui::PaletteForTheme(ui_theme_);
    dialog.SetBackgroundColour(palette.window);
    const auto style_palette_dialog = [&](auto&& self, wxWindow* window) -> void {
        window->SetForegroundColour(palette.text);
        window->SetBackgroundColour(palette.surface);
        window->SetFont(wxFontInfo(compact_mode_ ? 8 : 9).FaceName("Segoe UI"));
        if (dynamic_cast<wxTextCtrl*>(window) || dynamic_cast<wxListBox*>(window) ||
            dynamic_cast<wxButton*>(window)) window->SetBackgroundColour(palette.surface_alt);
        for (auto* child : window->GetChildren()) self(self, child);
    };
    for (auto* child : dialog.GetChildren()) style_palette_dialog(style_palette_dialog, child);

    std::vector<wxString> filtered;
    const auto refresh = [&] {
        const wxString query = search->GetValue().Lower();
        filtered.clear();
        list->Clear();
        for (const auto& item : commands) {
            if (query.empty() || item.Lower().Find(query) != wxNOT_FOUND) {
                filtered.push_back(item);
                list->Append(item);
            }
        }
        if (!filtered.empty()) list->SetSelection(0);
    };
    search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { refresh(); });
    search->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
        if (list->GetSelection() != wxNOT_FOUND) dialog.EndModal(wxID_OK);
    });
    list->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent&) { dialog.EndModal(wxID_OK); });
    dialog.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& event) {
        const int selection = list->GetSelection();
        if (event.GetKeyCode() == WXK_DOWN && !filtered.empty()) {
            list->SetSelection(selection == wxNOT_FOUND
                ? 0 : std::min(selection + 1, static_cast<int>(filtered.size()) - 1));
            return;
        }
        if (event.GetKeyCode() == WXK_UP && !filtered.empty()) {
            list->SetSelection(selection == wxNOT_FOUND
                ? 0 : std::max(selection - 1, 0));
            return;
        }
        if (event.GetKeyCode() == WXK_RETURN && selection != wxNOT_FOUND) {
            dialog.EndModal(wxID_OK);
            return;
        }
        event.Skip();
    });
    refresh();
    search->SetFocus();
    if (dialog.ShowModal() != wxID_OK || list->GetSelection() == wxNOT_FOUND) return;
    const auto command = filtered[static_cast<std::size_t>(list->GetSelection())];
    wxCommandEvent command_event;
    if (command == "New RBL File") NewDocument();
    else if (command == "New Text File") NewTextDocument();
    else if (command == "Open File") OpenDocument();
    else if (command == "Open Project Folder") OnOpenProject(command_event);
    else if (command == "Save") SaveCurrent();
    else if (command == "Save All") OnSaveAll(command_event);
    else if (command == "Save As") OnSaveAs(command_event);
    else if (command == "Close Tab") CloseCurrent();
    else if (command == "Close Other Tabs") OnCloseOtherTabs(command_event);
    else if (command == "Close All Tabs") OnCloseAll(command_event);
    else if (command == "Reopen Closed Tab") OnReopenClosedTab(command_event);
    else if (command == "Run") StartTask("run");
    else if (command == "Build") StartTask("build");
    else if (command == "Check") StartTask("check");
    else if (command == "Show Assembly") StartTask("assembly");
    else if (command == "Run Tests") StartTask("tests");
    else if (command == "Run Benchmarks") StartTask("benchmarks");
    else if (command == "Toolchain Doctor") StartTask("doctor");
    else if (command == "Stop Process") OnStop(command_event);
    else if (command == "Find") OnFind(command_event);
    else if (command == "Find Next") OnFindNext(command_event);
    else if (command == "Find Previous") OnFindPrevious(command_event);
    else if (command == "Replace") OnReplace(command_event);
    else if (command == "Find in Files") OnFindInFiles(command_event);
    else if (command == "Cancel Search") OnCancelSearch(command_event);
    else if (command == "Go to Line") OnGoToLine(command_event);
    else if (command == "Settings") OnSettings(command_event);
    else if (command == "Quick Reference") OnQuickReference(command_event);
    else if (command == "About") OnAbout(command_event);
}

void MainFrame::OnQuickReference(wxCommandEvent&) {
    wxMessageBox(
        "RBL Studio — editor shortcuts\n\n"
        "Ctrl+N / Ctrl+O — New / Open\n"
        "Ctrl+S / Ctrl+Shift+S — Save / Save All\n"
        "Ctrl+F / Ctrl+H — Find / Replace\n"
        "F3 / Shift+F3 — Find Next / Previous\n"
        "Ctrl+Shift+F — Find in Files\n"
        "Ctrl+G — Go to Line\n"
        "Ctrl+P — Quick Open\n"
        "Ctrl+Shift+P — Command Palette\n"
        "F5 — Run   F7 — Check   Ctrl+B — Build\n"
        "Ctrl+T — Tests   Ctrl+Shift+T — Benchmarks\n"
        "Shift+F5 — Stop\n\n"
        "RBL blocks use parenthesized headers, for example: func main()",
        "RBL Quick Reference", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnAbout(wxCommandEvent&) {
    wxMessageBox("RBL Studio 0.8.0\nNative wxWidgets + Scintilla frontend candidate\n"
        "Compiler/runtime backend: RBL 0.8.0\n\n"
        "This frontend is an engineering candidate and has not been released.",
        "About RBL Studio", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnSaveAll(wxCommandEvent&) {
    for (std::size_t index = 0; index < notebook_->GetPageCount(); ++index) {
        auto* page = notebook_->GetPage(index);
        const auto found = documents_.find(page);
        if (found != documents_.end() && found->second.editor->GetModify()) {
            if (!SaveDocument(page, false)) return;
        }
    }
    AppendOutput("[editor] all modified documents saved\n");
}

void MainFrame::OnCloseAll(wxCommandEvent&) {
    for (std::size_t index = notebook_->GetPageCount(); index > 0; --index) {
        auto* page = notebook_->GetPage(index - 1);
        if (!CanClosePage(page)) return;
        RememberClosedDocument(page);
        if (page == active_snapshot_editor_) active_snapshot_editor_ = nullptr;
        documents_.erase(page);
        notebook_->DeletePage(index - 1);
    }
    UpdateDocumentLimits();
    NewDocument();
}

void MainFrame::OnCloseOtherTabs(wxCommandEvent&) {
    auto* keep = notebook_->GetCurrentPage();
    if (!keep) return;
    for (std::size_t index = notebook_->GetPageCount(); index > 0; --index) {
        auto* page = notebook_->GetPage(index - 1);
        if (page == keep) continue;
        if (!CanClosePage(page)) return;
        RememberClosedDocument(page);
        if (page == active_snapshot_editor_) active_snapshot_editor_ = nullptr;
        documents_.erase(page);
        notebook_->DeletePage(index - 1);
    }
    const int keep_index = notebook_->GetPageIndex(keep);
    if (keep_index != wxNOT_FOUND) notebook_->SetSelection(static_cast<std::size_t>(keep_index));
    UpdateDocumentLimits();
}

void MainFrame::OnReopenClosedTab(wxCommandEvent&) {
    while (!recently_closed_.empty()) {
        const auto path = recently_closed_.back();
        recently_closed_.pop_back();
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            const auto path_u8 = path.u8string();
            OpenDocumentPath(wxString::FromUTF8(reinterpret_cast<const char*>(path_u8.data()), path_u8.size()));
            return;
        }
    }
    AppendOutput("[editor] no recently closed file is available to reopen\n");
}

void MainFrame::OnFindInFiles(wxCommandEvent&) {
    if (search_running_) {
        wxMessageBox("A project search is already running.", "Find in Files", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxTextEntryDialog dialog(this, "Search text in project files:", "Find in Files");
    if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
    const auto query = dialog.GetValue();
    const auto root = workspace_root_;
    search_results_timer_.Stop();
    pending_search_results_.clear();
    pending_search_result_index_ = 0;
    search_results_->DeleteAllItems();
    search_result_identities_.clear();
    output_tabs_->SetSelection(2);
    search_cancel_.store(false, std::memory_order_relaxed);
    search_running_ = true;
    AppendOutput("[search] scanning project files...\n");
    search_thread_ = std::thread([this, root, query]() {
        ProjectSearchResults results;
        const auto query_lower = query.Lower();
#ifdef _WIN32
        std::vector<std::pair<std::filesystem::path, std::size_t>> directories;
        directories.emplace_back(root, 0);
#else
        std::vector<PosixSearchDirectory> directories;
        const int root_fd = OpenDirectoryPathNoFollow(root);
        if (root_fd >= 0) {
            directories.push_back({root, 0, {}, {}, 0, 0,
                std::make_shared<PosixDirectoryHandle>(root_fd)});
        }
        else {
            const auto root_u8 = root.u8string();
            results.skipped_paths.emplace_back(
                reinterpret_cast<const char*>(root_u8.data()), root_u8.size());
        }
#endif
        std::size_t entries = 0;
        std::uint64_t bytes_scanned = 0;
        bool stop_search = false;
        constexpr std::size_t max_entries = 100000;
        constexpr std::size_t max_results = 10000;
        constexpr std::uint64_t max_bytes = 128ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t max_file_bytes = 2ULL * 1024ULL * 1024ULL;
        const std::unordered_set<std::string> ignored_dirs = {
            ".git", "target", "build", ".venv", "node_modules", "__pycache__"
        };
        const std::unordered_set<std::string> text_extensions = {
            ".rbl", ".txt", ".md", ".json", ".toml", ".yaml", ".yml", ".ini",
            ".py", ".rs", ".c", ".h", ".cpp", ".hpp", ".js", ".ts", ".html", ".css"
        };
        while (!directories.empty() && !stop_search) {
            if (search_cancel_.load(std::memory_order_relaxed)) {
                results.cancelled = true;
                break;
            }
#ifdef _WIN32
            auto [directory_path, depth] = std::move(directories.back());
            directories.pop_back();
#else
            auto directory = std::move(directories.back());
            directories.pop_back();
            const auto directory_path = directory.path;
            const auto depth = directory.depth;
#endif
            const auto record_skipped_path = [&](const std::filesystem::path& skipped) {
                if (results.skipped_paths.size() < 32) {
                    const auto path_u8 = skipped.u8string();
                    results.skipped_paths.emplace_back(
                        reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
                }
            };
#ifdef _WIN32
            WinDirectoryChainGuard directory_guard;
            std::error_code directory_lock_error;
            if (!LockWindowsDirectoryChain(directory_path, directory_guard, directory_lock_error)) {
                record_skipped_path(directory_path);
                continue;
            }
#else
            if (!directory.handle && directory.parent) {
                int child_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
                child_flags |= O_CLOEXEC;
#endif
                const int child_fd = ::openat(directory.parent->fd,
                    directory.entry_name.c_str(), child_flags);
                struct stat child_info{};
                if (child_fd < 0 || ::fstat(child_fd, &child_info) != 0 ||
                    !S_ISDIR(child_info.st_mode) || child_info.st_dev != directory.expected_device ||
                    child_info.st_ino != directory.expected_inode) {
                    if (child_fd >= 0) ::close(child_fd);
                    record_skipped_path(directory_path);
                    continue;
                }
                directory.handle = std::make_shared<PosixDirectoryHandle>(child_fd);
            }
            struct stat opened_directory_info{};
            if (!directory.handle || ::fstat(directory.handle->fd, &opened_directory_info) != 0 ||
                !S_ISDIR(opened_directory_info.st_mode)) {
                record_skipped_path(directory_path);
                continue;
            }
#endif
#ifdef _WIN32
            std::error_code directory_error;
            std::filesystem::directory_iterator iterator(directory_path,
                std::filesystem::directory_options::none, directory_error);
            const std::filesystem::directory_iterator end;
            if (directory_error) {
                record_skipped_path(directory_path);
                continue;
            }
#else
            const int scan_fd = ::dup(directory.handle->fd);
            DIR* directory_stream = scan_fd >= 0 ? ::fdopendir(scan_fd) : nullptr;
            if (!directory_stream) {
                if (scan_fd >= 0) ::close(scan_fd);
                record_skipped_path(directory_path);
                continue;
            }
#endif
            while (!stop_search) {
                if (search_cancel_.load(std::memory_order_relaxed)) {
                    results.cancelled = true;
                    break;
                }
#ifdef _WIN32
                if (iterator == end) break;
                if (++entries > max_entries) {
                    results.truncated = true;
                    stop_search = true;
                    break;
                }
                const auto entry = *iterator;
                const auto path = entry.path();
                std::error_code increment_error;
                iterator.increment(increment_error);
                const auto note_increment_error = [&]() {
                    if (!increment_error) return;
                    if (results.skipped_paths.size() < 32) {
                        const auto path_u8 = directory_path.u8string();
                        results.skipped_paths.emplace_back(
                            reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
                    }
                };
                std::error_code status_error;
                const auto status = entry.symlink_status(status_error);
                const bool entry_is_directory = !status_error && std::filesystem::is_directory(status);
                const bool entry_is_regular = !status_error && std::filesystem::is_regular_file(status);
#else
                errno = 0;
                dirent* dir_entry = ::readdir(directory_stream);
                if (!dir_entry) {
                    if (errno != 0) record_skipped_path(directory_path);
                    break;
                }
                const std::string entry_name(dir_entry->d_name);
                if (entry_name == "." || entry_name == "..") continue;
                if (++entries > max_entries) {
                    results.truncated = true;
                    stop_search = true;
                    break;
                }
                const auto path = directory_path / std::filesystem::path(entry_name);
                struct stat entry_info{};
                if (::fstatat(directory.handle->fd, entry_name.c_str(), &entry_info, AT_SYMLINK_NOFOLLOW) != 0) {
                    record_skipped_path(path);
                    continue;
                }
                const auto mode = entry_info.st_mode;
                const bool entry_is_directory = S_ISDIR(mode);
                const bool entry_is_regular = S_ISREG(mode);
                std::error_code increment_error;
                const auto note_increment_error = []() {};
#endif
#ifdef _WIN32
                if (status_error) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                if (std::filesystem::is_symlink(status)) {
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                // std::filesystem::is_symlink() does not consistently classify
                // Windows directory junctions as symlinks. Treat every
                // reparse point as a traversal boundary so junctions,
                // mount points and cloud placeholders cannot redirect a
                // workspace search outside the selected tree.
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
#endif
#ifndef _WIN32
                if (S_ISLNK(mode)) continue;
#endif
                const auto filename_u8 = path.filename().u8string();
                const std::string filename(reinterpret_cast<const char*>(filename_u8.data()), filename_u8.size());
                if (entry_is_directory) {
                    if (ignored_dirs.contains(filename)) {
                        if (increment_error) {
                            note_increment_error();
                            break;
                        }
                        continue;
                    }
                    if (depth >= 32) {
                        results.truncated = true;
                        if (results.skipped_paths.size() < 32) {
                            const auto path_u8 = path.u8string();
                            results.skipped_paths.emplace_back(reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
                        }
                    } else {
#ifdef _WIN32
                        directories.emplace_back(path, depth + 1);
#else
                        directories.push_back({path, depth + 1, directory.handle, filename,
                            entry_info.st_dev, entry_info.st_ino, {}});
#endif
                    }
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                if (!entry_is_regular) {
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                const auto extension_u8 = path.extension().u8string();
                std::string extension(reinterpret_cast<const char*>(extension_u8.data()), extension_u8.size());
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (!text_extensions.contains(extension)) {
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                SearchFileHandleGuard file_guard;
                std::uint64_t file_size = 0;
                FileIdentity search_file_identity;
#ifdef _WIN32
                file_guard.handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (file_guard.handle == INVALID_HANDLE_VALUE) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                BY_HANDLE_FILE_INFORMATION file_info_before{};
                if (!::GetFileInformationByHandle(file_guard.handle, &file_info_before) ||
                    (file_info_before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                    (file_info_before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                file_size = (static_cast<std::uint64_t>(file_info_before.nFileSizeHigh) << 32U) |
                    file_info_before.nFileSizeLow;
                search_file_identity = {
                    file_info_before.dwVolumeSerialNumber,
                    (static_cast<std::uint64_t>(file_info_before.nFileIndexHigh) << 32U) |
                        file_info_before.nFileIndexLow,
                    true,
                    file_info_before.nNumberOfLinks};
#else
                int file_flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
                file_flags |= O_CLOEXEC;
#endif
                file_guard.fd = ::openat(directory.handle->fd, filename.c_str(), file_flags);
                if (file_guard.fd < 0) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                struct stat file_info_before{};
                if (::fstat(file_guard.fd, &file_info_before) != 0 ||
                    !S_ISREG(file_info_before.st_mode) || file_info_before.st_size < 0 ||
                    file_info_before.st_dev != entry_info.st_dev ||
                    file_info_before.st_ino != entry_info.st_ino) {
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                file_size = static_cast<std::uint64_t>(file_info_before.st_size);
                search_file_identity = {
                    static_cast<std::uint64_t>(file_info_before.st_dev),
                    static_cast<std::uint64_t>(file_info_before.st_ino), true,
                    static_cast<std::uint64_t>(file_info_before.st_nlink)};
#endif
                if (file_size > max_file_bytes) {
                    results.truncated = true;
                    record_skipped_path(path);
                    if (increment_error) {
                        note_increment_error();
                        break;
                    }
                    continue;
                }
                if (file_size > max_bytes - bytes_scanned) {
                    results.truncated = true;
                    stop_search = true;
                    break;
                }
            const auto hits_before_file = results.hits.size();
            std::string line;
            std::size_t line_number = 1;
            std::uint64_t file_bytes_read = 0;
            bool has_nul = false;
            bool stop_file = false;
            bool read_error = false;
            bool file_changed = false;
            bool previous_was_cr = false;
            auto inspect_line = [&]() {
                const auto line_text = wxString::FromUTF8(line.data(), line.size());
                const auto match_column = line_text.Lower().Find(query_lower);
                if (!line_text.empty() && match_column != wxNOT_FOUND) {
                    const auto prefix_utf8 = line_text.Left(match_column).ToUTF8();
                    std::size_t codepoint_column = 0;
                    for (std::size_t offset = 0; offset < prefix_utf8.length(); ++offset) {
                        if ((static_cast<unsigned char>(prefix_utf8.data()[offset]) & 0xC0U) != 0x80U)
                            ++codepoint_column;
                    }
                    auto snippet = line_text;
                    snippet.Trim(false);
                    if (results.hits.size() < max_results) {
                        results.hits.push_back({path, line_number, codepoint_column + 1,
                            ToUtf8(snippet.Left(220)), search_file_identity});
                    } else {
                        results.truncated = true;
                        stop_file = true;
                        stop_search = true;
                    }
                }
                line.clear();
                ++line_number;
            };
            char chunk[64 * 1024];
            while (file_bytes_read < file_size && !stop_file && !has_nul &&
                   !search_cancel_.load(std::memory_order_relaxed)) {
                const auto remaining = std::min(max_file_bytes - file_bytes_read,
                    max_bytes - bytes_scanned);
                if (remaining == 0) {
                    results.truncated = true;
                    break;
                }
                const auto request_bytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(sizeof(chunk),
                        std::min(remaining, file_size - file_bytes_read)));
                std::size_t count = 0;
#ifdef _WIN32
                DWORD bytes_read = 0;
                if (!::ReadFile(file_guard.handle, chunk,
                        static_cast<DWORD>(request_bytes), &bytes_read, nullptr)) {
                    read_error = true;
                    break;
                }
                count = static_cast<std::size_t>(bytes_read);
#else
                const auto bytes_read = ::read(file_guard.fd, chunk, request_bytes);
                if (bytes_read < 0) {
                    if (errno == EINTR) continue;
                    read_error = true;
                    break;
                }
                count = static_cast<std::size_t>(bytes_read);
#endif
                if (count == 0) break;
                file_bytes_read += static_cast<std::uint64_t>(count);
                bytes_scanned += static_cast<std::uint64_t>(count);
                for (std::size_t offset = 0; offset < count; ++offset) {
                    const char ch = chunk[offset];
                    if (ch == '\0') { has_nul = true; break; }
                    if (ch == '\r') {
                        inspect_line();
                        previous_was_cr = true;
                    } else if (ch == '\n') {
                        if (!previous_was_cr) inspect_line();
                        previous_was_cr = false;
                    } else {
                        previous_was_cr = false;
                        line.push_back(ch);
                    }
                    if (stop_file) break;
                }
            }
#ifdef _WIN32
            BY_HANDLE_FILE_INFORMATION file_info_after{};
            if (!::GetFileInformationByHandle(file_guard.handle, &file_info_after) ||
                file_info_after.dwVolumeSerialNumber != file_info_before.dwVolumeSerialNumber ||
                file_info_after.nFileIndexHigh != file_info_before.nFileIndexHigh ||
                file_info_after.nFileIndexLow != file_info_before.nFileIndexLow ||
                file_info_after.nFileSizeHigh != file_info_before.nFileSizeHigh ||
                file_info_after.nFileSizeLow != file_info_before.nFileSizeLow ||
                file_info_after.ftLastWriteTime.dwHighDateTime != file_info_before.ftLastWriteTime.dwHighDateTime ||
                file_info_after.ftLastWriteTime.dwLowDateTime != file_info_before.ftLastWriteTime.dwLowDateTime) {
                file_changed = true;
            }
#else
            struct stat file_info_after{};
            if (::fstat(file_guard.fd, &file_info_after) != 0 ||
                file_info_after.st_dev != file_info_before.st_dev ||
                file_info_after.st_ino != file_info_before.st_ino ||
                file_info_after.st_size != file_info_before.st_size ||
                file_info_after.st_mtim.tv_sec != file_info_before.st_mtim.tv_sec ||
                file_info_after.st_mtim.tv_nsec != file_info_before.st_mtim.tv_nsec ||
                file_info_after.st_ctim.tv_sec != file_info_before.st_ctim.tv_sec ||
                file_info_after.st_ctim.tv_nsec != file_info_before.st_ctim.tv_nsec) {
                file_changed = true;
            }
#endif
            if (read_error || file_changed ||
                (!has_nul && !stop_file && file_bytes_read != file_size)) {
                results.hits.resize(hits_before_file);
                record_skipped_path(path);
            }
            if (search_cancel_.load(std::memory_order_relaxed)) results.cancelled = true;
            if (!has_nul && !stop_file && !read_error && !file_changed && !line.empty())
                inspect_line();
            if (!has_nul && !stop_file && !read_error && !file_changed &&
                file_bytes_read == file_size) ++results.files_scanned;
                if (increment_error) {
                    note_increment_error();
                    break;
                }
            }
#ifndef _WIN32
            ::closedir(directory_stream);
#endif
        }
        auto* done = new wxThreadEvent(wxEVT_THREAD, ID_ProjectSearchDone);
        done->SetPayload(std::move(results));
        wxQueueEvent(this, done);
    });
}

void MainFrame::OnCancelSearch(wxCommandEvent&) {
    if (!search_running_) return;
    search_cancel_.store(true, std::memory_order_relaxed);
    AppendOutput("[search] cancellation requested\n");
}

void MainFrame::OnProjectSearchDone(wxThreadEvent& event) {
    auto results = event.GetPayload<ProjectSearchResults>();
    if (search_thread_.joinable()) search_thread_.join();
    search_running_ = false;
    if (!close_after_search_) {
        pending_search_results_.clear();
        pending_search_results_.reserve(results.hits.size());
        for (auto& hit : results.hits) {
            pending_search_results_.push_back({
                std::move(hit.path), hit.line, hit.column, std::move(hit.snippet), hit.identity
            });
        }
        pending_search_result_index_ = 0;
    }
    AppendOutput(wxString::Format("[search] %llu match(es) in %llu file(s)%s%s%s\n",
        static_cast<unsigned long long>(results.hits.size()),
        static_cast<unsigned long long>(results.files_scanned),
        results.cancelled ? " — cancelled" : "",
        results.truncated ? " — scan limit reached; results may be incomplete" : "",
        results.skipped_paths.empty() ? "" : " — some paths were skipped"));
    for (const auto& skipped : results.skipped_paths)
        AppendOutput("[search] skipped or unreadable path: " + FromUtf8(skipped) + "\n");
    output_tabs_->SetSelection(2);
    if (close_after_search_) {
        close_after_search_ = false;
        CallAfter([this] { Close(); });
    } else if (!pending_search_results_.empty()) {
        // Insert a bounded batch per timer turn so a 10,000-result search does
        // not monopolize the GUI event loop while populating the report list.
        search_results_timer_.Start(1);
    }
}

void MainFrame::OnSearchResultsTimer(wxTimerEvent&) {
    constexpr std::size_t BatchSize = 50;
    const auto end = std::min(
        pending_search_result_index_ + BatchSize, pending_search_results_.size());
    search_results_->Freeze();
    for (; pending_search_result_index_ < end; ++pending_search_result_index_) {
        const auto& hit = pending_search_results_[pending_search_result_index_];
        const auto path_u8 = hit.path.u8string();
        const auto row = search_results_->InsertItem(search_results_->GetItemCount(),
            wxString::FromUTF8(reinterpret_cast<const char*>(path_u8.data()), path_u8.size()));
        if (row < 0) continue;
        const auto row_index = static_cast<std::size_t>(row);
        if (search_result_identities_.size() <= row_index)
            search_result_identities_.resize(row_index + 1);
        search_result_identities_[row_index] = hit.identity;
        search_results_->SetItem(row, 1, wxString::Format("%llu", static_cast<unsigned long long>(hit.line)));
        search_results_->SetItem(row, 2, wxString::Format("%llu", static_cast<unsigned long long>(hit.column)));
        search_results_->SetItem(row, 3, "Match");
        search_results_->SetItem(row, 4, wxString::FromUTF8(hit.snippet.data(), hit.snippet.size()));
    }
    search_results_->Thaw();
    if (pending_search_result_index_ >= pending_search_results_.size()) {
        search_results_timer_.Stop();
        pending_search_results_.clear();
        pending_search_result_index_ = 0;
    }
}

void MainFrame::OnSearchResultActivated(wxListEvent& event) {
    const auto row = event.GetIndex();
    if (row < 0) return;
    const auto path_text = search_results_->GetItemText(row, 0);
    const FileIdentity* expected_identity =
        static_cast<std::size_t>(row) < search_result_identities_.size() &&
        search_result_identities_[static_cast<std::size_t>(row)].valid
            ? &search_result_identities_[static_cast<std::size_t>(row)] : nullptr;
    OpenDocumentPath(path_text, expected_identity);
    auto* editor = CurrentEditor();
    if (!editor) return;
    const auto document = documents_.find(editor);
    std::error_code expected_error, document_error;
    const auto expected_path = std::filesystem::weakly_canonical(
        std::filesystem::path(path_text.ToStdWstring()), expected_error);
    const auto document_path = document != documents_.end()
        ? std::filesystem::weakly_canonical(document->second.path, document_error)
        : std::filesystem::path{};
    if (document == documents_.end() ||
        (expected_identity &&
         (!document->second.file_identity.valid ||
          !SameFileIdentity(document->second.file_identity, *expected_identity))) ||
        (expected_error ? std::filesystem::path(path_text.ToStdWstring()) : expected_path) !=
            (document_error ? document->second.path : document_path)) {
        AppendOutput("[search] could not open the selected result document\n");
        return;
    }
    long line = 1;
    long column = 1;
    search_results_->GetItemText(row, 1).ToLong(&line);
    search_results_->GetItemText(row, 2).ToLong(&column);
    const int target_line = std::clamp(static_cast<int>(line) - 1, 0, editor->GetLineCount() - 1);
    const auto position = editor->FindColumn(target_line, std::max(0, static_cast<int>(column) - 1));
    editor->GotoPos(position);
    editor->SetSelection(position, std::min(position + 1, editor->GetTextLength()));
    editor->EnsureCaretVisible();
    notebook_->SetFocus();
}

void MainFrame::PopulateProjectNode(const wxTreeItemId& item) {
    auto* data = dynamic_cast<ProjectPathData*>(project_->GetItemData(item));
    if (!data || data->loaded) return;
    project_->DeleteChildren(item);

#ifdef _WIN32
    // Hold every ancestor without FILE_SHARE_DELETE while enumerating. This
    // prevents a checked parent from being replaced by a junction mid-walk.
    WinDirectoryChainGuard directory_guard;
    std::error_code directory_lock_error;
    if (!LockWindowsDirectoryChain(data->path, directory_guard, directory_lock_error)) {
        project_->AppendItem(item, "Directory listing incomplete (expand to retry)");
        data->loaded = false;
        return;
    }
#endif

    struct Entry {
        std::filesystem::path path;
        std::filesystem::path relative_path;
        std::vector<FileIdentity> ancestor_identities;
        FileIdentity identity;
        bool directory{};
    };
    std::vector<Entry> entries;
    std::error_code error;
    std::size_t scanned = 0;
    bool truncated = false;
    bool incomplete = false;
    bool global_truncated = false;
#ifdef _WIN32
    std::filesystem::directory_iterator iterator(data->path,
        std::filesystem::directory_options::none, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        if (scanned++ >= 1500) { truncated = true; break; }
        const auto filename = iterator->path().filename().u8string();
        const std::string name(reinterpret_cast<const char*>(filename.data()), filename.size());
        if (name == "." || name == ".." || name == ".git" || name == "target" ||
            name == "build" || name == ".venv" || name == "node_modules" ||
            name == "__pycache__") continue;
        std::error_code type_error;
        const auto link_status = iterator->symlink_status(type_error);
        if (type_error) { incomplete = true; continue; }
        if (std::filesystem::is_symlink(link_status)) continue;
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            incomplete = true;
            continue;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
#endif
        const bool directory = std::filesystem::is_directory(link_status);
        std::string extension;
        if (!directory) {
            const auto extension_bytes = iterator->path().extension().u8string();
            extension.assign(reinterpret_cast<const char*>(extension_bytes.data()), extension_bytes.size());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            static const std::unordered_set<std::string> text_extensions = {
                ".rbl", ".txt", ".md", ".rst", ".json", ".jsonl", ".toml", ".yaml", ".yml",
                ".ini", ".cfg", ".conf", ".xml", ".csv", ".log", ".py", ".pyi", ".rs",
                ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".js", ".jsx", ".ts",
                ".tsx", ".html", ".css", ".scss", ".sql", ".sh", ".bash", ".ps1", ".bat",
                ".cmd", ".go", ".java", ".kt", ".lua", ".s", ".asm", ".make", ".cmake"
            };
            if (!text_extensions.contains(extension)) continue;
        }
        entries.push_back({iterator->path(), {}, {}, {}, directory});
    }
#else
    if (!data->root && data->relative_path.empty() && data->ancestor_identities.empty()) {
        data->root = OpenProjectRoot(data->path, error);
        if (data->root) data->expected_identity = data->root->identity;
    }
    if (!error && (!data->root || data->root->fd < 0 || !data->expected_identity.valid)) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
    }
    if (!error) {
        ScopedProjectFd current(::dup(data->root->fd));
        if (current.get() < 0) error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        struct stat root_info{};
        if (!error && (::fstat(current.get(), &root_info) != 0 ||
                       !SameProjectIdentity(data->root->identity, root_info))) {
            error = std::make_error_code(std::errc::state_not_recoverable);
        }
        std::size_t component_index = 0;
        for (const auto& component : data->relative_path) {
            const auto name = component.native();
            if (name.empty() || name == "." || name == ".." ||
                component_index >= data->ancestor_identities.size()) {
                error = std::make_error_code(std::errc::invalid_argument);
                break;
            }
            int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
            flags |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
            flags |= O_NONBLOCK;
#endif
            const int next_fd = ::openat(current.get(), name.c_str(), flags);
            if (next_fd < 0) {
                error = std::error_code(PosixErrorOrIoError(), std::generic_category());
                break;
            }
            ScopedProjectFd next(next_fd);
            struct stat component_info{};
            if (::fstat(next.get(), &component_info) != 0) {
                error = std::error_code(PosixErrorOrIoError(), std::generic_category());
                break;
            }
            if (!S_ISDIR(component_info.st_mode) ||
                !SameProjectIdentity(data->ancestor_identities[component_index], component_info)) {
                error = std::make_error_code(std::errc::state_not_recoverable);
                break;
            }
            current = std::move(next);
            ++component_index;
        }
        if (!error && component_index != data->ancestor_identities.size())
            error = std::make_error_code(std::errc::invalid_argument);
        struct stat directory_info{};
        if (!error && (::fstat(current.get(), &directory_info) != 0 ||
                       !SameProjectIdentity(data->expected_identity, directory_info)))
            error = std::make_error_code(std::errc::state_not_recoverable);

        int scan_fd = !error ? ::dup(current.get()) : -1;
        DIR* directory_stream = scan_fd >= 0 ? ::fdopendir(scan_fd) : nullptr;
        if (!error && !directory_stream) {
            const int saved_errno = PosixErrorOrIoError();
            if (scan_fd >= 0) ::close(scan_fd);
            error = std::error_code(saved_errno, std::generic_category());
        }
        if (directory_stream) {
            while (true) {
                errno = 0;
                const dirent* entry = ::readdir(directory_stream);
                if (!entry) {
                    if (errno != 0) error = std::error_code(PosixErrorOrIoError(), std::generic_category());
                    break;
                }
                if (scanned++ >= 1500) { truncated = true; break; }
                const std::string name(entry->d_name);
                if (name == "." || name == ".." || name == ".git" || name == "target" ||
                    name == "build" || name == ".venv" || name == "node_modules" ||
                    name == "__pycache__") continue;
                struct stat entry_info{};
                if (::fstatat(::dirfd(directory_stream), name.c_str(), &entry_info,
                              AT_SYMLINK_NOFOLLOW) != 0) {
                    incomplete = true;
                    continue;
                }
                if (S_ISLNK(entry_info.st_mode)) continue;
                const bool directory = S_ISDIR(entry_info.st_mode);
                if (!directory && !S_ISREG(entry_info.st_mode)) continue;
                std::string extension;
                if (!directory) {
                    extension = std::filesystem::path(name).extension().native();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    static const std::unordered_set<std::string> text_extensions = {
                        ".rbl", ".txt", ".md", ".rst", ".json", ".jsonl", ".toml", ".yaml", ".yml",
                        ".ini", ".cfg", ".conf", ".xml", ".csv", ".log", ".py", ".pyi", ".rs",
                        ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".js", ".jsx", ".ts",
                        ".tsx", ".html", ".css", ".scss", ".sql", ".sh", ".bash", ".ps1", ".bat",
                        ".cmd", ".go", ".java", ".kt", ".lua", ".s", ".asm", ".make", ".cmake"
                    };
                    if (!text_extensions.contains(extension)) continue;
                }
                const FileIdentity identity{
                    static_cast<std::uint64_t>(entry_info.st_dev),
                    static_cast<std::uint64_t>(entry_info.st_ino), true,
                    static_cast<std::uint64_t>(entry_info.st_nlink)};
                auto ancestors = data->ancestor_identities;
                ancestors.push_back(identity);
                const auto relative = data->relative_path / std::filesystem::path(name);
                entries.push_back({data->path / std::filesystem::path(name), relative,
                                   std::move(ancestors), identity, directory});
            }
            ::closedir(directory_stream);
        }
    }
#endif
    if (error || incomplete) {
        project_->AppendItem(item, "Directory listing incomplete (expand to retry)");
        data->loaded = false;
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.directory != right.directory) return left.directory > right.directory;
        return left.path.filename().native() < right.path.filename().native();
    });
    const auto current_nodes = static_cast<std::size_t>(project_->GetCount());
    const auto available_nodes = current_nodes < MaxProjectTreeNodes
        ? MaxProjectTreeNodes - current_nodes : 0;
    std::size_t required_nodes = 0;
    for (const auto& entry : entries) required_nodes += entry.directory ? 2U : 1U;
    if (required_nodes > available_nodes) {
        // Each directory consumes two tree nodes (the directory and its lazy
        // expansion placeholder). Reserve one node for an explicit truncation
        // marker so the global cap includes every visible node.
        std::size_t budget = available_nodes > 0 ? available_nodes - 1 : 0;
        std::size_t keep = 0;
        std::size_t used = 0;
        while (keep < entries.size()) {
            const std::size_t cost = entries[keep].directory ? 2U : 1U;
            if (cost > budget - used) break;
            used += cost;
            ++keep;
        }
        entries.resize(keep);
        truncated = true;
        global_truncated = true;
    }
    for (const auto& entry : entries) {
        const auto child = entry.path;
        const auto extension_bytes = child.extension().u8string();
        std::string extension(reinterpret_cast<const char*>(extension_bytes.data()), extension_bytes.size());
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const int image = entry.directory ? 1 : (extension == ".rbl" ? 2 : 3);
#ifdef _WIN32
        const auto child_item = project_->AppendItem(item, PathLabel(child), image, image,
            new ProjectPathData(child));
#else
        const auto child_item = project_->AppendItem(item, PathLabel(child), image, image,
            new ProjectPathData(child, data->root, entry.relative_path,
                                entry.ancestor_identities, entry.identity));
#endif
        if (entry.directory) {
            project_->SetItemBold(child_item, true);
            project_->AppendItem(child_item, "...", 1, 1);
        } else if (extension == ".rbl") {
            project_->SetItemTextColour(child_item, wxColour(145, 190, 255));
        }
    }
    if (global_truncated && project_->GetCount() < MaxProjectTreeNodes) {
        project_->AppendItem(item, "More entries omitted (global project tree node limit)");
    } else if (truncated && !global_truncated) {
        project_->AppendItem(item, "More entries omitted (1500-entry scan limit)");
    } else if (truncated) {
        AppendOutput("[project] global tree node limit reached; further entries are not expanded\n");
    }
    data->loaded = true;
}

