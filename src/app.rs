use crate::{
    editor::{self, EditorTab},
    process::{self, ProcessCommand, ProcessEvent, TaskKind},
    settings::{self, Settings},
};
use crossbeam_channel::{bounded, Receiver, Sender};
use eframe::egui::{self, Color32, RichText};
use std::{
    collections::HashSet,
    fs,
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

const OUTPUT_LIMIT_BYTES: usize = 1_000_000;
const MAX_OPEN_DOCUMENTS_BYTES: usize = editor::MAX_OPEN_DOCUMENT_MEMORY_BYTES;
const MAX_OPEN_TABS: usize = 32;
const MAX_PROJECT_SEARCH_RESULTS: usize = 500;
const MAX_PROJECT_SEARCH_BYTES: u64 = 256 * 1024 * 1024;
const MAX_PROCESS_EVENTS_PER_FRAME: usize = 128;

fn contains_case_insensitive_chunked(
    line: &str,
    query_lower: &str,
    cancel: &AtomicBool,
) -> Option<bool> {
    if query_lower.is_empty() {
        return Some(true);
    }
    let overlap = query_lower.len().saturating_add(8);
    let mut start = 0usize;
    loop {
        if cancel.load(Ordering::Relaxed) {
            return None;
        }
        let mut end = start.saturating_add(64 * 1024).min(line.len());
        while end > start && !line.is_char_boundary(end) {
            end -= 1;
        }
        if end == start {
            end = line.len();
        }
        let mut chunk_end = end.saturating_add(overlap).min(line.len());
        while chunk_end < line.len() && !line.is_char_boundary(chunk_end) {
            chunk_end += 1;
        }
        if line[start..chunk_end].to_lowercase().contains(query_lower) {
            return Some(true);
        }
        if end == line.len() {
            return Some(false);
        }
        start = end;
    }
}

struct ProjectSearchResult {
    path: PathBuf,
    line: usize,
    snippet: String,
}

fn can_open_document(tabs: &[EditorTab], candidate: &EditorTab) -> bool {
    if tabs.len() >= MAX_OPEN_TABS {
        return false;
    }
    let open_bytes = tabs
        .iter()
        .map(editor::memory_bytes)
        .fold(0usize, usize::saturating_add);
    open_bytes.saturating_add(editor::memory_bytes(candidate)) <= MAX_OPEN_DOCUMENTS_BYTES
}

fn comparable_path(path: &Path) -> PathBuf {
    if let Ok(canonical) = path.canonicalize() {
        return canonical;
    }
    let Some(file_name) = path.file_name() else {
        return path.to_path_buf();
    };
    path.parent()
        .and_then(|parent| parent.canonicalize().ok())
        .map(|parent| parent.join(file_name))
        .unwrap_or_else(|| path.to_path_buf())
}

fn duplicate_open_tab(tabs: &[EditorTab], target: &Path, except: usize) -> Option<usize> {
    let target = comparable_path(target);
    tabs.iter().enumerate().find_map(|(index, tab)| {
        (index != except
            && tab
                .path
                .as_deref()
                .is_some_and(|path| comparable_path(path) == target))
        .then_some(index)
    })
}

fn match_range_is_current(text: &str, query: &str, range: (usize, usize)) -> bool {
    let (start, end) = range;
    if query.is_empty() || start > end || end > text.chars().count() {
        return false;
    }
    let byte_start = editor::char_to_byte(text, start);
    let byte_end = editor::char_to_byte(text, end);
    text.get(byte_start..byte_end) == Some(query)
}

fn should_handle_global_shortcuts(editor_focused: bool, wants_keyboard_input: bool) -> bool {
    editor_focused || !wants_keyboard_input
}

fn move_list_selection(selection: &mut usize, count: usize, up: bool, down: bool) {
    if count == 0 {
        *selection = 0;
    } else if down {
        *selection = (*selection).saturating_add(1).min(count - 1);
    } else if up {
        *selection = (*selection).saturating_sub(1);
    } else {
        *selection = (*selection).min(count - 1);
    }
}

fn prepare_dialog_input(is_open: bool, query: &mut String, selection: &mut usize) {
    if !is_open {
        query.clear();
        *selection = 0;
    }
}

enum ProjectSearchEvent {
    Result(ProjectSearchResult),
    Finished {
        files_scanned: usize,
        matches: usize,
        cancelled: bool,
    },
}

struct ProjectTreeEvent {
    entries: Vec<(PathBuf, usize, bool)>,
    truncated: bool,
}

fn visible_project_entries(
    entries: &[(PathBuf, usize, bool)],
    collapsed_dirs: &HashSet<PathBuf>,
) -> Vec<(PathBuf, usize, bool)> {
    entries
        .iter()
        .filter(|(path, _, _)| {
            !collapsed_dirs
                .iter()
                .any(|collapsed| path != collapsed && path.starts_with(collapsed))
        })
        .cloned()
        .collect()
}

fn append_output(buffer: &mut String, line_starts: &mut Vec<u32>, text: &str) {
    if text.is_empty() {
        return;
    }
    if buffer.is_empty() {
        line_starts.clear();
        line_starts.push(0);
    } else if buffer.ends_with('\n') && line_starts.last().copied() != Some(buffer.len() as u32) {
        line_starts.push(buffer.len() as u32);
    }
    let base = buffer.len();
    buffer.push_str(text);
    for (offset, byte) in text.bytes().enumerate() {
        if byte == b'\n' && offset + 1 < text.len() {
            line_starts.push((base + offset + 1) as u32);
        }
    }
    if buffer.ends_with('\n') && line_starts.last().copied() != Some(buffer.len() as u32) {
        line_starts.push(buffer.len() as u32);
    }
    if buffer.len() > OUTPUT_LIMIT_BYTES {
        let mut boundary = buffer.len() - OUTPUT_LIMIT_BYTES;
        while !buffer.is_char_boundary(boundary) {
            boundary += 1;
        }
        buffer.drain(..boundary);
        const MARKER: &str = "[Earlier output truncated]\n";
        if !buffer.starts_with(MARKER) {
            buffer.insert_str(0, MARKER);
        }
        *line_starts = build_output_line_starts(buffer);
    }
}

fn build_output_line_starts(text: &str) -> Vec<u32> {
    if text.is_empty() {
        return Vec::new();
    }
    let mut starts = vec![0];
    for (index, byte) in text.bytes().enumerate() {
        if byte == b'\n' && index + 1 < text.len() {
            starts.push((index + 1) as u32);
        }
    }
    if text.ends_with('\n') {
        starts.push(text.len() as u32);
    }
    starts
}

fn output_line<'a>(text: &'a str, line_starts: &[u32], index: usize) -> &'a str {
    let Some(start) = line_starts
        .get(index)
        .copied()
        .map(|offset| offset as usize)
    else {
        return "";
    };
    let mut end = line_starts
        .get(index + 1)
        .copied()
        .map(|offset| offset as usize)
        .unwrap_or(text.len());
    if end > start && text.as_bytes()[end - 1] == b'\n' {
        end -= 1;
    }
    if end > start && text.as_bytes()[end - 1] == b'\r' {
        end -= 1;
    }
    &text[start..end]
}

pub struct RblStudioApp {
    root: PathBuf,
    window_title: String,
    settings: Settings,
    tabs: Vec<EditorTab>,
    active: usize,
    output: String,
    output_line_starts: Vec<u32>,
    problems: String,
    problems_line_starts: Vec<u32>,
    console: String,
    console_line_starts: Vec<u32>,
    output_tab: usize,
    status: String,
    tx: Sender<ProcessEvent>,
    rx: Receiver<ProcessEvent>,
    running: bool,
    process_worker: Option<JoinHandle<()>>,
    cancel: Option<Arc<AtomicBool>>,
    snapshot_alias: Option<Vec<(String, String)>>,
    current_task: Option<TaskKind>,
    process_input_tx: Option<Sender<ProcessCommand>>,
    run_input: String,
    run_input_focus_pending: bool,
    auto_check_at: Option<(u64, Instant)>,
    show_settings: bool,
    show_about: bool,
    show_quick_reference: bool,
    show_close_prompt: bool,
    pending_close: Option<u64>,
    file_tree_open: bool,
    project_files: Vec<(PathBuf, usize, bool)>,
    project_tree_entries: Vec<(PathBuf, usize, bool)>,
    project_file_paths: Vec<PathBuf>,
    project_tree_truncated: bool,
    project_tree_tx: Sender<ProjectTreeEvent>,
    project_tree_rx: Receiver<ProjectTreeEvent>,
    project_tree_running: bool,
    project_tree_refresh_pending: bool,
    collapsed_dirs: HashSet<PathBuf>,
    show_find: bool,
    find_focus_pending: bool,
    replace_mode: bool,
    find_query: String,
    replace_query: String,
    find_status: String,
    pending_selection: Option<(usize, usize)>,
    current_match: Option<(usize, usize)>,
    show_go_to_line: bool,
    goto_focus_pending: bool,
    go_to_line: String,
    show_command_palette: bool,
    palette_focus_pending: bool,
    command_query: String,
    palette_selection: usize,
    show_quick_open: bool,
    quick_open_focus_pending: bool,
    quick_open_query: String,
    quick_open_selection: usize,
    show_find_in_files: bool,
    project_search_focus_pending: bool,
    project_search_query: String,
    project_search_results: Vec<ProjectSearchResult>,
    project_search_tx: Sender<ProjectSearchEvent>,
    project_search_rx: Receiver<ProjectSearchEvent>,
    project_search_cancel: Option<Arc<AtomicBool>>,
    project_search_running: bool,
}

impl RblStudioApp {
    pub fn new(cc: &eframe::CreationContext<'_>, root: PathBuf, initial: Vec<PathBuf>) -> Self {
        let (settings, settings_warning) = settings::load_with_warning();
        apply_theme(&cc.egui_ctx, &settings.theme);
        let (tx, rx) = bounded(256);
        let (project_search_tx, project_search_rx) = bounded(128);
        let (project_tree_tx, project_tree_rx) = bounded(1);
        let mut app = Self {
            root,
            window_title: "RBL Studio".into(),
            settings,
            tabs: Vec::new(),
            active: 0,
            output: String::new(),
            output_line_starts: Vec::new(),
            problems: String::new(),
            problems_line_starts: Vec::new(),
            console: String::new(),
            console_line_starts: Vec::new(),
            output_tab: 0,
            status: "Ready".into(),
            tx,
            rx,
            running: false,
            process_worker: None,
            cancel: None,
            snapshot_alias: None,
            current_task: None,
            process_input_tx: None,
            run_input: String::new(),
            run_input_focus_pending: false,
            auto_check_at: None,
            show_settings: false,
            show_about: false,
            show_quick_reference: false,
            show_close_prompt: false,
            pending_close: None,
            file_tree_open: true,
            project_files: Vec::new(),
            project_tree_entries: Vec::new(),
            project_file_paths: Vec::new(),
            project_tree_truncated: false,
            project_tree_tx,
            project_tree_rx,
            project_tree_running: false,
            project_tree_refresh_pending: false,
            collapsed_dirs: HashSet::new(),
            show_find: false,
            find_focus_pending: false,
            replace_mode: false,
            find_query: String::new(),
            replace_query: String::new(),
            find_status: String::new(),
            pending_selection: None,
            current_match: None,
            show_go_to_line: false,
            goto_focus_pending: false,
            go_to_line: String::new(),
            show_command_palette: false,
            palette_focus_pending: false,
            command_query: String::new(),
            palette_selection: 0,
            show_quick_open: false,
            quick_open_focus_pending: false,
            quick_open_query: String::new(),
            quick_open_selection: 0,
            show_find_in_files: false,
            project_search_focus_pending: false,
            project_search_query: String::new(),
            project_search_results: Vec::new(),
            project_search_tx,
            project_search_rx,
            project_search_cancel: None,
            project_search_running: false,
        };
        app.refresh_project_files();
        for path in initial {
            app.open_file(path);
        }
        if app.tabs.is_empty() {
            app.new_file();
        }
        if let Some(warning) = settings_warning {
            app.status = warning;
        }
        app
    }

    fn current(&self) -> Option<&EditorTab> {
        self.tabs.get(self.active)
    }

    fn refresh_project_files(&mut self) {
        if self.project_tree_running {
            self.project_tree_refresh_pending = true;
            return;
        }
        self.project_tree_running = true;
        let root = self.root.clone();
        let tx = self.project_tree_tx.clone();
        thread::spawn(move || {
            let mut entries = Vec::new();
            let mut scanned_entries = 0usize;
            let truncated = collect_files(
                &root,
                &root,
                0,
                &mut entries,
                &HashSet::new(),
                &mut scanned_entries,
            );
            let _ = tx.send(ProjectTreeEvent { entries, truncated });
        });
    }

    fn poll_project_tree(&mut self) {
        let Ok(event) = self.project_tree_rx.try_recv() else {
            return;
        };
        self.project_tree_running = false;
        self.project_tree_truncated = event.truncated;
        self.project_tree_entries = event.entries;
        self.project_file_paths = self
            .project_tree_entries
            .iter()
            .filter_map(|(path, _, is_dir)| (!is_dir).then_some(path.clone()))
            .collect();
        self.rebuild_visible_project_files();
        if self.project_tree_refresh_pending {
            self.project_tree_refresh_pending = false;
            self.refresh_project_files();
        }
    }

    fn rebuild_visible_project_files(&mut self) {
        self.project_files =
            visible_project_entries(&self.project_tree_entries, &self.collapsed_dirs);
    }

    fn start_project_search(&mut self) {
        let query = self.project_search_query.trim().to_owned();
        if query.is_empty() {
            self.status = "Enter text to search for".into();
            return;
        }
        if self.project_tree_running {
            self.status = "Wait for the project tree scan to finish before searching".into();
            return;
        }
        if self.project_search_running {
            self.status =
                "A project search is already running; wait for it to finish or cancel it".into();
            return;
        }
        self.project_search_results.clear();
        let files = self.project_file_paths.clone();
        let query_lower = query.to_lowercase();
        let cancel = Arc::new(AtomicBool::new(false));
        self.project_search_cancel = Some(cancel.clone());
        self.project_search_running = true;
        let (tx, rx) = bounded(128);
        self.project_search_tx = tx.clone();
        self.project_search_rx = rx;
        thread::spawn(move || {
            let mut files_scanned = 0usize;
            let mut matches = 0usize;
            let mut bytes_scanned = 0u64;
            for path in files {
                if cancel.load(Ordering::Relaxed) || matches >= MAX_PROJECT_SEARCH_RESULTS {
                    break;
                }
                let Ok(metadata) = fs::metadata(&path) else {
                    continue;
                };
                if metadata.len() > editor::MAX_EDITABLE_FILE_BYTES
                    || bytes_scanned.saturating_add(metadata.len()) > MAX_PROJECT_SEARCH_BYTES
                {
                    continue;
                }
                let Ok((text, _)) = editor::read_text_file(&path) else {
                    continue;
                };
                bytes_scanned += metadata.len();
                files_scanned += 1;
                for (line_index, line) in text.lines().enumerate() {
                    if cancel.load(Ordering::Relaxed) || matches >= MAX_PROJECT_SEARCH_RESULTS {
                        break;
                    }
                    if contains_case_insensitive_chunked(line, &query_lower, &cancel) == Some(true)
                    {
                        matches += 1;
                        let snippet: String = line.trim().chars().take(220).collect();
                        if tx
                            .send(ProjectSearchEvent::Result(ProjectSearchResult {
                                path: path.clone(),
                                line: line_index + 1,
                                snippet,
                            }))
                            .is_err()
                        {
                            return;
                        }
                    }
                }
            }
            let _ = tx.send(ProjectSearchEvent::Finished {
                files_scanned,
                matches,
                cancelled: cancel.load(Ordering::Relaxed),
            });
        });
    }

    fn poll_project_search(&mut self) {
        while let Ok(event) = self.project_search_rx.try_recv() {
            match event {
                ProjectSearchEvent::Result(result) => {
                    if self.project_search_results.len() < MAX_PROJECT_SEARCH_RESULTS {
                        self.project_search_results.push(result);
                    }
                }
                ProjectSearchEvent::Finished {
                    files_scanned,
                    matches,
                    cancelled,
                } => {
                    self.project_search_running = false;
                    self.project_search_cancel = None;
                    self.status = if cancelled {
                        format!(
                            "Project search cancelled: {matches} match(es) in {files_scanned} file(s) scanned"
                        )
                    } else {
                        format!(
                            "Project search finished: {matches} match(es) in {files_scanned} file(s)"
                        )
                    };
                }
            }
        }
    }

    fn new_file(&mut self) {
        if self.tabs.len() >= MAX_OPEN_TABS {
            self.status = format!("Tab limit reached ({MAX_OPEN_TABS}); close a document first");
            return;
        }
        self.tabs.push(EditorTab::untitled());
        self.active = self.tabs.len() - 1;
        self.auto_check_at = None;
        self.current_match = None;
        self.pending_selection = None;
        self.status = "New file".into();
    }

    fn new_text_file(&mut self) {
        if self.tabs.len() >= MAX_OPEN_TABS {
            self.status = format!("Tab limit reached ({MAX_OPEN_TABS}); close a document first");
            return;
        }
        self.tabs.push(EditorTab::untitled_text());
        self.active = self.tabs.len() - 1;
        self.auto_check_at = None;
        self.current_match = None;
        self.pending_selection = None;
        self.status = "New text document".into();
    }

    fn open_dialog(&mut self) {
        if let Some(path) = rfd::FileDialog::new()
            .add_filter("RBL source", &["rbl"])
            .add_filter(
                "Text files",
                &[
                    "txt", "md", "json", "toml", "yaml", "yml", "xml", "csv", "ini", "cfg", "log",
                    "py", "rs", "c", "h", "cpp", "hpp", "js", "ts", "html", "css", "sql", "sh",
                    "ps1",
                ],
            )
            .add_filter("All files", &["*"])
            .pick_file()
        {
            self.open_file(path);
        }
    }

    fn open_file(&mut self, path: PathBuf) -> bool {
        let canonical = path.canonicalize().unwrap_or_else(|_| path.clone());
        if let Some(index) = self
            .tabs
            .iter()
            .position(|tab| tab.path.as_ref() == Some(&canonical))
        {
            self.active = index;
            self.auto_check_at = None;
            self.current_match = None;
            self.pending_selection = None;
            return true;
        }
        if self.tabs.len() >= MAX_OPEN_TABS {
            self.status = format!("Tab limit reached ({MAX_OPEN_TABS}); close documents first");
            return false;
        }
        match EditorTab::open(&canonical) {
            Ok(tab) => {
                let refresh_tree = canonical.starts_with(&self.root)
                    && !self
                        .project_file_paths
                        .iter()
                        .any(|path| path == &canonical);
                if !can_open_document(&self.tabs, &tab) {
                    self.status = format!(
                        "Open-document memory budget reached ({} MiB); close documents first",
                        MAX_OPEN_DOCUMENTS_BYTES / (1024 * 1024)
                    );
                    return false;
                }
                self.tabs.push(tab);
                self.active = self.tabs.len() - 1;
                self.auto_check_at = None;
                self.current_match = None;
                self.pending_selection = None;
                self.status = format!("Opened {}", canonical.display());
                if refresh_tree {
                    self.refresh_project_files();
                }
                true
            }
            Err(error) => {
                self.status = format!("Open failed: {error:#}");
                append_output(
                    &mut self.problems,
                    &mut self.problems_line_starts,
                    &format!("Open failed: {error:#}\n"),
                );
                self.output_tab = 1;
                false
            }
        }
    }

    fn save_tab(&mut self, index: usize) -> bool {
        if index >= self.tabs.len() {
            return false;
        }
        let path = self.tabs[index].path.clone().or_else(|| {
            rfd::FileDialog::new()
                .add_filter(
                    "Text files",
                    &[
                        "txt", "md", "json", "toml", "yaml", "yml", "xml", "csv", "ini", "cfg",
                        "log", "py", "rs", "c", "h", "cpp", "hpp", "js", "ts", "html", "css",
                        "sql", "sh", "ps1", "rbl",
                    ],
                )
                .add_filter("All files", &["*"])
                .set_file_name(self.tabs[index].title.clone())
                .save_file()
        });
        let Some(path) = path else {
            return false;
        };
        if let Some(existing) = duplicate_open_tab(&self.tabs, &path, index) {
            self.active = existing;
            self.status = "That file is already open in another tab; switched to it".into();
            return false;
        }
        match self.tabs[index].save(Some(path.clone())) {
            Ok(()) => {
                self.status = format!("Saved {}", path.display());
                self.refresh_project_files();
                true
            }
            Err(error) => {
                append_output(
                    &mut self.problems,
                    &mut self.problems_line_starts,
                    &format!("Save failed: {error:#}\n"),
                );
                self.output_tab = 1;
                false
            }
        }
    }

    fn save_current(&mut self) -> bool {
        self.save_tab(self.active)
    }

    fn save_as_tab(&mut self, index: usize) -> bool {
        if index >= self.tabs.len() {
            return false;
        }
        let Some(path) = rfd::FileDialog::new()
            .add_filter(
                "Text files",
                &[
                    "txt", "md", "json", "toml", "yaml", "yml", "xml", "csv", "ini", "cfg", "log",
                    "py", "rs", "c", "h", "cpp", "hpp", "js", "ts", "html", "css", "sql", "sh",
                    "ps1", "rbl",
                ],
            )
            .add_filter("All files", &["*"])
            .set_file_name(&self.tabs[index].title)
            .save_file()
        else {
            return false;
        };
        if let Some(existing) = duplicate_open_tab(&self.tabs, &path, index) {
            self.active = existing;
            self.status = "That file is already open in another tab; switched to it".into();
            return false;
        }
        match self.tabs[index].save(Some(path.clone())) {
            Ok(()) => {
                self.status = format!("Saved {}", path.display());
                self.refresh_project_files();
                true
            }
            Err(error) => {
                append_output(
                    &mut self.problems,
                    &mut self.problems_line_starts,
                    &format!("Save failed: {error:#}\n"),
                );
                self.output_tab = 1;
                false
            }
        }
    }

    fn close_tab(&mut self, index: usize) {
        if index >= self.tabs.len() {
            return;
        }
        if self.tabs[index].dirty {
            self.pending_close = Some(self.tabs[index].id);
            self.show_close_prompt = true;
            return;
        }
        self.remove_tab(index);
    }

    fn remove_tab(&mut self, index: usize) {
        if index < self.tabs.len() {
            self.tabs.remove(index);
        }
        if self.tabs.is_empty() {
            self.new_file();
        } else if index < self.active {
            self.active -= 1;
        } else if index == self.active {
            self.active = index.min(self.tabs.len() - 1);
        }
        self.current_match = None;
        self.pending_selection = None;
    }

    fn close_other_tabs(&mut self, keep: usize) {
        let mut keep_index = keep;
        let mut dirty_left = 0;
        for index in (0..self.tabs.len()).rev() {
            if index == keep_index {
                continue;
            }
            if self.tabs[index].dirty {
                dirty_left += 1;
                continue;
            }
            self.remove_tab(index);
            if index < keep_index {
                keep_index -= 1;
            }
        }
        self.active = keep_index.min(self.tabs.len().saturating_sub(1));
        self.status = if dirty_left == 0 {
            "Closed other tabs".into()
        } else {
            format!("Closed clean tabs; kept {dirty_left} unsaved document(s)")
        };
    }

    fn start_task(&mut self, kind: TaskKind) {
        if self.running {
            self.status = "A process is already running".into();
            return;
        }
        self.auto_check_at = None;
        if matches!(
            kind,
            TaskKind::Run | TaskKind::Build | TaskKind::Check | TaskKind::Assembly
        ) && !self.current().is_some_and(EditorTab::is_rbl)
        {
            self.status = "Compiler actions are available only for .rbl documents".into();
            append_output(
                &mut self.problems,
                &mut self.problems_line_starts,
                "This document is plain text. Save or open a .rbl file to use compiler actions.\n",
            );
            self.output_tab = 1;
            return;
        }
        let mut source = None;
        let mut snapshot = None;
        if matches!(kind, TaskKind::Run | TaskKind::Build | TaskKind::Assembly) {
            let needs_save = self
                .current()
                .is_some_and(|tab| tab.path.is_none() && !tab.dirty)
                || (self.settings.auto_save && self.current().is_some_and(|tab| tab.dirty));
            if needs_save && !self.save_current() {
                self.status = "Save cancelled; operation not started".into();
                return;
            }
            if !self.current().is_some_and(EditorTab::is_rbl) {
                self.status = "Compiler actions are available only for .rbl documents".into();
                return;
            }
            source = self.current().and_then(|t| t.path.clone());
            if self.current().is_some_and(|t| t.dirty) || source.is_none() {
                snapshot = self.current().map(|t| t.text.clone());
            }
        } else if kind == TaskKind::Check {
            if let Some(tab) = self.current() {
                source = tab.path.clone();
                if tab.dirty || tab.path.is_none() {
                    snapshot = Some(tab.text.clone());
                }
            }
        }
        match kind {
            TaskKind::Check => {
                self.problems.clear();
                self.problems_line_starts.clear();
            }
            TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor => {
                self.console.clear();
                self.console_line_starts.clear();
            }
            TaskKind::Run | TaskKind::Build | TaskKind::Assembly => {
                self.output.clear();
                self.output_line_starts.clear();
            }
        }
        self.output_tab = if kind == TaskKind::Check {
            1
        } else if matches!(
            kind,
            TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor
        ) {
            2
        } else {
            0
        };
        let header = format!("> {}\n", kind.label());
        match kind {
            TaskKind::Check => {
                append_output(&mut self.problems, &mut self.problems_line_starts, &header)
            }
            TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor => {
                append_output(&mut self.console, &mut self.console_line_starts, &header)
            }
            TaskKind::Run | TaskKind::Build | TaskKind::Assembly => {
                append_output(&mut self.output, &mut self.output_line_starts, &header)
            }
        }
        if let Some(path) = &source {
            let line = format!("  {}\n", path.display());
            match kind {
                TaskKind::Check => {
                    append_output(&mut self.problems, &mut self.problems_line_starts, &line)
                }
                TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor => {
                    append_output(&mut self.console, &mut self.console_line_starts, &line)
                }
                TaskKind::Run | TaskKind::Build | TaskKind::Assembly => {
                    append_output(&mut self.output, &mut self.output_line_starts, &line)
                }
            }
        }
        self.running = true;
        self.snapshot_alias = None;
        if kind == TaskKind::Run {
            self.output_tab = 0;
        }
        let cancel = Arc::new(AtomicBool::new(false));
        self.cancel = Some(cancel.clone());
        self.current_task = Some(kind);
        self.run_input.clear();
        self.run_input_focus_pending = kind == TaskKind::Run;
        let (input_tx, input_rx) = bounded(32);
        self.process_input_tx = (kind == TaskKind::Run).then_some(input_tx);
        self.status = format!("{} running…", kind.label());
        let source_is_rbl = self.current().is_some_and(EditorTab::is_rbl);
        self.process_worker = process::start(
            self.root.clone(),
            self.settings.clone(),
            kind,
            process::SourceDocument {
                path: source,
                is_rbl: source_is_rbl,
                snapshot,
            },
            self.tx.clone(),
            input_rx,
            cancel,
        );
    }

    fn stop_task(&mut self) {
        if let Some(cancel) = &self.cancel {
            cancel.store(true, Ordering::Relaxed);
            self.status = "Stopping process…".into();
        }
    }

    fn poll_process(&mut self) {
        for _ in 0..MAX_PROCESS_EVENTS_PER_FRAME {
            let Ok(event) = self.rx.try_recv() else {
                break;
            };
            match event {
                ProcessEvent::Started(name) => self.status = format!("{name} running…"),
                ProcessEvent::SnapshotCreated(temporary, original) => {
                    if let Some(original) = original {
                        self.snapshot_alias = Some(snapshot_aliases(&temporary, &original));
                    }
                }
                ProcessEvent::Output(is_stderr, text) => {
                    let text = if let Some(aliases) = &self.snapshot_alias {
                        aliases.iter().fold(text, |output, (temporary, original)| {
                            output.replace(temporary, original)
                        })
                    } else {
                        text
                    };
                    let task = self.current_task;
                    if task == Some(TaskKind::Check) {
                        append_output(&mut self.problems, &mut self.problems_line_starts, &text);
                    } else if matches!(
                        task,
                        Some(TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor)
                    ) {
                        append_output(&mut self.console, &mut self.console_line_starts, &text);
                    } else {
                        append_output(&mut self.output, &mut self.output_line_starts, &text);
                    }
                    if task == Some(TaskKind::Assembly) && !is_stderr {
                        for line in text.lines() {
                            let candidate = PathBuf::from(line.trim());
                            if candidate.extension().is_some_and(|ext| ext == "s")
                                && candidate.is_file()
                            {
                                self.open_file(candidate);
                            }
                        }
                    }
                }
                ProcessEvent::Finished(code) => {
                    self.running = false;
                    if let Some(worker) = self.process_worker.take() {
                        let _ = worker.join();
                    }
                    self.cancel = None;
                    self.process_input_tx = None;
                    self.run_input_focus_pending = false;
                    self.snapshot_alias = None;
                    let task = self.current_task.take();
                    let (target, line_starts) = if task == Some(TaskKind::Check) {
                        (&mut self.problems, &mut self.problems_line_starts)
                    } else if matches!(
                        task,
                        Some(TaskKind::Tests | TaskKind::Benchmarks | TaskKind::Doctor)
                    ) {
                        (&mut self.console, &mut self.console_line_starts)
                    } else {
                        (&mut self.output, &mut self.output_line_starts)
                    };
                    append_output(
                        target,
                        line_starts,
                        &format!(
                            "\n[{} exited with code {code}]\n",
                            task.map(TaskKind::label).unwrap_or("Process")
                        ),
                    );
                    self.status = if code == 0 {
                        "Completed successfully".into()
                    } else if code == 130 {
                        "Process stopped".into()
                    } else {
                        format!("Process failed (exit {code})")
                    };
                }
            }
        }
    }

    fn handle_shortcuts(&mut self, ctx: &egui::Context) {
        if ctx.input(|input| input.key_pressed(egui::Key::Escape)) {
            if self.show_settings {
                self.settings = settings::load();
                apply_theme(ctx, &self.settings.theme);
            }
            self.show_command_palette = false;
            self.show_quick_open = false;
            self.show_go_to_line = false;
            self.show_find = false;
            self.show_find_in_files = false;
            self.show_settings = false;
            self.show_about = false;
            self.show_quick_reference = false;
            self.show_close_prompt = false;
            self.pending_close = None;
            if let Some(cancel) = &self.project_search_cancel {
                cancel.store(true, Ordering::Relaxed);
            }
        }
        let editor_focused = self
            .tabs
            .get(self.active)
            .is_some_and(|tab| editor::editor_has_focus(ctx, tab));
        let modal_open = self.show_settings
            || self.show_about
            || self.show_quick_reference
            || self.show_close_prompt;
        if modal_open {
            return;
        }
        if !should_handle_global_shortcuts(editor_focused, ctx.wants_keyboard_input()) {
            return;
        }
        let (undo, redo) = ctx.input(|input| {
            let command = input.modifiers.ctrl || input.modifiers.command;
            (
                command && !input.modifiers.shift && input.key_pressed(egui::Key::Z),
                command
                    && (input.key_pressed(egui::Key::Y)
                        || (input.modifiers.shift && input.key_pressed(egui::Key::Z))),
            )
        });
        if (undo || redo) && editor_focused {
            let other_document_bytes: usize = self
                .tabs
                .iter()
                .enumerate()
                .filter(|(index, _)| *index != self.active)
                .map(|(_, tab)| editor::memory_bytes(tab))
                .sum();
            let aggregate_budget = MAX_OPEN_DOCUMENTS_BYTES.saturating_sub(other_document_bytes);
            let handled = self
                .tabs
                .get_mut(self.active)
                .is_some_and(|tab| editor::apply_undo(tab, redo, aggregate_budget));
            ctx.input_mut(|input| {
                input.events.retain(|event| match event {
                    egui::Event::Key {
                        key,
                        pressed: true,
                        modifiers,
                        ..
                    } => {
                        !((modifiers.ctrl || modifiers.command)
                            && ((*key == egui::Key::Z && (modifiers.shift == redo))
                                || (*key == egui::Key::Y && redo)))
                    }
                    _ => true,
                });
            });
            if handled {
                if let Some(tab) = self.tabs.get(self.active) {
                    self.pending_selection = Some((tab.cursor_char, tab.cursor_char));
                    if tab.is_rbl() && self.settings.auto_check {
                        self.auto_check_at =
                            Some((tab.id, Instant::now() + Duration::from_millis(700)));
                    }
                }
                self.current_match = None;
                self.status = if redo {
                    "Redid edit".into()
                } else {
                    "Undid edit".into()
                };
            }
        }
        let (
            new,
            open,
            save,
            save_as,
            run,
            build,
            check,
            tests,
            bench,
            close,
            find,
            replace,
            goto,
            palette,
            quick_open,
            next_tab,
            prev_tab,
            find_files,
        ) = ctx.input(|i| {
            (
                i.modifiers.ctrl && i.key_pressed(egui::Key::N),
                i.modifiers.ctrl && i.key_pressed(egui::Key::O),
                i.modifiers.ctrl && !i.modifiers.shift && i.key_pressed(egui::Key::S),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::S),
                i.key_pressed(egui::Key::F5),
                i.modifiers.ctrl && i.key_pressed(egui::Key::B),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::B),
                i.modifiers.ctrl && i.key_pressed(egui::Key::T),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::T),
                i.modifiers.ctrl && i.key_pressed(egui::Key::W),
                i.modifiers.ctrl && i.key_pressed(egui::Key::F),
                i.modifiers.ctrl && i.key_pressed(egui::Key::H),
                i.modifiers.ctrl && i.key_pressed(egui::Key::G),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::P),
                i.modifiers.ctrl && !i.modifiers.shift && i.key_pressed(egui::Key::P),
                i.modifiers.ctrl && !i.modifiers.shift && i.key_pressed(egui::Key::Tab),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::Tab),
                i.modifiers.ctrl && i.modifiers.shift && i.key_pressed(egui::Key::F),
            )
        });
        if new {
            self.new_file();
        }
        if open {
            self.open_dialog();
        }
        if save {
            self.save_current();
        }
        if save_as {
            self.save_as_tab(self.active);
        }
        if run {
            self.start_task(TaskKind::Run);
        }
        if build {
            self.start_task(TaskKind::Build);
        }
        if check {
            self.start_task(TaskKind::Check);
        }
        if tests {
            self.start_task(TaskKind::Tests);
        }
        if bench {
            self.start_task(TaskKind::Benchmarks);
        }
        if close {
            self.close_tab(self.active);
        }
        if find || replace {
            self.show_find = true;
            self.find_focus_pending = true;
            self.replace_mode = replace;
            self.find_status.clear();
        }
        if goto {
            if !self.show_go_to_line {
                self.go_to_line.clear();
            }
            self.show_go_to_line = true;
            self.goto_focus_pending = true;
        }
        if palette {
            prepare_dialog_input(
                self.show_command_palette,
                &mut self.command_query,
                &mut self.palette_selection,
            );
            self.show_command_palette = true;
            self.palette_focus_pending = true;
        }
        if quick_open {
            prepare_dialog_input(
                self.show_quick_open,
                &mut self.quick_open_query,
                &mut self.quick_open_selection,
            );
            self.show_quick_open = true;
            self.quick_open_focus_pending = true;
        }
        if find_files {
            if !self.show_find_in_files {
                self.project_search_query.clear();
                self.project_search_results.clear();
            }
            self.show_find_in_files = true;
            self.project_search_focus_pending = true;
        }
        if next_tab && !self.tabs.is_empty() {
            self.active = (self.active + 1) % self.tabs.len();
            self.auto_check_at = None;
            self.current_match = None;
            self.pending_selection = None;
        }
        if prev_tab && !self.tabs.is_empty() {
            self.active = (self.active + self.tabs.len() - 1) % self.tabs.len();
            self.auto_check_at = None;
            self.current_match = None;
            self.pending_selection = None;
        }
    }

    fn find_in_document(&mut self, backwards: bool) {
        let Some(tab) = self.current() else { return };
        let from = tab.cursor_char;
        if let Some(found) = editor::find_match(&tab.text, &self.find_query, from, backwards) {
            self.pending_selection = Some(found);
            self.current_match = Some(found);
            self.find_status = format!("{}–{}", found.0 + 1, found.1 + 1);
        } else {
            self.find_status = "No matches".into();
            self.pending_selection = None;
            self.current_match = None;
        }
    }

    fn replace_in_document(&mut self, all: bool) {
        let normalized_replacement = self.replace_query.replace("\r\n", "\n").replace('\r', "\n");
        if !self.find_query.is_empty() && self.find_query == normalized_replacement {
            self.find_status = "Replacement is identical; no changes made".into();
            return;
        }
        let other_document_bytes: usize = self
            .tabs
            .iter()
            .enumerate()
            .filter(|(index, _)| *index != self.active)
            .map(|(_, tab)| editor::memory_bytes(tab))
            .sum();
        let per_file_limit = self
            .tabs
            .get(self.active)
            .map(|tab| editor::decoded_text_limit(tab.encoding) as usize)
            .unwrap_or(editor::MAX_EDITABLE_FILE_BYTES as usize);
        let aggregate_budget = MAX_OPEN_DOCUMENTS_BYTES.saturating_sub(other_document_bytes);
        let document_limit = per_file_limit.min(aggregate_budget);
        if all {
            self.current_match = None;
            self.pending_selection = None;
            let Some(tab) = self.tabs.get_mut(self.active) else {
                return;
            };
            let count = editor::replace_all(
                tab,
                &self.find_query,
                &self.replace_query,
                document_limit,
                aggregate_budget,
            );
            if count > 0 {
                self.pending_selection = Some((tab.cursor_char, tab.cursor_char));
            }
            if count > 0 && tab.is_rbl() && self.settings.auto_check {
                self.auto_check_at = Some((tab.id, Instant::now() + Duration::from_millis(700)));
            }
            self.find_status = if count == 0 && !self.find_query.is_empty() {
                "No matches or size limit reached".into()
            } else {
                format!("Replaced {count} occurrence(s)")
            };
        } else {
            let current_match = self.current_match.filter(|range| {
                self.tabs
                    .get(self.active)
                    .is_some_and(|tab| match_range_is_current(&tab.text, &self.find_query, *range))
            });
            if current_match.is_none() {
                self.current_match = None;
            }
            let replaced = if let Some(tab) = self.tabs.get_mut(self.active) {
                if let Some((start, end)) = current_match {
                    editor::replace_range(
                        tab,
                        start,
                        end,
                        &self.replace_query,
                        document_limit,
                        aggregate_budget,
                    )
                } else {
                    editor::replace_one(
                        tab,
                        &self.find_query,
                        &self.replace_query,
                        document_limit,
                        aggregate_budget,
                    )
                }
            } else {
                false
            };
            if replaced {
                self.current_match = None;
                self.find_status = "Replaced".into();
                self.find_in_document(false);
            } else {
                self.current_match = current_match;
                self.find_status = "No matches or size limit reached".into();
            }
        }
    }

    fn menu_bar(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("menu_bar").show(ctx, |ui| {
            egui::menu::bar(ui, |ui| {
                ui.menu_button("File", |ui| {
                    if ui.button("New RBL file\tCtrl+N").clicked() {
                        self.new_file();
                        ui.close_menu();
                    }
                    if ui.button("New text file").clicked() {
                        self.new_text_file();
                        ui.close_menu();
                    }
                    if ui.button("Open…\tCtrl+O").clicked() {
                        self.open_dialog();
                        ui.close_menu();
                    }
                    ui.separator();
                    if ui.button("Save\tCtrl+S").clicked() {
                        self.save_current();
                        ui.close_menu();
                    }
                    if ui.button("Save As…\tCtrl+Shift+S").clicked() {
                        let i = self.active;
                        self.save_as_tab(i);
                        ui.close_menu();
                    }
                    ui.separator();
                    if ui.button("Close tab\tCtrl+W").clicked() {
                        self.close_tab(self.active);
                        ui.close_menu();
                    }
                    if ui.button("Exit").clicked() {
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                        ui.close_menu();
                    }
                });
                ui.menu_button("Edit", |ui| {
                    if ui.button("Find…\tCtrl+F").clicked() {
                        self.show_find = true;
                        self.find_focus_pending = true;
                        self.replace_mode = false;
                        ui.close_menu();
                    }
                    if ui.button("Replace…\tCtrl+H").clicked() {
                        self.show_find = true;
                        self.find_focus_pending = true;
                        self.replace_mode = true;
                        ui.close_menu();
                    }
                    if ui.button("Find in files…\tCtrl+Shift+F").clicked() {
                        self.show_find_in_files = true;
                        self.project_search_focus_pending = true;
                        self.project_search_query.clear();
                        self.project_search_results.clear();
                        ui.close_menu();
                    }
                    if ui.button("Go to line…\tCtrl+G").clicked() {
                        self.show_go_to_line = true;
                        self.goto_focus_pending = true;
                        self.go_to_line.clear();
                        ui.close_menu();
                    }
                });
                ui.menu_button("Navigate", |ui| {
                    if ui.button("Go to file…\tCtrl+P").clicked() {
                        self.show_quick_open = true;
                        self.quick_open_focus_pending = true;
                        self.quick_open_query.clear();
                        self.quick_open_selection = 0;
                        ui.close_menu();
                    }
                    if ui.button("Command palette…\tCtrl+Shift+P").clicked() {
                        self.show_command_palette = true;
                        self.palette_focus_pending = true;
                        self.command_query.clear();
                        self.palette_selection = 0;
                        ui.close_menu();
                    }
                });
                ui.menu_button("Run", |ui| {
                    if ui
                        .add_enabled(self.running, egui::Button::new("Stop process"))
                        .clicked()
                    {
                        self.stop_task();
                        ui.close_menu();
                    }
                    ui.separator();
                    for (label, key, kind) in [
                        ("Run", "F5", TaskKind::Run),
                        ("Build", "Ctrl+B", TaskKind::Build),
                        ("Syntax check", "Ctrl+Shift+B", TaskKind::Check),
                        ("Show generated ASM", "", TaskKind::Assembly),
                    ] {
                        if ui.button(format!("{label}\t{key}")).clicked() {
                            self.start_task(kind);
                            ui.close_menu();
                        }
                    }
                    ui.separator();
                    if ui.button("Run tests\tCtrl+T").clicked() {
                        self.start_task(TaskKind::Tests);
                        ui.close_menu();
                    }
                    if ui.button("Benchmarks\tCtrl+Shift+T").clicked() {
                        self.start_task(TaskKind::Benchmarks);
                        ui.close_menu();
                    }
                    if ui.button("Toolchain doctor").clicked() {
                        self.start_task(TaskKind::Doctor);
                        ui.close_menu();
                    }
                });
                ui.menu_button("View", |ui| {
                    if ui.button("Settings…").clicked() {
                        self.show_settings = true;
                        ui.close_menu();
                    }
                    ui.separator();
                    for theme in ["Dark", "Light", "Nord", "Monokai"] {
                        if ui
                            .selectable_label(self.settings.theme == theme, theme)
                            .clicked()
                        {
                            self.settings.theme = theme.into();
                            if let Err(error) = settings::save(&self.settings) {
                                self.status = format!("Could not save settings: {error}");
                            }
                            apply_theme(ctx, theme);
                            ui.close_menu();
                        }
                    }
                    if ui.button("Clear output").clicked() {
                        self.output.clear();
                        self.output_line_starts.clear();
                        self.problems.clear();
                        self.problems_line_starts.clear();
                        self.console.clear();
                        self.console_line_starts.clear();
                        ui.close_menu();
                    }
                });
                ui.menu_button("Help", |ui| {
                    if ui.button("RBL Quick Reference").clicked() {
                        self.show_quick_reference = true;
                        ui.close_menu();
                    }
                    if ui.button("About RBL Studio").clicked() {
                        self.show_about = true;
                        ui.close_menu();
                    }
                });
                ui.separator();
                ui.label(
                    RichText::new("RBL Studio · Native Edition")
                        .color(Color32::from_rgb(155, 125, 220)),
                );
                if self.running {
                    ui.spinner();
                    ui.label("Working…");
                }
            });
        });
    }

    fn toolbar(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("toolbar").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                ui.menu_button("＋ New", |ui| {
                    if ui.button("RBL source file").clicked() {
                        self.new_file();
                        ui.close_menu();
                    }
                    if ui.button("Plain text file").clicked() {
                        self.new_text_file();
                        ui.close_menu();
                    }
                });
                if ui.button("Open").clicked() {
                    self.open_dialog();
                }
                if ui.button("Save").clicked() {
                    self.save_current();
                }
                ui.separator();
                let can_compile = self.current().is_some_and(EditorTab::is_rbl);
                let run = ui.add_enabled(
                    can_compile,
                    egui::Button::new(RichText::new("▶ Run").strong().color(Color32::WHITE))
                        .fill(Color32::from_rgb(45, 145, 105))
                        .min_size(egui::vec2(62.0, 0.0)),
                );
                if run.clicked() {
                    self.start_task(TaskKind::Run);
                }
                if !can_compile {
                    run.on_hover_text("Run is available for .rbl files only");
                }
                for (label, kind) in [
                    ("Build", TaskKind::Build),
                    ("✓ Check", TaskKind::Check),
                    ("ASM", TaskKind::Assembly),
                ] {
                    if ui
                        .add_enabled(can_compile, egui::Button::new(label))
                        .clicked()
                    {
                        self.start_task(kind);
                    }
                }
                for (label, kind) in [("Tests", TaskKind::Tests), ("Bench", TaskKind::Benchmarks)] {
                    if ui.button(label).clicked() {
                        self.start_task(kind);
                    }
                }
                ui.separator();
                if ui
                    .small_button("Find")
                    .on_hover_text("Find in document · Ctrl+F")
                    .clicked()
                {
                    self.show_find = true;
                    self.find_focus_pending = true;
                    self.replace_mode = false;
                }
                if ui
                    .small_button("⌕ Files")
                    .on_hover_text("Quick open · Ctrl+P")
                    .clicked()
                {
                    self.show_quick_open = true;
                    self.quick_open_focus_pending = true;
                    self.quick_open_query.clear();
                }
                if ui
                    .small_button("⌕ Project")
                    .on_hover_text("Find in files · Ctrl+Shift+F")
                    .clicked()
                {
                    self.show_find_in_files = true;
                    self.project_search_focus_pending = true;
                    self.project_search_query.clear();
                    self.project_search_results.clear();
                }
                if ui
                    .add_enabled(
                        self.running,
                        egui::Button::new(RichText::new("■ Stop").color(Color32::WHITE))
                            .fill(Color32::from_rgb(170, 65, 70)),
                    )
                    .clicked()
                {
                    self.stop_task();
                }
            });
        });
    }

    fn file_tree(&mut self, ctx: &egui::Context) {
        egui::SidePanel::left("project_tree")
            .default_width(245.0)
            .min_width(190.0)
            .max_width(360.0)
            .resizable(true)
            .show_separator_line(true)
            .frame(
                egui::Frame::side_top_panel(&ctx.style()).inner_margin(egui::Margin {
                    left: 8,
                    right: 0,
                    top: 2,
                    bottom: 2,
                }),
            )
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label(
                        RichText::new("PROJECT")
                            .strong()
                            .color(ui.visuals().strong_text_color()),
                    );
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui
                            .add_enabled(!self.project_tree_running, egui::Button::new("⟳").small())
                            .on_hover_text("Refresh project files")
                            .clicked()
                        {
                            self.refresh_project_files();
                        }
                        ui.label(RichText::new("FILES").small().weak());
                    });
                    if ui
                        .small_button(if self.file_tree_open { "▾" } else { "▸" })
                        .on_hover_text("Collapse or expand the project tree")
                        .clicked()
                    {
                        self.file_tree_open = !self.file_tree_open;
                    }
                });
                if self.project_tree_running {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(RichText::new("Scanning project…").small().weak());
                    });
                }
                ui.add(
                    egui::Label::new(
                        RichText::new(
                            self.root
                                .file_name()
                                .and_then(|x| x.to_str())
                                .unwrap_or("RBLStudio"),
                        )
                        .small()
                        .color(ui.visuals().weak_text_color()),
                    )
                    .truncate(),
                );
                if self.project_tree_truncated {
                    ui.label(
                        RichText::new(
                            "Project tree is incomplete: scan limit reached or filesystem entries could not be read",
                        )
                        .small()
                        .color(Color32::from_rgb(220, 170, 95)),
                    );
                }
                ui.separator();
                if self.file_tree_open {
                    let mut open_requested = None;
                    let mut toggle_directory = None;
                    let active_path = self.current().and_then(|tab| tab.path.as_ref());
                    let row_height = ui.text_style_height(&egui::TextStyle::Body);
                    let tree_width = ui.available_width();
                    egui::ScrollArea::vertical()
                        .max_width(tree_width)
                        .auto_shrink([true, false])
                        .show_rows(
                        ui,
                        row_height,
                        self.project_files.len(),
                        |ui, row_range| {
                            ui.set_width(tree_width);
                            ui.style_mut().wrap_mode = Some(egui::TextWrapMode::Truncate);
                            for index in row_range {
                                let (path, depth, is_dir) = &self.project_files[index];
                                let name = path
                                    .file_name()
                                    .and_then(|x| x.to_str())
                                    .unwrap_or("?")
                                    .to_owned();
                                ui.horizontal(|ui| {
                                    ui.add_space(*depth as f32 * 12.0);
                                    let icon = if *is_dir {
                                        if self.collapsed_dirs.contains(path) {
                                            "▸"
                                        } else {
                                            "▾"
                                        }
                                    } else if path.extension().is_some_and(|extension| {
                                        extension.eq_ignore_ascii_case("rbl")
                                    }) {
                                        "◇"
                                    } else {
                                        "·"
                                    };
                                    let selected =
                                        !is_dir && active_path.is_some_and(|open| open == path);
                                    let response = ui.selectable_label(
                                        selected,
                                        RichText::new(format!("{icon} {name}")).color(
                                            if path.extension().is_some_and(|extension| {
                                                extension.eq_ignore_ascii_case("rbl")
                                            }) {
                                                Color32::from_rgb(130, 190, 225)
                                            } else {
                                                ui.visuals().text_color()
                                            },
                                        ),
                                    ).on_hover_text(path.display().to_string());
                                    if response.clicked() {
                                        if *is_dir {
                                            toggle_directory = Some(path.clone());
                                        } else {
                                            open_requested = Some(path.clone());
                                        }
                                    }
                                });
                            }
                        },
                    );
                    if let Some(path) = open_requested {
                        self.open_file(path);
                    }
                    if let Some(path) = toggle_directory {
                        if !self.collapsed_dirs.insert(path.clone()) {
                            self.collapsed_dirs.remove(&path);
                        }
                        self.rebuild_visible_project_files();
                    }
                }
            });
    }

    fn editor_panel(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default()
            .frame(
                egui::Frame::central_panel(&ctx.style()).inner_margin(egui::Margin {
                    left: 0,
                    right: 8,
                    top: 8,
                    bottom: 8,
                }),
            )
            .show(ctx, |ui| {
                ui.spacing_mut().item_spacing.y = 2.0;
                let mut page_delta = 0i8;
                if let Some(tab) = self.current() {
                    if tab.text.len() > editor::LARGE_DOCUMENT_THRESHOLD_BYTES {
                        let (page_start, page_end) = editor::page_byte_range(tab);
                        let offset_kib = page_start / 1024;
                        let total_kib = tab.text.len().div_ceil(1024);
                        ui.horizontal(|ui| {
                            ui.label(RichText::new("LARGE DOCUMENT").small().strong());
                            if ui
                                .add_enabled(page_start > 0, egui::Button::new("‹"))
                                .clicked()
                            {
                                page_delta = -1;
                            }
                            ui.label(
                                RichText::new(format!("Window {offset_kib} / {total_kib} KiB"))
                                    .small()
                                    .weak(),
                            );
                            if ui
                                .add_enabled(page_end < tab.text.len(), egui::Button::new("›"))
                                .clicked()
                            {
                                page_delta = 1;
                            }
                            ui.label(RichText::new("64 KiB editor window").small().weak());
                        });
                    }
                }
                if page_delta != 0 {
                    if let Some(tab) = self.tabs.get_mut(self.active) {
                        let target = if page_delta < 0 {
                            tab.page_history.pop().unwrap_or_else(|| {
                                tab.page_start_byte
                                    .saturating_sub(editor::EDITOR_PAGE_BYTES)
                            })
                        } else {
                            editor::push_page_history(tab, tab.page_start_byte);
                            editor::page_byte_range(tab).1
                        };
                        editor::set_page_start(tab, target);
                        tab.cursor_char = tab.page_start_char;
                        tab.cursor_line = tab.page_start_line;
                        tab.cursor_column = tab.page_start_column;
                    }
                }
                if self.show_find {
                    let mut action = None;
                    ui.horizontal(|ui| {
                        ui.label("Find");
                        let find = ui.add(
                            egui::TextEdit::singleline(&mut self.find_query)
                                .desired_width(190.0)
                                .char_limit(256)
                                .hint_text("Text to find"),
                        );
                        if find.changed() {
                            self.current_match = None;
                            self.pending_selection = None;
                            self.find_status.clear();
                        }
                        if self.find_focus_pending {
                            find.request_focus();
                            self.find_focus_pending = false;
                        }
                        if find.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                            action = Some("next");
                        }
                        if self.replace_mode {
                            ui.add(
                                egui::TextEdit::singleline(&mut self.replace_query)
                                    .desired_width(170.0)
                                    .char_limit(256)
                                    .hint_text("Replace with"),
                            );
                        }
                        if ui
                            .small_button("↑")
                            .on_hover_text("Previous match")
                            .clicked()
                        {
                            action = Some("previous");
                        }
                        if ui.small_button("↓").on_hover_text("Next match").clicked() {
                            action = Some("next");
                        }
                        if self.replace_mode {
                            if ui.small_button("Replace").clicked() {
                                action = Some("replace");
                            }
                            if ui
                                .small_button("All")
                                .on_hover_text("Replace all matches")
                                .clicked()
                            {
                                action = Some("replace_all");
                            }
                        }
                        ui.label(RichText::new(&self.find_status).small().weak());
                        if ui.small_button("×").on_hover_text("Close search").clicked() {
                            self.show_find = false;
                            self.pending_selection = None;
                            self.current_match = None;
                        }
                    });
                    if let Some(action) = action {
                        match action {
                            "previous" => self.find_in_document(true),
                            "next" => self.find_in_document(false),
                            "replace" => self.replace_in_document(false),
                            "replace_all" => self.replace_in_document(true),
                            _ => {}
                        }
                    }
                }
                let mut close = None;
                let mut close_others = None;
                egui::ScrollArea::horizontal()
                    .id_salt("editor-tabs-scroll")
                    .auto_shrink([false, true])
                    .show(ui, |ui| {
                        ui.horizontal(|ui| {
                            for (index, tab) in self.tabs.iter().enumerate() {
                                let response = ui.selectable_label(
                                    index == self.active,
                                    RichText::new(tab.display_title()).size(13.0),
                                );
                                if response.clicked() {
                                    self.active = index;
                                    self.auto_check_at = None;
                                    self.current_match = None;
                                    self.pending_selection = None;
                                }
                                response.context_menu(|ui| {
                                    if ui.button("Close").clicked() {
                                        close = Some(index);
                                        ui.close_menu();
                                    }
                                    if ui.button("Close other tabs").clicked() {
                                        close_others = Some(index);
                                        ui.close_menu();
                                    }
                                    if let Some(path) = &tab.path {
                                        ui.label(
                                            RichText::new(path.display().to_string())
                                                .small()
                                                .weak(),
                                        );
                                    }
                                });
                                if ui.small_button("×").on_hover_text("Close tab").clicked() {
                                    close = Some(index);
                                }
                            }
                            if ui.small_button("+").on_hover_text("New file").clicked() {
                                self.new_file();
                            }
                        });
                    });
                if let Some(index) = close {
                    self.close_tab(index);
                }
                if let Some(keep) = close_others {
                    self.close_other_tabs(keep);
                }
                ui.separator();
                let pending_selection = self.pending_selection.take();
                let other_document_bytes: usize = self
                    .tabs
                    .iter()
                    .enumerate()
                    .filter(|(index, _)| *index != self.active)
                    .map(|(_, tab)| editor::memory_bytes(tab))
                    .sum();
                let active_undo_bytes = self
                    .tabs
                    .get(self.active)
                    .map(|tab| editor::memory_bytes(tab).saturating_sub(tab.text.capacity()))
                    .unwrap_or(0);
                let aggregate_budget =
                    MAX_OPEN_DOCUMENTS_BYTES.saturating_sub(other_document_bytes);
                let document_limit = MAX_OPEN_DOCUMENTS_BYTES
                    .saturating_sub(other_document_bytes)
                    .saturating_sub(active_undo_bytes)
                    .min(
                        self.tabs
                            .get(self.active)
                            .map(|tab| editor::decoded_text_limit(tab.encoding) as usize)
                            .unwrap_or(editor::MAX_EDITABLE_FILE_BYTES as usize),
                    );
                let limit_reached = if let Some(tab) = self.tabs.get_mut(self.active) {
                    let syntax_highlighting = tab.is_rbl() && self.settings.syntax_highlighting;
                    let tab_id = tab.id;
                    let page_start = editor::page_byte_range(tab).0;
                    let changed = egui::ScrollArea::both()
                        .id_salt(("source-editor-scroll", tab_id, page_start))
                        .auto_shrink([false, false])
                        .show(ui, |ui| {
                            editor::show_editor(
                                ui,
                                tab,
                                editor::EditorViewOptions {
                                    font_size: self.settings.font_size,
                                    tab_size: self.settings.tab_size,
                                    line_numbers: self.settings.show_line_numbers,
                                    syntax_highlighting,
                                    soft_wrap: self.settings.soft_wrap,
                                    indent_guides: self.settings.indent_guides,
                                    document_limit_bytes: document_limit,
                                    undo_budget_bytes: aggregate_budget,
                                },
                                pending_selection,
                            )
                        })
                        .inner;
                    if changed {
                        self.current_match = None;
                    }
                    if changed && self.settings.auto_check && tab.is_rbl() {
                        self.auto_check_at =
                            Some((tab_id, Instant::now() + Duration::from_millis(700)));
                    }
                    tab.limit_reached
                } else {
                    false
                };
                if limit_reached {
                    self.status =
                        "Document memory limit reached; close tabs or reduce the text".into();
                }
            });
    }

    fn output_panel(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::bottom("output_panel")
            .resizable(true)
            .default_height(230.0)
            .min_height(100.0)
            .max_height((ctx.available_rect().height() * 0.45).max(140.0))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label(RichText::new("OUTPUT").strong().small());
                    ui.separator();
                    for (i, label) in ["Run", "Problems", "Console"].iter().enumerate() {
                        if ui.selectable_label(self.output_tab == i, *label).clicked() {
                            self.output_tab = i;
                        }
                    }
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.small_button("Clear").clicked() {
                            self.output.clear();
                            self.output_line_starts.clear();
                            self.problems.clear();
                            self.problems_line_starts.clear();
                            self.console.clear();
                            self.console_line_starts.clear();
                        }
                        if self.running {
                            ui.label(
                                RichText::new("● Running")
                                    .small()
                                    .color(Color32::from_rgb(80, 190, 140)),
                            );
                        }
                    });
                });
                ui.separator();
                if self.running && self.current_task == Some(TaskKind::Run) && self.output_tab == 0
                {
                    let mut submit_input = false;
                    let mut close_stdin = false;
                    ui.horizontal(|ui| {
                        ui.label(RichText::new("STDIN").small().weak());
                        let stdin_open = self.process_input_tx.is_some();
                        let input = ui.add(
                            egui::TextEdit::singleline(&mut self.run_input)
                                .desired_width(f32::INFINITY)
                                .char_limit(16 * 1024)
                                .hint_text("Type a line and press Enter")
                                .interactive(stdin_open),
                        );
                        if self.run_input_focus_pending {
                            input.request_focus();
                            self.run_input_focus_pending = false;
                        }
                        submit_input = (input.has_focus() || input.lost_focus())
                            && ui.input(|i| i.key_pressed(egui::Key::Enter));
                        submit_input |= ui
                            .add_enabled(stdin_open, egui::Button::new("Send"))
                            .clicked();
                        close_stdin = ui
                            .add_enabled(stdin_open, egui::Button::new("EOF").small())
                            .on_hover_text("Close program input (end of stdin)")
                            .clicked();
                        if !stdin_open {
                            ui.label(RichText::new("stdin closed").small().weak());
                        }
                    });
                    if submit_input {
                        if let Some(sender) = &self.process_input_tx {
                            let line = std::mem::take(&mut self.run_input);
                            match sender.try_send(ProcessCommand::StdinLine(line.clone())) {
                                Ok(()) => {
                                    append_output(
                                        &mut self.output,
                                        &mut self.output_line_starts,
                                        &format!("> {line}\n"),
                                    );
                                    self.run_input_focus_pending = true;
                                }
                                Err(error) => {
                                    self.run_input = line;
                                    self.status = format!("Could not queue program input: {error}");
                                }
                            }
                        }
                    }
                    if close_stdin {
                        if let Some(sender) = self.process_input_tx.take() {
                            match sender.try_send(ProcessCommand::CloseStdin) {
                                Ok(()) => self.run_input.clear(),
                                Err(error) => {
                                    self.status = format!("Could not close program input: {error}");
                                    self.process_input_tx = Some(sender);
                                }
                            }
                        }
                    }
                }
                let text = match self.output_tab {
                    1 => &self.problems,
                    2 => &self.console,
                    _ => &self.output,
                };
                let line_starts = match self.output_tab {
                    1 => &self.problems_line_starts,
                    2 => &self.console_line_starts,
                    _ => &self.output_line_starts,
                };
                let mut jump = None;
                let row_height = ui.text_style_height(&egui::TextStyle::Monospace);
                egui::ScrollArea::both()
                    .stick_to_bottom(true)
                    .auto_shrink([false, false])
                    .show_rows(ui, row_height, line_starts.len(), |ui, range| {
                        for index in range {
                            let line = output_line(text, line_starts, index);
                            if self.output_tab == 1 {
                                if let Some(location) = parse_diagnostic_location(line) {
                                    if ui
                                        .link(RichText::new(line).monospace().size(12.0))
                                        .clicked()
                                    {
                                        jump = Some(location);
                                    }
                                    continue;
                                }
                            }
                            ui.add(
                                egui::Label::new(RichText::new(line).monospace().size(12.0))
                                    .selectable(true),
                            );
                        }
                    });
                if self.output_tab == 1 {
                    if let Some((path, line, column)) = jump {
                        let path = if path.is_absolute() {
                            path
                        } else {
                            self.root.join(path)
                        };
                        if path.is_file() && self.open_file(path) {
                            if let Some(tab) = self.current() {
                                let start = editor::line_start_char(&tab.text, line);
                                let cursor = (start + column.saturating_sub(1))
                                    .min(tab.text.chars().count());
                                self.pending_selection = Some((cursor, cursor));
                            }
                        }
                    }
                }
            });
    }

    fn status_bar(&self, ctx: &egui::Context) {
        egui::TopBottomPanel::bottom("status_bar").show(ctx, |ui| {
            ui.horizontal(|ui| {
                let status_color = if self.running {
                    Color32::from_rgb(95, 185, 145)
                } else if self.status.contains("failed") || self.status.contains("Failed") {
                    Color32::from_rgb(220, 115, 115)
                } else {
                    ui.visuals().weak_text_color()
                };
                ui.label(RichText::new("●").color(status_color));
                ui.label(RichText::new(&self.status).small());
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if let Some(tab) = self.current() {
                        let (line, column) = editor::cursor_position(tab);
                        let encoding = match tab.encoding {
                            editor::TextEncoding::Utf8 => "UTF-8",
                            editor::TextEncoding::Utf8Bom => "UTF-8 BOM",
                            editor::TextEncoding::Utf16Le => "UTF-16 LE",
                            editor::TextEncoding::Utf16Be => "UTF-16 BE",
                        };
                        ui.label(
                            RichText::new(format!(
                                "{} · {} · {} lines · Ln {line}, Col {column}",
                                tab.path
                                    .as_ref()
                                    .map(|p| p.display().to_string())
                                    .unwrap_or_else(|| "Untitled".into()),
                                encoding,
                                tab.line_count
                            ))
                            .small()
                            .weak(),
                        );
                    }
                    ui.label(
                        RichText::new(self.settings.wsl_mode.to_uppercase())
                            .small()
                            .weak(),
                    );
                });
            });
        });
    }

    fn dialogs(&mut self, ctx: &egui::Context) {
        if self.show_find_in_files {
            let mut start_search = false;
            let mut open_result: Option<(PathBuf, usize)> = None;
            let mut close_search = false;
            egui::Window::new("Find in files")
                .collapsible(false)
                .resizable(true)
                .default_size([760.0, 520.0])
                .show(ctx, |ui| {
                    ui.horizontal(|ui| {
                        let query = ui.add(
                            egui::TextEdit::singleline(&mut self.project_search_query)
                                .desired_width(f32::INFINITY)
                                .char_limit(256)
                                .hint_text("Search text across project files…"),
                        );
                        if self.project_search_focus_pending {
                            query.request_focus();
                            self.project_search_focus_pending = false;
                        }
                        if query.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                            start_search = true;
                        }
                        if ui
                            .add_enabled(!self.project_search_running, egui::Button::new("Find"))
                            .clicked()
                        {
                            start_search = true;
                        }
                        if self.project_search_running {
                            ui.spinner();
                            if ui.small_button("Cancel").clicked() {
                                if let Some(cancel) = &self.project_search_cancel {
                                    cancel.store(true, Ordering::Relaxed);
                                }
                            }
                        }
                        if ui.small_button("×").clicked() {
                            close_search = true;
                        }
                    });
                    ui.separator();
                    ui.label(
                        RichText::new(format!(
                            "{} result(s) · scans up to 20,000 project files (16 MiB each), bounded to 256 MiB per search; very large project trees may be truncated",
                            self.project_search_results.len()
                        ))
                        .small()
                        .weak(),
                    );
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        for result in &self.project_search_results {
                            let relative = result
                                .path
                                .strip_prefix(&self.root)
                                .unwrap_or(&result.path)
                                .display();
                            let label = format!("{relative}:{}  {}", result.line, result.snippet);
                            if ui.selectable_label(false, label).clicked() {
                                open_result = Some((result.path.clone(), result.line));
                            }
                        }
                    });
                });
            if start_search {
                self.start_project_search();
            }
            if close_search {
                self.show_find_in_files = false;
                if let Some(cancel) = &self.project_search_cancel {
                    cancel.store(true, Ordering::Relaxed);
                }
            }
            if let Some((path, line)) = open_result {
                let canonical = path.canonicalize().unwrap_or_else(|_| path.clone());
                if self.open_file(path) {
                    if let Some(index) = self
                        .tabs
                        .iter()
                        .position(|tab| tab.path.as_ref() == Some(&canonical))
                    {
                        self.active = index;
                        let tab = &self.tabs[index];
                        let cursor = editor::line_start_char(&tab.text, line);
                        self.pending_selection = Some((cursor, cursor));
                        self.show_find_in_files = false;
                    }
                }
            }
        }

        if self.show_go_to_line {
            let mut go = false;
            egui::Window::new("Go to line")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_TOP, egui::vec2(0.0, 80.0))
                .show(ctx, |ui| {
                    ui.horizontal(|ui| {
                        ui.label("Line:");
                        let input = ui.add(
                            egui::TextEdit::singleline(&mut self.go_to_line)
                                .desired_width(110.0)
                                .hint_text("e.g. 120"),
                        );
                        if self.goto_focus_pending {
                            input.request_focus();
                            self.goto_focus_pending = false;
                        }
                        if input.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                            go = true;
                        }
                        if ui.button("Go").clicked() {
                            go = true;
                        }
                    });
                });
            if go {
                if let (Some(tab), Ok(line)) = (self.current(), self.go_to_line.parse::<usize>()) {
                    let cursor = editor::line_start_char(&tab.text, line);
                    self.pending_selection = Some((cursor, cursor));
                }
                self.show_go_to_line = false;
            }
        }

        if self.show_quick_open {
            let mut open_path = None;
            egui::Window::new("Go to file")
                .collapsible(false)
                .resizable(false)
                .default_width(520.0)
                .anchor(egui::Align2::CENTER_TOP, egui::vec2(0.0, 70.0))
                .show(ctx, |ui| {
                    let input = ui.add(
                        egui::TextEdit::singleline(&mut self.quick_open_query)
                            .desired_width(f32::INFINITY)
                            .char_limit(256)
                            .hint_text("Type a file name or relative path…"),
                    );
                    if self.quick_open_focus_pending {
                        input.request_focus();
                        self.quick_open_focus_pending = false;
                    }
                    let query = self.quick_open_query.to_lowercase();
                    let matches: Vec<PathBuf> = self
                        .project_file_paths
                        .iter()
                        .filter(|path| {
                            let relative = path.strip_prefix(&self.root).unwrap_or(path);
                            query.is_empty()
                                || relative.to_string_lossy().to_lowercase().contains(&query)
                        })
                        .take(40)
                        .cloned()
                        .collect();
                    if input.changed() {
                        self.quick_open_selection = 0;
                    }
                    let has_focus = input.has_focus();
                    let (up, down, enter) = ui.input(|i| {
                        (
                            has_focus && i.key_pressed(egui::Key::ArrowUp),
                            has_focus && i.key_pressed(egui::Key::ArrowDown),
                            has_focus && i.key_pressed(egui::Key::Enter),
                        )
                    });
                    move_list_selection(&mut self.quick_open_selection, matches.len(), up, down);
                    ui.separator();
                    egui::ScrollArea::vertical()
                        .max_height(360.0)
                        .show(ui, |ui| {
                            for (index, path) in matches.iter().enumerate() {
                                let relative = path.strip_prefix(&self.root).unwrap_or(path);
                                let display = relative.to_string_lossy();
                                if ui
                                    .selectable_label(
                                        self.quick_open_selection == index,
                                        display.as_ref(),
                                    )
                                    .clicked()
                                {
                                    open_path = Some(path.clone());
                                }
                            }
                            if matches.is_empty() && self.project_tree_running {
                                ui.label(RichText::new("Scanning project files…").weak());
                            } else if matches.is_empty() {
                                ui.label(
                                    RichText::new("No matching files in the project tree").weak(),
                                );
                            }
                        });
                    if enter {
                        open_path = matches.get(self.quick_open_selection).cloned();
                    }
                });
            if let Some(path) = open_path {
                if self.open_file(path) {
                    self.show_quick_open = false;
                }
            }
        }

        if self.show_command_palette {
            let mut action = None;
            let mut enter = false;
            let mut filtered_commands: Vec<(&'static str, &'static str)> = Vec::new();
            egui::Window::new("Command palette")
                .collapsible(false)
                .resizable(false)
                .default_width(440.0)
                .anchor(egui::Align2::CENTER_TOP, egui::vec2(0.0, 70.0))
                .show(ctx, |ui| {
                    let input = ui.add(
                        egui::TextEdit::singleline(&mut self.command_query)
                            .desired_width(f32::INFINITY)
                            .char_limit(128)
                            .hint_text("Search commands…"),
                    );
                    if self.palette_focus_pending {
                        input.request_focus();
                        self.palette_focus_pending = false;
                    }
                    ui.separator();
                    let commands = [
                        ("New file", "New"),
                        ("New text file", "NewText"),
                        ("Open file…", "Open"),
                        ("Save current file", "Save"),
                        ("Save current file as…", "SaveAs"),
                        ("Close current tab", "Close"),
                        ("Next tab", "NextTab"),
                        ("Previous tab", "PreviousTab"),
                        ("Find in document", "Find"),
                        ("Replace in document", "Replace"),
                        ("Find in project files", "FindFiles"),
                        ("Go to line", "Goto"),
                        ("Quick open file", "QuickOpen"),
                        ("Run current RBL file", "Run"),
                        ("Build current RBL file", "Build"),
                        ("Check current RBL file", "Check"),
                        ("Generate assembly (ASM)", "Assembly"),
                        ("Run test suite", "Tests"),
                        ("Run benchmarks", "Benchmarks"),
                        ("Toolchain doctor", "Doctor"),
                        ("Stop running process", "Stop"),
                        ("Open settings", "Settings"),
                        ("Refresh project tree", "Refresh"),
                        ("Theme: Dark", "ThemeDark"),
                        ("Theme: Light", "ThemeLight"),
                        ("Theme: Nord", "ThemeNord"),
                        ("Theme: Monokai", "ThemeMonokai"),
                        ("Clear output and problems", "ClearOutput"),
                        ("RBL quick reference", "QuickReference"),
                        ("About RBL Studio", "About"),
                        ("Exit RBL Studio", "Exit"),
                    ];
                    let query = self.command_query.to_lowercase();
                    filtered_commands = commands
                        .into_iter()
                        .filter(|(label, _)| {
                            query.is_empty() || label.to_lowercase().contains(&query)
                        })
                        .collect();
                    if input.changed() {
                        self.palette_selection = 0;
                    }
                    let has_focus = input.has_focus();
                    let (up, down) = ui.input(|i| {
                        (
                            has_focus && i.key_pressed(egui::Key::ArrowUp),
                            has_focus && i.key_pressed(egui::Key::ArrowDown),
                        )
                    });
                    move_list_selection(
                        &mut self.palette_selection,
                        filtered_commands.len(),
                        up,
                        down,
                    );
                    enter = has_focus && ui.input(|i| i.key_pressed(egui::Key::Enter));
                    for (index, (label, command)) in filtered_commands.iter().enumerate() {
                        if ui
                            .selectable_label(self.palette_selection == index, *label)
                            .clicked()
                        {
                            action = Some(*command);
                        }
                    }
                });
            if action.is_none() && enter {
                action = filtered_commands
                    .get(self.palette_selection)
                    .map(|(_, command)| *command);
            }
            if let Some(action) = action {
                self.show_command_palette = false;
                match action {
                    "New" => self.new_file(),
                    "NewText" => self.new_text_file(),
                    "Open" => self.open_dialog(),
                    "Save" => {
                        self.save_current();
                    }
                    "SaveAs" => {
                        self.save_as_tab(self.active);
                    }
                    "Close" => self.close_tab(self.active),
                    "NextTab" => {
                        if !self.tabs.is_empty() {
                            self.active = (self.active + 1) % self.tabs.len();
                            self.auto_check_at = None;
                            self.current_match = None;
                            self.pending_selection = None;
                        }
                    }
                    "PreviousTab" => {
                        if !self.tabs.is_empty() {
                            self.active = self.active.checked_sub(1).unwrap_or(self.tabs.len() - 1);
                            self.auto_check_at = None;
                            self.current_match = None;
                            self.pending_selection = None;
                        }
                    }
                    "Find" => {
                        self.show_find = true;
                        self.find_focus_pending = true;
                        self.replace_mode = false;
                    }
                    "Replace" => {
                        self.show_find = true;
                        self.find_focus_pending = true;
                        self.replace_mode = true;
                    }
                    "FindFiles" => {
                        self.show_find_in_files = true;
                        self.project_search_focus_pending = true;
                        self.project_search_query.clear();
                        self.project_search_results.clear();
                    }
                    "Goto" => {
                        self.show_go_to_line = true;
                        self.goto_focus_pending = true;
                        self.go_to_line.clear();
                    }
                    "QuickOpen" => {
                        self.show_quick_open = true;
                        self.quick_open_focus_pending = true;
                        self.quick_open_query.clear();
                    }
                    "Run" => self.start_task(TaskKind::Run),
                    "Build" => self.start_task(TaskKind::Build),
                    "Check" => self.start_task(TaskKind::Check),
                    "Assembly" => self.start_task(TaskKind::Assembly),
                    "Tests" => self.start_task(TaskKind::Tests),
                    "Benchmarks" => self.start_task(TaskKind::Benchmarks),
                    "Doctor" => self.start_task(TaskKind::Doctor),
                    "Stop" => self.stop_task(),
                    "Settings" => self.show_settings = true,
                    "Refresh" => self.refresh_project_files(),
                    "ThemeDark" | "ThemeLight" | "ThemeNord" | "ThemeMonokai" => {
                        let theme = match action {
                            "ThemeDark" => "Dark",
                            "ThemeLight" => "Light",
                            "ThemeNord" => "Nord",
                            _ => "Monokai",
                        };
                        self.settings.theme = theme.into();
                        apply_theme(ctx, theme);
                        if let Err(error) = settings::save(&self.settings) {
                            self.status = format!("Could not save settings: {error}");
                        }
                    }
                    "ClearOutput" => {
                        self.output.clear();
                        self.output_line_starts.clear();
                        self.problems.clear();
                        self.problems_line_starts.clear();
                        self.console.clear();
                        self.console_line_starts.clear();
                    }
                    "QuickReference" => self.show_quick_reference = true,
                    "About" => self.show_about = true,
                    "Exit" => ctx.send_viewport_cmd(egui::ViewportCommand::Close),
                    _ => {}
                }
            }
        }

        if self.show_settings {
            egui::Window::new("RBL Studio settings")
                .collapsible(false)
                .resizable(true)
                .default_size([480.0, 560.0])
                .show(ctx, |ui| {
                    ui.heading("Editor");
                    ui.horizontal(|ui| {
                        ui.label("Theme");
                        egui::ComboBox::from_id_salt("theme")
                            .selected_text(&self.settings.theme)
                            .show_ui(ui, |ui| {
                                for theme in ["Dark", "Light", "Nord", "Monokai"] {
                                    ui.selectable_value(
                                        &mut self.settings.theme,
                                        theme.into(),
                                        theme,
                                    );
                                }
                            });
                    });
                    ui.add(
                        egui::Slider::new(&mut self.settings.font_size, 9.0..=24.0)
                            .text("Editor font size"),
                    );
                    ui.add(egui::Slider::new(&mut self.settings.tab_size, 2..=8).text("Tab size"));
                    ui.checkbox(&mut self.settings.auto_check, "Automatic syntax checks");
                    ui.checkbox(&mut self.settings.auto_save, "Auto-save before Run/Build");
                    ui.checkbox(&mut self.settings.show_line_numbers, "Show line numbers");
                    ui.checkbox(
                        &mut self.settings.syntax_highlighting,
                        "RBL syntax highlighting",
                    )
                    .on_hover_text("Highlight RBL keywords, strings, comments and numbers");
                    ui.checkbox(&mut self.settings.soft_wrap, "Wrap long editor lines")
                        .on_hover_text(
                            "Wrap long lines to the editor width instead of scrolling horizontally",
                        );
                    ui.checkbox(&mut self.settings.indent_guides, "Show indentation guides")
                        .on_hover_text("Draw vertical guides through indented code blocks");
                    ui.separator();
                    ui.heading("Toolchain");
                    ui.horizontal(|ui| {
                        ui.label("Windows toolchain");
                        egui::ComboBox::from_id_salt("wsl_mode")
                            .selected_text(&self.settings.wsl_mode)
                            .show_ui(ui, |ui| {
                                for mode in ["auto", "native", "wsl"] {
                                    ui.selectable_value(
                                        &mut self.settings.wsl_mode,
                                        mode.into(),
                                        mode,
                                    );
                                }
                            });
                    });
                    ui.horizontal(|ui| {
                        ui.label("WSL distribution");
                        ui.text_edit_singleline(&mut self.settings.wsl_distro);
                    });
                    ui.separator();
                    ui.horizontal(|ui| {
                        if ui.button("Restore defaults").clicked() {
                            self.settings = Settings::default();
                            apply_theme(ctx, &self.settings.theme);
                        }
                        if ui.button("Apply").clicked() {
                            match settings::save(&self.settings) {
                                Ok(()) => {
                                    apply_theme(ctx, &self.settings.theme);
                                    self.show_settings = false;
                                    self.status = "Settings saved".into();
                                }
                                Err(error) => {
                                    self.status = format!("Could not save settings: {error}");
                                }
                            }
                        }
                        if ui.button("Cancel").clicked() {
                            self.settings = settings::load();
                            apply_theme(ctx, &self.settings.theme);
                            self.show_settings = false;
                        }
                    });
                });
        }
        if self.show_about {
            egui::Window::new("About RBL Studio")
                .collapsible(false)
                .resizable(false)
                .show(ctx, |ui| {
                    ui.heading("RBL Studio");
                    ui.label("Native desktop IDE · Rust · egui");
                    ui.label("Direct x86-64 assembly toolchain for RBL 0.8.0");
                    ui.label("No browser engine, WebView or embedded web runtime.");
                    ui.label(
                        "Compiler integration uses the repository's existing rbltool.py driver.",
                    );
                    if ui.button("Close").clicked() {
                        self.show_about = false;
                    }
                });
        }
        if self.show_quick_reference {
            egui::Window::new("RBL Quick Reference")
                .default_size([850.0, 650.0])
                .show(ctx, |ui| {
                    let path = self
                        .root
                        .join("docs")
                        .join("eng")
                        .join("reference")
                        .join("syntax.md");
                    match fs::read_to_string(path) {
                        Ok(text) => {
                            egui::ScrollArea::vertical().show(ui, |ui| {
                                ui.add(
                                    egui::Label::new(RichText::new(text).monospace())
                                        .selectable(true),
                                );
                            });
                        }
                        Err(error) => {
                            ui.label(format!("Cannot read quick reference: {error}"));
                        }
                    }
                    if ui.button("Close").clicked() {
                        self.show_quick_reference = false;
                    }
                });
        }
        if self.show_close_prompt {
            egui::Window::new("Unsaved changes")
                .collapsible(false)
                .resizable(false)
                .show(ctx, |ui| {
                    ui.label(if self.pending_close.is_some() {
                        "This document has unsaved changes."
                    } else {
                        "There are unsaved documents. Save before exiting?"
                    });
                    ui.horizontal(|ui| {
                        if ui.button("Save").clicked() {
                            if let Some(tab_id) = self.pending_close {
                                if let Some(index) =
                                    self.tabs.iter().position(|tab| tab.id == tab_id)
                                {
                                    if self.save_tab(index) {
                                        self.remove_tab(index);
                                        self.show_close_prompt = false;
                                        self.pending_close = None;
                                    }
                                } else {
                                    self.show_close_prompt = false;
                                    self.pending_close = None;
                                    self.status = "The document was already closed".into();
                                }
                            } else {
                                let dirty: Vec<usize> = self
                                    .tabs
                                    .iter()
                                    .enumerate()
                                    .filter_map(|(i, t)| t.dirty.then_some(i))
                                    .collect();
                                let mut all_saved = true;
                                for index in dirty {
                                    if !self.save_tab(index) {
                                        all_saved = false;
                                        break;
                                    }
                                }
                                if all_saved {
                                    self.show_close_prompt = false;
                                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                                }
                            }
                        }
                        if ui.button("Discard").clicked() {
                            if let Some(tab_id) = self.pending_close {
                                if let Some(index) =
                                    self.tabs.iter().position(|tab| tab.id == tab_id)
                                {
                                    self.remove_tab(index);
                                }
                            } else {
                                for tab in &mut self.tabs {
                                    tab.dirty = false;
                                }
                                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                            }
                            self.show_close_prompt = false;
                            self.pending_close = None;
                        }
                        if ui.button("Cancel").clicked() {
                            self.show_close_prompt = false;
                            self.pending_close = None;
                        }
                    });
                });
        }
    }
}

impl eframe::App for RblStudioApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.poll_process();
        if !self.rx.is_empty() {
            ctx.request_repaint();
        }
        self.poll_project_tree();
        self.poll_project_search();
        self.handle_shortcuts(ctx);
        let title = self
            .current()
            .map(|tab| format!("RBL Studio — {}", tab.display_title()))
            .unwrap_or_else(|| "RBL Studio".into());
        if title != self.window_title {
            self.window_title.clone_from(&title);
            ctx.send_viewport_cmd(egui::ViewportCommand::Title(title));
        }
        if ctx.input(|i| i.viewport().close_requested()) {
            if self.running {
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
                self.status =
                    "A process is running. Stop it or wait for completion before closing.".into();
            } else if self.tabs.iter().any(|tab| tab.dirty) {
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
                self.pending_close = None;
                self.show_close_prompt = true;
            }
        }
        self.menu_bar(ctx);
        self.toolbar(ctx);
        self.status_bar(ctx);
        self.output_panel(ctx);
        self.file_tree(ctx);
        self.editor_panel(ctx);
        self.dialogs(ctx);
        // Explicit UI actions (especially Run) take priority over a debounced
        // background check that becomes due in the same frame.
        if let Some((tab_id, deadline)) = self.auto_check_at {
            if Instant::now() >= deadline {
                if !self
                    .current()
                    .is_some_and(|tab| tab.id == tab_id && tab.is_rbl())
                {
                    self.auto_check_at = None;
                } else if self.running {
                    self.auto_check_at =
                        Some((tab_id, Instant::now() + Duration::from_millis(150)));
                } else {
                    self.auto_check_at = None;
                    self.start_task(TaskKind::Check);
                }
            }
        }
        // Input and egui animations already wake the UI when needed. Only poll while
        // background work or a debounced check is active; a static editor can sleep.
        if self.running
            || self.auto_check_at.is_some()
            || self.project_search_running
            || self.project_tree_running
        {
            ctx.request_repaint_after(Duration::from_millis(50));
        }
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        if let Some(cancel) = &self.cancel {
            cancel.store(true, Ordering::Relaxed);
        }
        self.process_input_tx.take();
        // Disconnect readers before joining: blocked channel sends then fail
        // instead of waiting for a UI frame that will never happen.
        let (_replacement_tx, replacement_rx) = bounded(1);
        let old_rx = std::mem::replace(&mut self.rx, replacement_rx);
        drop(old_rx);
        if let Some(worker) = self.process_worker.take() {
            let _ = worker.join();
        }
        if let Some(cancel) = &self.project_search_cancel {
            cancel.store(true, Ordering::Relaxed);
        }
    }
}

const MAX_PROJECT_ENTRIES: usize = 20_000;
const MAX_PROJECT_DIRECTORY_SCAN_ENTRIES: usize = 40_000;
const MAX_PROJECT_SCAN_ENTRIES: usize = 100_000;

fn parse_diagnostic_location(line: &str) -> Option<(PathBuf, usize, usize)> {
    let marker = line.find(": error").or_else(|| line.find(": warning"))?;
    let location = &line[..marker];
    let mut parts = location.rsplitn(3, ':');
    let column = parts.next()?.parse::<usize>().ok()?;
    let line_number = parts.next()?.parse::<usize>().ok()?;
    let path = parts.next()?;
    if path.is_empty() || line_number == 0 || column == 0 {
        return None;
    }
    Some((PathBuf::from(path), line_number, column))
}

fn collect_bounded_sorted<T, K>(
    entries: impl IntoIterator<Item = T>,
    limit: usize,
    scan_limit: usize,
    scanned_total: &mut usize,
    mut include: impl FnMut(&T) -> bool,
    mut key: impl FnMut(&T) -> K,
) -> (Vec<T>, bool)
where
    K: Ord,
{
    let mut scanned = 0usize;
    let mut selected = Vec::with_capacity(scan_limit.min(1024));
    for entry in entries.into_iter().take(scan_limit.saturating_add(1)) {
        scanned += 1;
        *scanned_total = (*scanned_total).saturating_add(1);
        if scanned > scan_limit {
            break;
        }
        if include(&entry) {
            selected.push(entry);
        }
    }
    let truncated = scanned > scan_limit || selected.len() > limit;
    selected.sort_by_key(&mut key);
    selected.truncate(limit);
    (selected, truncated)
}

fn collect_files(
    _root: &Path,
    dir: &Path,
    depth: usize,
    out: &mut Vec<(PathBuf, usize, bool)>,
    collapsed: &HashSet<PathBuf>,
    scanned_total: &mut usize,
) -> bool {
    if depth > 8 || out.len() >= MAX_PROJECT_ENTRIES || *scanned_total >= MAX_PROJECT_SCAN_ENTRIES {
        return true;
    }
    let Ok(entries) = fs::read_dir(dir) else {
        return true;
    };
    let remaining = MAX_PROJECT_ENTRIES.saturating_sub(out.len());
    let mut had_errors = false;
    let entries = entries.map(|entry| entry.ok());
    let remaining_scan = MAX_PROJECT_SCAN_ENTRIES.saturating_sub(*scanned_total);
    let scan_limit = remaining_scan
        .saturating_sub(1)
        .min(MAX_PROJECT_DIRECTORY_SCAN_ENTRIES);
    if scan_limit == 0 {
        return true;
    }
    let (entries, mut truncated) = collect_bounded_sorted(
        entries,
        remaining,
        scan_limit,
        scanned_total,
        |entry| {
            let Some(entry) = entry.as_ref() else {
                had_errors = true;
                return false;
            };
            let Ok(file_type) = entry.file_type() else {
                had_errors = true;
                return false;
            };
            if file_type.is_symlink() || (!file_type.is_dir() && !file_type.is_file()) {
                return false;
            }
            let file_name = entry.file_name();
            let Some(file_name) = file_name.to_str() else {
                had_errors = true;
                return false;
            };
            !file_name.starts_with('.')
                && !matches!(
                    file_name,
                    "target" | "build" | "bin" | "__pycache__" | ".venv" | "node_modules"
                )
        },
        |entry| {
            let Some(entry) = entry.as_ref() else {
                return (true, std::ffi::OsString::new());
            };
            (
                !entry.file_type().is_ok_and(|file_type| file_type.is_dir()),
                entry.file_name(),
            )
        },
    );
    truncated |= had_errors;
    for entry in entries {
        let Some(entry) = entry else {
            continue;
        };
        if out.len() >= MAX_PROJECT_ENTRIES {
            truncated = true;
            break;
        }
        let path = entry.path();
        let Ok(file_type) = entry.file_type() else {
            truncated = true;
            continue;
        };
        let is_dir = file_type.is_dir();
        out.push((path.clone(), depth, is_dir));
        if is_dir && !collapsed.contains(&path) {
            truncated |= collect_files(_root, &path, depth + 1, out, collapsed, scanned_total);
        }
    }
    truncated
}

fn snapshot_aliases(temporary: &Path, original: &Path) -> Vec<(String, String)> {
    #[allow(unused_mut)]
    let mut aliases = vec![(
        temporary.to_string_lossy().into_owned(),
        original.to_string_lossy().into_owned(),
    )];
    #[cfg(windows)]
    if let (Some(temporary_wsl), Some(original_wsl)) = (
        windows_path_to_wsl(temporary),
        windows_path_to_wsl(original),
    ) {
        aliases.push((temporary_wsl, original_wsl));
    }
    aliases
}

#[cfg(windows)]
fn windows_path_to_wsl(path: &Path) -> Option<String> {
    let value = path.to_string_lossy().replace('\\', "/");
    let bytes = value.as_bytes();
    if bytes.len() < 3 || bytes[1] != b':' || bytes[2] != b'/' || !bytes[0].is_ascii_alphabetic() {
        return None;
    }
    Some(format!(
        "/mnt/{}/{}",
        (bytes[0] as char).to_ascii_lowercase(),
        &value[3..]
    ))
}

fn apply_theme(ctx: &egui::Context, theme: &str) {
    let mut visuals = match theme {
        "Light" => egui::Visuals::light(),
        _ => egui::Visuals::dark(),
    };
    let (bg, panel, accent) = match theme {
        "Light" => (
            Color32::from_rgb(245, 245, 249),
            Color32::from_rgb(232, 232, 240),
            Color32::from_rgb(105, 65, 165),
        ),
        "Nord" => (
            Color32::from_rgb(46, 52, 64),
            Color32::from_rgb(59, 66, 82),
            Color32::from_rgb(136, 192, 208),
        ),
        "Monokai" => (
            Color32::from_rgb(39, 40, 34),
            Color32::from_rgb(48, 49, 43),
            Color32::from_rgb(174, 129, 255),
        ),
        _ => (
            Color32::from_rgb(24, 22, 31),
            Color32::from_rgb(34, 30, 44),
            Color32::from_rgb(157, 118, 216),
        ),
    };
    visuals.panel_fill = panel;
    visuals.window_fill = bg;
    visuals.extreme_bg_color = bg;
    visuals.selection.bg_fill = accent;
    visuals.widgets.active.bg_fill = accent;
    visuals.widgets.hovered.bg_fill = blend(panel, accent, 0.28);
    visuals.widgets.inactive.bg_fill = blend(panel, bg, 0.45);
    visuals.widgets.noninteractive.bg_fill = panel;
    visuals.widgets.active.fg_stroke.color = Color32::WHITE;
    visuals.widgets.hovered.fg_stroke.color = visuals.text_color();
    visuals.widgets.inactive.corner_radius = egui::CornerRadius::same(5);
    visuals.widgets.hovered.corner_radius = egui::CornerRadius::same(5);
    visuals.widgets.active.corner_radius = egui::CornerRadius::same(5);
    visuals.window_corner_radius = egui::CornerRadius::same(8);
    visuals.menu_corner_radius = egui::CornerRadius::same(7);
    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = egui::vec2(7.0, 5.0);
    style.spacing.button_padding = egui::vec2(8.0, 4.0);
    style.spacing.window_margin = egui::Margin::same(10);
    style.spacing.indent = 14.0;
    style.visuals = visuals;
    ctx.set_style(style);
}

fn blend(base: Color32, tint: Color32, amount: f32) -> Color32 {
    let amount = amount.clamp(0.0, 1.0);
    let channel = |a: u8, b: u8| (a as f32 * (1.0 - amount) + b as f32 * amount).round() as u8;
    Color32::from_rgba_unmultiplied(
        channel(base.r(), tint.r()),
        channel(base.g(), tint.g()),
        channel(base.b(), tint.b()),
        channel(base.a(), tint.a()),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keyboard_selection_moves_within_visible_results() {
        let mut selected = 0;
        move_list_selection(&mut selected, 3, false, true);
        assert_eq!(selected, 1);
        move_list_selection(&mut selected, 3, false, true);
        move_list_selection(&mut selected, 3, false, true);
        assert_eq!(selected, 2);
        move_list_selection(&mut selected, 3, false, true);
        assert_eq!(selected, 2);
        move_list_selection(&mut selected, 3, true, false);
        assert_eq!(selected, 1);
        move_list_selection(&mut selected, 0, true, true);
        assert_eq!(selected, 0);
    }

    #[test]
    fn global_shortcuts_do_not_steal_keys_from_dialog_text_inputs() {
        assert!(!should_handle_global_shortcuts(false, true));
        assert!(should_handle_global_shortcuts(true, true));
        assert!(should_handle_global_shortcuts(false, false));
    }

    #[test]
    fn save_as_cannot_create_two_tabs_for_the_same_canonical_file() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("main.rbl");
        fs::write(&path, "func main() ()").unwrap();
        let tabs = vec![EditorTab::open(&path).unwrap(), EditorTab::untitled()];
        assert_eq!(duplicate_open_tab(&tabs, &path, 1), Some(0));
        assert_eq!(duplicate_open_tab(&tabs, &path, 0), None);
    }

    #[test]
    fn replace_match_validation_rejects_stale_offsets_after_unicode_edits() {
        let text = "prefix Ж🙂 target suffix";
        let target = text.find("target").unwrap();
        let target_char = text[..target].chars().count();
        assert!(match_range_is_current(
            text,
            "target",
            (target_char, target_char + 6)
        ));
        assert!(!match_range_is_current(text, "target", (0, 6)));
        assert!(!match_range_is_current(
            text,
            "target",
            (target_char + 1, target_char + 7)
        ));
    }

    #[test]
    fn reopening_an_active_query_dialog_preserves_its_query_and_selection() {
        let mut query = "needle".to_owned();
        let mut selection = 3;
        prepare_dialog_input(true, &mut query, &mut selection);
        assert_eq!(query, "needle");
        assert_eq!(selection, 3);

        prepare_dialog_input(false, &mut query, &mut selection);
        assert!(query.is_empty());
        assert_eq!(selection, 0);
    }

    #[test]
    fn project_entry_collection_is_bounded_by_the_directory_scan_limit() {
        let consumed = std::cell::Cell::new(0usize);
        let mut scanned_total = 0usize;
        let (entries, truncated) = collect_bounded_sorted(
            (0..1_000_000).inspect(|_| consumed.set(consumed.get() + 1)),
            8,
            32,
            &mut scanned_total,
            |_| true,
            |value| *value,
        );
        assert!(truncated);
        assert_eq!(consumed.get(), 33);
        assert_eq!(scanned_total, 33);
        assert_eq!(entries, (0..8).collect::<Vec<_>>());

        let mut scanned_total = 0usize;
        let (entries, truncated) =
            collect_bounded_sorted(0..8, 8, 100, &mut scanned_total, |_| true, |value| *value);
        assert!(!truncated);
        assert_eq!(entries, (0..8).collect::<Vec<_>>());
    }

    #[test]
    fn project_entry_scan_is_bounded_even_when_all_entries_are_filtered() {
        let consumed = std::cell::Cell::new(0usize);
        let mut scanned_total = 0usize;
        let (entries, truncated) = collect_bounded_sorted(
            (0..1_000_000).inspect(|_| consumed.set(consumed.get() + 1)),
            8,
            32,
            &mut scanned_total,
            |_| false,
            |value| *value,
        );
        assert!(truncated);
        assert!(entries.is_empty());
        assert_eq!(consumed.get(), 33);
        assert_eq!(scanned_total, 33);
    }

    #[test]
    fn project_entry_sort_keeps_directories_ahead_of_files_in_scanned_window() {
        let entries = [
            (false, "file-a"),
            (false, "file-b"),
            (false, "file-c"),
            (true, "source-dir"),
        ];
        let mut scanned_total = 0usize;
        let (entries, truncated) = collect_bounded_sorted(
            entries,
            3,
            16,
            &mut scanned_total,
            |_| true,
            |(is_dir, name)| (!*is_dir, *name),
        );
        assert!(truncated);
        assert_eq!(entries[0], (true, "source-dir"));
    }

    #[test]
    fn project_tree_skips_generated_and_hidden_directories() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join("src")).unwrap();
        fs::create_dir_all(temp.path().join("build")).unwrap();
        fs::create_dir_all(temp.path().join(".git")).unwrap();
        fs::write(temp.path().join("src/main.rbl"), "func main() ()").unwrap();
        fs::write(temp.path().join("build/generated.rbl"), "").unwrap();
        fs::write(temp.path().join(".hidden.rbl"), "").unwrap();
        let mut files = Vec::new();
        let mut scanned_total = 0usize;
        collect_files(
            temp.path(),
            temp.path(),
            0,
            &mut files,
            &HashSet::new(),
            &mut scanned_total,
        );
        let paths: Vec<_> = files
            .iter()
            .map(|(path, _, _)| {
                path.strip_prefix(temp.path())
                    .unwrap()
                    .to_string_lossy()
                    .replace('\\', "/")
            })
            .collect();
        assert!(paths.iter().any(|path| path == "src/main.rbl"));
        assert!(!paths.iter().any(|path| path.contains("build")));
        assert!(!paths.iter().any(|path| path.contains(".git")));
        assert!(!paths.iter().any(|path| path.contains(".hidden")));
    }

    #[test]
    fn project_tree_reports_filesystem_access_errors_as_incomplete() {
        let temp = tempfile::tempdir().unwrap();
        let missing = temp.path().join("missing-directory");
        let mut entries = Vec::new();
        let mut scanned_total = 0usize;
        assert!(collect_files(
            &missing,
            &missing,
            0,
            &mut entries,
            &HashSet::new(),
            &mut scanned_total,
        ));
    }

    #[test]
    fn project_tree_lists_arbitrary_text_files_and_collapses_directories() {
        let temp = tempfile::tempdir().unwrap();
        let nested = temp.path().join("notes");
        fs::create_dir_all(&nested).unwrap();
        fs::write(nested.join("readme.txt"), "text").unwrap();
        fs::write(temp.path().join("settings.json"), "{}").unwrap();
        fs::write(temp.path().join("script.py"), "print(1)").unwrap();

        let mut files = Vec::new();
        let mut scanned_total = 0usize;
        collect_files(
            temp.path(),
            temp.path(),
            0,
            &mut files,
            &HashSet::new(),
            &mut scanned_total,
        );
        assert!(files
            .iter()
            .any(|(path, _, _)| path.ends_with("settings.json")));
        assert!(files
            .iter()
            .any(|(path, _, _)| path.ends_with("readme.txt")));
        assert!(files.iter().any(|(path, _, _)| path.ends_with("script.py")));

        let mut collapsed = HashSet::new();
        collapsed.insert(nested.clone());
        files.clear();
        scanned_total = 0;
        collect_files(
            temp.path(),
            temp.path(),
            0,
            &mut files,
            &collapsed,
            &mut scanned_total,
        );
        assert!(files
            .iter()
            .any(|(path, _, is_dir)| path == &nested && *is_dir));
        assert!(!files
            .iter()
            .any(|(path, _, _)| path.ends_with("readme.txt")));
    }

    #[test]
    fn collapsing_project_directory_filters_cached_entries_without_rescanning() {
        let entries = vec![
            (PathBuf::from("src"), 0, true),
            (PathBuf::from("src/main.rbl"), 1, false),
            (PathBuf::from("src/nested"), 1, true),
            (PathBuf::from("src/nested/lib.rbl"), 2, false),
            (PathBuf::from("README.md"), 0, false),
        ];
        let mut collapsed = HashSet::new();
        collapsed.insert(PathBuf::from("src"));
        let visible = visible_project_entries(&entries, &collapsed);
        assert_eq!(
            visible
                .iter()
                .map(|(path, _, _)| path.as_path())
                .collect::<Vec<_>>(),
            vec![Path::new("src"), Path::new("README.md")]
        );
        assert_eq!(entries.len(), 5, "the cached full tree must remain intact");
    }

    #[test]
    fn diagnostic_locations_parse_unix_and_windows_paths() {
        assert_eq!(
            parse_diagnostic_location("src/main.rbl:12:4: error [parser]: expected expression"),
            Some((PathBuf::from("src/main.rbl"), 12, 4))
        );
        assert_eq!(
            parse_diagnostic_location(
                r"Q:\Projects\RBL Studio\main.rbl:7:19: warning [semantic]: unused value"
            ),
            Some((PathBuf::from(r"Q:\Projects\RBL Studio\main.rbl"), 7, 19))
        );
        assert_eq!(parse_diagnostic_location("not a diagnostic"), None);
    }

    #[test]
    fn chunked_project_search_handles_utf8_boundary_and_cancellation() {
        let mut line = "x".repeat(64 * 1024 - 3);
        line.push_str("Ж🙂Needle");
        let cancel = AtomicBool::new(false);
        assert_eq!(
            contains_case_insensitive_chunked(&line, "needle", &cancel),
            Some(true)
        );
        cancel.store(true, Ordering::Relaxed);
        assert_eq!(
            contains_case_insensitive_chunked(&line, "needle", &cancel),
            None
        );
    }

    #[test]
    fn output_buffer_is_bounded_and_keeps_utf8_boundaries() {
        let mut output = String::new();
        let mut line_starts = Vec::new();
        append_output(
            &mut output,
            &mut line_starts,
            &"🙂".repeat(OUTPUT_LIMIT_BYTES / 4 + 100),
        );
        assert!(output.len() <= OUTPUT_LIMIT_BYTES + "[Earlier output truncated]\n".len());
        assert!(output.is_char_boundary(output.len()));
        assert!(output.starts_with("[Earlier output truncated]\n"));
        assert_eq!(line_starts, build_output_line_starts(&output));
    }

    #[test]
    fn output_line_index_handles_appends_newlines_and_truncation() {
        let mut output = String::new();
        let mut starts = Vec::new();
        append_output(&mut output, &mut starts, "first\n");
        assert_eq!(starts.len(), 2);
        append_output(&mut output, &mut starts, "second\r\nthird");
        assert_eq!(starts.len(), 3);
        assert_eq!(output_line(&output, &starts, 0), "first");
        assert_eq!(output_line(&output, &starts, 1), "second");
        assert_eq!(output_line(&output, &starts, 2), "third");
    }

    #[test]
    fn output_line_index_keeps_the_final_empty_line_after_newline() {
        let text = "last line\n";
        let starts = build_output_line_starts(text);
        assert_eq!(starts, vec![0, text.len() as u32]);
        assert_eq!(output_line(text, &starts, 0), "last line");
        assert_eq!(output_line(text, &starts, 1), "");
    }

    #[test]
    fn aggregate_open_document_budget_counts_text_and_page_history_across_tabs() {
        let mut tabs = Vec::new();
        for _ in 0..3 {
            let mut tab = EditorTab::untitled_text();
            tab.text = "x".repeat(16 * 1024 * 1024);
            tabs.push(tab);
        }
        tabs[0].page_history = Vec::with_capacity(1024 * 1024);
        let mut candidate = EditorTab::untitled_text();
        candidate.text = "y".repeat(
            8 * 1024 * 1024 - 4 * editor::MAX_PAGE_HISTORY_ENTRIES * std::mem::size_of::<usize>(),
        );
        assert!(can_open_document(&tabs, &candidate));

        candidate.text.push('z');
        assert!(!can_open_document(&tabs, &candidate));
    }

    #[test]
    fn aggregate_open_document_budget_enforces_the_tab_count_limit() {
        let tabs: Vec<_> = (0..MAX_OPEN_TABS)
            .map(|_| EditorTab::untitled_text())
            .collect();
        assert!(!can_open_document(&tabs, &EditorTab::untitled_text()));
    }

    #[cfg(windows)]
    #[test]
    fn wsl_path_alias_preserves_spaces_and_unicode() {
        let path = PathBuf::from(r"Q:\Проекты\RBL Studio\main.rbl");
        assert_eq!(
            windows_path_to_wsl(&path).as_deref(),
            Some("/mnt/q/Проекты/RBL Studio/main.rbl")
        );
    }
}
