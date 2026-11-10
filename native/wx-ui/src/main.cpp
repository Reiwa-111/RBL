#include "main_frame.hpp"

#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/cmdline.h>
#include <wx/event.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>
#include <wx/window.h>

#ifdef __WXMSW__
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class RBLStudioApp final : public wxApp {
public:
    int FilterEvent(wxEvent& event) override {
#ifdef __WXMSW__
        static thread_local bool forwardingWheel = false;
        if (!forwardingWheel && event.GetEventType() == wxEVT_MOUSEWHEEL) {
            auto* mouse = dynamic_cast<wxMouseEvent*>(&event);
            auto* source = dynamic_cast<wxWindow*>(event.GetEventObject());
            if (mouse && source && mouse->GetWheelRotation() != 0) {
                wxWindow* target = nullptr;
                for (wxWindow* current = wxFindWindowAtPoint(wxGetMousePosition());
                     current; current = current->GetParent()) {
                    if (wxDynamicCast(current, wxTreeCtrl) ||
                        wxDynamicCast(current, wxListCtrl) ||
                        wxDynamicCast(current, wxListBox)) {
                        target = current;
                        break;
                    }
                    if (current == GetTopWindow()) {
                        target = nullptr;
                        break;
                    }
                }

                // Windows normally routes WM_MOUSEWHEEL to the focused window.
                // Forward it to the file/list control under the pointer instead.
                if (target && target != source && target->IsShownOnScreen() && target->IsEnabled()) {
                    const HWND handle = static_cast<HWND>(target->GetHandle());
                    if (handle) {
                        WORD keyState = 0;
                        if (mouse->ShiftDown()) keyState |= MK_SHIFT;
                        if (mouse->ControlDown()) keyState |= MK_CONTROL;
                        if (mouse->LeftIsDown()) keyState |= MK_LBUTTON;
                        if (mouse->MiddleIsDown()) keyState |= MK_MBUTTON;
                        if (mouse->RightIsDown()) keyState |= MK_RBUTTON;

                        const wxPoint screen = wxGetMousePosition();
                        const WPARAM wParam = MAKEWPARAM(keyState,
                            static_cast<WORD>(static_cast<short>(mouse->GetWheelRotation())));
                        const LPARAM lParam = MAKELPARAM(
                            static_cast<short>(screen.x), static_cast<short>(screen.y));
                        const UINT message = mouse->GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL
                            ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
                        forwardingWheel = true;
                        SendMessageW(handle, message, wParam, lParam);
                        forwardingWheel = false;
                        return 0;
                    }
                }
            }
        }
#endif
        return wxApp::FilterEvent(event);
    }

    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddParam("source-file", wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL | wxCMD_LINE_PARAM_MULTIPLE);
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        for (std::size_t index = 0; index < parser.GetParamCount(); ++index)
            startup_files_.Add(parser.GetParam(index));
        return wxApp::OnCmdLineParsed(parser);
    }

    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        // The application icon is loaded from the packaged ICO. wxWidgets does
        // not register the ICO handler unless image handlers are initialized.
        // Without this, startup shows a modal "No image handler" warning and
        // silently falls back to the temporary text-drawn icon.
        wxInitAllImageHandlers();
        auto* frame = new MainFrame();
        frame->Show();
        frame->OpenStartupFiles(startup_files_);
        return true;
    }

private:
    wxArrayString startup_files_;
};

wxIMPLEMENT_APP(RBLStudioApp);

