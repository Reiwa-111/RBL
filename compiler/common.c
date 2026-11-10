#include "rblc_internal.h"

static const char *g_diag_file;
static const char *g_diag_source;
static size_t g_diag_source_len;
static DiagnosticStage g_diag_stage;

static const char *diagnostic_stage_name(DiagnosticStage stage) {
    switch (stage) {
        case DIAG_STAGE_LEXER: return "lexer";
        case DIAG_STAGE_PARSER: return "parser";
        case DIAG_STAGE_SEMANTIC: return "semantic";
        case DIAG_STAGE_CODEGEN: return "codegen";
        default: return "compiler";
    }
}

void diagnostic_set_source(const char *file, const char *source, size_t length) {
    g_diag_file = file;
    g_diag_source = source;
    g_diag_source_len = length;
}

void diagnostic_set_stage(DiagnosticStage stage) {
    g_diag_stage = stage;
}

SourcePos source_pos_advance(SourcePos pos, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        pos.pos++;
        if (c == '\n') {
            pos.line++;
            pos.col = 1;
        } else if (c == '\r' && (i + 1 == length || text[i + 1] != '\n')) {
            pos.line++;
            pos.col = 1;
        } else if (c != '\r' && (c & 0xC0u) != 0x80u) {
            pos.col++;
        }
    }
    return pos;
}

size_t utf8_sequence_length(const unsigned char *s, size_t length, size_t offset) {
    if (offset >= length) return 0;
    unsigned char a = s[offset];
    if (a <= 0x7F) return 1;
    if (a >= 0xC2 && a <= 0xDF)
        return offset + 1 < length && (s[offset + 1] & 0xC0u) == 0x80u ? 2 : 0;
    if (a >= 0xE0 && a <= 0xEF) {
        if (offset + 2 >= length) return 0;
        unsigned char b = s[offset + 1], c = s[offset + 2];
        if ((c & 0xC0u) != 0x80u) return 0;
        if (a == 0xE0) return b >= 0xA0 && b <= 0xBF ? 3 : 0;
        if (a == 0xED) return b >= 0x80 && b <= 0x9F ? 3 : 0;
        return (b & 0xC0u) == 0x80u ? 3 : 0;
    }
    if (a >= 0xF0 && a <= 0xF4) {
        if (offset + 3 >= length) return 0;
        unsigned char b = s[offset + 1], c = s[offset + 2], d = s[offset + 3];
        if ((c & 0xC0u) != 0x80u || (d & 0xC0u) != 0x80u) return 0;
        if (a == 0xF0) return b >= 0x90 && b <= 0xBF ? 4 : 0;
        if (a == 0xF4) return b >= 0x80 && b <= 0x8F ? 4 : 0;
        return (b & 0xC0u) == 0x80u ? 4 : 0;
    }
    return 0;
}

static uint32_t utf8_decode_codepoint(const unsigned char *s, size_t width) {
    if (width == 1) return s[0];
    if (width == 2) return ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
    if (width == 3) return ((uint32_t)(s[0] & 0x0Fu) << 12) |
                           ((uint32_t)(s[1] & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
    return ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
           ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
}

static bool codepoint_in_range(uint32_t cp, uint32_t first, uint32_t last) {
    return cp >= first && cp <= last;
}

/* Approximate terminal-cell width for excerpts; SourcePos remains code-point based. */
static size_t unicode_cell_width(uint32_t cp) {
    if (cp == 0x200Cu || cp == 0x200Du || codepoint_in_range(cp, 0x0300u, 0x036Fu) ||
        codepoint_in_range(cp, 0x0483u, 0x0489u) ||
        codepoint_in_range(cp, 0x0591u, 0x05BDu) ||
        codepoint_in_range(cp, 0x05C1u, 0x05C2u) ||
        codepoint_in_range(cp, 0x0610u, 0x061Au) ||
        codepoint_in_range(cp, 0x064Bu, 0x065Fu) ||
        codepoint_in_range(cp, 0x06D6u, 0x06EDu) ||
        codepoint_in_range(cp, 0x0711u, 0x0711u) ||
        codepoint_in_range(cp, 0x0730u, 0x074Au) ||
        codepoint_in_range(cp, 0x07A6u, 0x07B0u) ||
        codepoint_in_range(cp, 0x07EBu, 0x07F3u) ||
        codepoint_in_range(cp, 0x0816u, 0x082Du) ||
        codepoint_in_range(cp, 0x0859u, 0x085Bu) ||
        codepoint_in_range(cp, 0x08D3u, 0x0902u) ||
        codepoint_in_range(cp, 0x093Au, 0x093Cu) ||
        codepoint_in_range(cp, 0x0941u, 0x0948u) ||
        codepoint_in_range(cp, 0x094Du, 0x094Du) ||
        codepoint_in_range(cp, 0x0951u, 0x0957u) ||
        codepoint_in_range(cp, 0x0962u, 0x0963u) ||
        codepoint_in_range(cp, 0x1AB0u, 0x1AFFu) ||
        codepoint_in_range(cp, 0x1DC0u, 0x1DFFu) ||
        codepoint_in_range(cp, 0x20D0u, 0x20FFu) ||
        codepoint_in_range(cp, 0xFE00u, 0xFE0Fu) ||
        codepoint_in_range(cp, 0xFE20u, 0xFE2Fu) ||
        codepoint_in_range(cp, 0xE0100u, 0xE01EFu)) return 0;

    if (codepoint_in_range(cp, 0x1100u, 0x115Fu) ||
        codepoint_in_range(cp, 0x231Au, 0x231Bu) ||
        codepoint_in_range(cp, 0x2329u, 0x232Au) ||
        codepoint_in_range(cp, 0x23E9u, 0x23ECu) ||
        codepoint_in_range(cp, 0x23F0u, 0x23F0u) ||
        codepoint_in_range(cp, 0x23F3u, 0x23F3u) ||
        codepoint_in_range(cp, 0x25FDu, 0x25FEu) ||
        codepoint_in_range(cp, 0x2614u, 0x2615u) ||
        codepoint_in_range(cp, 0x2648u, 0x2653u) ||
        codepoint_in_range(cp, 0x267Fu, 0x267Fu) ||
        codepoint_in_range(cp, 0x2693u, 0x2693u) ||
        codepoint_in_range(cp, 0x26A1u, 0x26A1u) ||
        codepoint_in_range(cp, 0x26AAu, 0x26ABu) ||
        codepoint_in_range(cp, 0x26BDu, 0x26BEu) ||
        codepoint_in_range(cp, 0x26C4u, 0x26C5u) ||
        codepoint_in_range(cp, 0x26CEu, 0x26CEu) ||
        codepoint_in_range(cp, 0x26D4u, 0x26D4u) ||
        codepoint_in_range(cp, 0x26EAu, 0x26EAu) ||
        codepoint_in_range(cp, 0x26F2u, 0x26F3u) ||
        codepoint_in_range(cp, 0x26F5u, 0x26F5u) ||
        codepoint_in_range(cp, 0x26FAu, 0x26FAu) ||
        codepoint_in_range(cp, 0x26FDu, 0x26FDu) ||
        codepoint_in_range(cp, 0x2705u, 0x2705u) ||
        codepoint_in_range(cp, 0x270Au, 0x270Bu) ||
        codepoint_in_range(cp, 0x2728u, 0x2728u) ||
        codepoint_in_range(cp, 0x274Cu, 0x274Cu) ||
        codepoint_in_range(cp, 0x274Eu, 0x274Eu) ||
        codepoint_in_range(cp, 0x2753u, 0x2755u) ||
        codepoint_in_range(cp, 0x2757u, 0x2757u) ||
        codepoint_in_range(cp, 0x2795u, 0x2797u) ||
        codepoint_in_range(cp, 0x27B0u, 0x27B0u) ||
        codepoint_in_range(cp, 0x27BFu, 0x27BFu) ||
        codepoint_in_range(cp, 0x2B1Bu, 0x2B1Cu) ||
        codepoint_in_range(cp, 0x2B50u, 0x2B50u) ||
        codepoint_in_range(cp, 0x2B55u, 0x2B55u) ||
        codepoint_in_range(cp, 0x2E80u, 0x303Eu) ||
        codepoint_in_range(cp, 0x3040u, 0xA4CFu) ||
        codepoint_in_range(cp, 0xAC00u, 0xD7A3u) ||
        codepoint_in_range(cp, 0xF900u, 0xFAFFu) ||
        codepoint_in_range(cp, 0xFE10u, 0xFE19u) ||
        codepoint_in_range(cp, 0xFE30u, 0xFE6Fu) ||
        codepoint_in_range(cp, 0xFF00u, 0xFF60u) ||
        codepoint_in_range(cp, 0xFFE0u, 0xFFE6u) ||
        codepoint_in_range(cp, 0x1F300u, 0x1FAFFu) ||
        codepoint_in_range(cp, 0x20000u, 0x3FFFDu)) return 2;
    return 1;
}

static void diagnostic_source_excerpt(SourcePos p) {
    if (!g_diag_source || !g_diag_file || !p.file || strcmp(g_diag_file, p.file) ||
        p.pos > g_diag_source_len) return;

    size_t start = p.pos;
    while (start && g_diag_source[start - 1] != '\n' && g_diag_source[start - 1] != '\r') start--;
    size_t end = p.pos;
    while (end < g_diag_source_len && g_diag_source[end] != '\n' && g_diag_source[end] != '\r') end++;

    char number[32];
    int number_len = snprintf(number, sizeof(number), "%zu", p.line);
    if (number_len < 1) number_len = 1;
    fprintf(stderr, "  %*s | ", number_len, number);

    size_t visual_col = 0;
    for (size_t i = start; i < end;) {
        unsigned char c = (unsigned char)g_diag_source[i];
        if (c == '\t') {
            size_t spaces = 4 - (visual_col % 4);
            for (size_t j = 0; j < spaces; j++) fputc(' ', stderr);
            visual_col += spaces;
            i++;
        } else if (c == '\0') {
            fputs("\\0", stderr);
            visual_col += 2;
            i++;
        } else if (c >= 0x80) {
            size_t width = utf8_sequence_length((const unsigned char *)g_diag_source,
                                                g_diag_source_len, i);
            if (!width) {
                fprintf(stderr, "\\x%02X", (unsigned)c);
                visual_col += 4;
                i++;
                continue;
            }
            fwrite(g_diag_source + i, 1, width, stderr);
            visual_col += unicode_cell_width(utf8_decode_codepoint(
                (const unsigned char *)g_diag_source + i, width));
            i += width;
            continue;
        } else {
            fputc(c, stderr);
            visual_col++;
            i++;
        }
    }
    fputc('\n', stderr);
    fprintf(stderr, "  %*s | ", number_len, "");
    size_t caret_col = 0;
    size_t caret_end = p.pos < end ? p.pos : end;
    for (size_t i = start; i < caret_end;) {
        unsigned char c = (unsigned char)g_diag_source[i];
        if (c == '\t') {
            caret_col += 4 - (caret_col % 4);
            i++;
        } else if (c >= 0x80u) {
            size_t width = utf8_sequence_length((const unsigned char *)g_diag_source,
                                                g_diag_source_len, i);
            if (width && i + width <= caret_end) {
                caret_col += unicode_cell_width(utf8_decode_codepoint(
                    (const unsigned char *)g_diag_source + i, width));
                i += width;
            } else {
                caret_col += 4;
                i++;
            }
        } else {
            caret_col++;
            i++;
        }
    }
    for (size_t i = 0; i < caret_col; i++) fputc(' ', stderr);
    fputs("^\n", stderr);
}

/* FNV-1a: stable, allocation-free hashing for compiler-local symbol indexes. */
size_t rbl_hash_string(const char *text){
    uint64_t h=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)text;*p;p++){
        h^=(uint64_t)*p;
        h*=UINT64_C(1099511628211);
    }
    return (size_t)h;
}

void fatal_at(SourcePos p, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s:%zu:%zu: error [%s]: ", p.file ? p.file : "<input>",
            p.line, p.col, diagnostic_stage_name(g_diag_stage));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    diagnostic_source_excerpt(p);
    exit(1);
}

 void fatal(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "RBL compiler error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

 void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { perror("malloc"); exit(1); }
    return p;
}
 void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) { perror("calloc"); exit(1); }
    return p;
}
 void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { perror("realloc"); exit(1); }
    return q;
}
 char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}
 char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

