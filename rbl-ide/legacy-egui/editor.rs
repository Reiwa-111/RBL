use eframe::egui::{
    self,
    text::{LayoutJob, TextFormat},
    Align2, Color32, FontId, Id, Key, Sense, TextEdit,
};
use std::sync::atomic::{AtomicU64, Ordering};
use std::{
    collections::HashSet,
    fs,
    hash::{Hash, Hasher},
    io::Read,
    path::{Path, PathBuf},
    time::SystemTime,
};

static NEXT_TAB_ID: AtomicU64 = AtomicU64::new(1);
pub const MAX_EDITABLE_FILE_BYTES: u64 = 16 * 1024 * 1024;
pub const LARGE_DOCUMENT_THRESHOLD_BYTES: usize = 512 * 1024;
pub const EDITOR_PAGE_BYTES: usize = 64 * 1024;
const MAX_FOLDED_BLOCKS: usize = 1024;

fn append_enter_events(
    events: &mut Vec<egui::Event>,
    physical_key: Option<Key>,
    repeat: bool,
    modifiers: egui::Modifiers,
    indent: &str,
) {
    // Multiline TextEdit handles newlines from Key::Enter; Event::Text("\n") is ignored by egui.
    events.push(egui::Event::Key {
        key: Key::Enter,
        physical_key,
        pressed: true,
        repeat,
        modifiers,
    });
    if !indent.is_empty() {
        events.push(egui::Event::Text(indent.to_owned()));
    }
}

fn resolve_editor_selection(
    tab: &mut EditorTab,
    requested: Option<(usize, usize)>,
) -> Option<(usize, usize)> {
    let pending = tab.pending_cursor_selection.take();
    requested.or_else(|| pending.map(|cursor| (cursor, cursor)))
}

fn toggle_fold_line(folded_lines: &mut Vec<usize>, line: usize) -> bool {
    match folded_lines.binary_search(&line) {
        Ok(index) => {
            folded_lines.remove(index);
            true
        }
        Err(index) if folded_lines.len() < MAX_FOLDED_BLOCKS => {
            folded_lines.insert(index, line);
            true
        }
        Err(_) => false,
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum TextEncoding {
    #[default]
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum LineEnding {
    #[default]
    Lf,
    CrLf,
    Cr,
}

#[derive(Clone, Copy, Debug)]
pub struct EditorViewOptions {
    pub font_size: f32,
    pub tab_size: usize,
    pub line_numbers: bool,
    pub syntax_highlighting: bool,
    pub soft_wrap: bool,
    pub indent_guides: bool,
    pub document_limit_bytes: usize,
    pub undo_budget_bytes: usize,
}

#[derive(Clone)]
pub struct EditorTab {
    pub id: u64,
    pub path: Option<PathBuf>,
    pub title: String,
    pub text: String,
    encoded_len: usize,
    pub dirty: bool,
    pub last_edit: SystemTime,
    pub cursor_char: usize,
    pub encoding: TextEncoding,
    pub line_ending: LineEnding,
    pub line_count: usize,
    pub cursor_line: usize,
    pub cursor_column: usize,
    pub page_start_byte: usize,
    pub page_start_char: usize,
    pub page_start_line: usize,
    pub page_start_column: usize,
    page_inside_triple_string: bool,
    pub page_history: Vec<usize>,
    pub folded_lines: Vec<usize>,
    undo_stack: Vec<UndoRecord>,
    redo_stack: Vec<UndoRecord>,
    saved_hash: u64,
    saved_file_hash: Option<u64>,
    pub limit_reached: bool,
    pending_cursor_selection: Option<usize>,
}

#[derive(Clone)]
struct EditorPosition {
    cursor_char: usize,
    cursor_line: usize,
    cursor_column: usize,
    line_count: usize,
    page_start_byte: usize,
    page_start_char: usize,
    page_start_line: usize,
    page_start_column: usize,
}

#[derive(Clone)]
enum UndoRecord {
    Delta {
        start_byte: usize,
        alternate_text: String,
        expected_text: String,
        alternate_position: EditorPosition,
    },
    DocumentSwap {
        alternate_text: String,
        alternate_page_history: Vec<usize>,
        expected_hash: u64,
        expected_len: usize,
        alternate_position: EditorPosition,
        alternate_encoded_len: usize,
    },
}

const MAX_UNDO_HISTORY_BYTES: usize = 48 * 1024 * 1024;
pub const MAX_OPEN_DOCUMENT_MEMORY_BYTES: usize = 64 * 1024 * 1024;
pub const MAX_PAGE_HISTORY_ENTRIES: usize = 64;

impl EditorPosition {
    fn capture(tab: &EditorTab) -> Self {
        Self {
            cursor_char: tab.cursor_char,
            cursor_line: tab.cursor_line,
            cursor_column: tab.cursor_column,
            line_count: tab.line_count,
            page_start_byte: tab.page_start_byte,
            page_start_char: tab.page_start_char,
            page_start_line: tab.page_start_line,
            page_start_column: tab.page_start_column,
        }
    }

    fn swap_with(&mut self, tab: &mut EditorTab) {
        std::mem::swap(&mut tab.cursor_char, &mut self.cursor_char);
        std::mem::swap(&mut tab.cursor_line, &mut self.cursor_line);
        std::mem::swap(&mut tab.cursor_column, &mut self.cursor_column);
        std::mem::swap(&mut tab.line_count, &mut self.line_count);
        std::mem::swap(&mut tab.page_start_byte, &mut self.page_start_byte);
        std::mem::swap(&mut tab.page_start_char, &mut self.page_start_char);
        std::mem::swap(&mut tab.page_start_line, &mut self.page_start_line);
        std::mem::swap(&mut tab.page_start_column, &mut self.page_start_column);
    }
}

impl UndoRecord {
    fn memory_bytes(&self) -> usize {
        match self {
            Self::Delta {
                alternate_text,
                expected_text,
                ..
            } => alternate_text.capacity() + expected_text.capacity(),
            Self::DocumentSwap {
                alternate_text,
                alternate_page_history,
                ..
            } => {
                alternate_text.capacity()
                    + alternate_page_history.capacity() * std::mem::size_of::<usize>()
            }
        }
    }
}

impl EditorTab {
    pub fn untitled() -> Self {
        Self::untitled_named("Untitled.rbl")
    }

    pub fn untitled_text() -> Self {
        Self::untitled_named("Untitled.txt")
    }

    fn untitled_named(title: &str) -> Self {
        Self {
            id: NEXT_TAB_ID.fetch_add(1, Ordering::Relaxed),
            path: None,
            title: title.into(),
            text: String::new(),
            encoded_len: 0,
            dirty: false,
            last_edit: SystemTime::now(),
            cursor_char: 0,
            encoding: TextEncoding::Utf8,
            line_ending: LineEnding::Lf,
            line_count: 1,
            cursor_line: 1,
            cursor_column: 1,
            page_start_byte: 0,
            page_start_char: 0,
            page_start_line: 1,
            page_start_column: 1,
            page_inside_triple_string: false,
            page_history: Vec::with_capacity(MAX_PAGE_HISTORY_ENTRIES),
            folded_lines: Vec::new(),
            undo_stack: Vec::new(),
            redo_stack: Vec::new(),
            saved_hash: text_hash(""),
            saved_file_hash: None,
            limit_reached: false,
            pending_cursor_selection: None,
        }
    }

    pub fn open(path: &Path) -> anyhow::Result<Self> {
        let canonical = path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
        let source_bytes = fs::read(&canonical)?;
        let saved_file_hash = Some(bytes_hash(&source_bytes));
        let (text, encoding, line_ending) = read_text_file_with_line_ending(&canonical)?;
        let encoded_len = encoded_text_size(&text, encoding, line_ending);
        let saved_hash = text_hash(&text);
        let line_count = text.bytes().filter(|byte| *byte == b'\n').count() + 1;
        let title = canonical
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("untitled")
            .to_owned();
        Ok(Self {
            id: NEXT_TAB_ID.fetch_add(1, Ordering::Relaxed),
            path: Some(canonical),
            title,
            text,
            encoded_len,
            dirty: false,
            last_edit: SystemTime::now(),
            cursor_char: 0,
            encoding,
            line_ending,
            line_count,
            cursor_line: 1,
            cursor_column: 1,
            page_start_byte: 0,
            page_start_char: 0,
            page_start_line: 1,
            page_start_column: 1,
            page_inside_triple_string: false,
            page_history: Vec::with_capacity(MAX_PAGE_HISTORY_ENTRIES),
            folded_lines: Vec::new(),
            undo_stack: Vec::new(),
            redo_stack: Vec::new(),
            saved_hash,
            saved_file_hash,
            limit_reached: false,
            pending_cursor_selection: None,
        })
    }

    pub fn save(&mut self, target: Option<PathBuf>) -> anyhow::Result<()> {
        let decoded_limit = decoded_text_limit(self.encoding);
        if self.text.len() as u64 > decoded_limit {
            anyhow::bail!(
                "document text exceeds the {} MiB editing limit for {:?}",
                decoded_limit / (1024 * 1024),
                self.encoding
            );
        }
        let requested_path = target
            .or_else(|| self.path.clone())
            .ok_or_else(|| anyhow::anyhow!("no file path selected"))?;
        let path = resolve_save_path(&requested_path)?;
        let same_target = self
            .path
            .as_deref()
            .and_then(|current| resolve_save_path(current).ok())
            .is_some_and(|current| current == path);
        if same_target {
            let disk_bytes = fs::read(&path).map_err(|error| {
                anyhow::anyhow!(
                    "cannot save because the original file is no longer available: {error}"
                )
            })?;
            if Some(bytes_hash(&disk_bytes)) != self.saved_file_hash {
                anyhow::bail!(
                    "file changed outside RBL Studio since it was opened or last saved; reload it or use Save As to avoid overwriting external changes"
                );
            }
        }
        let parent = path
            .parent()
            .ok_or_else(|| anyhow::anyhow!("file path has no parent"))?;
        let mut temp = tempfile::Builder::new()
            .prefix(".rblstudio-")
            .suffix(".tmp")
            .tempfile_in(parent)?;
        use std::io::Write;
        let bytes = encode_text(&self.text, self.encoding, self.line_ending)?;
        if bytes.len() as u64 > MAX_EDITABLE_FILE_BYTES {
            anyhow::bail!(
                "document exceeds the {} MiB editing limit",
                MAX_EDITABLE_FILE_BYTES / (1024 * 1024)
            );
        }
        temp.write_all(&bytes)?;
        #[cfg(unix)]
        if let Ok(metadata) = fs::metadata(&path) {
            temp.as_file().set_permissions(metadata.permissions())?;
        }
        temp.persist(&path).map_err(|e| e.error)?;
        self.encoded_len = bytes.len();
        self.path = Some(path.clone());
        self.title = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("untitled")
            .to_owned();
        self.saved_hash = text_hash(&self.text);
        self.saved_file_hash = Some(bytes_hash(&bytes));
        self.dirty = false;
        Ok(())
    }

    pub fn display_title(&self) -> String {
        if self.dirty {
            format!("{} *", self.title)
        } else {
            self.title.clone()
        }
    }

    pub fn is_rbl(&self) -> bool {
        self.path
            .as_ref()
            .and_then(|path| path.extension())
            .is_some_and(|extension| extension.eq_ignore_ascii_case("rbl"))
            || (self.path.is_none() && self.title.to_ascii_lowercase().ends_with(".rbl"))
    }
}

fn resolve_save_path(path: &Path) -> anyhow::Result<PathBuf> {
    match fs::symlink_metadata(path) {
        Ok(_) => return Ok(fs::canonicalize(path)?),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => return Err(error.into()),
    }
    let file_name = path
        .file_name()
        .ok_or_else(|| anyhow::anyhow!("file path has no file name"))?;
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent)?;
    Ok(fs::canonicalize(parent)?.join(file_name))
}

fn history_bytes(records: &Vec<UndoRecord>) -> usize {
    records
        .iter()
        .map(UndoRecord::memory_bytes)
        .sum::<usize>()
        .saturating_add(records.capacity() * std::mem::size_of::<UndoRecord>())
}

fn document_base_bytes(tab: &EditorTab) -> usize {
    tab.text
        .capacity()
        .saturating_add(
            tab.page_history
                .capacity()
                .saturating_mul(std::mem::size_of::<usize>()),
        )
        .saturating_add(
            tab.folded_lines
                .capacity()
                .saturating_mul(std::mem::size_of::<usize>()),
        )
}

fn push_history(records: &mut Vec<UndoRecord>, record: UndoRecord, budget: usize) {
    let record_bytes = record.memory_bytes();
    let record_overhead = std::mem::size_of::<UndoRecord>();
    if record_bytes.saturating_add(record_overhead) > budget {
        records.clear();
        records.shrink_to_fit();
        return;
    }

    loop {
        if records.len() == records.capacity() {
            records.reserve_exact(1);
        }
        if history_bytes(records).saturating_add(record_bytes) <= budget {
            break;
        }
        if records.is_empty() {
            records.shrink_to_fit();
            records.reserve_exact(1);
            if history_bytes(records).saturating_add(record_bytes) > budget {
                return;
            }
            break;
        }
        records.remove(0);
    }
    records.push(record);
}

fn common_diff(before: &str, after: &str) -> (usize, usize, usize) {
    let mut prefix = before
        .bytes()
        .zip(after.bytes())
        .take_while(|(left, right)| left == right)
        .count();
    while prefix > 0 && (!before.is_char_boundary(prefix) || !after.is_char_boundary(prefix)) {
        prefix -= 1;
    }
    let mut old_end = before.len();
    let mut new_end = after.len();
    while old_end > prefix
        && new_end > prefix
        && before.as_bytes()[old_end - 1] == after.as_bytes()[new_end - 1]
    {
        old_end -= 1;
        new_end -= 1;
    }
    while old_end < before.len() && !before.is_char_boundary(old_end) {
        old_end += 1;
        new_end += 1;
    }
    (prefix, old_end, new_end)
}

fn rebase_page_offsets(
    tab: &mut EditorTab,
    edit_start: usize,
    removed_bytes: usize,
    inserted_bytes: usize,
    active_page_before: usize,
) {
    let old_end = edit_start.saturating_add(removed_bytes);
    let rebase = |offset: usize, text: &str| {
        let shifted = if offset <= edit_start {
            offset
        } else if offset >= old_end {
            offset
                .saturating_sub(removed_bytes)
                .saturating_add(inserted_bytes)
        } else {
            edit_start
        };
        let mut boundary = shifted.min(text.len());
        while boundary > 0 && !text.is_char_boundary(boundary) {
            boundary -= 1;
        }
        boundary
    };

    for offset in &mut tab.page_history {
        *offset = rebase(*offset, &tab.text);
    }
    tab.page_history.dedup();
    if tab.page_history.len() > MAX_PAGE_HISTORY_ENTRIES {
        let excess = tab.page_history.len() - MAX_PAGE_HISTORY_ENTRIES;
        tab.page_history.drain(..excess);
    }
    let active_page = rebase(active_page_before, &tab.text);
    set_page_start(tab, active_page);
}

fn record_delta(
    tab: &mut EditorTab,
    start_byte: usize,
    before: &str,
    after: &str,
    before_position: EditorPosition,
    history_budget: usize,
) {
    if before == after {
        return;
    }
    let (prefix, old_end, new_end) = common_diff(before, after);
    let alternate_text = before[prefix..old_end].to_owned();
    let expected_text = after[prefix..new_end].to_owned();
    if alternate_text == expected_text {
        return;
    }
    tab.encoded_len = tab
        .encoded_len
        .saturating_sub(encoded_fragment_size(
            &alternate_text,
            tab.encoding,
            tab.line_ending,
        ))
        .saturating_add(encoded_fragment_size(
            &expected_text,
            tab.encoding,
            tab.line_ending,
        ));
    rebase_page_offsets(
        tab,
        start_byte + prefix,
        old_end - prefix,
        new_end - prefix,
        before_position.page_start_byte,
    );
    tab.redo_stack.clear();
    tab.redo_stack.shrink_to_fit();
    let record = UndoRecord::Delta {
        start_byte: start_byte + prefix,
        alternate_text,
        expected_text,
        alternate_position: before_position,
    };
    push_history(
        &mut tab.undo_stack,
        record,
        history_budget.min(MAX_UNDO_HISTORY_BYTES),
    );
    tab.dirty = text_hash(&tab.text) != tab.saved_hash;
}

fn record_page_edit(
    tab: &mut EditorTab,
    page_start_byte: usize,
    before: &str,
    after: &str,
    before_position: EditorPosition,
    aggregate_budget: usize,
) {
    let history_budget = aggregate_budget.saturating_sub(document_base_bytes(tab));
    record_delta(
        tab,
        page_start_byte,
        before,
        after,
        before_position,
        history_budget,
    );
}

#[cfg(test)]
fn record_document_edit(
    tab: &mut EditorTab,
    before: &str,
    before_position: EditorPosition,
    aggregate_budget: usize,
) {
    if before == tab.text {
        return;
    }
    let (prefix, old_end, new_end) = common_diff(before, &tab.text);
    let alternate_text = before[prefix..old_end].to_owned();
    let expected_text = tab.text[prefix..new_end].to_owned();
    let before_encoded_len = encoded_text_size(before, tab.encoding, tab.line_ending);
    tab.encoded_len = before_encoded_len
        .saturating_sub(encoded_fragment_size(
            &alternate_text,
            tab.encoding,
            tab.line_ending,
        ))
        .saturating_add(encoded_fragment_size(
            &expected_text,
            tab.encoding,
            tab.line_ending,
        ));
    rebase_page_offsets(
        tab,
        prefix,
        old_end - prefix,
        new_end - prefix,
        before_position.page_start_byte,
    );
    tab.redo_stack.clear();
    tab.redo_stack.shrink_to_fit();
    let record = UndoRecord::Delta {
        start_byte: prefix,
        alternate_text,
        expected_text,
        alternate_position: before_position,
    };
    let history_budget = aggregate_budget.saturating_sub(document_base_bytes(tab));
    push_history(
        &mut tab.undo_stack,
        record,
        history_budget.min(MAX_UNDO_HISTORY_BYTES),
    );
    tab.dirty = text_hash(&tab.text) != tab.saved_hash;
}

fn trim_history_to_budget(records: &mut Vec<UndoRecord>, budget: usize) {
    while !records.is_empty() && history_bytes(records) > budget {
        records.remove(0);
    }
    if records.is_empty() || records.capacity() * std::mem::size_of::<UndoRecord>() > budget {
        records.shrink_to_fit();
    }
}

fn record_document_swap(
    tab: &mut EditorTab,
    alternate_text: String,
    alternate_page_history: Vec<usize>,
    alternate_position: EditorPosition,
    alternate_encoded_len: usize,
    aggregate_budget: usize,
) {
    tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
    tab.redo_stack.clear();
    tab.redo_stack.shrink_to_fit();
    let record = UndoRecord::DocumentSwap {
        alternate_text,
        alternate_page_history,
        expected_hash: text_hash(&tab.text),
        expected_len: tab.text.len(),
        alternate_position,
        alternate_encoded_len,
    };
    let history_budget = aggregate_budget
        .saturating_sub(document_base_bytes(tab))
        .min(MAX_UNDO_HISTORY_BYTES);
    push_history(&mut tab.undo_stack, record, history_budget);
    tab.dirty = text_hash(&tab.text) != tab.saved_hash;
    tab.last_edit = SystemTime::now();
}

pub fn memory_bytes(tab: &EditorTab) -> usize {
    document_base_bytes(tab) + history_bytes(&tab.undo_stack) + history_bytes(&tab.redo_stack)
}

pub fn apply_undo(tab: &mut EditorTab, redo: bool, aggregate_budget: usize) -> bool {
    let mut record = if redo {
        tab.redo_stack.pop()
    } else {
        tab.undo_stack.pop()
    };
    let Some(mut record) = record.take() else {
        return false;
    };
    let other_history_bytes = if redo {
        history_bytes(&tab.redo_stack)
    } else {
        history_bytes(&tab.undo_stack)
    };
    let destination_budget = aggregate_budget
        .saturating_sub(document_base_bytes(tab))
        .saturating_sub(other_history_bytes)
        .min(MAX_UNDO_HISTORY_BYTES.saturating_sub(other_history_bytes));
    if record
        .memory_bytes()
        .saturating_add(std::mem::size_of::<UndoRecord>())
        > destination_budget
    {
        if redo {
            tab.redo_stack.push(record);
        } else {
            tab.undo_stack.push(record);
        }
        return false;
    }
    let applied = match &mut record {
        UndoRecord::Delta {
            start_byte,
            alternate_text,
            expected_text,
            alternate_position,
        } => {
            let end_byte = start_byte.saturating_add(expected_text.len());
            if end_byte > tab.text.len()
                || tab.text.get(*start_byte..end_byte) != Some(expected_text.as_str())
            {
                false
            } else {
                tab.encoded_len = tab
                    .encoded_len
                    .saturating_sub(encoded_fragment_size(
                        expected_text,
                        tab.encoding,
                        tab.line_ending,
                    ))
                    .saturating_add(encoded_fragment_size(
                        alternate_text,
                        tab.encoding,
                        tab.line_ending,
                    ));
                let active_page_before = tab.page_start_byte;
                tab.text
                    .replace_range(*start_byte..end_byte, alternate_text);
                rebase_page_offsets(
                    tab,
                    *start_byte,
                    expected_text.len(),
                    alternate_text.len(),
                    active_page_before,
                );
                std::mem::swap(alternate_text, expected_text);
                alternate_position.swap_with(tab);
                true
            }
        }
        UndoRecord::DocumentSwap {
            alternate_text,
            alternate_page_history,
            expected_hash,
            expected_len,
            alternate_position,
            alternate_encoded_len,
        } => {
            if *expected_len != tab.text.len() || *expected_hash != text_hash(&tab.text) {
                false
            } else {
                std::mem::swap(&mut tab.text, alternate_text);
                std::mem::swap(&mut tab.page_history, alternate_page_history);
                std::mem::swap(&mut tab.encoded_len, alternate_encoded_len);
                alternate_position.swap_with(tab);
                *expected_len = tab.text.len();
                *expected_hash = text_hash(&tab.text);
                true
            }
        }
    };
    if !applied {
        if redo {
            tab.redo_stack.push(record);
        } else {
            tab.undo_stack.push(record);
        }
        return false;
    }
    tab.folded_lines.clear();
    refresh_page_triple_string_context(tab);
    tab.dirty = text_hash(&tab.text) != tab.saved_hash;
    tab.last_edit = SystemTime::now();
    if redo {
        push_history(&mut tab.undo_stack, record, destination_budget);
    } else {
        push_history(&mut tab.redo_stack, record, destination_budget);
    }
    true
}

fn text_hash(text: &str) -> u64 {
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    text.hash(&mut hasher);
    hasher.finish()
}

fn bytes_hash(bytes: &[u8]) -> u64 {
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    bytes.hash(&mut hasher);
    hasher.finish()
}

fn unfold_selection(
    folded_lines: &mut Vec<usize>,
    foldable: &[(usize, usize)],
    start: usize,
    end: usize,
) {
    folded_lines.retain(|header| {
        !foldable.iter().any(|(fold_header, fold_end)| {
            fold_header == header && start <= *fold_end && end > *header
        })
    });
}

pub fn editor_has_focus(ctx: &egui::Context, tab: &EditorTab) -> bool {
    let page_id = if tab.text.len() > LARGE_DOCUMENT_THRESHOLD_BYTES {
        tab.page_start_byte
    } else {
        0
    };
    let id = Id::new(("rbl-source-editor", tab.id, page_id, false));
    ctx.memory(|memory| memory.has_focus(id))
}

pub(crate) fn read_text_file(path: &Path) -> anyhow::Result<(String, TextEncoding)> {
    let (text, encoding, _) = read_text_file_with_line_ending(path)?;
    Ok((text, encoding))
}

fn read_text_file_with_line_ending(
    path: &Path,
) -> anyhow::Result<(String, TextEncoding, LineEnding)> {
    let metadata = fs::metadata(path)?;
    if metadata.len() > MAX_EDITABLE_FILE_BYTES {
        anyhow::bail!(
            "file is {} MiB; the editor limit is {} MiB to keep memory bounded",
            metadata.len().div_ceil(1024 * 1024),
            MAX_EDITABLE_FILE_BYTES / (1024 * 1024)
        );
    }
    let mut bytes = Vec::with_capacity(metadata.len() as usize);
    fs::File::open(path)?
        .take(MAX_EDITABLE_FILE_BYTES + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() as u64 > MAX_EDITABLE_FILE_BYTES {
        anyhow::bail!(
            "file grew beyond the {} MiB editor limit while opening",
            MAX_EDITABLE_FILE_BYTES / (1024 * 1024)
        );
    }
    let (text, encoding) = decode_text_owned(bytes)?;
    let mut line_ending = detect_line_ending(&text);
    let text = normalize_line_endings_owned(text);
    let decoded_limit = decoded_text_limit(encoding);
    if text.len() as u64 > decoded_limit {
        anyhow::bail!(
            "decoded text exceeds the {} MiB editing limit for {:?}",
            decoded_limit / (1024 * 1024),
            encoding
        );
    }
    if encoded_text_size(&text, encoding, line_ending) > MAX_EDITABLE_FILE_BYTES as usize {
        line_ending = LineEnding::Lf;
    }
    if encoded_text_size(&text, encoding, line_ending) > MAX_EDITABLE_FILE_BYTES as usize {
        anyhow::bail!(
            "normalized text exceeds the {} MiB encoded-file limit",
            MAX_EDITABLE_FILE_BYTES / (1024 * 1024)
        );
    }
    Ok((text, encoding, line_ending))
}

pub fn decoded_text_limit(encoding: TextEncoding) -> u64 {
    match encoding {
        TextEncoding::Utf16Le | TextEncoding::Utf16Be => MAX_EDITABLE_FILE_BYTES,
        TextEncoding::Utf8 => MAX_EDITABLE_FILE_BYTES,
        TextEncoding::Utf8Bom => MAX_EDITABLE_FILE_BYTES - 3,
    }
}

fn encoded_fragment_size(text: &str, encoding: TextEncoding, line_ending: LineEnding) -> usize {
    match encoding {
        TextEncoding::Utf8 | TextEncoding::Utf8Bom => {
            text.len()
                + if line_ending == LineEnding::CrLf {
                    text.bytes().filter(|byte| *byte == b'\n').count()
                } else {
                    0
                }
        }
        TextEncoding::Utf16Le | TextEncoding::Utf16Be => {
            (text.encode_utf16().count()
                + if line_ending == LineEnding::CrLf {
                    text.bytes().filter(|byte| *byte == b'\n').count()
                } else {
                    0
                })
                * 2
        }
    }
}

fn encoded_text_size(text: &str, encoding: TextEncoding, line_ending: LineEnding) -> usize {
    encoded_fragment_size(text, encoding, line_ending)
        + match encoding {
            TextEncoding::Utf8 => 0,
            TextEncoding::Utf8Bom => 3,
            TextEncoding::Utf16Le | TextEncoding::Utf16Be => 2,
        }
}

fn encoded_char_size(ch: char, encoding: TextEncoding, line_ending: LineEnding) -> usize {
    match encoding {
        TextEncoding::Utf8 | TextEncoding::Utf8Bom => {
            ch.len_utf8() + usize::from(ch == '\n' && line_ending == LineEnding::CrLf)
        }
        TextEncoding::Utf16Le | TextEncoding::Utf16Be => {
            (ch.len_utf16() + usize::from(ch == '\n' && line_ending == LineEnding::CrLf)) * 2
        }
    }
}

fn truncate_to_limits(
    text: &str,
    max_text_bytes: usize,
    max_encoded_bytes: usize,
    encoding: TextEncoding,
    line_ending: LineEnding,
) -> &str {
    let mut accepted_bytes = 0usize;
    let mut accepted_encoded = 0usize;
    for (byte_index, ch) in text.char_indices() {
        let next_text = accepted_bytes.saturating_add(ch.len_utf8());
        let next_encoded =
            accepted_encoded.saturating_add(encoded_char_size(ch, encoding, line_ending));
        if next_text > max_text_bytes || next_encoded > max_encoded_bytes {
            break;
        }
        accepted_bytes = byte_index + ch.len_utf8();
        accepted_encoded = next_encoded;
    }
    &text[..accepted_bytes]
}

fn decode_text_owned(mut bytes: Vec<u8>) -> anyhow::Result<(String, TextEncoding)> {
    let (text, encoding) = if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
        bytes.drain(..3);
        (String::from_utf8(bytes)?, TextEncoding::Utf8Bom)
    } else if bytes.starts_with(&[0xFF, 0xFE]) {
        (decode_utf16(&bytes[2..], true)?, TextEncoding::Utf16Le)
    } else if bytes.starts_with(&[0xFE, 0xFF]) {
        (decode_utf16(&bytes[2..], false)?, TextEncoding::Utf16Be)
    } else {
        (String::from_utf8(bytes)?, TextEncoding::Utf8)
    };
    validate_decoded_text(text, encoding)
}

fn validate_decoded_text(
    text: String,
    encoding: TextEncoding,
) -> anyhow::Result<(String, TextEncoding)> {
    if text
        .chars()
        .any(|ch| ch == '\0' || (ch.is_control() && !matches!(ch, '\n' | '\r' | '\t' | '\u{000C}')))
    {
        anyhow::bail!("file contains binary/control data and cannot be opened as text");
    }
    Ok((text, encoding))
}

fn decode_utf16(bytes: &[u8], little_endian: bool) -> anyhow::Result<String> {
    if bytes.len() % 2 != 0 {
        anyhow::bail!("UTF-16 file has an incomplete 16-bit code unit");
    }
    let units = bytes.chunks_exact(2).map(|pair| {
        if little_endian {
            u16::from_le_bytes([pair[0], pair[1]])
        } else {
            u16::from_be_bytes([pair[0], pair[1]])
        }
    });
    let mut text = String::with_capacity(bytes.len());
    for decoded in char::decode_utf16(units) {
        text.push(decoded.map_err(|_| anyhow::anyhow!("invalid UTF-16 surrogate sequence"))?);
    }
    Ok(text)
}

fn detect_line_ending(text: &str) -> LineEnding {
    let bytes = text.as_bytes();
    for (index, byte) in bytes.iter().enumerate() {
        if *byte == b'\r' {
            return if bytes.get(index + 1) == Some(&b'\n') {
                LineEnding::CrLf
            } else {
                LineEnding::Cr
            };
        }
        if *byte == b'\n' {
            return LineEnding::Lf;
        }
    }
    LineEnding::Lf
}

fn normalize_line_endings_owned(mut text: String) -> String {
    if !text.as_bytes().contains(&b'\r') {
        return text;
    }
    // SAFETY: CR and LF are single-byte ASCII values and cannot occur inside a
    // UTF-8 multibyte sequence. The compaction below only rewrites CR to LF,
    // removes the LF in CRLF pairs, and copies every other byte unchanged.
    // The resulting byte sequence therefore remains valid UTF-8.
    let bytes = unsafe { text.as_mut_vec() };
    let mut read = 0usize;
    let mut write = 0usize;
    while read < bytes.len() {
        let byte = bytes[read];
        if byte == b'\r' {
            bytes[write] = b'\n';
            write += 1;
            read += 1;
            if bytes.get(read) == Some(&b'\n') {
                read += 1;
            }
        } else {
            bytes[write] = byte;
            write += 1;
            read += 1;
        }
    }
    bytes.truncate(write);
    text
}

fn encode_text(
    text: &str,
    encoding: TextEncoding,
    line_ending: LineEnding,
) -> anyhow::Result<Vec<u8>> {
    let converted;
    let text = match line_ending {
        LineEnding::Lf => text,
        LineEnding::CrLf => {
            converted = text.replace('\n', "\r\n");
            &converted
        }
        LineEnding::Cr => {
            converted = text.replace('\n', "\r");
            &converted
        }
    };
    let mut bytes = Vec::with_capacity(text.len() + 3);
    match encoding {
        TextEncoding::Utf8 => bytes.extend_from_slice(text.as_bytes()),
        TextEncoding::Utf8Bom => {
            bytes.extend_from_slice(&[0xEF, 0xBB, 0xBF]);
            bytes.extend_from_slice(text.as_bytes());
        }
        TextEncoding::Utf16Le | TextEncoding::Utf16Be => {
            bytes.extend_from_slice(if encoding == TextEncoding::Utf16Le {
                &[0xFF, 0xFE]
            } else {
                &[0xFE, 0xFF]
            });
            for unit in text.encode_utf16() {
                let encoded = if encoding == TextEncoding::Utf16Le {
                    unit.to_le_bytes()
                } else {
                    unit.to_be_bytes()
                };
                bytes.extend_from_slice(&encoded);
            }
        }
    }
    Ok(bytes)
}

pub fn show_editor(
    ui: &mut egui::Ui,
    tab: &mut EditorTab,
    options: EditorViewOptions,
    selection: Option<(usize, usize)>,
) -> bool {
    let selection = resolve_editor_selection(tab, selection);
    let EditorViewOptions {
        font_size,
        tab_size,
        line_numbers,
        syntax_highlighting,
        soft_wrap,
        indent_guides,
        document_limit_bytes,
        undo_budget_bytes,
    } = options;
    if let Some((selection_start, selection_end)) = selection {
        let start_byte = char_to_byte(&tab.text, selection_start);
        let end_byte = char_to_byte(&tab.text, selection_end);
        let (page_start, page_end) = page_byte_range(tab);
        if start_byte < page_start || end_byte > page_end {
            set_page_for_range(tab, start_byte, end_byte);
        }
    }
    let paged = tab.text.len() > LARGE_DOCUMENT_THRESHOLD_BYTES;
    let (page_start_byte, page_end_byte) = page_byte_range(tab);
    let page_start_char = if paged { tab.page_start_char } else { 0 };
    let page_start_line = if paged { tab.page_start_line } else { 1 };
    let page_start_column = if paged { tab.page_start_column } else { 1 };
    let mut page_text = tab.text[page_start_byte..page_end_byte].to_owned();
    let gutter_width = if line_numbers {
        (font_size * 3.6).max(50.0)
    } else if tab.is_rbl() {
        18.0
    } else {
        0.0
    };
    let position_before = EditorPosition::capture(tab);
    let page_id = if paged { page_start_byte } else { 0 };
    tab.limit_reached = false;
    let mut editor_rect = ui.max_rect();
    editor_rect.min.x += gutter_width;
    let pointer_pressed_in_editor = ui.input(|input| {
        input.pointer.button_pressed(egui::PointerButton::Primary)
            && input
                .pointer
                .interact_pos()
                .is_some_and(|position| editor_rect.contains(position))
    });
    let page_end_is_line_boundary = page_end_byte == tab.text.len()
        || tab.text.as_bytes().get(page_end_byte.saturating_sub(1)) == Some(&b'\n');
    let page_foldable = if tab.is_rbl() {
        foldable_ranges(
            &page_text,
            page_start_line,
            page_start_column == 1,
            page_end_is_line_boundary,
            tab.page_inside_triple_string,
        )
    } else {
        Vec::new()
    };
    if let Some((selection_start, selection_end)) = selection {
        let start_byte = char_to_byte(&tab.text, selection_start);
        let end_byte = char_to_byte(&tab.text, selection_end);
        let start_line = page_start_line
            + tab.text[page_start_byte..start_byte.min(page_end_byte)]
                .bytes()
                .filter(|byte| *byte == b'\n')
                .count();
        let end_line = page_start_line
            + tab.text[page_start_byte..end_byte.min(page_end_byte)]
                .bytes()
                .filter(|byte| *byte == b'\n')
                .count();
        unfold_selection(&mut tab.folded_lines, &page_foldable, start_line, end_line);
    }
    let folded = page_foldable
        .iter()
        .any(|(header, end)| *end > header + 1 && tab.folded_lines.binary_search(header).is_ok());
    let id = Id::new(("rbl-source-editor", tab.id, page_id, folded));
    // TextEdit acquires focus later in this frame. Include a pointer press in its
    // viewport so same-frame paste/text still passes through our size and undo guards.
    let focused =
        !folded && (ui.memory(|memory| memory.has_focus(id)) || pointer_pressed_in_editor);
    let mut outdented = false;
    let mut pasted_text: Option<String> = None;
    let mut paste_applied = false;
    let mut local_cursor = tab.cursor_char.saturating_sub(page_start_char);
    if focused {
        let cursor_byte = char_to_byte(&page_text, local_cursor);
        let line_start = page_text[..cursor_byte]
            .rfind('\n')
            .map_or(0, |index| index + 1);
        let before_cursor = &page_text[line_start..cursor_byte];
        let current_indent: String = before_cursor
            .chars()
            .take_while(|ch| ch.is_whitespace())
            .collect();
        let indent = if before_cursor.trim_end().ends_with('(') {
            format!("{current_indent}{}", " ".repeat(tab_size))
        } else {
            current_indent
        };
        let selected_range = selection
            .map(|(start, end)| {
                (
                    start.saturating_sub(page_start_char),
                    end.saturating_sub(page_start_char),
                )
            })
            .or_else(|| {
                egui::text_edit::TextEditState::load(ui.ctx(), id)
                    .and_then(|state| state.cursor.char_range())
                    .map(|range| {
                        (
                            range.primary.index.min(range.secondary.index),
                            range.primary.index.max(range.secondary.index),
                        )
                    })
            });
        let selected_bytes = selected_range
            .map(|(start, end)| {
                let start = start.min(page_text.chars().count());
                let end = end.min(page_text.chars().count());
                char_to_byte(&page_text, end) - char_to_byte(&page_text, start)
            })
            .unwrap_or(0);
        let selected_encoded_bytes = selected_range
            .map(|(start, end)| {
                let char_count = page_text.chars().count();
                let start = start.min(char_count);
                let end = end.min(char_count);
                let start_byte = char_to_byte(&page_text, start);
                let end_byte = char_to_byte(&page_text, end);
                encoded_fragment_size(
                    &page_text[start_byte..end_byte],
                    tab.encoding,
                    tab.line_ending,
                )
            })
            .unwrap_or(0);
        ui.input_mut(|input| {
            let mut events = Vec::with_capacity(input.events.len());
            let mut shift_tab = false;
            let mut added_text_bytes = 0usize;
            let mut added_encoded_bytes = 0usize;
            let mut selection_credit = selected_bytes;
            let mut selection_encoded_credit = selected_encoded_bytes;
            for event in input.events.drain(..) {
                match event {
                    egui::Event::Key {
                        key: Key::Tab,
                        pressed: true,
                        modifiers,
                        ..
                    } if !modifiers.shift
                        && !modifiers.ctrl
                        && !modifiers.command
                        && !modifiers.alt =>
                    {
                        let remaining = document_limit_bytes
                            .saturating_sub(tab.text.len())
                            .saturating_add(selection_credit)
                            .saturating_sub(added_text_bytes);
                        let spaces = " ".repeat(tab_size);
                        let encoded_remaining = (MAX_EDITABLE_FILE_BYTES as usize)
                            .saturating_sub(tab.encoded_len)
                            .saturating_add(selection_encoded_credit)
                            .saturating_sub(added_encoded_bytes);
                        let accepted = truncate_to_limits(
                            &spaces,
                            remaining,
                            encoded_remaining,
                            tab.encoding,
                            tab.line_ending,
                        );
                        added_text_bytes += accepted.len();
                        added_encoded_bytes +=
                            encoded_fragment_size(accepted, tab.encoding, tab.line_ending);
                        if accepted.len() < spaces.len() {
                            tab.limit_reached = true;
                        }
                        if !accepted.is_empty() {
                            selection_credit = 0;
                            selection_encoded_credit = 0;
                            events.push(egui::Event::Text(accepted.to_owned()));
                        }
                    }
                    egui::Event::Key {
                        key: Key::Tab,
                        pressed: true,
                        modifiers,
                        ..
                    } if modifiers.shift
                        && !modifiers.ctrl
                        && !modifiers.command
                        && !modifiers.alt =>
                    {
                        shift_tab = true;
                    }
                    egui::Event::Text(text) => {
                        let text = normalize_line_endings_owned(text);
                        let remaining = document_limit_bytes
                            .saturating_sub(tab.text.len())
                            .saturating_add(selection_credit)
                            .saturating_sub(added_text_bytes);
                        let encoded_remaining = (MAX_EDITABLE_FILE_BYTES as usize)
                            .saturating_sub(tab.encoded_len)
                            .saturating_add(selection_encoded_credit)
                            .saturating_sub(added_encoded_bytes);
                        let accepted = truncate_to_limits(
                            &text,
                            remaining,
                            encoded_remaining,
                            tab.encoding,
                            tab.line_ending,
                        );
                        added_text_bytes += accepted.len();
                        added_encoded_bytes +=
                            encoded_fragment_size(accepted, tab.encoding, tab.line_ending);
                        if accepted.len() < text.len() {
                            tab.limit_reached = true;
                        }
                        if !accepted.is_empty() {
                            selection_credit = 0;
                            selection_encoded_credit = 0;
                            events.push(egui::Event::Text(accepted.to_owned()));
                        }
                    }
                    egui::Event::Paste(text) => {
                        let text = normalize_line_endings_owned(text);
                        if let Some(existing) = &mut pasted_text {
                            existing.push_str(&text);
                        } else {
                            pasted_text = Some(text);
                        }
                        selection_credit = 0;
                    }
                    egui::Event::Key {
                        key: Key::Enter,
                        pressed: true,
                        physical_key,
                        repeat,
                        modifiers,
                    } if !modifiers.ctrl && !modifiers.command && !modifiers.alt => {
                        let remaining = document_limit_bytes
                            .saturating_sub(tab.text.len())
                            .saturating_add(selection_credit)
                            .saturating_sub(added_text_bytes);
                        let encoded_remaining = (MAX_EDITABLE_FILE_BYTES as usize)
                            .saturating_sub(tab.encoded_len)
                            .saturating_add(selection_encoded_credit)
                            .saturating_sub(added_encoded_bytes);
                        let newline_fits = truncate_to_limits(
                            "\n",
                            remaining,
                            encoded_remaining,
                            tab.encoding,
                            tab.line_ending,
                        )
                        .len()
                            == 1;
                        if !newline_fits {
                            tab.limit_reached = true;
                        } else {
                            let indent_remaining = remaining.saturating_sub(1);
                            let indent_encoded_remaining = encoded_remaining.saturating_sub(
                                encoded_fragment_size("\n", tab.encoding, tab.line_ending),
                            );
                            let accepted_indent = truncate_to_limits(
                                &indent,
                                indent_remaining,
                                indent_encoded_remaining,
                                tab.encoding,
                                tab.line_ending,
                            );
                            if accepted_indent.len() < indent.len() {
                                tab.limit_reached = true;
                            }
                            append_enter_events(
                                &mut events,
                                physical_key,
                                repeat,
                                modifiers,
                                accepted_indent,
                            );
                            added_text_bytes += 1 + accepted_indent.len();
                            added_encoded_bytes += encoded_fragment_size(
                                &format!("\n{accepted_indent}"),
                                tab.encoding,
                                tab.line_ending,
                            );
                            selection_credit = 0;
                            selection_encoded_credit = 0;
                        }
                    }
                    other => events.push(other),
                }
            }
            input.events = events;
            if shift_tab {
                let removed = outdent_text_line(&mut page_text, local_cursor, tab_size);
                outdented = removed > 0;
                local_cursor = local_cursor.saturating_sub(removed);
            }
        });
    }
    let mut layouter = |ui: &egui::Ui, text: &str, wrap_width: f32| {
        let job = highlight(text, font_size, wrap_width, ui.visuals().text_color());
        ui.fonts(|fonts| fonts.layout_job(job))
    };
    let viewport_width = (ui.available_width() - gutter_width).max(1.0);
    let viewport_height = ui.available_height().max(1.0);
    let viewport_size = egui::vec2(viewport_width, viewport_height);
    let projection = if folded {
        project_folded_text(
            &page_text,
            page_start_line,
            &tab.folded_lines,
            page_foldable,
        )
    } else {
        FoldProjection {
            text: String::new(),
            visible_lines: (0..page_text.split('\n').count())
                .map(|line| page_start_line + line)
                .collect(),
            foldable_ranges: page_foldable,
        }
    };
    let structural_guide_segments = indent_guides.then(|| {
        tab.is_rbl().then(|| {
            block_guide_segments(
                &page_text,
                page_start_line,
                &projection.visible_lines,
                &projection.foldable_ranges,
                tab_size,
            )
        })
    });
    let mut display_text = if folded { projection.text } else { page_text };
    let indent_guide_lines = indent_guides.then(|| {
        display_text
            .split('\n')
            .map(|line| (indentation_columns(line, tab_size), line.trim().is_empty()))
            .collect::<Vec<_>>()
    });
    let mut edit = TextEdit::multiline(&mut display_text)
        .id(id)
        .font(FontId::monospace(font_size))
        .desired_width(if soft_wrap {
            viewport_width
        } else {
            f32::INFINITY
        })
        .desired_rows(1)
        .min_size(viewport_size)
        .interactive(!folded)
        .code_editor();
    if syntax_highlighting {
        edit = edit.layouter(&mut layouter);
    }
    let mut fold_toggle = None;
    let mut output = if gutter_width > 0.0 {
        ui.horizontal_top(|ui| {
            let (gutter, _) =
                ui.allocate_exact_size(egui::vec2(gutter_width, viewport_height), Sense::hover());
            let output = edit.show(ui);
            let gutter_rect = egui::Rect::from_min_size(
                gutter.min,
                egui::vec2(gutter_width, output.galley.size().y.max(viewport_height)),
            );
            let painter = ui.painter();
            painter.rect_filled(gutter_rect, 0.0, ui.visuals().code_bg_color);
            painter.vline(
                gutter_rect.right(),
                gutter_rect.y_range(),
                ui.visuals().widgets.noninteractive.bg_stroke,
            );
            let number_color = ui.visuals().weak_text_color();
            let mut visible_line = 0usize;
            let mut first_visual_row = true;
            for row in &output.galley.rows {
                let y = output.galley_pos.y + row.rect.center().y;
                if first_visual_row && y >= gutter_rect.top() && y <= gutter_rect.bottom() {
                    let line_number = projection
                        .visible_lines
                        .get(visible_line)
                        .copied()
                        .unwrap_or(page_start_line + visible_line);
                    if line_numbers {
                        painter.text(
                            egui::pos2(gutter_rect.right() - 5.0, y),
                            Align2::RIGHT_CENTER,
                            line_number.to_string(),
                            FontId::monospace(font_size * 0.9),
                            number_color,
                        );
                    }
                    if projection
                        .foldable_ranges
                        .binary_search_by_key(&line_number, |(header, _)| *header)
                        .is_ok()
                    {
                        let fold_rect = egui::Rect::from_center_size(
                            egui::pos2(gutter_rect.left() + 8.0, y),
                            egui::vec2(14.0, row.rect.height().max(12.0)),
                        );
                        let response = ui.interact(
                            fold_rect,
                            Id::new(("rbl-fold-line", tab.id, line_number)),
                            Sense::click(),
                        );
                        painter.text(
                            fold_rect.center(),
                            Align2::CENTER_CENTER,
                            if tab.folded_lines.binary_search(&line_number).is_ok() {
                                "▸"
                            } else {
                                "▾"
                            },
                            FontId::monospace(font_size * 0.85),
                            number_color,
                        );
                        if response.clicked() {
                            fold_toggle = Some(line_number);
                        }
                    }
                }
                if row.ends_with_newline {
                    visible_line += 1;
                    first_visual_row = true;
                } else {
                    first_visual_row = false;
                }
            }
            if indent_guides {
                paint_indent_guides(
                    ui,
                    &output.galley,
                    output.galley_pos,
                    indent_guide_lines.as_deref().unwrap_or_default(),
                    structural_guide_segments
                        .as_ref()
                        .and_then(Option::as_deref),
                    tab_size,
                    font_size,
                );
            }
            output
        })
        .inner
    } else {
        edit.show(ui)
    };
    if let Some(line_number) = fold_toggle {
        if toggle_fold_line(&mut tab.folded_lines, line_number) {
            ui.ctx().request_repaint();
        }
    }
    let mut page_edit_applied = !folded && (output.response.changed() || outdented);
    if page_edit_applied {
        let before_page = tab.text[page_start_byte..page_end_byte].to_owned();
        let old_newlines = tab.text[page_start_byte..page_end_byte]
            .bytes()
            .filter(|byte| *byte == b'\n')
            .count();
        let new_newlines = display_text.bytes().filter(|byte| *byte == b'\n').count();
        tab.text
            .replace_range(page_start_byte..page_end_byte, &display_text);
        if memory_bytes(tab) > undo_budget_bytes {
            tab.text.replace_range(
                page_start_byte..page_start_byte + display_text.len(),
                &before_page,
            );
            tab.text.shrink_to_fit();
            restore_editor_position(tab, &position_before);
            let local_cursor = position_before
                .cursor_char
                .saturating_sub(page_start_char)
                .min(before_page.chars().count());
            output
                .state
                .cursor
                .set_char_range(Some(egui::text::CCursorRange::two(
                    egui::text::CCursor::new(local_cursor),
                    egui::text::CCursor::new(local_cursor),
                )));
            page_edit_applied = false;
            tab.limit_reached = true;
        } else {
            tab.line_count = tab.line_count.saturating_sub(old_newlines) + new_newlines;
            tab.last_edit = SystemTime::now();
            record_page_edit(
                tab,
                page_start_byte,
                &before_page,
                &display_text,
                position_before,
                undo_budget_bytes,
            );
        }
    }
    if let Some((start, end)) = selection {
        let local_start = start.saturating_sub(page_start_char);
        let local_end = end.saturating_sub(page_start_char);
        output
            .state
            .cursor
            .set_char_range(Some(egui::text::CCursorRange::two(
                egui::text::CCursor::new(local_start.min(display_text.chars().count())),
                egui::text::CCursor::new(local_end.min(display_text.chars().count())),
            )));
        output.response.request_focus();
        tab.cursor_char = page_start_char + local_end.min(display_text.chars().count());
        update_cursor_metrics(
            tab,
            &display_text,
            local_end.min(display_text.chars().count()),
            page_start_line,
            page_start_column,
        );
        ui.ctx().request_repaint();
    } else if outdented {
        let local_cursor = local_cursor.min(display_text.chars().count());
        output
            .state
            .cursor
            .set_char_range(Some(egui::text::CCursorRange::two(
                egui::text::CCursor::new(local_cursor),
                egui::text::CCursor::new(local_cursor),
            )));
        output.response.request_focus();
        tab.cursor_char = page_start_char + local_cursor;
        update_cursor_metrics(
            tab,
            &display_text,
            local_cursor,
            page_start_line,
            page_start_column,
        );
    } else if !folded {
        if let Some(cursor) = output.cursor_range {
            local_cursor = cursor.primary.ccursor.index;
            tab.cursor_char = page_start_char + local_cursor;
            update_cursor_metrics(
                tab,
                &display_text,
                local_cursor,
                page_start_line,
                page_start_column,
            );
        }
    }
    if let Some(paste) = pasted_text {
        let paste_range = output
            .state
            .cursor
            .char_range()
            .map(|range| {
                (
                    page_start_char + range.primary.index.min(range.secondary.index),
                    page_start_char + range.primary.index.max(range.secondary.index),
                )
            })
            .unwrap_or((tab.cursor_char, tab.cursor_char));
        paste_applied = insert_paste(
            tab,
            paste,
            paste_range.0,
            paste_range.1,
            document_limit_bytes,
            undo_budget_bytes,
        );
    }
    output.state.clear_undoer();
    output.state.store(ui.ctx(), id);
    page_edit_applied || paste_applied
}

pub fn push_page_history(tab: &mut EditorTab, page_start: usize) {
    if tab.page_history.last().copied() == Some(page_start) {
        return;
    }
    if tab.page_history.len() >= MAX_PAGE_HISTORY_ENTRIES {
        tab.page_history.remove(0);
    }
    tab.page_history.push(page_start);
}

fn clone_page_history(history: &[usize]) -> Vec<usize> {
    let mut cloned = Vec::with_capacity(MAX_PAGE_HISTORY_ENTRIES);
    cloned.extend_from_slice(history);
    cloned
}

pub fn set_page_start(tab: &mut EditorTab, byte: usize) {
    let mut start = byte.min(tab.text.len());
    if start == tab.text.len() && start > 0 {
        start -= 1;
    }
    while !tab.text.is_char_boundary(start) {
        start -= 1;
    }
    let page_changed = start != tab.page_start_byte;
    if page_changed {
        tab.folded_lines.clear();
    }
    tab.page_start_byte = start;
    let prefix = &tab.text[..start];
    tab.page_start_char = prefix.chars().count();
    tab.page_start_line = prefix.bytes().filter(|byte| *byte == b'\n').count() + 1;
    tab.page_start_column = prefix
        .rsplit('\n')
        .next()
        .unwrap_or_default()
        .chars()
        .count()
        + 1;
    if page_changed {
        tab.page_inside_triple_string = triple_string_state_at(prefix);
    }
}

fn refresh_page_triple_string_context(tab: &mut EditorTab) {
    let start = tab.page_start_byte.min(tab.text.len());
    tab.page_inside_triple_string = triple_string_state_at(&tab.text[..start]);
}

pub fn set_page_for_range(tab: &mut EditorTab, start_byte: usize, end_byte: usize) {
    let (current_start, current_end) = page_byte_range(tab);
    if start_byte >= current_start && end_byte <= current_end {
        return;
    }
    let range_len = end_byte.saturating_sub(start_byte);
    let desired_start = if range_len <= EDITOR_PAGE_BYTES / 2 {
        start_byte.saturating_sub(EDITOR_PAGE_BYTES / 2)
    } else {
        (start_byte / EDITOR_PAGE_BYTES) * EDITOR_PAGE_BYTES
    };
    if desired_start != tab.page_start_byte {
        push_page_history(tab, tab.page_start_byte);
    }
    set_page_start(tab, desired_start);
}

pub fn page_byte_range(tab: &EditorTab) -> (usize, usize) {
    if tab.text.len() <= LARGE_DOCUMENT_THRESHOLD_BYTES {
        return (0, tab.text.len());
    }
    let start = tab.page_start_byte.min(tab.text.len());
    let mut end = start.saturating_add(EDITOR_PAGE_BYTES).min(tab.text.len());
    while !tab.text.is_char_boundary(end) {
        end += 1;
    }
    (start, end)
}

pub fn refresh_document_metrics(tab: &mut EditorTab) {
    tab.line_count = tab.text.bytes().filter(|byte| *byte == b'\n').count() + 1;
    tab.cursor_char = tab.cursor_char.min(tab.text.chars().count());
    if tab.text.len() <= LARGE_DOCUMENT_THRESHOLD_BYTES {
        tab.page_history.clear();
        set_page_start(tab, 0);
    } else if tab.page_start_byte > tab.text.len() {
        set_page_start(tab, tab.text.len());
    } else {
        set_page_start(tab, tab.page_start_byte);
    }
    let cursor_byte = char_to_byte(&tab.text, tab.cursor_char);
    let prefix = &tab.text[..cursor_byte];
    tab.cursor_line = prefix.bytes().filter(|byte| *byte == b'\n').count() + 1;
    tab.cursor_column = prefix
        .rsplit('\n')
        .next()
        .unwrap_or_default()
        .chars()
        .count()
        + 1;
}

fn update_cursor_metrics(
    tab: &mut EditorTab,
    page_text: &str,
    local_cursor: usize,
    page_start_line: usize,
    page_start_column: usize,
) {
    let local_byte = char_to_byte(page_text, local_cursor);
    let prefix = &page_text[..local_byte];
    let local_lines = prefix.bytes().filter(|byte| *byte == b'\n').count();
    tab.cursor_line = page_start_line + local_lines;
    tab.cursor_column = if local_lines == 0 {
        page_start_column + prefix.chars().count()
    } else {
        prefix
            .rsplit('\n')
            .next()
            .unwrap_or_default()
            .chars()
            .count()
            + 1
    };
}

fn restore_editor_position(tab: &mut EditorTab, position: &EditorPosition) {
    tab.cursor_char = position.cursor_char;
    tab.page_start_byte = position.page_start_byte;
    refresh_document_metrics(tab);
}

fn outdent_text_line(text: &mut String, cursor_char: usize, tab_size: usize) -> usize {
    let cursor_byte = char_to_byte(text, cursor_char);
    let line_start = text[..cursor_byte].rfind('\n').map_or(0, |index| index + 1);
    let indent_bytes = if text[line_start..].starts_with('\t') {
        1
    } else {
        text[line_start..]
            .bytes()
            .take_while(|byte| *byte == b' ')
            .take(tab_size)
            .count()
    };
    if indent_bytes == 0 {
        return 0;
    }
    text.replace_range(line_start..line_start + indent_bytes, "");
    indent_bytes
}

fn insert_paste(
    tab: &mut EditorTab,
    mut paste: String,
    start_char: usize,
    end_char: usize,
    document_limit_bytes: usize,
    aggregate_budget: usize,
) -> bool {
    let start_char = start_char.min(tab.text.chars().count());
    let end_char = end_char.max(start_char).min(tab.text.chars().count());
    let start_byte = char_to_byte(&tab.text, start_char);
    let end_byte = char_to_byte(&tab.text, end_char);
    let remaining =
        document_limit_bytes.saturating_sub(tab.text.len().saturating_sub(end_byte - start_byte));
    let selected_encoded = encoded_fragment_size(
        &tab.text[start_byte..end_byte],
        tab.encoding,
        tab.line_ending,
    );
    let encoded_remaining = (MAX_EDITABLE_FILE_BYTES as usize)
        .saturating_sub(tab.encoded_len.saturating_sub(selected_encoded));
    let accepted_len = truncate_to_limits(
        &paste,
        remaining,
        encoded_remaining,
        tab.encoding,
        tab.line_ending,
    )
    .len();
    if accepted_len < paste.len() {
        tab.limit_reached = true;
    }
    paste.truncate(accepted_len);
    if paste.is_empty() {
        return false;
    }

    let before_position = EditorPosition::capture(tab);
    let before_fragment = tab.text[start_byte..end_byte].to_owned();
    let before_page_history = clone_page_history(&tab.page_history);
    tab.text.replace_range(start_byte..end_byte, &paste);
    tab.cursor_char = start_char + paste.chars().count();
    refresh_document_metrics(tab);
    if memory_bytes(tab) > aggregate_budget {
        tab.text
            .replace_range(start_byte..start_byte + paste.len(), &before_fragment);
        tab.text.shrink_to_fit();
        tab.page_history = before_page_history;
        restore_editor_position(tab, &before_position);
        refresh_page_triple_string_context(tab);
        tab.limit_reached = true;
        return false;
    }
    refresh_page_triple_string_context(tab);
    tab.last_edit = SystemTime::now();
    record_delta(
        tab,
        start_byte,
        &before_fragment,
        &paste,
        before_position,
        aggregate_budget.saturating_sub(document_base_bytes(tab)),
    );
    let cursor_byte = char_to_byte(&tab.text, tab.cursor_char);
    set_page_for_range(tab, cursor_byte, cursor_byte);
    tab.pending_cursor_selection = Some(tab.cursor_char);
    true
}

pub fn find_match(
    text: &str,
    query: &str,
    from_char: usize,
    backwards: bool,
) -> Option<(usize, usize)> {
    if query.is_empty() {
        return None;
    }
    let byte_from = char_to_byte(text, from_char);
    let found = if backwards {
        let search_end = char_to_byte(text, from_char.saturating_sub(query.chars().count()));
        text[..search_end]
            .rmatch_indices(query)
            .next()
            .or_else(|| text.rmatch_indices(query).next())
    } else {
        text[byte_from..]
            .match_indices(query)
            .next()
            .map(|(offset, matched)| (byte_from + offset, matched))
            .or_else(|| text.match_indices(query).next())
    }?;
    let start = text[..found.0].chars().count();
    Some((start, start + found.1.chars().count()))
}

pub fn replace_all(
    tab: &mut EditorTab,
    query: &str,
    replacement: &str,
    max_bytes: usize,
    aggregate_budget: usize,
) -> usize {
    if query.is_empty() {
        return 0;
    }
    // Keep the editor's internal representation LF-only, just like typing and paste.
    // Otherwise CRLF replacements would be expanded a second time on save.
    let normalized_replacement = normalize_line_endings_owned(replacement.to_owned());
    let replacement = normalized_replacement.as_str();
    if replacement == query {
        return 0;
    }
    let count = tab.text.matches(query).count();
    if count > 0 {
        let new_len = tab
            .text
            .len()
            .saturating_sub(query.len().saturating_mul(count))
            .saturating_add(replacement.len().saturating_mul(count));
        if new_len > max_bytes.min(MAX_EDITABLE_FILE_BYTES as usize) {
            return 0;
        }
        let removed_encoded =
            encoded_fragment_size(query, tab.encoding, tab.line_ending).saturating_mul(count);
        let added_encoded =
            encoded_fragment_size(replacement, tab.encoding, tab.line_ending).saturating_mul(count);
        if tab
            .encoded_len
            .saturating_sub(removed_encoded)
            .saturating_add(added_encoded)
            > MAX_EDITABLE_FILE_BYTES as usize
        {
            return 0;
        }
        let old_capacity = tab.text.capacity();
        let record_overhead = std::mem::size_of::<UndoRecord>();
        let page_history_capacity_bytes = tab
            .page_history
            .capacity()
            .saturating_mul(std::mem::size_of::<usize>());
        let page_history_snapshot_bytes =
            MAX_PAGE_HISTORY_ENTRIES.saturating_mul(std::mem::size_of::<usize>());
        let snapshot_bytes = old_capacity
            .saturating_add(page_history_capacity_bytes)
            .saturating_add(page_history_snapshot_bytes)
            .saturating_add(record_overhead);
        let new_document_bytes = new_len.saturating_add(page_history_snapshot_bytes);
        if snapshot_bytes > aggregate_budget.min(MAX_UNDO_HISTORY_BYTES)
            || snapshot_bytes.saturating_add(new_document_bytes) > aggregate_budget
        {
            return 0;
        }
        let history_budget = aggregate_budget
            .saturating_sub(snapshot_bytes)
            .saturating_sub(new_document_bytes)
            .min(MAX_UNDO_HISTORY_BYTES.saturating_sub(snapshot_bytes));
        tab.redo_stack.clear();
        tab.redo_stack.shrink_to_fit();
        trim_history_to_budget(&mut tab.undo_stack, history_budget);
        tab.folded_lines.clear();
        let before_position = EditorPosition::capture(tab);
        let before_page_history = clone_page_history(&tab.page_history);
        let before_cursor_byte = char_to_byte(&tab.text, tab.cursor_char);
        let before_encoded_len = tab.encoded_len;
        let before_text = std::mem::take(&mut tab.text);
        let mut replaced_text = String::with_capacity(new_len);
        let mut remaining = before_text.as_str();
        while let Some(match_start) = remaining.find(query) {
            replaced_text.push_str(&remaining[..match_start]);
            replaced_text.push_str(replacement);
            remaining = &remaining[match_start + query.len()..];
        }
        replaced_text.push_str(remaining);
        tab.text = replaced_text;
        let mut mapped_page_history = Vec::with_capacity(MAX_PAGE_HISTORY_ENTRIES);
        mapped_page_history.extend(
            before_page_history
                .iter()
                .map(|offset| map_replace_all_offset(&before_text, query, replacement, *offset)),
        );
        tab.page_history = mapped_page_history;
        tab.page_history.dedup();
        if tab.page_history.len() > MAX_PAGE_HISTORY_ENTRIES {
            let excess = tab.page_history.len() - MAX_PAGE_HISTORY_ENTRIES;
            tab.page_history.drain(..excess);
        }
        let mapped_page_start = map_replace_all_offset(
            &before_text,
            query,
            replacement,
            before_position.page_start_byte,
        );
        let mapped_cursor_byte =
            map_replace_all_offset(&before_text, query, replacement, before_cursor_byte);
        set_page_start(tab, mapped_page_start);
        tab.cursor_char = tab.text[..mapped_cursor_byte.min(tab.text.len())]
            .chars()
            .count();
        refresh_document_metrics(tab);
        refresh_page_triple_string_context(tab);
        record_document_swap(
            tab,
            before_text,
            before_page_history,
            before_position,
            before_encoded_len,
            aggregate_budget,
        );
    }
    count
}

fn map_replace_all_offset(text: &str, query: &str, replacement: &str, offset: usize) -> usize {
    if query.is_empty() {
        return offset.min(text.len());
    }
    let mut shift = 0isize;
    for (match_start, matched) in text.match_indices(query) {
        let match_end = match_start + matched.len();
        let mapped_start = match_start.saturating_add_signed(shift);
        if offset < match_start {
            break;
        }
        if offset < match_end {
            return mapped_start;
        }
        if offset == match_end {
            return mapped_start.saturating_add(replacement.len());
        }
        shift += replacement.len() as isize - query.len() as isize;
    }
    offset.saturating_add_signed(shift)
}

pub fn replace_one(
    tab: &mut EditorTab,
    query: &str,
    replacement: &str,
    max_bytes: usize,
    aggregate_budget: usize,
) -> bool {
    let Some((start, end)) = find_match(&tab.text, query, tab.cursor_char, false) else {
        return false;
    };
    replace_range(tab, start, end, replacement, max_bytes, aggregate_budget)
}

pub fn replace_range(
    tab: &mut EditorTab,
    start: usize,
    end: usize,
    replacement: &str,
    max_bytes: usize,
    aggregate_budget: usize,
) -> bool {
    // Public replacement APIs must preserve the same LF-normalized invariant as
    // keyboard input and clipboard paste.
    let normalized_replacement = normalize_line_endings_owned(replacement.to_owned());
    let replacement = normalized_replacement.as_str();
    let char_count = tab.text.chars().count();
    if start > end || end > char_count {
        return false;
    }
    let byte_start = char_to_byte(&tab.text, start);
    let byte_end = char_to_byte(&tab.text, end);
    if tab.text.get(byte_start..byte_end) == Some(replacement) {
        return false;
    }
    let new_len = tab
        .text
        .len()
        .saturating_sub(byte_end - byte_start)
        .saturating_add(replacement.len());
    if new_len > max_bytes.min(MAX_EDITABLE_FILE_BYTES as usize) {
        return false;
    }
    let removed_encoded = encoded_fragment_size(
        &tab.text[byte_start..byte_end],
        tab.encoding,
        tab.line_ending,
    );
    let added_encoded = encoded_fragment_size(replacement, tab.encoding, tab.line_ending);
    if tab
        .encoded_len
        .saturating_sub(removed_encoded)
        .saturating_add(added_encoded)
        > MAX_EDITABLE_FILE_BYTES as usize
    {
        return false;
    }
    tab.folded_lines.clear();
    let before_position = EditorPosition::capture(tab);
    let before_fragment = tab.text[byte_start..byte_end].to_owned();
    let before_page_history = clone_page_history(&tab.page_history);
    tab.text.replace_range(byte_start..byte_end, replacement);
    tab.cursor_char = start + replacement.chars().count();
    refresh_document_metrics(tab);
    if memory_bytes(tab) > aggregate_budget {
        tab.text
            .replace_range(byte_start..byte_start + replacement.len(), &before_fragment);
        tab.text.shrink_to_fit();
        tab.page_history = before_page_history;
        restore_editor_position(tab, &before_position);
        tab.limit_reached = true;
        return false;
    }
    tab.last_edit = SystemTime::now();
    record_delta(
        tab,
        byte_start,
        &before_fragment,
        replacement,
        before_position,
        aggregate_budget.saturating_sub(document_base_bytes(tab)),
    );
    true
}

pub fn line_start_char(text: &str, line: usize) -> usize {
    let target = line.max(1);
    if target == 1 {
        return 0;
    }
    text.match_indices('\n')
        .nth(target - 2)
        .map(|(byte, _)| text[..=byte].chars().count())
        .unwrap_or_else(|| text.chars().count())
}

pub fn char_to_byte(source: &str, char_index: usize) -> usize {
    source
        .char_indices()
        .nth(char_index)
        .map_or(source.len(), |(byte, _)| byte)
}

pub fn cursor_position(tab: &EditorTab) -> (usize, usize) {
    (tab.cursor_line, tab.cursor_column)
}

#[derive(Debug)]
struct FoldProjection {
    text: String,
    visible_lines: Vec<usize>,
    foldable_ranges: Vec<(usize, usize)>,
}

fn indentation_columns(line: &str, tab_size: usize) -> usize {
    let tab_size = tab_size.max(1);
    let mut columns = 0usize;
    for character in line.chars() {
        match character {
            ' ' => columns += 1,
            '\t' => columns += tab_size - columns % tab_size,
            _ => break,
        }
    }
    columns
}

fn code_before_comment(line: &str) -> &str {
    let bytes = line.as_bytes();
    let mut index = 0usize;
    let mut in_string = false;
    let mut triple_string = false;
    let mut escaped = false;
    while index < bytes.len() {
        let byte = bytes[index];
        if in_string {
            if escaped {
                escaped = false;
                index += 1;
            } else if byte == b'\\' {
                escaped = true;
                index += 1;
            } else if triple_string && bytes[index..].starts_with(b"\"\"\"") {
                in_string = false;
                triple_string = false;
                index += 3;
            } else if !triple_string && byte == b'"' {
                in_string = false;
                index += 1;
            } else {
                index += 1;
            }
        } else if byte == b'"' {
            triple_string = bytes[index..].starts_with(b"\"\"\"");
            in_string = true;
            index += if triple_string { 3 } else { 1 };
        } else if byte == b'#' || (byte == b'/' && bytes.get(index + 1) == Some(&b'/')) {
            return &line[..index];
        } else {
            index += 1;
        }
    }
    line
}

fn triple_string_state_at(source: &str) -> bool {
    let bytes = source.as_bytes();
    let mut index = 0usize;
    let mut in_string = false;
    let mut triple_string = false;
    let mut escaped = false;
    let mut in_comment = false;
    while index < bytes.len() {
        let byte = bytes[index];
        if in_comment {
            if byte == b'\n' {
                in_comment = false;
            }
            index += 1;
        } else if in_string {
            if escaped {
                escaped = false;
                index += 1;
            } else if byte == b'\\' {
                escaped = true;
                index += 1;
            } else if triple_string && bytes[index..].starts_with(b"\"\"\"") {
                in_string = false;
                triple_string = false;
                index += 3;
            } else if !triple_string && byte == b'"' {
                in_string = false;
                index += 1;
            } else {
                index += 1;
            }
        } else if byte == b'#' || (byte == b'/' && bytes.get(index + 1) == Some(&b'/')) {
            in_comment = true;
            index += if byte == b'/' { 2 } else { 1 };
        } else if byte == b'"' {
            triple_string = bytes[index..].starts_with(b"\"\"\"");
            in_string = true;
            index += if triple_string { 3 } else { 1 };
        } else {
            index += 1;
        }
    }
    in_string && triple_string
}

fn foldable_ranges(
    source: &str,
    first_line: usize,
    first_line_complete: bool,
    last_line_complete: bool,
    starts_inside_triple_string: bool,
) -> Vec<(usize, usize)> {
    let is_block_header = |line: &str| {
        let line = line.trim_start().trim_end();
        if !line.ends_with('(') {
            return false;
        }
        [
            "func", "if", "elif", "else", "for", "switch", "case", "struct",
        ]
        .iter()
        .any(|keyword| {
            line.strip_prefix(keyword).is_some_and(|rest| {
                rest.is_empty()
                    || rest.starts_with('(')
                    || rest.chars().next().is_some_and(char::is_whitespace)
            })
        })
    };
    let mut block_openers = HashSet::new();
    let mut line_start = 0usize;
    let source_line_count = source.bytes().filter(|byte| *byte == b'\n').count() + 1;
    for (line_index, line) in source.split_inclusive('\n').enumerate() {
        let code = code_before_comment(line.trim_end_matches('\n')).trim_end();
        let is_partial_edge = (line_index == 0 && !first_line_complete)
            || (line_index + 1 == source_line_count && !last_line_complete);
        if !is_partial_edge && is_block_header(code) {
            if let Some(open) = code.rfind('(') {
                block_openers.insert(line_start + open);
            }
        }
        line_start += line.len();
    }

    // Match only grammar-level block parentheses. Strings (including triple
    // quoted strings), f-strings and comments are skipped so their delimiters
    // cannot create phantom fold regions.
    let bytes = source.as_bytes();
    let mut stack: Vec<(usize, bool)> = Vec::new();
    let mut ranges = Vec::new();
    let mut index = 0usize;
    let mut line = 0usize;
    let mut in_string = starts_inside_triple_string;
    let mut triple_string = starts_inside_triple_string;
    let mut escaped = false;
    let mut in_comment = false;
    while index < bytes.len() {
        let byte = bytes[index];
        if in_comment {
            if byte == b'\n' {
                line += 1;
                in_comment = false;
            }
            index += 1;
            continue;
        }
        if in_string {
            if escaped {
                escaped = false;
                if byte == b'\n' {
                    line += 1;
                }
                index += 1;
            } else if byte == b'\\' {
                escaped = true;
                index += 1;
            } else if triple_string && bytes[index..].starts_with(b"\"\"\"") {
                in_string = false;
                triple_string = false;
                index += 3;
            } else if !triple_string && byte == b'"' {
                in_string = false;
                index += 1;
            } else {
                if byte == b'\n' {
                    line += 1;
                }
                index += 1;
            }
            continue;
        }
        if byte == b'#' || (byte == b'/' && bytes.get(index + 1) == Some(&b'/')) {
            in_comment = true;
            index += if byte == b'/' { 2 } else { 1 };
            continue;
        }
        if byte == b'"' {
            triple_string = bytes[index..].starts_with(b"\"\"\"");
            in_string = true;
            index += if triple_string { 3 } else { 1 };
            continue;
        }
        match byte {
            b'(' => stack.push((line, block_openers.contains(&index))),
            b')' => {
                if let Some((header_line, is_block)) = stack.pop() {
                    let close_is_complete = last_line_complete || line + 1 < source_line_count;
                    if is_block && line > header_line + 1 && close_is_complete {
                        ranges.push((first_line + header_line, first_line + line));
                    }
                }
            }
            b'\n' => line += 1,
            _ => {}
        }
        index += 1;
    }
    ranges.sort_unstable_by_key(|(header, _)| *header);
    ranges
}

fn project_folded_text(
    source: &str,
    first_line: usize,
    folded_headers: &[usize],
    foldable_ranges: Vec<(usize, usize)>,
) -> FoldProjection {
    let lines: Vec<&str> = source.split('\n').collect();
    let mut text = String::with_capacity(source.len());
    let mut visible_lines = Vec::with_capacity(lines.len());
    let mut index = 0usize;
    while index < lines.len() {
        let line_number = first_line + index;
        let folded_end = foldable_ranges
            .binary_search_by_key(&line_number, |(header, _)| *header)
            .ok()
            .map(|index| foldable_ranges[index].1)
            .filter(|_| folded_headers.binary_search(&line_number).is_ok());
        if let Some(end_line) = folded_end {
            let hidden_lines = end_line.saturating_sub(line_number + 1);
            text.push_str(lines[index]);
            text.push_str(&format!("  … {hidden_lines} lines"));
            visible_lines.push(line_number);
            if index + 1 < lines.len() {
                text.push('\n');
            }
            index = (end_line - first_line).min(lines.len());
        } else {
            text.push_str(lines[index]);
            visible_lines.push(line_number);
            if index + 1 < lines.len() {
                text.push('\n');
            }
            index += 1;
        }
    }
    FoldProjection {
        text,
        visible_lines,
        foldable_ranges,
    }
}

fn indentation_guide_segments(
    indent_lines: &[(usize, bool)],
    tab_size: usize,
) -> Vec<(usize, usize, usize)> {
    let tab_size = tab_size.max(1);
    let mut segments = Vec::new();
    let mut active_starts = [None; 32];
    for (index, (indent, blank)) in indent_lines.iter().copied().enumerate() {
        if blank {
            continue;
        }
        for (level, active_start) in active_starts.iter_mut().enumerate() {
            let column = (level + 1) * tab_size;
            if indent >= column {
                active_start.get_or_insert(index);
            } else if let Some(start) = active_start.take() {
                segments.push((column, start, index.saturating_sub(1)));
            }
        }
    }
    let last_line = indent_lines.len().saturating_sub(1);
    for (level, active_start) in active_starts.into_iter().enumerate() {
        if let Some(start) = active_start {
            segments.push(((level + 1) * tab_size, start, last_line));
        }
    }
    segments
}

fn block_guide_segments(
    source: &str,
    first_line: usize,
    visible_lines: &[usize],
    block_ranges: &[(usize, usize)],
    tab_size: usize,
) -> Vec<(usize, usize, usize)> {
    let lines: Vec<&str> = source.split('\n').collect();
    let tab_size = tab_size.max(1);
    let mut segments = Vec::with_capacity(block_ranges.len());
    for (header, end) in block_ranges.iter().copied() {
        let (Ok(header_index), Ok(end_index)) = (
            visible_lines.binary_search(&header),
            visible_lines.binary_search(&end),
        ) else {
            continue;
        };
        let Some(header_text) = lines.get(header.saturating_sub(first_line)) else {
            continue;
        };
        let column = indentation_columns(header_text, tab_size).saturating_add(tab_size);
        segments.push((column, header_index, end_index));
    }
    segments.sort_unstable();
    segments
}

fn paint_indent_guides(
    ui: &egui::Ui,
    galley: &egui::Galley,
    galley_pos: egui::Pos2,
    indent_lines: &[(usize, bool)],
    structural_segments: Option<&[(usize, usize, usize)]>,
    tab_size: usize,
    font_size: f32,
) {
    let mut logical_ranges = Vec::with_capacity(indent_lines.len());
    let mut logical_start = None;
    for row in &galley.rows {
        let top = galley_pos.y + row.rect.top();
        let bottom = galley_pos.y + row.rect.bottom();
        let start = logical_start.get_or_insert(top);
        if row.ends_with_newline {
            logical_ranges.push((*start, bottom));
            logical_start = None;
        }
    }
    if let Some(start) = logical_start {
        logical_ranges.push((start, galley_pos.y + galley.size().y));
    }
    let count = indent_lines.len().min(logical_ranges.len());
    if count == 0 {
        return;
    }
    let font = FontId::monospace(font_size);
    let space_width = ui.fonts(|fonts| fonts.glyph_width(&font, ' '));
    let color = ui
        .visuals()
        .widgets
        .noninteractive
        .bg_stroke
        .color
        .gamma_multiply(0.55);
    let painter = ui.painter();
    let segments = structural_segments
        .map(|segments| segments.to_vec())
        .unwrap_or_else(|| indentation_guide_segments(&indent_lines[..count], tab_size));
    for (column, start, end) in segments {
        let Some(start_y) = logical_ranges.get(start).map(|range| range.0) else {
            continue;
        };
        let Some(end_y) = logical_ranges.get(end).map(|range| range.1) else {
            continue;
        };
        let x = galley_pos.x + column as f32 * space_width;
        painter.vline(x, start_y..=end_y, egui::Stroke::new(1.0_f32, color));
    }
}

fn highlight(source: &str, font_size: f32, wrap_width: f32, base: Color32) -> LayoutJob {
    let mut job = LayoutJob::default();
    job.wrap.max_width = wrap_width;
    let mut chars = source.char_indices().peekable();
    let mono = FontId::monospace(font_size);
    while let Some((start, ch)) = chars.next() {
        let next = chars.peek().map(|(_, next)| *next);
        let mut end = start + ch.len_utf8();
        let color = if (ch == '/' && next == Some('/')) || ch == '#' {
            if ch == '/' {
                if let Some((offset, slash)) = chars.next() {
                    end = offset + slash.len_utf8();
                }
            }
            for (offset, next_ch) in chars.by_ref() {
                if next_ch == '\n' {
                    end = offset + next_ch.len_utf8();
                    break;
                }
                end = offset + next_ch.len_utf8();
            }
            Color32::from_rgb(120, 130, 140)
        } else if ch == '"' {
            let triple = source[start..].starts_with("\"\"\"");
            let mut escaped = false;
            if triple {
                for _ in 0..2 {
                    if let Some((offset, quote)) = chars.next() {
                        end = offset + quote.len_utf8();
                    }
                }
                while let Some((offset, c)) = chars.next() {
                    end = offset + c.len_utf8();
                    if escaped {
                        escaped = false;
                    } else if c == '\\' {
                        escaped = true;
                    } else if c == '"' && source[offset..].starts_with("\"\"\"") {
                        for _ in 0..2 {
                            if let Some((quote_offset, quote)) = chars.next() {
                                end = quote_offset + quote.len_utf8();
                            }
                        }
                        break;
                    }
                }
            } else {
                for (offset, c) in chars.by_ref() {
                    end = offset + c.len_utf8();
                    if c == '"' && !escaped {
                        break;
                    }
                    escaped = c == '\\' && !escaped;
                }
            }
            Color32::from_rgb(170, 210, 135)
        } else if ch.is_ascii_digit() {
            while chars
                .peek()
                .is_some_and(|(_, c)| c.is_ascii_alphanumeric() || matches!(c, '.' | '_'))
            {
                let (offset, c) = chars.next().unwrap();
                end = offset + c.len_utf8();
            }
            Color32::from_rgb(120, 185, 240)
        } else if ch.is_alphabetic() || ch == '_' {
            while chars
                .peek()
                .is_some_and(|(_, c)| c.is_alphanumeric() || *c == '_')
            {
                let (offset, c) = chars.next().unwrap();
                end = offset + c.len_utf8();
            }
            let word = &source[start..end];
            let next_non_whitespace = chars
                .clone()
                .find(|(_, c)| !c.is_whitespace())
                .map(|(_, c)| c);
            if matches!(
                word,
                "set"
                    | "let"
                    | "func"
                    | "if"
                    | "elif"
                    | "else"
                    | "for"
                    | "in"
                    | "return"
                    | "break"
                    | "continue"
                    | "true"
                    | "false"
                    | "and"
                    | "or"
                    | "not"
                    | "switch"
                    | "case"
                    | "default"
                    | "struct"
            ) {
                Color32::from_rgb(205, 150, 245)
            } else if next_non_whitespace == Some('(') {
                Color32::from_rgb(245, 205, 130)
            } else {
                base
            }
        } else {
            while chars.peek().is_some_and(|(_, next)| {
                !matches!(*next, '"' | '#' | '/' | '0'..='9' | '_') && !next.is_alphabetic()
            }) {
                let (offset, next) = chars.next().unwrap();
                end = offset + next.len_utf8();
            }
            base
        };
        job.append(
            &source[start..end],
            0.0,
            TextFormat {
                font_id: mono.clone(),
                color,
                ..Default::default()
            },
        );
    }
    job
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn character_offset_is_converted_to_utf8_byte_offset() {
        assert_eq!(char_to_byte("aЖ🙂b", 0), 0);
        assert_eq!(char_to_byte("aЖ🙂b", 1), 1);
        assert_eq!(char_to_byte("aЖ🙂b", 2), 3);
        assert_eq!(char_to_byte("aЖ🙂b", 3), 7);
        assert_eq!(char_to_byte("aЖ🙂b", 99), 8);
    }

    #[test]
    fn structural_folding_ignores_grouping_comments_and_triple_string_content() {
        let source = concat!(
            "func main() (\n",
            "    let tuple = (\n",
            "        1,\n",
            "    )\n",
            "    let text = \"\"\"\n",
            "if (fake) (\n",
            "    ignored\n",
            ")\n",
            "\"\"\"\n",
            "    # if (commented) (\n",
            "    // if (commented) (\n",
            "    if (true) ( // block comment\n",
            "        print(1)\n",
            "    )\n",
            ")\n",
            "print(3)"
        );
        let ranges = foldable_ranges(source, 1, true, true, false);
        let actual_if_line = source
            .lines()
            .position(|line| line.trim_start().starts_with("if (true)"))
            .unwrap()
            + 1;
        assert!(ranges.contains(&(1, 15)));
        assert!(ranges.contains(&(actual_if_line, 14)));
        assert!(!ranges.iter().any(|(header, _)| *header == 2));
        assert!(!ranges.iter().any(|(header, _)| *header == 6));
        assert!(!ranges.iter().any(|(header, _)| *header == 10));

        let folded = project_folded_text(source, 1, &[1], ranges.clone());
        assert_eq!(folded.visible_lines, vec![1, 15, 16]);
        assert!(folded.text.contains("… 13 lines"));
        assert!(folded.text.contains("\n)\nprint(3)"));
    }

    #[test]
    fn selection_unfolds_only_the_collapsed_scope_that_contains_it() {
        let foldable = [(2, 12), (4, 8), (20, 24)];
        let mut folded = vec![2, 4, 20];
        unfold_selection(&mut folded, &foldable, 6, 6);
        assert_eq!(folded, vec![20]);
    }

    #[test]
    fn folding_rejects_partial_page_edges_and_cross_page_blocks() {
        let source = "func main() (\n    print(1)\n)";
        assert!(foldable_ranges(source, 1, false, true, false).is_empty());
        assert!(foldable_ranges(source, 1, true, false, false).is_empty());
    }

    #[test]
    fn folding_carries_triple_string_state_across_large_document_pages() {
        let prefix = "let text = \"\"\"\n";
        assert!(triple_string_state_at(prefix));
        assert!(!triple_string_state_at("let text = \"ordinary string\"\n"));
        let mut tab = EditorTab::untitled_text();
        tab.text =
            format!("{prefix}if (fake) (\n    ignored\n)\n\"\"\"\nfunc after() (\n    print(1)\n)");
        set_page_start(&mut tab, prefix.len());
        assert!(tab.page_inside_triple_string);
        let page = "if (fake) (\n    ignored\n)\n\"\"\"\nfunc after() (\n    print(1)\n)";
        let ranges = foldable_ranges(
            page,
            tab.page_start_line,
            true,
            true,
            tab.page_inside_triple_string,
        );
        assert!(!ranges.iter().any(|(header, _)| *header == 2));
        assert!(ranges.contains(&(6, 8)));
    }

    #[test]
    fn replace_before_large_document_page_refreshes_triple_string_context() {
        let mut tab = EditorTab::untitled();
        tab.text = format!(
            "let text = \"\"\"body\"\"\"{}",
            "x".repeat(EDITOR_PAGE_BYTES + 128)
        );
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        set_page_start(&mut tab, EDITOR_PAGE_BYTES);
        assert!(!tab.page_inside_triple_string);

        let closing = tab.text.rfind("\"\"\"").unwrap();
        let start = tab.text[..closing].chars().count();
        assert!(replace_range(
            &mut tab,
            start,
            start + 3,
            "xxx",
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert_eq!(tab.page_start_byte, EDITOR_PAGE_BYTES);
        assert!(tab.page_inside_triple_string);
    }

    #[test]
    fn fold_state_is_sorted_bounded_and_cleared_on_page_change_and_undo() {
        let mut folded = Vec::new();
        assert!(toggle_fold_line(&mut folded, 12));
        assert!(toggle_fold_line(&mut folded, 4));
        assert!(toggle_fold_line(&mut folded, 8));
        assert_eq!(folded, vec![4, 8, 12]);
        let full = (0..MAX_FOLDED_BLOCKS).collect::<Vec<_>>();
        assert!(!toggle_fold_line(&mut full.clone(), MAX_FOLDED_BLOCKS + 1));

        let mut tab = EditorTab::untitled_text();
        tab.text = "x".repeat(EDITOR_PAGE_BYTES * 2);
        tab.folded_lines = vec![4, 8, 12];
        set_page_start(&mut tab, EDITOR_PAGE_BYTES);
        assert!(tab.folded_lines.is_empty());

        tab.text = "before".into();
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.saved_hash = text_hash(&tab.text);
        tab.folded_lines = vec![1];
        assert!(replace_range(
            &mut tab,
            0,
            6,
            "after",
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert!(tab.folded_lines.is_empty());
        tab.folded_lines = vec![1];
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.folded_lines.is_empty());
    }

    #[test]
    fn indentation_guides_use_tab_stops_not_raw_tab_characters() {
        assert_eq!(indentation_columns("\tprint(1)", 4), 4);
        assert_eq!(indentation_columns(" \tprint(1)", 4), 4);
        assert_eq!(indentation_columns("  print(1)", 4), 2);
        let segments = indentation_guide_segments(
            &[
                (0, false),
                (4, false),
                (8, false),
                (8, false),
                (4, false),
                (0, false),
            ],
            4,
        );
        assert!(segments.contains(&(4, 1, 4)));
        assert!(segments.contains(&(8, 2, 3)));
    }

    #[test]
    fn rbl_block_guides_follow_matched_parentheses_and_nested_scopes() {
        let source = concat!(
            "func main() (\n",
            "    if (true) (\n",
            "        print(1)\n",
            "    )\n",
            ")\n"
        );
        let ranges = foldable_ranges(source, 1, true, true, false);
        let visible = (1..=source.lines().count()).collect::<Vec<_>>();
        let segments = block_guide_segments(source, 1, &visible, &ranges, 4);
        assert!(segments.contains(&(4, 0, 4)));
        assert!(segments.contains(&(8, 1, 3)));
    }

    #[test]
    fn structural_folding_scales_to_thousands_of_blocks() {
        let mut source = String::new();
        for index in 0..1500 {
            source.push_str(&format!("func f{index}() (\n    print({index})\n)\n"));
        }
        let ranges = foldable_ranges(&source, 1, true, true, false);
        assert_eq!(ranges.len(), 1500);
        let folded_headers: Vec<usize> = ranges
            .iter()
            .take(MAX_FOLDED_BLOCKS)
            .map(|(header, _)| *header)
            .collect();
        let projection = project_folded_text(&source, 1, &folded_headers, ranges);
        assert_eq!(projection.visible_lines.len(), 4501 - MAX_FOLDED_BLOCKS);
    }

    #[test]
    fn explicit_selection_discards_stale_pending_cursor_selection() {
        let mut tab = EditorTab::untitled_text();
        tab.pending_cursor_selection = Some(90);
        assert_eq!(
            resolve_editor_selection(&mut tab, Some((4, 4))),
            Some((4, 4))
        );
        assert_eq!(tab.pending_cursor_selection, None);
    }

    #[test]
    fn cursor_position_counts_unicode_characters_and_lines() {
        let mut tab = EditorTab::untitled();
        tab.text = "first\nЖ🙂x".into();
        tab.cursor_char = 8;
        refresh_document_metrics(&mut tab);
        assert_eq!(cursor_position(&tab), (2, 3));
    }

    #[test]
    fn open_and_atomic_save_preserve_unicode_source() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("программа.rbl");
        fs::write(&path, "func main() (\n    print(\"Привет\")\n)\n").unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        assert_eq!(tab.text, "func main() (\n    print(\"Привет\")\n)\n");
        tab.text.push_str("// сохранено\n");
        tab.dirty = true;
        tab.save(None).unwrap();
        assert!(!tab.dirty);
        assert!(fs::read_to_string(path)
            .unwrap()
            .ends_with("// сохранено\n"));
    }

    #[test]
    fn save_refuses_to_overwrite_external_file_changes() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("externally-edited.rbl");
        fs::write(&path, "func main() (\n    print(1)\n)\n").unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        tab.text.push_str("// local edit\n");
        tab.dirty = true;

        fs::write(&path, "external editor version\n").unwrap();
        let external_bytes = fs::read(&path).unwrap();
        let error = tab.save(None).unwrap_err();

        assert!(format!("{error:#}").contains("file changed outside RBL Studio"));
        assert_eq!(fs::read(&path).unwrap(), external_bytes);
        assert!(tab.dirty);
    }

    #[test]
    fn text_documents_preserve_utf8_bom_and_utf16_byte_order() {
        let dir = tempfile::tempdir().unwrap();
        for (name, bytes, encoding) in [
            (
                "utf8-bom.txt",
                [vec![0xEF, 0xBB, 0xBF], "hello Ж🙂\r\n".as_bytes().to_vec()].concat(),
                TextEncoding::Utf8Bom,
            ),
            (
                "utf16-le.txt",
                [
                    vec![0xFF, 0xFE],
                    "hello Ж🙂\r\n"
                        .encode_utf16()
                        .flat_map(u16::to_le_bytes)
                        .collect(),
                ]
                .concat(),
                TextEncoding::Utf16Le,
            ),
            (
                "utf16-be.txt",
                [
                    vec![0xFE, 0xFF],
                    "hello Ж🙂\r\n"
                        .encode_utf16()
                        .flat_map(u16::to_be_bytes)
                        .collect(),
                ]
                .concat(),
                TextEncoding::Utf16Be,
            ),
        ] {
            let path = dir.path().join(name);
            fs::write(&path, bytes).unwrap();
            let mut tab = EditorTab::open(&path).unwrap();
            assert_eq!(tab.text, "hello Ж🙂\n");
            assert_eq!(tab.encoding, encoding);
            assert_eq!(tab.line_ending, LineEnding::CrLf);
            tab.text.push('!');
            tab.dirty = true;
            tab.save(None).unwrap();
            let saved = fs::read(&path).unwrap();
            assert!(saved.starts_with(match encoding {
                TextEncoding::Utf8Bom => &[0xEF, 0xBB, 0xBF],
                TextEncoding::Utf16Le => &[0xFF, 0xFE],
                TextEncoding::Utf16Be => &[0xFE, 0xFF],
                TextEncoding::Utf8 => &[],
            }));
            let reopened = EditorTab::open(&path).unwrap();
            assert_eq!(reopened.text, "hello Ж🙂\n!");
            assert_eq!(reopened.line_ending, LineEnding::CrLf);
        }
    }

    #[cfg(unix)]
    #[test]
    fn save_as_follows_existing_symlink_instead_of_replacing_the_link() {
        use std::os::unix::fs::symlink;

        let dir = tempfile::tempdir().unwrap();
        let target = dir.path().join("real-target.txt");
        let alias = dir.path().join("alias.txt");
        fs::write(&target, "old contents").unwrap();
        symlink(&target, &alias).unwrap();

        let canonical_target = target.canonicalize().unwrap();
        let mut tab = EditorTab::untitled_text();
        tab.text = "new contents".into();
        tab.dirty = true;
        tab.save(Some(alias.clone())).unwrap();

        assert!(fs::symlink_metadata(&alias)
            .unwrap()
            .file_type()
            .is_symlink());
        assert_eq!(fs::read_to_string(&target).unwrap(), "new contents");
        assert_eq!(tab.path.as_ref(), Some(&canonical_target));
    }

    #[test]
    fn cr_only_text_files_are_normalized_and_saved_with_original_line_ending() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("classic-mac.txt");
        fs::write(&path, b"first\rsecond\rthird").unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        assert_eq!(tab.text, "first\nsecond\nthird");
        assert_eq!(tab.line_count, 3);
        assert_eq!(line_start_char(&tab.text, 2), 6);
        assert_eq!(tab.line_ending, LineEnding::Cr);
        tab.text.push_str("\nfourth");
        tab.dirty = true;
        tab.save(None).unwrap();
        assert_eq!(fs::read(path).unwrap(), b"first\rsecond\rthird\rfourth");
    }

    #[test]
    fn mixed_line_endings_are_normalized_to_the_first_detected_style() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("mixed-newlines.txt");
        fs::write(&path, b"first\r\nsecond\nthird\rfourth").unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        assert_eq!(tab.text, "first\nsecond\nthird\nfourth");
        assert_eq!(tab.line_ending, LineEnding::CrLf);
        tab.dirty = true;
        tab.save(None).unwrap();
        assert_eq!(
            fs::read(path).unwrap(),
            b"first\r\nsecond\r\nthird\r\nfourth"
        );
    }

    #[test]
    fn owned_newline_normalization_compacts_in_place_and_preserves_unicode() {
        let mut source = String::with_capacity(1024);
        source.push_str("Ж🙂\r\nA\rB\n終わり\r");
        let capacity = source.capacity();
        let normalized = normalize_line_endings_owned(source);
        assert_eq!(normalized, "Ж🙂\nA\nB\n終わり\n");
        assert_eq!(normalized.capacity(), capacity);
    }

    #[test]
    fn binary_files_are_rejected_as_text() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("binary.dat");
        fs::write(&path, [0x41, 0x00, 0x42]).unwrap();
        assert!(EditorTab::open(&path).is_err());
    }

    #[test]
    fn oversized_text_files_are_rejected_before_loading() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("too-large.txt");
        let file = fs::File::create(&path).unwrap();
        file.set_len(MAX_EDITABLE_FILE_BYTES + 1).unwrap();
        let error = EditorTab::open(&path).err().unwrap();
        assert!(error.to_string().contains("editor limit"));
    }

    #[test]
    fn decoded_utf16_expansion_is_also_bounded() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("utf16-expanded.txt");
        let units = (MAX_EDITABLE_FILE_BYTES as usize / 3) + 1;
        let mut bytes = Vec::with_capacity(units * 2 + 2);
        bytes.extend_from_slice(&[0xFF, 0xFE]);
        for _ in 0..units {
            bytes.extend_from_slice(&0x4E2Du16.to_le_bytes());
        }
        fs::write(&path, bytes).unwrap();
        let error = read_text_file(&path).err().unwrap();
        assert!(error.to_string().contains("decoded text exceeds"));
    }

    #[test]
    fn find_replace_and_line_navigation_use_character_offsets() {
        let text = "Ж🙂 apple\napple Ж🙂\n";
        assert_eq!(find_match(text, "apple", 0, false), Some((3, 8)));
        assert_eq!(find_match(text, "apple", 8, false), Some((9, 14)));
        assert_eq!(find_match(text, "apple", 14, true), Some((3, 8)));
        assert_eq!(find_match("Ж🙂x", "x", 2, true), Some((2, 3)));
        assert_eq!(line_start_char(text, 2), 9);
        let mut tab = EditorTab::untitled();
        tab.text = text.into();
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        assert!(replace_range(
            &mut tab,
            3,
            8,
            "orange",
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_UNDO_HISTORY_BYTES,
        ));
        assert_eq!(tab.text, "Ж🙂 orange\napple Ж🙂\n");
        assert!(tab.dirty);
    }

    #[test]
    fn replace_range_rejects_invalid_ranges_and_rolls_back_capacity_growth() {
        let mut tab = EditorTab::untitled_text();
        tab.text = String::with_capacity(16);
        tab.text.push_str("abc");
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        let before = tab.text.clone();
        let budget = memory_bytes(&tab) + 4;

        assert!(!replace_range(&mut tab, 2, 1, "x", 1024, budget));
        assert!(!replace_range(&mut tab, 0, 4, "x", 1024, budget));
        assert!(!replace_range(
            &mut tab,
            1,
            2,
            &"x".repeat(128),
            1024,
            budget
        ));
        assert_eq!(tab.text, before);
        assert!(tab.limit_reached);
        assert!(memory_bytes(&tab) <= budget);
    }

    #[test]
    fn replace_all_respects_the_aggregate_document_budget() {
        let mut tab = EditorTab::untitled();
        tab.text = "a".repeat(100);
        let original = tab.text.clone();
        assert_eq!(replace_all(&mut tab, "a", &"b".repeat(100), 1_000, 150), 0);
        assert_eq!(tab.text, original);
    }

    #[test]
    fn replace_all_rejects_when_page_history_leaves_no_room_for_undo_snapshot() {
        let mut tab = EditorTab::untitled_text();
        tab.text = "a".repeat(64);
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.page_history = Vec::with_capacity(64);
        let original = tab.text.clone();
        assert_eq!(
            replace_all(&mut tab, "a", "b", MAX_EDITABLE_FILE_BYTES as usize, 512),
            0
        );
        assert_eq!(tab.text, original);
        assert!(tab.undo_stack.is_empty());
    }

    #[test]
    fn document_delta_undo_and_redo_restore_text_and_cursor() {
        let mut tab = EditorTab::untitled();
        tab.text = "before\ntext".into();
        refresh_document_metrics(&mut tab);
        tab.cursor_char = 4;
        tab.saved_hash = text_hash(&tab.text);
        tab.dirty = false;
        let before = tab.text.clone();
        let position = EditorPosition::capture(&tab);
        tab.text = "after\ntext".into();
        tab.cursor_char = 5;
        refresh_document_metrics(&mut tab);
        record_document_edit(&mut tab, &before, position, MAX_UNDO_HISTORY_BYTES);

        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "before\ntext");
        assert_eq!(tab.cursor_char, 4);
        assert!(!tab.dirty);
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "after\ntext");
        assert_eq!(tab.cursor_char, 5);
        assert!(tab.dirty);
    }

    #[test]
    fn undo_refuses_to_overwrite_an_untracked_conflicting_change() {
        let mut tab = EditorTab::untitled();
        tab.text = "before".into();
        let before = tab.text.clone();
        let position = EditorPosition::capture(&tab);
        tab.text = "after".into();
        record_document_edit(&mut tab, &before, position, MAX_UNDO_HISTORY_BYTES);
        tab.text = "later".into();
        assert!(!apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "later");
    }

    #[test]
    fn undo_after_save_restores_dirty_state_from_saved_content_hash() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("saved.txt");
        fs::write(&path, "before").unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        let before = tab.text.clone();
        let position = EditorPosition::capture(&tab);
        tab.text = "after".into();
        refresh_document_metrics(&mut tab);
        record_document_edit(&mut tab, &before, position, MAX_UNDO_HISTORY_BYTES);
        tab.save(None).unwrap();
        assert!(!tab.dirty);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "before");
        assert!(tab.dirty);
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "after");
        assert!(!tab.dirty);
    }

    #[test]
    fn undo_history_is_document_wide_across_editor_windows() {
        let mut tab = EditorTab::untitled();
        tab.text = "a".repeat(LARGE_DOCUMENT_THRESHOLD_BYTES + EDITOR_PAGE_BYTES * 3);
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.saved_hash = text_hash(&tab.text);
        set_page_start(&mut tab, 0);
        let first_start = tab.page_start_byte;
        let first_before = tab.text[first_start..first_start + EDITOR_PAGE_BYTES].to_owned();
        let first_position = EditorPosition::capture(&tab);
        tab.text
            .replace_range(first_start + 10..first_start + 11, "X");
        tab.cursor_char = first_start + 11;
        let first_after = tab.text[first_start..first_start + EDITOR_PAGE_BYTES].to_owned();
        record_page_edit(
            &mut tab,
            first_start,
            &first_before,
            &first_after,
            first_position,
            MAX_UNDO_HISTORY_BYTES,
        );

        set_page_start(&mut tab, EDITOR_PAGE_BYTES * 2);
        let second_start = tab.page_start_byte;
        let second_before = tab.text[second_start..second_start + EDITOR_PAGE_BYTES].to_owned();
        let second_position = EditorPosition::capture(&tab);
        tab.text
            .replace_range(second_start + 20..second_start + 21, "Y");
        tab.cursor_char = second_start + 21;
        let second_after = tab.text[second_start..second_start + EDITOR_PAGE_BYTES].to_owned();
        record_page_edit(
            &mut tab,
            second_start,
            &second_before,
            &second_after,
            second_position,
            MAX_UNDO_HISTORY_BYTES,
        );

        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(&tab.text[second_start + 20..second_start + 21], "a");
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(&tab.text[first_start + 10..first_start + 11], "a");
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(&tab.text[first_start + 10..first_start + 11], "X");
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(&tab.text[second_start + 20..second_start + 21], "Y");
    }

    #[test]
    fn page_navigation_history_tracks_length_changing_edits_and_undo() {
        let mut tab = EditorTab::untitled();
        tab.text = "a".repeat(LARGE_DOCUMENT_THRESHOLD_BYTES + EDITOR_PAGE_BYTES * 3);
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        set_page_start(&mut tab, EDITOR_PAGE_BYTES * 2);
        let previous_page = tab.page_start_byte;
        push_page_history(&mut tab, previous_page);
        set_page_start(&mut tab, 0);
        assert_eq!(
            tab.page_history.last().copied(),
            Some(EDITOR_PAGE_BYTES * 2)
        );

        let before_page = tab.text[..EDITOR_PAGE_BYTES].to_owned();
        let before_position = EditorPosition::capture(&tab);
        tab.text.insert_str(0, "xyz");
        let after_page = tab.text[..EDITOR_PAGE_BYTES + 3].to_owned();
        record_page_edit(
            &mut tab,
            0,
            &before_page,
            &after_page,
            before_position,
            MAX_UNDO_HISTORY_BYTES,
        );
        assert_eq!(
            tab.page_history.last().copied(),
            Some(EDITOR_PAGE_BYTES * 2 + 3)
        );

        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(
            tab.page_history.last().copied(),
            Some(EDITOR_PAGE_BYTES * 2)
        );
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(
            tab.page_history.last().copied(),
            Some(EDITOR_PAGE_BYTES * 2 + 3)
        );
    }

    #[test]
    fn replace_all_is_one_undoable_document_operation() {
        let mut tab = EditorTab::untitled();
        tab.text = "one two one three one".into();
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.saved_hash = text_hash(&tab.text);
        assert_eq!(
            replace_all(
                &mut tab,
                "one",
                "ONE",
                MAX_EDITABLE_FILE_BYTES as usize,
                MAX_UNDO_HISTORY_BYTES,
            ),
            3
        );
        assert_eq!(tab.text, "ONE two ONE three ONE");
        assert!(tab.dirty);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "one two one three one");
        assert!(!tab.dirty);
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "ONE two ONE three ONE");
        assert!(tab.dirty);
    }

    #[test]
    fn page_history_capacity_is_reserved_and_never_grows_past_its_limit() {
        let mut tab = EditorTab::untitled_text();
        assert_eq!(tab.page_history.capacity(), MAX_PAGE_HISTORY_ENTRIES);
        let initial_memory = memory_bytes(&tab);

        for page in 0..MAX_PAGE_HISTORY_ENTRIES {
            push_page_history(&mut tab, page * EDITOR_PAGE_BYTES);
        }
        assert_eq!(tab.page_history.len(), MAX_PAGE_HISTORY_ENTRIES);
        assert_eq!(tab.page_history.capacity(), MAX_PAGE_HISTORY_ENTRIES);
        assert!(memory_bytes(&tab) >= initial_memory);

        push_page_history(&mut tab, MAX_PAGE_HISTORY_ENTRIES * EDITOR_PAGE_BYTES);
        assert_eq!(tab.page_history.len(), MAX_PAGE_HISTORY_ENTRIES);
        assert_eq!(tab.page_history[0], EDITOR_PAGE_BYTES);
        assert_eq!(
            tab.page_history.capacity(),
            MAX_PAGE_HISTORY_ENTRIES,
            "rolling page history must not grow its Vec capacity"
        );

        let cloned = clone_page_history(&tab.page_history);
        assert_eq!(cloned, tab.page_history);
        assert_eq!(cloned.capacity(), MAX_PAGE_HISTORY_ENTRIES);
    }

    #[test]
    fn replace_all_uses_a_single_document_swap_snapshot_for_large_changes() {
        let mut tab = EditorTab::untitled_text();
        tab.text = "a".repeat(1024 * 1024);
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        let before_capacity = tab.text.capacity();
        assert_eq!(
            replace_all(
                &mut tab,
                "a",
                "b",
                MAX_EDITABLE_FILE_BYTES as usize,
                MAX_OPEN_DOCUMENT_MEMORY_BYTES,
            ),
            1024 * 1024
        );
        assert!(matches!(
            tab.undo_stack.last(),
            Some(UndoRecord::DocumentSwap { .. })
        ));
        assert!(memory_bytes(&tab) <= before_capacity * 2 + 4096);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.text.bytes().all(|byte| byte == b'a'));
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.text.bytes().all(|byte| byte == b'b'));
    }

    #[test]
    fn replace_all_rebases_cursor_and_page_history_across_disjoint_matches() {
        let mut tab = EditorTab::untitled_text();
        let mut text = "a".repeat(1024 * 1024);
        text.replace_range(100..101, "X");
        text.replace_range(700_000..700_001, "X");
        tab.text = text;
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.saved_hash = text_hash(&tab.text);
        tab.cursor_char = 500_000;
        set_page_start(&mut tab, 400_000);
        tab.page_history = vec![200_000, 600_000];
        let original_text = tab.text.clone();
        let original_cursor = tab.cursor_char;
        let original_page = tab.page_start_byte;
        let original_history = tab.page_history.clone();

        assert_eq!(
            replace_all(
                &mut tab,
                "X",
                "LONG",
                MAX_EDITABLE_FILE_BYTES as usize,
                MAX_OPEN_DOCUMENT_MEMORY_BYTES,
            ),
            2
        );
        assert_eq!(tab.cursor_char, original_cursor + 3);
        assert_eq!(tab.page_start_byte, original_page + 3);
        assert_eq!(tab.page_history, vec![200_003, 600_003]);
        let replaced_text = tab.text.clone();
        let replaced_cursor = tab.cursor_char;
        let replaced_page = tab.page_start_byte;
        let replaced_history = tab.page_history.clone();

        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, original_text);
        assert_eq!(tab.cursor_char, original_cursor);
        assert_eq!(tab.page_start_byte, original_page);
        assert_eq!(tab.page_history, original_history);

        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, replaced_text);
        assert_eq!(tab.cursor_char, replaced_cursor);
        assert_eq!(tab.page_start_byte, replaced_page);
        assert_eq!(tab.page_history, replaced_history);
    }

    #[test]
    fn replace_all_offset_mapping_handles_utf8_match_boundaries_and_shrinking() {
        let text = "a猫猫b猫c";
        let map = |offset| map_replace_all_offset(text, "猫", "x", offset);
        assert_eq!(map(0), 0); // before the first match
        assert_eq!(map(1), 1); // exactly at the first match start
        assert_eq!(map(2), 1); // inside a UTF-8 match maps to its start
        assert_eq!(map(4), 2); // end of first match / start of second
        assert_eq!(map(7), 3); // end of second match
        assert_eq!(map(8), 4); // start of the third match after two deltas
        assert_eq!(map(11), 5); // end of the final match
        assert_eq!(map(12), 6); // after all matches

        let expanded = map_replace_all_offset(text, "猫", "🙂", 12);
        assert_eq!(expanded, 15);
    }

    #[test]
    fn identical_replace_all_is_a_true_no_op() {
        let mut tab = EditorTab::untitled_text();
        tab.text = "same text same text".into();
        tab.dirty = true;
        let text = tab.text.clone();
        let memory = memory_bytes(&tab);
        let undo_len = tab.undo_stack.len();
        let redo_len = tab.redo_stack.len();

        assert_eq!(
            replace_all(
                &mut tab,
                "same",
                "same",
                MAX_EDITABLE_FILE_BYTES as usize,
                MAX_OPEN_DOCUMENT_MEMORY_BYTES,
            ),
            0
        );
        assert_eq!(tab.text, text);
        assert_eq!(memory_bytes(&tab), memory);
        assert_eq!(tab.undo_stack.len(), undo_len);
        assert_eq!(tab.redo_stack.len(), redo_len);
        assert!(tab.dirty);
    }

    #[test]
    fn replace_normalizes_crlf_and_cr_for_every_supported_encoding() {
        let dir = tempfile::tempdir().unwrap();
        for encoding in [
            TextEncoding::Utf8,
            TextEncoding::Utf8Bom,
            TextEncoding::Utf16Le,
            TextEncoding::Utf16Be,
        ] {
            for line_ending in [LineEnding::Lf, LineEnding::CrLf, LineEnding::Cr] {
                let mut tab = EditorTab::untitled_text();
                tab.text = "target middle target\n".into();
                tab.encoding = encoding;
                tab.line_ending = line_ending;
                tab.encoded_len = encoded_text_size(&tab.text, encoding, line_ending);

                assert!(replace_one(
                    &mut tab,
                    "target",
                    "one\r\ntwo\rthree",
                    MAX_EDITABLE_FILE_BYTES as usize,
                    MAX_OPEN_DOCUMENT_MEMORY_BYTES,
                ));
                assert_eq!(
                    tab.text, "one\ntwo\nthree middle target\n",
                    "replace-one normalization failed for {encoding:?}/{line_ending:?}"
                );
                assert_eq!(tab.line_count, 4);

                assert_eq!(
                    replace_all(
                        &mut tab,
                        "target",
                        "four\r\nfive",
                        MAX_EDITABLE_FILE_BYTES as usize,
                        MAX_OPEN_DOCUMENT_MEMORY_BYTES,
                    ),
                    1
                );
                assert_eq!(tab.text, "one\ntwo\nthree middle four\nfive\n");
                assert_eq!(tab.line_count, 5);

                let path = dir
                    .path()
                    .join(format!("replace-{encoding:?}-{line_ending:?}.txt"));
                tab.save(Some(path.clone())).unwrap();
                let reopened = EditorTab::open(&path).unwrap();
                assert_eq!(reopened.text, tab.text);
                assert_eq!(reopened.encoding, encoding);
                assert_eq!(reopened.line_ending, line_ending);
            }
        }
    }

    #[test]
    fn undo_history_memory_is_included_in_document_budget() {
        let mut tab = EditorTab::untitled();
        tab.text = "before".into();
        let baseline = memory_bytes(&tab);
        let before = tab.text.clone();
        let position = EditorPosition::capture(&tab);
        tab.text = "after".into();
        record_document_edit(&mut tab, &before, position, MAX_UNDO_HISTORY_BYTES);
        assert!(memory_bytes(&tab) > tab.text.capacity());
        assert!(memory_bytes(&tab) > baseline);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(memory_bytes(&tab) > tab.text.capacity());
    }

    #[test]
    fn undo_delta_boundaries_remain_valid_for_multibyte_unicode() {
        let mut tab = EditorTab::untitled();
        tab.text = "start Ж🙂 finish".into();
        tab.saved_hash = text_hash(&tab.text);
        let before = tab.text.clone();
        let position = EditorPosition::capture(&tab);
        tab.text = "start Жx🙂 finish".into();
        record_document_edit(&mut tab, &before, position, MAX_UNDO_HISTORY_BYTES);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "start Ж🙂 finish");
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "start Жx🙂 finish");
    }

    #[test]
    fn decoded_text_limits_account_for_boms_and_encoding_expansion() {
        assert_eq!(
            decoded_text_limit(TextEncoding::Utf8),
            MAX_EDITABLE_FILE_BYTES
        );
        assert_eq!(
            decoded_text_limit(TextEncoding::Utf8Bom),
            MAX_EDITABLE_FILE_BYTES - 3
        );
        assert_eq!(
            decoded_text_limit(TextEncoding::Utf16Le),
            MAX_EDITABLE_FILE_BYTES
        );
    }

    #[test]
    fn encoded_size_estimates_match_the_actual_saved_bytes() {
        let text = "A\nЖ🙂\n";
        for encoding in [
            TextEncoding::Utf8,
            TextEncoding::Utf8Bom,
            TextEncoding::Utf16Le,
            TextEncoding::Utf16Be,
        ] {
            for line_ending in [LineEnding::Lf, LineEnding::CrLf, LineEnding::Cr] {
                let estimated = encoded_text_size(text, encoding, line_ending);
                let actual = encode_text(text, encoding, line_ending).unwrap().len();
                assert_eq!(estimated, actual, "{encoding:?} / {line_ending:?}");
            }
        }
    }

    #[test]
    fn encoded_length_stays_consistent_through_edit_undo_redo_and_save() {
        for encoding in [
            TextEncoding::Utf8Bom,
            TextEncoding::Utf16Le,
            TextEncoding::Utf16Be,
        ] {
            let dir = tempfile::tempdir().unwrap();
            let path = dir.path().join("encoded-roundtrip.txt");
            let mut tab = EditorTab::untitled_text();
            tab.encoding = encoding;
            tab.line_ending = LineEnding::CrLf;
            tab.text = "first\nЖ🙂\nlast".into();
            tab.encoded_len = encoded_text_size(&tab.text, encoding, tab.line_ending);
            tab.saved_hash = text_hash(&tab.text);
            let before = tab.text.clone();
            let position = EditorPosition::capture(&tab);
            tab.text.insert_str(0, "inserted\n");
            refresh_document_metrics(&mut tab);
            record_document_edit(&mut tab, &before, position, MAX_OPEN_DOCUMENT_MEMORY_BYTES);
            assert_eq!(
                tab.encoded_len,
                encoded_text_size(&tab.text, encoding, LineEnding::CrLf)
            );
            assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
            assert_eq!(
                tab.encoded_len,
                encoded_text_size(&tab.text, encoding, LineEnding::CrLf)
            );
            assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
            assert_eq!(
                tab.encoded_len,
                encoded_text_size(&tab.text, encoding, LineEnding::CrLf)
            );
            tab.save(Some(path.clone())).unwrap();
            assert_eq!(fs::metadata(&path).unwrap().len() as usize, tab.encoded_len);
            let reopened = EditorTab::open(&path).unwrap();
            assert_eq!(reopened.text, tab.text);
            assert_eq!(reopened.encoding, encoding);
            assert_eq!(reopened.line_ending, LineEnding::CrLf);
        }
    }

    #[test]
    fn utf16_crlf_paste_is_limited_by_encoded_bytes_not_only_utf8_length() {
        let input = "a\n".repeat(10);
        let accepted = truncate_to_limits(&input, 100, 8, TextEncoding::Utf16Le, LineEnding::CrLf);
        assert_eq!(accepted, "a\na");
        assert_eq!(
            encoded_text_size(accepted, TextEncoding::Utf16Le, LineEnding::CrLf),
            10
        );
    }

    #[test]
    fn utf16_crlf_large_paste_never_creates_an_unsavable_document() {
        let mut tab = EditorTab::untitled_text();
        tab.encoding = TextEncoding::Utf16Le;
        tab.line_ending = LineEnding::CrLf;
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        let paste = "\n".repeat((MAX_EDITABLE_FILE_BYTES as usize) / 2);
        assert!(insert_paste(
            &mut tab,
            paste,
            0,
            0,
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert!(tab.limit_reached);
        assert!(tab.encoded_len <= MAX_EDITABLE_FILE_BYTES as usize);
        assert_eq!(
            tab.encoded_len,
            encoded_text_size(&tab.text, tab.encoding, tab.line_ending)
        );

        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("utf16-crlf-large.txt");
        tab.save(Some(path.clone())).unwrap();
        assert_eq!(fs::metadata(&path).unwrap().len() as usize, tab.encoded_len);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.text.is_empty());
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.encoded_len <= MAX_EDITABLE_FILE_BYTES as usize);
    }

    #[test]
    fn large_paste_updates_the_document_without_expanding_the_editor_window() {
        let mut tab = EditorTab::untitled_text();
        let paste = "Ж🙂line\n".repeat(60_000);
        let expected = paste.clone();
        assert!(insert_paste(
            &mut tab,
            paste,
            0,
            0,
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert_eq!(tab.text, expected);
        assert!(tab.page_start_byte > 0);
        let (page_start, page_end) = page_byte_range(&tab);
        assert!(page_end - page_start <= EDITOR_PAGE_BYTES + 4);
        assert_eq!(tab.pending_cursor_selection, Some(tab.cursor_char));
        assert!(memory_bytes(&tab) <= MAX_OPEN_DOCUMENT_MEMORY_BYTES);
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert!(tab.text.is_empty());
        assert!(apply_undo(&mut tab, true, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, expected);
    }

    #[test]
    fn paste_rolls_back_when_string_capacity_would_exceed_the_aggregate_budget() {
        let mut tab = EditorTab::untitled_text();
        tab.text = String::with_capacity(16);
        tab.text.push_str("abc");
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        let before = tab.text.clone();
        let budget = memory_bytes(&tab) + 4;

        assert!(!insert_paste(&mut tab, "x".repeat(128), 3, 3, 1024, budget,));
        assert_eq!(tab.text, before);
        assert!(tab.limit_reached);
        assert!(memory_bytes(&tab) <= budget);
    }

    #[test]
    fn paste_replaces_a_unicode_selection_and_truncates_on_utf8_boundaries() {
        let mut tab = EditorTab::untitled_text();
        tab.text = "AЖ🙂B".into();
        tab.encoded_len = encoded_text_size(&tab.text, tab.encoding, tab.line_ending);
        tab.saved_hash = text_hash(&tab.text);
        assert!(insert_paste(
            &mut tab,
            "x".into(),
            1,
            3,
            MAX_EDITABLE_FILE_BYTES as usize,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert_eq!(tab.text, "AxB");
        assert!(apply_undo(&mut tab, false, MAX_OPEN_DOCUMENT_MEMORY_BYTES));
        assert_eq!(tab.text, "AЖ🙂B");

        let mut limited = EditorTab::untitled_text();
        limited.text = "abc".into();
        limited.encoded_len =
            encoded_text_size(&limited.text, limited.encoding, limited.line_ending);
        assert!(insert_paste(
            &mut limited,
            "Ж🙂".into(),
            3,
            3,
            5,
            MAX_OPEN_DOCUMENT_MEMORY_BYTES,
        ));
        assert_eq!(limited.text, "abcЖ");
        assert!(limited.limit_reached);
    }

    #[test]
    fn shift_tab_outdents_the_current_line_without_touching_other_lines() {
        let mut tab = EditorTab::untitled();
        tab.text = "    first\n        second".into();
        tab.cursor_char = tab.text.chars().count();
        let removed = outdent_text_line(&mut tab.text, tab.cursor_char, 4);
        assert_eq!(removed, 4);
        tab.cursor_char -= removed;
        assert_eq!(tab.text, "    first\n    second");
        assert_eq!(tab.cursor_char, tab.text.chars().count());
    }

    #[test]
    fn shift_tab_removes_one_leading_tab_without_touching_other_lines() {
        let mut text = "first\n\tsecond\n\t\tthird".to_owned();
        let cursor = text.chars().count();
        assert_eq!(outdent_text_line(&mut text, cursor, 4), 1);
        assert_eq!(text, "first\n\tsecond\n\tthird");
    }

    #[test]
    fn shift_tab_removes_one_tab_before_mixed_indentation() {
        let mut text = "\t    mixed".to_owned();
        let cursor = text.chars().count();
        assert_eq!(outdent_text_line(&mut text, cursor, 4), 1);
        assert_eq!(text, "    mixed");
    }

    #[test]
    fn large_documents_are_split_into_utf8_safe_editor_pages_without_gaps() {
        for original in ["Ж🙂line\n".repeat(100_000), "Ж🙂".repeat(300_000)] {
            let mut tab = EditorTab::untitled();
            tab.text = original.clone();
            let mut rendered = String::new();
            set_page_start(&mut tab, 0);
            loop {
                let (start, end) = page_byte_range(&tab);
                assert!(tab.text.is_char_boundary(start));
                assert!(tab.text.is_char_boundary(end));
                assert!(end > start);
                assert!(end - start <= EDITOR_PAGE_BYTES + 4);
                rendered.push_str(&tab.text[start..end]);
                if end == tab.text.len() {
                    break;
                }
                set_page_start(&mut tab, end);
            }
            assert_eq!(rendered, original);
        }
    }

    #[test]
    fn large_document_search_selection_can_cross_a_window_boundary() {
        let mut tab = EditorTab::untitled();
        tab.text = "a".repeat(LARGE_DOCUMENT_THRESHOLD_BYTES + EDITOR_PAGE_BYTES);
        set_page_start(&mut tab, 0);
        let start = EDITOR_PAGE_BYTES - 4;
        let end = EDITOR_PAGE_BYTES + 4;
        set_page_for_range(&mut tab, start, end);
        let (page_start, page_end) = page_byte_range(&tab);
        assert!(page_start <= start);
        assert!(page_end >= end);
        assert!(page_end - page_start <= EDITOR_PAGE_BYTES + 4);
    }

    #[test]
    fn failed_save_as_does_not_change_document_path() {
        let dir = tempfile::tempdir().unwrap();
        let original = dir.path().join("original.txt");
        fs::write(&original, "original").unwrap();
        let mut tab = EditorTab::open(&original).unwrap();
        let canonical_original = original.canonicalize().unwrap();
        let directory_target = dir.path().join("existing-directory");
        fs::create_dir(&directory_target).unwrap();
        assert!(tab.save(Some(directory_target)).is_err());
        assert_eq!(tab.path.as_deref(), Some(canonical_original.as_path()));
    }

    #[cfg(unix)]
    #[test]
    fn atomic_save_preserves_existing_unix_mode_bits() {
        use std::os::unix::fs::PermissionsExt;

        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("mode-preserved.txt");
        fs::write(&path, "before").unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o754)).unwrap();
        let mut tab = EditorTab::open(&path).unwrap();
        tab.text = "after".into();
        tab.dirty = true;
        tab.save(None).unwrap();
        assert_eq!(
            fs::metadata(path).unwrap().permissions().mode() & 0o777,
            0o754
        );
    }

    #[test]
    fn arbitrary_text_documents_are_not_compilable_rbl_sources() {
        let dir = tempfile::tempdir().unwrap();
        for name in ["notes.txt", "readme.MD", "script.py", "data.json"] {
            let path = dir.path().join(name);
            fs::write(&path, "ordinary text\n").unwrap();
            let tab = EditorTab::open(&path).unwrap();
            assert!(!tab.is_rbl(), "{name} was classified as RBL");
        }
        let rbl = dir.path().join("program.RBL");
        fs::write(&rbl, "func main() ()").unwrap();
        assert!(EditorTab::open(&rbl).unwrap().is_rbl());
        assert!(EditorTab::untitled().is_rbl());
        assert!(!EditorTab::untitled_text().is_rbl());
    }

    #[test]
    fn unknown_extension_utf8_text_can_be_edited_saved_and_reopened() {
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("opaque-format.datax");
        let original = "Заголовок\nkey = value\nemoji = 🙂\n";
        fs::write(&source, original.as_bytes()).unwrap();

        let mut tab = EditorTab::open(&source).unwrap();
        assert!(!tab.is_rbl());
        assert_eq!(tab.text, original);
        tab.text.push_str("追加 = сохранено\n");
        tab.dirty = true;
        tab.save(None).unwrap();

        let saved = fs::read(&source).unwrap();
        assert_eq!(saved, tab.text.as_bytes());
        let reopened = EditorTab::open(&source).unwrap();
        assert_eq!(reopened.text, tab.text);
        assert!(!reopened.is_rbl());
    }

    #[test]
    fn malformed_text_encodings_fail_without_changing_source_bytes() {
        let dir = tempfile::tempdir().unwrap();
        for (name, bytes) in [
            ("invalid-utf8.unknown", vec![0x61, 0xFF, 0x62]),
            ("odd-utf16.txt", vec![0xFF, 0xFE, 0x41]),
            ("invalid-surrogate.log", vec![0xFF, 0xFE, 0x00, 0xD8]),
        ] {
            let path = dir.path().join(name);
            fs::write(&path, &bytes).unwrap();
            assert!(
                EditorTab::open(&path).is_err(),
                "{name} unexpectedly opened"
            );
            assert_eq!(fs::read(&path).unwrap(), bytes, "{name} was modified");
        }
    }

    #[test]
    fn highlighter_handles_unicode_comments_and_fstrings_without_panicking() {
        let job = highlight(
            "func main() (\n  print(f\"Привет {42:>6}\") // comment\n)\n",
            14.0,
            f32::INFINITY,
            Color32::WHITE,
        );
        assert!(!job.sections.is_empty());
        assert!(job.text.ends_with(")\n"));
        assert_eq!(
            job.text,
            "func main() (\n  print(f\"Привет {42:>6}\") // comment\n)\n"
        );
    }

    #[test]
    fn highlighter_keeps_triple_quoted_contents_in_one_string_span() {
        let source = "let text = \"\"\"\nlet fake = 1 // # not a comment\n\"\"\"\nlet real = 2\n";
        let job = highlight(source, 14.0, f32::INFINITY, Color32::WHITE);
        let color_at = |needle: &str| {
            let byte = source.find(needle).unwrap();
            job.sections
                .iter()
                .find(|section| section.byte_range.contains(&byte))
                .map(|section| section.format.color)
                .unwrap()
        };
        let string_color = Color32::from_rgb(170, 210, 135);
        assert_eq!(color_at("let fake"), string_color);
        assert_eq!(color_at("// # not"), string_color);
        assert_eq!(color_at("let real"), Color32::from_rgb(205, 150, 245));
    }

    #[test]
    fn multiline_enter_keeps_the_key_event_and_appends_indentation_after_it() {
        let mut events = Vec::new();
        append_enter_events(
            &mut events,
            Some(Key::Enter),
            false,
            egui::Modifiers::NONE,
            "    ",
        );
        assert!(matches!(
            events.first(),
            Some(egui::Event::Key {
                key: Key::Enter,
                pressed: true,
                ..
            })
        ));
        assert!(matches!(events.get(1), Some(egui::Event::Text(text)) if text == "    "));

        let mut no_indent = Vec::new();
        append_enter_events(&mut no_indent, None, false, egui::Modifiers::NONE, "");
        assert_eq!(no_indent.len(), 1);
    }

    #[test]
    fn focused_multiline_editor_inserts_a_newline_on_enter() {
        let ctx = egui::Context::default();
        let mut tab = EditorTab::untitled_text();
        let id = Id::new(("rbl-source-editor", tab.id, 0usize, false));
        ctx.memory_mut(|memory| memory.request_focus(id));
        let input = egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(
                egui::Pos2::ZERO,
                egui::vec2(900.0, 600.0),
            )),
            events: vec![egui::Event::Key {
                key: Key::Enter,
                physical_key: Some(Key::Enter),
                pressed: true,
                repeat: false,
                modifiers: egui::Modifiers::NONE,
            }],
            ..Default::default()
        };
        let _ = ctx.run(input, |ctx| {
            egui::CentralPanel::default().show(ctx, |ui| {
                show_editor(
                    ui,
                    &mut tab,
                    EditorViewOptions {
                        font_size: 14.0,
                        tab_size: 4,
                        line_numbers: true,
                        syntax_highlighting: false,
                        soft_wrap: true,
                        indent_guides: true,
                        document_limit_bytes: MAX_EDITABLE_FILE_BYTES as usize,
                        undo_budget_bytes: MAX_OPEN_DOCUMENT_MEMORY_BYTES,
                    },
                    None,
                );
            });
        });
        assert_eq!(tab.text, "\n");
        assert_eq!(tab.line_count, 2);
    }

    #[test]
    fn paste_on_initial_editor_focus_still_obeys_the_document_limit() {
        let ctx = egui::Context::default();
        let mut tab = EditorTab::untitled_text();
        let position = egui::pos2(120.0, 120.0);
        let input = egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(
                egui::Pos2::ZERO,
                egui::vec2(900.0, 600.0),
            )),
            events: vec![
                egui::Event::PointerMoved(position),
                egui::Event::PointerButton {
                    pos: position,
                    button: egui::PointerButton::Primary,
                    pressed: true,
                    modifiers: egui::Modifiers::NONE,
                },
                egui::Event::Paste("AB".into()),
            ],
            ..Default::default()
        };
        let _ = ctx.run(input, |ctx| {
            egui::CentralPanel::default().show(ctx, |ui| {
                show_editor(
                    ui,
                    &mut tab,
                    EditorViewOptions {
                        font_size: 14.0,
                        tab_size: 4,
                        line_numbers: true,
                        syntax_highlighting: false,
                        soft_wrap: true,
                        indent_guides: true,
                        document_limit_bytes: 1,
                        undo_budget_bytes: MAX_OPEN_DOCUMENT_MEMORY_BYTES,
                    },
                    None,
                );
            });
        });
        assert_eq!(tab.text, "A");
        assert!(tab.limit_reached);
    }
}
