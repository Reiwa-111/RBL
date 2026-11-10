#include "rblc_internal.h"

 static bool byte_is_ident_start(unsigned char c) {
    return (c == '_') || (c < 128 && isalpha(c)) || c >= 128;
}
 static bool byte_is_ident_cont(unsigned char c) {
    return (c == '_') || (c < 128 && isalnum(c)) || c >= 128;
}
 static char l_peek(const Lexer *l) { return l->pos < l->len ? l->src[l->pos] : '\0'; }
 static char l_peek2(const Lexer *l) { return l->pos + 1 < l->len ? l->src[l->pos+1] : '\0'; }
 static char l_peek3(const Lexer *l) { return l->pos + 2 < l->len ? l->src[l->pos+2] : '\0'; }
 static void l_adv(Lexer *l) {
    if (l->pos >= l->len) return;
    unsigned char c = (unsigned char)l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else if (c == '\r' && (l->pos >= l->len || l->src[l->pos] != '\n')) { l->line++; l->col = 1; }
    else if (c != '\r' && (c & 0xC0u) != 0x80u) l->col++;
}
 static SourcePos l_here(const Lexer *l) {
    SourcePos p = { l->file, l->base_offset + l->pos, l->line, l->col };
    return p;
}

 static Token tok_simple(Lexer *l, TokenKind k, SourcePos p) { (void)l;
    Token t; memset(&t, 0, sizeof(t)); t.kind = k; t.pos = p; return t;
}

 static TokenKind keyword_kind(const char *s) {
    if (!strcmp(s,"set")) return TOK_SET;
    if (!strcmp(s,"let")) return TOK_LET;
    if (!strcmp(s,"func")) return TOK_FUNC;
    if (!strcmp(s,"if")) return TOK_IF;
    if (!strcmp(s,"elif")) return TOK_ELIF;
    if (!strcmp(s,"else")) return TOK_ELSE;
    if (!strcmp(s,"return")) return TOK_RETURN;
    if (!strcmp(s,"true")) return TOK_TRUE;
    if (!strcmp(s,"false")) return TOK_FALSE;
    if (!strcmp(s,"and")) return TOK_AND;
    if (!strcmp(s,"or")) return TOK_OR;
    if (!strcmp(s,"not")) return TOK_NOT;
    if (!strcmp(s,"null")) return TOK_NULL;
    if (!strcmp(s,"is")) return TOK_IS;
    if (!strcmp(s,"for")) return TOK_FOR;
    if (!strcmp(s,"break")) return TOK_BREAK;
    if (!strcmp(s,"continue")) return TOK_CONTINUE;
    if (!strcmp(s,"in")) return TOK_IN;
    return TOK_IDENT;
}

 static void skip_ws_comments(Lexer *l) {
    for (;;) {
        while (isspace((unsigned char)l_peek(l))) l_adv(l);
        /* '//' and '#' both start a comment that runs to the end of the line. */
        if ((l_peek(l) == '/' && l_peek2(l) == '/') || l_peek(l) == '#') {
            while (l_peek(l) && l_peek(l) != '\n' && l_peek(l) != '\r') l_adv(l);
            continue;
        }
        break;
    }
}

/* Digits accepted by the number lexer: 0-9 always, a-f/A-F only below base 16. */
 static bool digit_in_base(char c, int base) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;
    return v < base;
}

/* A '_' separator is valid only between two digits of the literal. */
 static bool sep_is_valid(const Lexer *l, int base) {
    char prev = l->pos > 0 ? l->src[l->pos - 1] : '\0';
    return digit_in_base(prev, base) && digit_in_base(l_peek2(l), base);
}

/* Copy [start,end) without '_' separators so strtoll/strtod see a plain number. */
 static char *number_text(const Lexer *l, size_t start, size_t end) {
    char *out = xmalloc(end - start + 1);
    size_t n = 0;
    for (size_t i = start; i < end; i++) if (l->src[i] != '_') out[n++] = l->src[i];
    out[n] = '\0';
    return out;
}

/* Numeric literals: 42, 0xFF, 0b1010, 0o755, 1_000_000, 1.5, 10f, 1e42, 1.5e-3.
   A leading zero without a prefix stays decimal, so 017 is seventeen and octal
   literals are always written 0o17 (no C-style octal trap). */
 static Token lex_number(Lexer *l) {
    SourcePos p = l_here(l);
    int base = 10;
    bool is_float = false;
    bool sep_bad = false;
    SourcePos sep_bad_pos = p;

    if (l_peek(l) == '0') {
        char c = l_peek2(l);
        if (c == 'x' || c == 'X') { base = 16; l_adv(l); l_adv(l); }
        else if (c == 'b' || c == 'B') { base = 2; l_adv(l); l_adv(l); }
        else if (c == 'o' || c == 'O') { base = 8; l_adv(l); l_adv(l); }
    }

    size_t digits_start = l->pos;
    size_t digits_end = l->pos;
    for (;;) {
        char c = l_peek(l);
        if (digit_in_base(c, base)) { l_adv(l); digits_end = l->pos; continue; }
        if (c == '_') { if (!sep_is_valid(l, base) && !sep_bad) { sep_bad = true; sep_bad_pos = l_here(l); } l_adv(l); continue; }
        break;
    }

    if (base == 10) {
        if (l_peek(l) == '.' && isdigit((unsigned char)l_peek2(l))) {
            is_float = true;
            l_adv(l);
            for (;;) {
                char c = l_peek(l);
                if (isdigit((unsigned char)c)) { l_adv(l); digits_end = l->pos; continue; }
                if (c == '_') { if (!sep_is_valid(l, 10) && !sep_bad) { sep_bad = true; sep_bad_pos = l_here(l); } l_adv(l); continue; }
                break;
            }
        }
        /* Exponent only when real digits follow, so "10e" stays int 10 + ident e. */
        if (l_peek(l) == 'e' || l_peek(l) == 'E') {
            size_t save = l->pos;
            l_adv(l);
            if (l_peek(l) == '+' || l_peek(l) == '-') l_adv(l);
            if (isdigit((unsigned char)l_peek(l))) {
                is_float = true;
                for (;;) {
                    char c = l_peek(l);
                    if (isdigit((unsigned char)c)) { l_adv(l); digits_end = l->pos; continue; }
                    if (c == '_') { if (!sep_is_valid(l, 10) && !sep_bad) { sep_bad = true; sep_bad_pos = l_here(l); } l_adv(l); continue; }
                    break;
                }
            } else {
                l->pos = save;
            }
        }
        if (l_peek(l) == 'f') { is_float = true; l_adv(l); }
    }

    if (sep_bad) fatal_at(sep_bad_pos, "invalid digit separator in numeric literal");
    if (digits_end == digits_start) fatal_at(p, "numeric literal has no digits");

    char *s = number_text(l, digits_start, digits_end);
    char *end = NULL;
    errno = 0;
    if (is_float) {
        double v = strtod(s, &end);
        if (errno || !end || *end) fatal_at(p, "invalid float literal");
        free(s);
        Token t = tok_simple(l, TOK_FLOAT, p);
        t.f = v;
        return t;
    }
    long long v = strtoll(s, &end, base);
    if (errno || !end || *end) fatal_at(p, "invalid integer literal");
    free(s);
    Token t = tok_simple(l, TOK_INT, p);
    t.i = (int64_t)v;
    return t;
}

 static Token lex_ident(Lexer *l) {
    SourcePos p = l_here(l);
    size_t start = l->pos;
    while (byte_is_ident_cont((unsigned char)l_peek(l))) l_adv(l);
    char *s = xstrndup(l->src + start, l->pos - start);
    Token t = tok_simple(l, keyword_kind(s), p);
    t.text = s;
    return t;
}

 static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

 void str_push(char **buf, size_t *n, size_t *cap, char c) {
    if (*n + 1 >= *cap) { *cap *= 2; *buf = xrealloc(*buf, *cap); }
    (*buf)[(*n)++] = c;
}

 static void str_push_utf8(char **buf, size_t *n, size_t *cap, unsigned long cp) {
    if (cp < 0x80) {
        str_push(buf, n, cap, (char)cp);
    } else if (cp < 0x800) {
        str_push(buf, n, cap, (char)(0xC0 | (cp >> 6)));
        str_push(buf, n, cap, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        str_push(buf, n, cap, (char)(0xE0 | (cp >> 12)));
        str_push(buf, n, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        str_push(buf, n, cap, (char)(0x80 | (cp & 0x3F)));
    } else {
        str_push(buf, n, cap, (char)(0xF0 | (cp >> 18)));
        str_push(buf, n, cap, (char)(0x80 | ((cp >> 12) & 0x3F)));
        str_push(buf, n, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        str_push(buf, n, cap, (char)(0x80 | (cp & 0x3F)));
    }
}

/* String literals: "…" with escape sequences, and """…""" for multi-line text.
   Supported escapes are \n \t \r \\ \" \xNN \uXXXX \UXXXXXXXX. RBL strings are
   NUL-terminated C strings, so a NUL byte inside a literal is a compile error
   instead of a silently truncated string. */
 static Token lex_string(Lexer *l) {
    SourcePos p = l_here(l);
    bool multiline = (l_peek(l) == '"' && l_peek2(l) == '"' && l_peek3(l) == '"');
    l_adv(l); /* open quote */
    if (multiline) { l_adv(l); l_adv(l); }
    size_t cap = 32, n = 0;
    char *out = xmalloc(cap);
    for (;;) {
        char c = l_peek(l);
        if (!c) fatal_at(l_here(l), multiline ? "unterminated multi-line string literal" : "unterminated string literal");
        if (multiline) {
            if (c == '"' && l_peek2(l) == '"' && l_peek3(l) == '"') { l_adv(l); l_adv(l); l_adv(l); break; }
        } else if (c == '"') {
            l_adv(l);
            break;
        }
        if (c == '\\') {
            SourcePos escape_pos = l_here(l);
            l_adv(l);
            char e = l_peek(l);
            if (!e) fatal_at(escape_pos, "unterminated escape sequence");
            l_adv(l);
            switch (e) {
                case 'n': str_push(&out, &n, &cap, '\n'); break;
                case 't': str_push(&out, &n, &cap, '\t'); break;
                case 'r': str_push(&out, &n, &cap, '\r'); break;
                case '\\': str_push(&out, &n, &cap, '\\'); break;
                case '"': str_push(&out, &n, &cap, '"'); break;
                case '0':
                    fatal_at(escape_pos, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    break;
                case 'x': {
                    int v = 0, digits = 0;
                    while (digits < 2 && hex_val(l_peek(l)) >= 0) { v = v * 16 + hex_val(l_peek(l)); l_adv(l); digits++; }
                    if (digits == 0) fatal_at(escape_pos, "\\x needs two hexadecimal digits");
                    if (v == 0) fatal_at(escape_pos, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    str_push(&out, &n, &cap, (char)v);
                    break;
                }
                case 'u': case 'U': {
                    int want = (e == 'u') ? 4 : 8, digits = 0;
                    unsigned long v = 0;
                    while (digits < want && hex_val(l_peek(l)) >= 0) { v = v * 16 + (unsigned long)hex_val(l_peek(l)); l_adv(l); digits++; }
                    if (digits != want) fatal_at(escape_pos, e == 'u' ? "\\u needs four hexadecimal digits" : "\\U needs eight hexadecimal digits");
                    if (v == 0) fatal_at(escape_pos, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) fatal_at(escape_pos, "invalid Unicode code point in escape sequence");
                    str_push_utf8(&out, &n, &cap, v);
                    break;
                }
                default:
                    fatal_at(escape_pos, "unknown escape sequence '\\%c'", e);
                    break;
            }
            continue;
        }
        str_push(&out, &n, &cap, c);
        l_adv(l);
    }
    out[n] = '\0';
    Token t = tok_simple(l, TOK_STR, p);
    t.text = out;
    return t;
}

/* Interpolated string: f"…" / f"""…""". The raw body is stored untouched; the
   parser splits it into literal text and {expression} parts, so escape decoding
   and expression parsing both reuse the normal lexer. */
 static Token lex_fstring(Lexer *l) {
    SourcePos p = l_here(l);
    l_adv(l); /* the 'f' prefix */
    bool multiline = (l_peek(l) == '"' && l_peek2(l) == '"' && l_peek3(l) == '"');
    l_adv(l); /* open quote */
    if (multiline) { l_adv(l); l_adv(l); }
    SourcePos body_pos = l_here(l);
    size_t cap = 32, n = 0;
    char *raw = xmalloc(cap);
    for (;;) {
        char c = l_peek(l);
        if (!c) fatal_at(l_here(l), multiline ? "unterminated multi-line f-string literal" : "unterminated f-string literal");
        if (multiline) {
            if (c == '"' && l_peek2(l) == '"' && l_peek3(l) == '"') { l_adv(l); l_adv(l); l_adv(l); break; }
        } else if (c == '"') {
            l_adv(l);
            break;
        }
        if (c == '\\') {
            SourcePos escape_pos = l_here(l);
            str_push(&raw, &n, &cap, c);
            l_adv(l);
            char d = l_peek(l);
            if (!d) fatal_at(escape_pos, "unterminated escape sequence");
            str_push(&raw, &n, &cap, d);
            l_adv(l);
            continue;
        }
        str_push(&raw, &n, &cap, c);
        l_adv(l);
    }
    raw[n] = '\0';
    Token t = tok_simple(l, TOK_FSTR, p);
    t.text = raw;
    t.content_pos = body_pos;
    return t;
}

 static Token lex_one(Lexer *l) {
    skip_ws_comments(l);
    SourcePos p = l_here(l);
    char c = l_peek(l);
    if (!c) return tok_simple(l, TOK_EOF, p);
    if (isdigit((unsigned char)c)) return lex_number(l);
    if (c == 'f' && l_peek2(l) == '"') return lex_fstring(l);
    if (byte_is_ident_start((unsigned char)c)) return lex_ident(l);
    if (c == '"') return lex_string(l);

    l_adv(l);
    switch (c) {
        case '+': return tok_simple(l, TOK_PLUS, p);
        case '-': return tok_simple(l, TOK_MINUS, p);
        case '*': return tok_simple(l, TOK_STAR, p);
        case '/': return tok_simple(l, TOK_SLASH, p);
        case '%': return tok_simple(l, TOK_PERCENT, p);
        case '{': return tok_simple(l, TOK_LBRACE, p);
        case '}': return tok_simple(l, TOK_RBRACE, p);
        case ':': return tok_simple(l, TOK_COLON, p);
        case '[': return tok_simple(l, TOK_LBRACKET, p);
        case ']': return tok_simple(l, TOK_RBRACKET, p);
        case '(': return tok_simple(l, TOK_LPAREN, p);
        case ')': return tok_simple(l, TOK_RPAREN, p);
        case ',': return tok_simple(l, TOK_COMMA, p);
        case '!':
            if (l_peek(l) == '=') { l_adv(l); return tok_simple(l, TOK_NE, p); }
            return tok_simple(l, TOK_BANG, p);
        case '=':
            if (l_peek(l) == '=') { l_adv(l); return tok_simple(l, TOK_EQ, p); }
            return tok_simple(l, TOK_ASSIGN, p);
        case '<':
            if (l_peek(l) == '=') { l_adv(l); return tok_simple(l, TOK_LE, p); }
            return tok_simple(l, TOK_LT, p);
        case '>':
            if (l_peek(l) == '=') { l_adv(l); return tok_simple(l, TOK_GE, p); }
            return tok_simple(l, TOK_GT, p);
        case '.':
            if (l_peek(l) == '.') {
                l_adv(l);
                if (l_peek(l) == '=') { l_adv(l); return tok_simple(l, TOK_DOTDOTEQ, p); }
                return tok_simple(l, TOK_DOTDOT, p);
            }
            return tok_simple(l, TOK_DOT, p);
        default:
            fatal_at(p, "unknown symbol '%c'", c);
    }
    return tok_simple(l, TOK_EOF, p);
}

Token *lex_all_at(const char *src, const char *file, size_t *out_n, SourcePos origin) {
    Lexer l = { .src = src, .len = strlen(src), .pos = 0,
                .line = origin.line, .col = origin.col,
                .file = file, .base_offset = origin.pos };
    size_t cap = 64, n = 0;
    Token *v = xmalloc(cap * sizeof(*v));
    for (;;) {
        if (n == cap) { cap *= 2; v = xrealloc(v, cap * sizeof(*v)); }
        v[n] = lex_one(&l);
        if (v[n].kind == TOK_EOF) { n++; break; }
        n++;
    }
    *out_n = n;
    return v;
}

Token *lex_all(const char *src, const char *file, size_t *out_n) {
    SourcePos origin = { file, 0, 1, 1 };
    return lex_all_at(src, file, out_n, origin);
}

 const char *tok_name(TokenKind k) {
    switch (k) {
#define X(x) case x: return #x;
        X(TOK_EOF) X(TOK_INT) X(TOK_FLOAT) X(TOK_STR) X(TOK_FSTR) X(TOK_IDENT)
        X(TOK_SET) X(TOK_LET) X(TOK_FUNC) X(TOK_IF) X(TOK_ELIF) X(TOK_ELSE) X(TOK_RETURN)
        X(TOK_TRUE) X(TOK_FALSE) X(TOK_NULL) X(TOK_AND) X(TOK_OR) X(TOK_NOT) X(TOK_IS) X(TOK_FOR) X(TOK_IN) X(TOK_BREAK) X(TOK_CONTINUE)
        X(TOK_PLUS) X(TOK_MINUS) X(TOK_STAR) X(TOK_SLASH) X(TOK_PERCENT) X(TOK_ASSIGN) X(TOK_EQ) X(TOK_NE)
        X(TOK_LT) X(TOK_GT) X(TOK_LE) X(TOK_GE) X(TOK_BANG)
        X(TOK_DOTDOT) X(TOK_DOTDOTEQ) X(TOK_DOT)
        X(TOK_LBRACE) X(TOK_RBRACE) X(TOK_LPAREN) X(TOK_RPAREN) X(TOK_LBRACKET) X(TOK_RBRACKET) X(TOK_COLON) X(TOK_COMMA)
#undef X
    }
    return "<unknown>";
}

 void free_tokens(Token *toks, size_t n) {
    for (size_t i = 0; i < n; i++) free(toks[i].text);
    free(toks);
}

