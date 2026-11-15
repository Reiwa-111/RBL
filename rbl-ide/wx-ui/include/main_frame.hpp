#pragma once

#include <wx/aui/aui.h>
#include <wx/arrstr.h>
#include <wx/aui/auibook.h>
#include <wx/aui/auibar.h>
#include <wx/imaglist.h>
#include <wx/frame.h>
#include <wx/fileconf.h>
#include <wx/process.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <atomic>
#include <thread>
#include <vector>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include "file_metadata.hpp"
#include "theme.hpp"

#include <filesystem>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class wxAuiNotebook;
class wxAuiToolBar;
class wxButton;
class wxPanel;
class wxCheckBox;
class wxStaticText;
class wxDPIChangedEvent;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;
    void OpenStartupFile(const wxString& path);
    void OpenStartupFiles(const wxArrayString& paths);

private:
    struct Document {
        enum class Encoding { Utf8, Utf8Bom, Utf16Le, Utf16Be };
        wxStyledTextCtrl* editor{};
        std::filesystem::path path;
        bool is_rbl{true};
        std::size_t saved_hash{};
        Encoding encoding{Encoding::Utf8};
        std::size_t undo_bytes{};
        bool folding_disabled{};
        bool fold_scan_active{};
        int fold_next_line{};
        int fold_line_count{};
        int fold_depth{};
        bool fold_in_triple_string{};
        rbl::wxui::FileIdentity file_identity;
        // Hard links retained by failed Windows saves are owned recovery
        // artifacts, not additional user-visible aliases of the document.
        std::uint64_t retained_original_backup_links{};
    };
    struct PendingSearchResult {
        std::filesystem::path path;
        std::size_t line{};
        std::size_t column{};
        std::string snippet;
        rbl::wxui::FileIdentity identity;
    };
    struct PendingDiagnostic {
        wxString path;
        wxString line;
        wxString column;
        wxString severity;
        wxString message;
        bool is_error{};
    };

    wxAuiManager manager_;
    wxAuiNotebook* notebook_{};
    wxAuiToolBar* toolbar_{};
    wxPanel* find_panel_{};
    wxTextCtrl* find_query_{};
    wxTextCtrl* replace_query_{};
    wxCheckBox* find_match_case_{};
    wxStaticText* find_status_{};
    wxTreeCtrl* project_{};
    wxImageList* project_images_{};
    wxTextCtrl* output_{};
    wxAuiNotebook* output_tabs_{};
    wxListCtrl* problems_{};
    wxListCtrl* search_results_{};
    wxTextCtrl* input_{};
    wxButton* send_button_{};
    wxButton* eof_button_{};
    wxProcess* worker_{};
    long worker_pid_{-1};
    wxTimer worker_timer_;
    wxTimer fold_timer_;
    wxTimer auto_check_timer_;
    wxTimer search_results_timer_;
    wxTimer diagnostics_timer_;
    wxStyledTextCtrl* auto_check_editor_{};
    std::unordered_set<wxStyledTextCtrl*> fold_pending_;
    std::string worker_buffer_;
    std::string worker_error_buffer_;
    std::string diagnostic_buffer_;
    wxString pending_stdin_;
    bool pending_stdin_active_{};
    bool stdin_eof_pending_{};
    std::atomic_bool search_cancel_{false};
    std::thread search_thread_;
    bool search_running_{};
    bool close_after_search_{};
    bool diagnostics_truncated_{};
    bool diagnostics_stale_{};
    bool diagnostic_input_finished_{};
    bool diagnostic_discarding_oversized_line_{};
    bool diagnostics_sorted_{};
    bool diagnostics_notice_emitted_{};
    bool worker_ready_{};
    bool task_running_{};
    bool task_started_{};
    bool close_after_task_{};
    bool active_task_accepts_stdin_{};
    bool restart_worker_pending_{};
    bool worker_restart_after_task_{};
    bool worker_shutdown_kill_sent_{};
    std::chrono::steady_clock::time_point worker_shutdown_deadline_{};
    std::uint64_t next_request_id_{1};
    std::uint64_t active_request_id_{};
    std::filesystem::path application_root_;
    std::filesystem::path workspace_root_;
    std::string application_root_error_;
    std::filesystem::path active_snapshot_path_;
    std::filesystem::path active_source_path_;
    wxStyledTextCtrl* active_snapshot_editor_{};
    std::unique_ptr<wxFileConfig> ui_config_;
    int editor_font_size_{11};
    int editor_tab_size_{4};
    wxString ui_theme_{"RBL Dark"};
    wxString editor_color_scheme_{"RBL Dark"};
    bool editor_line_numbers_{true};
    bool editor_soft_wrap_{false};
    bool editor_indent_guides_{true};
    bool editor_auto_check_{true};
    bool editor_auto_save_{false};
    bool editor_syntax_highlighting_{true};
    bool compact_mode_{};
    wxString default_layout_;
    wxString toolchain_mode_{"auto"};
    wxString wsl_distribution_{"Ubuntu-24.04"};
    std::unordered_map<wxWindow*, Document> documents_;
    std::vector<std::filesystem::path> recently_closed_;
    std::vector<PendingSearchResult> pending_search_results_;
    std::vector<rbl::wxui::FileIdentity> search_result_identities_;
    std::size_t pending_search_result_index_{};
    std::vector<PendingDiagnostic> pending_diagnostics_;
    std::size_t pending_diagnostic_render_index_{};

    void BuildLayout();
    void BuildMenus();
    void LoadUiState();
    void SaveUiState();
    void OnDpiChanged(wxDPIChangedEvent& event);
    void ApplyDpiMetrics(double scale_ratio);
    void RebuildDpiAssets();
    void ApplyEditorSettings(wxStyledTextCtrl* editor);
    void ApplyUiTheme();
    void UpdateToolbarState();
    void UpdateDocumentLimits();
    void NewDocument();
    void NewTextDocument();
    void OpenDocument();
    void OpenDocumentPath(const wxString& path,
                          const rbl::wxui::FileIdentity* expected_identity = nullptr);
    bool SaveDocument(wxWindow* page, bool save_as);
    bool CanClosePage(wxWindow* page);
    bool SaveCurrent();
    void CloseCurrent();
    void RememberClosedDocument(wxWindow* page);
    void AppendOutput(const wxString& text, bool parse_diagnostics = false);
    void ParseDiagnostics(const wxString& text);
    void StartWorker();
    void RestartWorker();
    bool SendWorkerLine(const wxString& line);
    bool SendWorkerRequest(const wxString& json);
    void StartTask(const wxString& task);
    void PollWorker();
    void HandleWorkerEvent(const wxString& line);
    void SendStdin();
    void SendStdinEof();
    wxStyledTextCtrl* CurrentEditor() const;
    void ConfigureEditor(wxStyledTextCtrl* editor);
    void StyleRbl(wxStyledTextCtrl* editor, int requested_end);
    void UpdateFolding(wxStyledTextCtrl* editor);
    void UpdateDocumentTitle(wxStyledTextCtrl* editor);
    void PopulateProjectNode(const wxTreeItemId& item);

    void OnNew(wxCommandEvent& event);
    void OnNewText(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnOpenProject(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnCloseTab(wxAuiNotebookEvent& event);
    void OnPageChanged(wxAuiNotebookEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnRun(wxCommandEvent& event);
    void OnCheck(wxCommandEvent& event);
    void OnBuild(wxCommandEvent& event);
    void OnAssembly(wxCommandEvent& event);
    void OnTests(wxCommandEvent& event);
    void OnBenchmarks(wxCommandEvent& event);
    void OnDoctor(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnSendStdin(wxCommandEvent& event);
    void OnWorkerTimer(wxTimerEvent& event);
    void OnWorkerExit(wxProcessEvent& event);
    void OnEditorStyleNeeded(wxStyledTextEvent& event);
    void OnEditorModified(wxStyledTextEvent& event);
    void OnEditorSavePoint(wxStyledTextEvent& event);
    void OnEditorUpdateUi(wxStyledTextEvent& event);
    void OnEditorCharAdded(wxStyledTextEvent& event);
    void OnFoldTimer(wxTimerEvent& event);
    void OnAutoCheckTimer(wxTimerEvent& event);
    void OnFind(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnFindNext(wxCommandEvent& event);
    void OnFindPrevious(wxCommandEvent& event);
    void ShowFindPanel(bool show_replace);
    void FindInCurrentEditor(bool backwards);
    void ReplaceInCurrentEditor(bool all);
    void OnCloseCurrentMenu(wxCommandEvent& event);
    void OnFrameClose(wxCloseEvent& event);
    void OnStdinEof(wxCommandEvent& event);
    void OnProjectFileActivated(wxTreeEvent& event);
    void OnProjectItemExpanding(wxTreeEvent& event);
    void OnProjectItemCollapsed(wxTreeEvent& event);
    void OnDiagnosticActivated(wxListEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnToggleProjectPane(wxCommandEvent& event);
    void OnToggleOutputPane(wxCommandEvent& event);
    void OnResetLayout(wxCommandEvent& event);
    void OnFindInFiles(wxCommandEvent& event);
    void OnCancelSearch(wxCommandEvent& event);
    void OnProjectSearchDone(wxThreadEvent& event);
    void OnSearchResultsTimer(wxTimerEvent& event);
    void OnDiagnosticsTimer(wxTimerEvent& event);
    void OnSearchResultActivated(wxListEvent& event);
    void OnGoToLine(wxCommandEvent& event);
    void OnQuickOpen(wxCommandEvent& event);
    void OnCommandPalette(wxCommandEvent& event);
    void OnQuickReference(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnSaveAll(wxCommandEvent& event);
    void OnCloseAll(wxCommandEvent& event);
    void OnCloseOtherTabs(wxCommandEvent& event);
    void OnReopenClosedTab(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

