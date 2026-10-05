#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * RBL native compiler, v0.7-stdlib-parenthesized-direct-asm.
 *
 * Pipeline:
 *   UTF-8 source -> lexer -> recursive-descent parser -> AST -> static analysis
 *   -> loop optimizer + register allocation -> direct x86-64 assembly -> GNU as/ld -> native ELF executable.
 *
 * The generated C runtime intentionally models the semantics of the original
 * Rust interpreter rather than introducing a new language dialect.
 */

typedef struct {
    const char *file;
    size_t pos;
    size_t line;
    size_t col;
} SourcePos;

static void fatal_at(SourcePos p, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "RBL compiler error at %s:%zu:%zu: ", p.file ? p.file : "<input>", p.line, p.col);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void fatal(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "RBL compiler error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { perror("malloc"); exit(1); }
    return p;
}
static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) { perror("calloc"); exit(1); }
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { perror("realloc"); exit(1); }
    return q;
}
static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}
static char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ============================== LEXER ============================== */

typedef enum {
    TOK_EOF,
    TOK_INT, TOK_FLOAT, TOK_STR, TOK_FSTR, TOK_IDENT,
    TOK_SET, TOK_LET, TOK_FUNC, TOK_IF, TOK_ELIF, TOK_ELSE, TOK_RETURN,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_AND, TOK_OR, TOK_NOT, TOK_IS, TOK_FOR, TOK_IN, TOK_BREAK, TOK_CONTINUE,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_ASSIGN, TOK_EQ, TOK_NE,
    TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_BANG,
    TOK_DOTDOT, TOK_DOTDOTEQ, TOK_DOT,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET, TOK_COLON, TOK_COMMA
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    int64_t i;
    double f;
    SourcePos pos;
} Token;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    const char *file;
} Lexer;

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
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; } else l->col++;
}
static SourcePos l_here(const Lexer *l) {
    SourcePos p = { l->file, l->pos, l->line, l->col };
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
            while (l_peek(l) && l_peek(l) != '\n') l_adv(l);
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
        if (c == '_') { if (!sep_is_valid(l, base)) sep_bad = true; l_adv(l); continue; }
        break;
    }

    if (base == 10) {
        if (l_peek(l) == '.' && isdigit((unsigned char)l_peek2(l))) {
            is_float = true;
            l_adv(l);
            for (;;) {
                char c = l_peek(l);
                if (isdigit((unsigned char)c)) { l_adv(l); digits_end = l->pos; continue; }
                if (c == '_') { if (!sep_is_valid(l, 10)) sep_bad = true; l_adv(l); continue; }
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
                    if (c == '_') { if (!sep_is_valid(l, 10)) sep_bad = true; l_adv(l); continue; }
                    break;
                }
            } else {
                l->pos = save;
            }
        }
        if (l_peek(l) == 'f') { is_float = true; l_adv(l); }
    }

    if (sep_bad) fatal_at(p, "invalid digit separator in numeric literal");
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

static void str_push(char **buf, size_t *n, size_t *cap, char c) {
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
        if (!c) fatal_at(p, multiline ? "unterminated multi-line string literal" : "unterminated string literal");
        if (multiline) {
            if (c == '"' && l_peek2(l) == '"' && l_peek3(l) == '"') { l_adv(l); l_adv(l); l_adv(l); break; }
        } else if (c == '"') {
            l_adv(l);
            break;
        }
        if (c == '\\') {
            l_adv(l);
            char e = l_peek(l);
            if (!e) fatal_at(p, "unterminated escape sequence");
            l_adv(l);
            switch (e) {
                case 'n': str_push(&out, &n, &cap, '\n'); break;
                case 't': str_push(&out, &n, &cap, '\t'); break;
                case 'r': str_push(&out, &n, &cap, '\r'); break;
                case '\\': str_push(&out, &n, &cap, '\\'); break;
                case '"': str_push(&out, &n, &cap, '"'); break;
                case '0':
                    fatal_at(p, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    break;
                case 'x': {
                    int v = 0, digits = 0;
                    while (digits < 2 && hex_val(l_peek(l)) >= 0) { v = v * 16 + hex_val(l_peek(l)); l_adv(l); digits++; }
                    if (digits == 0) fatal_at(p, "\\x needs two hexadecimal digits");
                    if (v == 0) fatal_at(p, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    str_push(&out, &n, &cap, (char)v);
                    break;
                }
                case 'u': case 'U': {
                    int want = (e == 'u') ? 4 : 8, digits = 0;
                    unsigned long v = 0;
                    while (digits < want && hex_val(l_peek(l)) >= 0) { v = v * 16 + (unsigned long)hex_val(l_peek(l)); l_adv(l); digits++; }
                    if (digits != want) fatal_at(p, e == 'u' ? "\\u needs four hexadecimal digits" : "\\U needs eight hexadecimal digits");
                    if (v == 0) fatal_at(p, "NUL byte is not allowed in a string literal (RBL strings are NUL-terminated)");
                    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) fatal_at(p, "invalid Unicode code point in escape sequence");
                    str_push_utf8(&out, &n, &cap, v);
                    break;
                }
                default:
                    fatal_at(p, "unknown escape sequence '\\%c'", e);
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
    size_t cap = 32, n = 0;
    char *raw = xmalloc(cap);
    for (;;) {
        char c = l_peek(l);
        if (!c) fatal_at(p, multiline ? "unterminated multi-line f-string literal" : "unterminated f-string literal");
        if (multiline) {
            if (c == '"' && l_peek2(l) == '"' && l_peek3(l) == '"') { l_adv(l); l_adv(l); l_adv(l); break; }
        } else if (c == '"') {
            l_adv(l);
            break;
        }
        if (c == '\\') {
            str_push(&raw, &n, &cap, c);
            l_adv(l);
            char d = l_peek(l);
            if (!d) fatal_at(p, "unterminated escape sequence");
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

static Token *lex_all(const char *src, const char *file, size_t *out_n) {
    Lexer l = { src, strlen(src), 0, 1, 1, file };
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

static const char *tok_name(TokenKind k) {
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

static void free_tokens(Token *toks, size_t n) {
    for (size_t i = 0; i < n; i++) free(toks[i].text);
    free(toks);
}

/* ============================== AST ============================== */

typedef enum { EX_INT, EX_FLOAT, EX_STR, EX_BOOL, EX_IDENT, EX_UNARY, EX_BINARY, EX_CALL, EX_METHOD, EX_RANGE, EX_TERNARY } ExprKind;
typedef enum { U_NEG, U_NOT } UnaryOp;
typedef enum { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_EQ, B_NE, B_LT, B_GT, B_LE, B_GE, B_AND, B_OR, B_IS } BinaryOp;

typedef struct Expr Expr;
typedef struct ExprVec ExprVec;
struct ExprVec { Expr **data; size_t len, cap; };

struct Expr {
    ExprKind kind;
    SourcePos pos;
    union {
        int64_t i;
        double f;
        char *s;
        bool b;
        char *ident;
        struct { UnaryOp op; Expr *expr; } unary;
        struct { Expr *left; BinaryOp op; Expr *right; } binary;
        struct { char *callee; ExprVec *args; } call;
        struct { char *object; char *method; ExprVec *args; } method;
        struct { Expr *start; Expr *end; bool inclusive; } range;
        struct { Expr *cond; Expr *then_expr; Expr *else_expr; } tern;
    } as;
};

/* forward-sized vector definitions */
typedef struct Stmt Stmt;
typedef struct StmtVec { Stmt **data; size_t len, cap; } StmtVec;

typedef struct {
    char *name;
    char *type;
    SourcePos pos;
} Param;
typedef struct { Param *data; size_t len, cap; } ParamVec;

typedef enum { ST_SET, ST_LET, ST_RETURN, ST_EXPR, ST_IF, ST_FOR, ST_BREAK, ST_CONTINUE } StmtKind;
typedef struct { Expr *cond; StmtVec then_block; struct Elif *elifs; size_t elif_len, elif_cap; StmtVec else_block; bool has_else; } IfStmt;
typedef struct Elif { Expr *cond; StmtVec body; } Elif;
typedef struct { char *var; Expr *range; StmtVec body; } ForStmt;
struct Stmt {
    StmtKind kind;
    SourcePos pos;
    union {
        struct { char *name; Expr *value; } assign;
        Expr *ret;
        Expr *expr;
        IfStmt ifs;
        ForStmt fors;
    } as;
};

typedef struct {
    char *name;
    ParamVec params;
    bool has_return_type;
    char *return_type;
    StmtVec body;
    SourcePos pos;
} Function;
typedef struct { Function *data; size_t len, cap; } FuncVec;

typedef struct { FuncVec funcs; } Program;

static ExprVec exprvec_new(void) { ExprVec v={0}; return v; }
static StmtVec stmtvec_new(void) { StmtVec v={0}; return v; }
static ParamVec paramvec_new(void) { ParamVec v={0}; return v; }
static void exprvec_push(ExprVec *v, Expr *e) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=e; }
static void stmtvec_push(StmtVec *v, Stmt *s) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:8;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=s; }
static void paramvec_push(ParamVec *v, Param p) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=p; }
static void funcvec_push(FuncVec *v, Function f) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=f; }
static void elif_push(IfStmt *ifs, Elif e) { if(ifs->elif_len==ifs->elif_cap){ifs->elif_cap=ifs->elif_cap?ifs->elif_cap*2:2;ifs->elifs=xrealloc(ifs->elifs,ifs->elif_cap*sizeof(*ifs->elifs));} ifs->elifs[ifs->elif_len++]=e; }

static Expr *new_expr(SourcePos p, ExprKind k) { Expr *e=xcalloc(1,sizeof(*e)); e->pos=p; e->kind=k; return e; }
static Stmt *new_stmt(SourcePos p, StmtKind k) { Stmt *s=xcalloc(1,sizeof(*s)); s->pos=p; s->kind=k; return s; }

/* ============================== PARSER ============================== */

typedef struct { Token *toks; size_t len; size_t pos; const char *file; } Parser;
static Token *p_peek(Parser *p) { return &p->toks[p->pos]; }
static Token p_adv(Parser *p) { Token t=p->toks[p->pos]; if(p->pos+1<p->len)p->pos++; return t; }
static bool p_is(Parser *p, TokenKind k) { return p_peek(p)->kind==k; }
static void p_expect(Parser *p, TokenKind k, const char *ctx) {
    if (!p_is(p,k)) fatal_at(p_peek(p)->pos,"expected %s (%s), found %s",tok_name(k),ctx,tok_name(p_peek(p)->kind));
    p_adv(p);
}
static char *p_ident(Parser *p, const char *ctx) {
    if (!p_is(p,TOK_IDENT)) fatal_at(p_peek(p)->pos,"expected identifier (%s), found %s",ctx,tok_name(p_peek(p)->kind));
    Token t=p_adv(p); return xstrdup(t.text);
}

/* True when the tokens at p->pos and p->pos+1 are the given kinds written
   adjacently in the source, i.e. '++', '--' or '+=' rather than '+ ='. */
static bool p_two_adjacent(Parser *p, TokenKind a, TokenKind b) {
    if (p->pos + 1 >= p->len) return false;
    Token *t1 = &p->toks[p->pos], *t2 = &p->toks[p->pos + 1];
    return t1->kind == a && t2->kind == b && t2->pos.pos == t1->pos.pos + 1;
}

static Expr *parse_expr(Parser *p);
static Expr *parse_range(Parser *p);
static Expr *parse_atom(Parser *p);
static Stmt *parse_stmt(Parser *p);
static StmtVec parse_block(Parser *p);

/* Binary node helper for the desugaring below. */
static Expr *mk_binary(SourcePos pos, BinaryOp op, Expr *l, Expr *r) {
    Expr *e = new_expr(pos, EX_BINARY);
    e->as.binary.left = l;
    e->as.binary.op = op;
    e->as.binary.right = r;
    return e;
}

/* x <op>= rhs desugars to a rebinding of the same variable: x = x <op> rhs. */
static Expr *mk_self_op_expr(SourcePos pos, const char *name, BinaryOp op, Expr *rhs) {
    Expr *lhs = new_expr(pos, EX_IDENT);
    lhs->as.ident = xstrdup(name);
    return mk_binary(pos, op, lhs, rhs);
}

/* x++ / ++x desugar to x = x + 1 (and likewise for '-'). */
static Expr *mk_self_op(SourcePos pos, const char *name, BinaryOp op) {
    Expr *one = new_expr(pos, EX_INT);
    one->as.i = 1;
    return mk_self_op_expr(pos, name, op, one);
}

/* switch (subject) (
       case 1 ( … )  case 2, 3 ( … )  case 4..=9 ( … )  default ( … )
   )
   No fallthrough: exactly one branch runs. Desugared at parse time into an
   if/elif/else chain of comparisons, so nothing is needed in the code generator
   and both backends behave identically by construction. The subject must be a
   variable or a literal — it is re-compared for every case, so a call with side
   effects would run more than once; that is rejected instead of silently
   misbehaving. */
static Stmt *parse_switch(Parser *p) {
    SourcePos pos=p_adv(p).pos;                 /* the 'switch' identifier */
    p_expect(p,TOK_LPAREN,"switch (");
    Expr *subj=parse_expr(p);
    p_expect(p,TOK_RPAREN,"switch subject end");
    if(subj->kind!=EX_IDENT&&subj->kind!=EX_INT&&subj->kind!=EX_FLOAT&&subj->kind!=EX_STR&&subj->kind!=EX_BOOL)
        fatal_at(pos,"switch subject must be a variable or a literal, because it is compared once per case");
    p_expect(p,TOK_LPAREN,"switch body");
    IfStmt ifs; memset(&ifs,0,sizeof(ifs));
    bool have_case=false,have_default=false;
    while(!p_is(p,TOK_RPAREN)){
        if(p_is(p,TOK_EOF)) fatal_at(p_peek(p)->pos,"unterminated switch body");
        Token t=p_adv(p);
        if(t.kind!=TOK_IDENT) fatal_at(t.pos,"expected 'case' or 'default' in switch");
        if(!strcmp(t.text,"default")){
            if(have_default) fatal_at(t.pos,"duplicate 'default' in switch");
            have_default=true; ifs.has_else=true; ifs.else_block=parse_block(p);
            continue;
        }
        if(strcmp(t.text,"case")) fatal_at(t.pos,"expected 'case' or 'default', found '%s'",t.text);
        Expr *cond=NULL;
        for(;;){
            Expr *v=parse_range(p);
            Expr *one;
            if(v->kind==EX_RANGE){
                Expr *lo=mk_binary(t.pos,B_GE,subj,v->as.range.start);
                Expr *hi=mk_binary(t.pos,v->as.range.inclusive?B_LE:B_LT,subj,v->as.range.end);
                one=mk_binary(t.pos,B_AND,lo,hi);
            } else {
                one=mk_binary(t.pos,B_EQ,subj,v);
            }
            cond=cond?mk_binary(t.pos,B_OR,cond,one):one;
            if(p_is(p,TOK_COMMA)){p_adv(p);continue;}
            break;
        }
        StmtVec cb=parse_block(p);
        if(!have_case){ ifs.cond=cond; ifs.then_block=cb; have_case=true; }
        else{
            if(ifs.elif_len==ifs.elif_cap){ ifs.elif_cap=ifs.elif_cap?ifs.elif_cap*2:4; ifs.elifs=xrealloc(ifs.elifs,ifs.elif_cap*sizeof(Elif)); }
            ifs.elifs[ifs.elif_len].cond=cond; ifs.elifs[ifs.elif_len].body=cb; ifs.elif_len++;
        }
    }
    p_expect(p,TOK_RPAREN,"switch body end");
    if(!have_case) fatal_at(pos,"switch needs at least one 'case'");
    Stmt *s=new_stmt(pos,ST_IF); s->as.ifs=ifs; return s;
}

/* A literal run of an f-string is decoded by re-lexing it as a normal string
   literal, so escapes behave exactly as in "…" and there is no second decoder. */
static Expr *fstring_literal_expr(SourcePos pos, const char *raw, size_t len) {
    char *src = xmalloc(len + 3);
    src[0] = '"';
    memcpy(src + 1, raw, len);
    src[len + 1] = '"';
    src[len + 2] = '\0';
    size_t nt = 0;
    Token *toks = lex_all(src, "<f-string>", &nt);
    Expr *e = NULL;
    if (nt == 2 && toks[0].kind == TOK_STR && toks[1].kind == TOK_EOF) {
        e = new_expr(pos, EX_STR);
        e->as.s = xstrdup(toks[0].text);
    }
    free_tokens(toks, nt);
    free(src);
    if (!e) fatal_at(pos, "unsupported quote inside an f-string literal part");
    return e;
}

/* Interpolated string -> string concatenation. Literal runs become string
   literals, {expression} parts become str(expression) calls, and the parts are
   joined with '+', which the existing code generator and runtime already
   implement for strings. '{{' and '}}' produce literal braces. */
static Expr *fstring_expr(SourcePos pos, const char *raw, const char *file) {
    Expr *result = NULL;
    size_t cap = 32, n = 0;
    char *lit = xmalloc(cap);
    size_t i = 0;
    while (raw[i]) {
        if (raw[i] == '{' && raw[i + 1] == '{') { str_push(&lit, &n, &cap, '{'); i += 2; continue; }
        if (raw[i] == '}' && raw[i + 1] == '}') { str_push(&lit, &n, &cap, '}'); i += 2; continue; }
        if (raw[i] == '}') fatal_at(pos, "single '}' is not allowed in an f-string; write '}}'");
        if (raw[i] != '{') { str_push(&lit, &n, &cap, raw[i]); i++; continue; }

        if (n) {
            Expr *s = fstring_literal_expr(pos, lit, n);
            result = result ? mk_binary(pos, B_ADD, result, s) : s;
            n = 0;
        }
        size_t j = i + 1, depth = 0;
        while (raw[j]) {
            if (raw[j] == '{') depth++;
            else if (raw[j] == '}') { if (!depth) break; depth--; }
            j++;
        }
        if (!raw[j]) fatal_at(pos, "unterminated '{' in an f-string");
        size_t colon=(size_t)-1;
        for (size_t k = i + 1; k < j; k++) {
            if (raw[k] == ':') { colon = k; break; }
        }
        if (colon != (size_t)-1) {
            /* Format specs need runtime helpers in both back ends; until they land,
               say exactly what was written and what is planned. */
            char *spec = xstrndup(raw + colon, j - colon);
            fatal_at(pos, "format spec '%s' is not implemented yet (planned: {x:.2f}, {x:x}, {x:>8})", spec);
        }
        size_t expr_len = j - i - 1;
        if (!expr_len) fatal_at(pos, "empty expression in an f-string");

        char *text = xstrndup(raw + i + 1, expr_len);
        size_t nt = 0;
        Token *toks = lex_all(text, file, &nt);
        Parser sub = { toks, nt, 0, file };
        Expr *inner = parse_expr(&sub);
        if (!p_is(&sub, TOK_EOF)) fatal_at(pos, "unexpected token in an f-string expression");
        free_tokens(toks, nt);
        free(text);

        ExprVec args = exprvec_new();
        exprvec_push(&args, inner);
        Expr *call = new_expr(pos, EX_CALL);
        call->as.call.callee = xstrdup("str");
        call->as.call.args = xmalloc(sizeof(ExprVec));
        *call->as.call.args = args;
        result = result ? mk_binary(pos, B_ADD, result, call) : call;
        i = j + 1;
    }
    if (n) {
        Expr *s = fstring_literal_expr(pos, lit, n);
        result = result ? mk_binary(pos, B_ADD, result, s) : s;
    }
    free(lit);
    if (!result) {
        result = new_expr(pos, EX_STR);
        result->as.s = xstrdup("");
    }
    return result;
}

/* A string literal expression, used for container keys. */
static Expr *new_str_expr(SourcePos pos, const char *s) {
    Expr *e = new_expr(pos, EX_STR);
    e->as.s = xstrdup(s);
    return e;
}

/* struct declarations: compile-time tables, no runtime type objects. */
#define RBL_MAX_STRUCTS 64
static char *g_struct_name[RBL_MAX_STRUCTS];
static char **g_struct_fields[RBL_MAX_STRUCTS];
static size_t g_struct_nfields[RBL_MAX_STRUCTS];
static size_t g_struct_count = 0;

static int struct_lookup(const char *name) {
    for (size_t i = 0; i < g_struct_count; i++) {
        if (!strcmp(g_struct_name[i], name)) return (int)i;
    }
    return -1;
}

/* struct Point ( x int  y int ) — instances are dictionaries with these string
   keys, so fields and methods need no new object model in either back end. */
static Stmt *parse_struct(Parser *p) {
    SourcePos pos = p_adv(p).pos;               /* the 'struct' identifier */
    char *name = p_ident(p, "struct name");
    if (struct_lookup(name) >= 0) fatal_at(pos, "struct '%s' is already declared", name);
    if (g_struct_count >= RBL_MAX_STRUCTS) fatal_at(pos, "too many struct declarations (max %d)", RBL_MAX_STRUCTS);
    p_expect(p,TOK_LPAREN,"struct body");
    char **fields = NULL;
    size_t n = 0, cap = 0;
    while (!p_is(p,TOK_RPAREN)) {
        if (p_is(p,TOK_EOF)) fatal_at(p_peek(p)->pos,"unterminated struct body");
        char *fname = p_ident(p, "field name");
        char *ftype = p_ident(p, "field type");   /* recorded for readers, values stay dynamic */
        (void)ftype;
        if (n == cap) { cap = cap ? cap * 2 : 8; fields = xrealloc(fields, cap * sizeof(char *)); }
        fields[n++] = fname;
    }
    p_expect(p,TOK_RPAREN,"struct body end");
    if (!n) fatal_at(pos, "struct '%s' needs at least one field", name);
    g_struct_name[g_struct_count] = name;
    g_struct_fields[g_struct_count] = fields;
    g_struct_nfields[g_struct_count] = n;
    g_struct_count++;
    /* A declaration produces no code; an integer literal statement is a no-op. */
    Expr *z = new_expr(pos, EX_INT);
    z->as.i = 0;
    Stmt *st = new_stmt(pos, ST_EXPR);
    st->as.expr = z;
    return st;
}

/* Build a call to an internal runtime entry point (containers use these, so the
   parser needs no new expression kinds and the code generator needs no new nodes). */
static Expr *builtin_call0(SourcePos pos, const char *name) {
    Expr *c = new_expr(pos, EX_CALL);
    c->as.call.callee = xstrdup(name);
    c->as.call.args = xmalloc(sizeof(ExprVec));
    *c->as.call.args = exprvec_new();
    return c;
}
static Expr *builtin_call1(SourcePos pos, const char *name, Expr *a) {
    Expr *c = builtin_call0(pos, name);
    exprvec_push(c->as.call.args, a);
    return c;
}
static Expr *builtin_call2(SourcePos pos, const char *name, Expr *a, Expr *b) {
    Expr *c = builtin_call1(pos, name, a);
    exprvec_push(c->as.call.args, b);
    return c;
}
static Expr *builtin_call3(SourcePos pos, const char *name, Expr *a, Expr *b, Expr *c3) {
    Expr *c = builtin_call2(pos, name, a, b);
    exprvec_push(c->as.call.args, c3);
    return c;
}

/* Primary expression plus its postfix forms, so `a[i]` chains like any other
   primary (parse_atom builds the base, indexing wraps it). */
static Expr *parse_primary(Parser *p) {
    Expr *e = parse_atom(p);
    while (p_is(p,TOK_LBRACKET)) {
        SourcePos pos = p_adv(p).pos;
        Expr *idx = parse_expr(p);
        p_expect(p,TOK_RBRACKET,"]");
        e = builtin_call2(pos,"index_get",e,idx);
    }
    return e;
}

static Expr *parse_atom(Parser *p) {
    Token t=p_adv(p);
    switch(t.kind) {
        case TOK_INT: { Expr *e=new_expr(t.pos,EX_INT); e->as.i=t.i; return e; }
        case TOK_FLOAT: { Expr *e=new_expr(t.pos,EX_FLOAT); e->as.f=t.f; return e; }
        case TOK_STR: { Expr *e=new_expr(t.pos,EX_STR); e->as.s=xstrdup(t.text); return e; }
        case TOK_FSTR: return fstring_expr(t.pos, t.text, p->file);
        case TOK_TRUE: case TOK_FALSE: { Expr *e=new_expr(t.pos,EX_BOOL); e->as.b=(t.kind==TOK_TRUE); return e; }
        /* null is its own value (tag 8), distinct from a function that returns
           nothing (Unit, tag 0), so the two print differently. */
        case TOK_NULL: return builtin_call0(t.pos,"null_value");
        case TOK_LBRACKET: {
            /* [a, b] -> list_push(list_push(list_new(), a), b) */
            Expr *list = builtin_call0(t.pos,"list_new");
            if (!p_is(p,TOK_RBRACKET)) {
                for (;;) {
                    list = builtin_call2(t.pos,"list_push",list,parse_expr(p));
                    if (p_is(p,TOK_COMMA)) { p_adv(p); if (p_is(p,TOK_RBRACKET)) break; continue; }
                    break;
                }
            }
            p_expect(p,TOK_RBRACKET,"]");
            return list;
        }
        case TOK_LBRACE: {
            /* {k: v, ...} -> dict_set(dict_set(dict_new(), k, v), ...); {} is empty. */
            Expr *dict = builtin_call0(t.pos,"dict_new");
            if (!p_is(p,TOK_RBRACE)) {
                for (;;) {
                    Expr *k = parse_expr(p);
                    p_expect(p,TOK_COLON,":");
                    Expr *v = parse_expr(p);
                    dict = builtin_call3(t.pos,"dict_set",dict,k,v);
                    if (p_is(p,TOK_COMMA)) { p_adv(p); if (p_is(p,TOK_RBRACE)) break; continue; }
                    break;
                }
            }
            p_expect(p,TOK_RBRACE,"}");
            return dict;
        }
        case TOK_LPAREN: {
            /* (a) is grouping; (a, b) and () are tuples. */
            if (p_is(p,TOK_RPAREN)) { p_adv(p); return builtin_call0(t.pos,"tuple_new"); }
            Expr *first=parse_expr(p);
            if (!p_is(p,TOK_COMMA)) { p_expect(p,TOK_RPAREN,")"); return first; }
            Expr *tup=builtin_call2(t.pos,"tuple_push",builtin_call0(t.pos,"tuple_new"),first);
            while (p_is(p,TOK_COMMA)) {
                p_adv(p);
                if (p_is(p,TOK_RPAREN)) break;
                tup=builtin_call2(t.pos,"tuple_push",tup,parse_expr(p));
            }
            p_expect(p,TOK_RPAREN,")");
            return tup;
        }
        case TOK_IDENT: {
            char *name=xstrdup(t.text);
            if (p_is(p,TOK_LPAREN)) {
                p_adv(p);
                ExprVec args=exprvec_new();
                if (!p_is(p,TOK_RPAREN)) {
                    for (;;) {
                        exprvec_push(&args,parse_expr(p));
                        if(p_is(p,TOK_COMMA)){p_adv(p);continue;}
                        break;
                    }
                }
                p_expect(p,TOK_RPAREN,"call arguments");
                /* clamp(x, lo, hi) -> min(max(x, lo), hi): built from the existing
                   variadic min/max builtins, and every argument is evaluated once. */
                if (!strcmp(name,"clamp") && args.len==3) {
                    Expr *mx = new_expr(t.pos, EX_CALL);
                    mx->as.call.callee = xstrdup("max");
                    mx->as.call.args = xmalloc(sizeof(ExprVec));
                    *mx->as.call.args = exprvec_new();
                    exprvec_push(mx->as.call.args, args.data[0]);
                    exprvec_push(mx->as.call.args, args.data[1]);
                    Expr *mn = new_expr(t.pos, EX_CALL);
                    mn->as.call.callee = xstrdup("min");
                    mn->as.call.args = xmalloc(sizeof(ExprVec));
                    *mn->as.call.args = exprvec_new();
                    exprvec_push(mn->as.call.args, mx);
                    exprvec_push(mn->as.call.args, args.data[2]);
                    return mn;
                }
                int si = struct_lookup(name);
                if (si >= 0) {
                    if (args.len != g_struct_nfields[si])
                        fatal_at(t.pos,"struct '%s' has %zu field(s), got %zu",name,g_struct_nfields[si],args.len);
                    Expr *dict = builtin_call0(t.pos,"dict_new");
                    for (size_t bi=0; bi<args.len; bi++) {
                        dict = builtin_call3(t.pos,"dict_set",dict,new_str_expr(t.pos,g_struct_fields[si][bi]),args.data[bi]);
                    }
                    return dict;
                }
                /* degrees()/radians() are pure unit conversions: rewrite them to
                   float(x) * const at parse time, so no runtime entry point is
                   needed and int arguments are converted by float(). */
                if ((!strcmp(name,"degrees") || !strcmp(name,"radians")) && args.len==1) {
                    ExprVec conv = exprvec_new();
                    exprvec_push(&conv, args.data[0]);
                    Expr *f = new_expr(t.pos, EX_CALL);
                    f->as.call.callee = xstrdup("float");
                    f->as.call.args = xmalloc(sizeof(ExprVec));
                    *f->as.call.args = conv;
                    Expr *k = new_expr(t.pos, EX_FLOAT);
                    k->as.f = !strcmp(name,"degrees") ? 57.29577951308232 : 0.017453292519943295;
                    return mk_binary(t.pos, B_MUL, f, k);
                }
                Expr *e=new_expr(t.pos,EX_CALL); e->as.call.callee=name; e->as.call.args=&(ExprVec){0};
                e->as.call.args=xmalloc(sizeof(ExprVec)); *e->as.call.args=args; return e;
            }
            if (p_is(p,TOK_DOT)) {
                char *object = name;
                char *method = NULL;
                while (p_is(p,TOK_DOT)) {
                    p_adv(p);
                    char *part = p_ident(p, "qualified name segment");
                    free(method);
                    method = part;
                    if (p_is(p, TOK_DOT)) {
                        size_t no = strlen(object), nm = strlen(method);
                        char *joined = xmalloc(no + nm + 2);
                        memcpy(joined, object, no);
                        joined[no] = '.';
                        memcpy(joined + no + 1, method, nm + 1);
                        free(object);
                        object = joined;
                    }
                }
                /* Not a namespace: a struct field read (`p.x`) or a receiver method
                   call (`p.move(dx)`). Both resolve without knowing p's type: fields
                   are dictionary keys, and `p.move(dx)` becomes `move(p, dx)`. */
                if (strncmp(object,"rbl.",4)!=0 && strcmp(object,"warn") && strcmp(object,"error")) {
                    if (strchr(object,'.')) fatal_at(t.pos,"unknown namespace '%s'",object);
                    Expr *obj = new_expr(t.pos, EX_IDENT);
                    obj->as.ident = xstrdup(object);
                    if (!p_is(p,TOK_LPAREN)) return builtin_call2(t.pos,"index_get",obj,new_str_expr(t.pos,method));
                    p_adv(p);
                    ExprVec margs = exprvec_new();
                    exprvec_push(&margs,obj);
                    if (!p_is(p,TOK_RPAREN)) {
                        for(;;){
                            exprvec_push(&margs,parse_expr(p));
                            if(p_is(p,TOK_COMMA)){p_adv(p);continue;}
                            break;
                        }
                    }
                    p_expect(p,TOK_RPAREN,"method arguments end");
                    Expr *call=new_expr(t.pos,EX_CALL);
                    call->as.call.callee=xstrdup(method);
                    call->as.call.args=xmalloc(sizeof(ExprVec));
                    *call->as.call.args=margs;
                    return call;
                }
                /* Constants: a qualified name without '(' is a value, not a call.
                   rbl.math.pi and rbl.math.e are the only ones for now. */
                if (!p_is(p, TOK_LPAREN) && !strcmp(object, "rbl.math")) {
                    double cv;
                    if (!strcmp(method, "pi")) cv = 3.14159265358979323846;
                    else if (!strcmp(method, "e")) cv = 2.71828182845904523536;
                    else fatal_at(t.pos, "unknown constant '%s.%s'", object, method);
                    Expr *k = new_expr(t.pos, EX_FLOAT);
                    k->as.f = cv;
                    return k;
                }
                p_expect(p,TOK_LPAREN,"method arguments");
                ExprVec args=exprvec_new();
                if (!p_is(p,TOK_RPAREN)) {
                    for (;;) {
                        exprvec_push(&args,parse_expr(p));
                        if(p_is(p,TOK_COMMA)){p_adv(p);continue;}
                        break;
                    }
                }
                p_expect(p,TOK_RPAREN,"method arguments end");
                Expr *e=new_expr(t.pos,EX_METHOD); e->as.method.object=object; e->as.method.method=method;
                e->as.method.args=xmalloc(sizeof(ExprVec)); *e->as.method.args=args; return e;
            }
            Expr *e=new_expr(t.pos,EX_IDENT); e->as.ident=name; return e;
        }
        default:
            fatal_at(t.pos,"unexpected token in expression: %s",tok_name(t.kind));
    }
    return NULL;
}

static Expr *parse_unary(Parser *p) {
    if (p_is(p,TOK_MINUS)) { SourcePos pos=p_adv(p).pos; Expr *e=new_expr(pos,EX_UNARY); e->as.unary.op=U_NEG; e->as.unary.expr=parse_unary(p); return e; }
    if (p_is(p,TOK_NOT) || p_is(p,TOK_BANG)) { SourcePos pos=p_adv(p).pos; Expr *e=new_expr(pos,EX_UNARY); e->as.unary.op=U_NOT; e->as.unary.expr=parse_unary(p); return e; }
    return parse_primary(p);
}
static Expr *parse_mul(Parser *p) {
    Expr *left=parse_unary(p);
    for(;;){ BinaryOp op; SourcePos pos=p_peek(p)->pos; if(p_is(p,TOK_STAR))op=B_MUL; else if(p_is(p,TOK_SLASH))op=B_DIV; else if(p_is(p,TOK_PERCENT))op=B_MOD; else break; p_adv(p); Expr *r=parse_unary(p); Expr *e=new_expr(pos,EX_BINARY); e->as.binary.left=left;e->as.binary.op=op;e->as.binary.right=r;left=e; }
    return left;
}
static Expr *parse_add(Parser *p) {
    Expr *left=parse_mul(p);
    for(;;){ BinaryOp op; SourcePos pos=p_peek(p)->pos; if(p_is(p,TOK_PLUS))op=B_ADD; else if(p_is(p,TOK_MINUS))op=B_SUB; else break; p_adv(p); Expr *r=parse_mul(p); Expr *e=new_expr(pos,EX_BINARY); e->as.binary.left=left;e->as.binary.op=op;e->as.binary.right=r;left=e; }
    return left;
}
static Expr *parse_range(Parser *p) {
    Expr *start=parse_add(p);
    if(p_is(p,TOK_DOTDOT) || p_is(p,TOK_DOTDOTEQ)) { bool inc=p_is(p,TOK_DOTDOTEQ); SourcePos pos=p_adv(p).pos; Expr *end=parse_add(p); Expr *e=new_expr(pos,EX_RANGE); e->as.range.start=start;e->as.range.end=end;e->as.range.inclusive=inc; return e; }
    return start;
}
static Expr *parse_cmp(Parser *p) {
    Expr *left=parse_range(p);
    for(;;){ BinaryOp op; if(p_is(p,TOK_LT))op=B_LT; else if(p_is(p,TOK_GT))op=B_GT; else if(p_is(p,TOK_LE))op=B_LE; else if(p_is(p,TOK_GE))op=B_GE; else break; p_adv(p); Expr *r=parse_range(p); Expr *e=new_expr(left->pos,EX_BINARY); e->as.binary.left=left;e->as.binary.op=op;e->as.binary.right=r;left=e; }
    return left;
}
static Expr *parse_eq(Parser *p) {
    Expr *left=parse_cmp(p);
    for(;;){ if(p_is(p,TOK_IN)){ p_adv(p); Expr *r=parse_cmp(p); left=builtin_call2(left->pos,"contains",r,left); continue; } BinaryOp op; if(p_is(p,TOK_EQ))op=B_EQ; else if(p_is(p,TOK_NE))op=B_NE; else if(p_is(p,TOK_IS))op=B_IS; else break; p_adv(p); Expr *r=parse_cmp(p); Expr *e=new_expr(left->pos,EX_BINARY); e->as.binary.left=left;e->as.binary.op=op;e->as.binary.right=r;left=e; }
    return left;
}
static Expr *parse_and(Parser *p) {
    Expr *left=parse_eq(p);
    while(p_is(p,TOK_AND)){p_adv(p); Expr *r=parse_eq(p); Expr *e=new_expr(left->pos,EX_BINARY);e->as.binary.left=left;e->as.binary.op=B_AND;e->as.binary.right=r;left=e;}
    return left;
}
static Expr *parse_or(Parser *p) {
    Expr *left=parse_and(p);
    while(p_is(p,TOK_OR)){p_adv(p); Expr *r=parse_and(p); Expr *e=new_expr(left->pos,EX_BINARY);e->as.binary.left=left;e->as.binary.op=B_OR;e->as.binary.right=r;left=e;}
    return left;
}
static Expr *parse_expr(Parser *p) {
    Expr *e=parse_or(p);
    /* Conditional expression, Python style and lowest precedence:
       `a if (cond) else b`. Only the taken branch is evaluated, so
       `x if (ok) else risky()` never runs risky() when ok is true. */
    /* The `if` must sit on the same source line as the expression it qualifies.
       Statements are not separated by a terminator, so without this check the
       `if` of the *next* statement would be mistaken for a ternary. */
    if(p_is(p,TOK_IF) && p->pos>0 && p->toks[p->pos-1].pos.line==p->toks[p->pos].pos.line){
        SourcePos pos=p_adv(p).pos;
        p_expect(p,TOK_LPAREN,"(");
        Expr *cond=parse_expr(p);
        p_expect(p,TOK_RPAREN,")");
        p_expect(p,TOK_ELSE,"else");
        Expr *els=parse_expr(p);
        Expr *t=new_expr(pos,EX_TERNARY);
        t->as.tern.cond=cond; t->as.tern.then_expr=e; t->as.tern.else_expr=els;
        return t;
    }
    return e;
}

static Stmt *parse_stmt(Parser *p) {
    if(p_is(p,TOK_SET)){
        SourcePos pos=p_adv(p).pos; char *name=p_ident(p,"variable name"); p_expect(p,TOK_ASSIGN,"="); Expr *v=parse_expr(p); Stmt *s=new_stmt(pos,ST_SET);s->as.assign.name=name;s->as.assign.value=v;return s;
    }
    if(p_is(p,TOK_LET)){
        SourcePos pos=p_adv(p).pos;
        /* let ++i / let --i : prefix increment. Increments are statements only,
           so the prefix and postfix forms are equivalent. */
        if(p_two_adjacent(p,TOK_PLUS,TOK_PLUS) || p_two_adjacent(p,TOK_MINUS,TOK_MINUS)){
            bool inc=p_is(p,TOK_PLUS); p_adv(p); p_adv(p);
            char *nm=p_ident(p,"variable name");
            Stmt *s=new_stmt(pos,ST_LET); s->as.assign.name=nm; s->as.assign.value=mk_self_op(pos,nm,inc?B_ADD:B_SUB);
            return s;
        }
        char *name=p_ident(p,"variable name");
        /* let a[i] = v mutates a list element; it is not a rebinding. */
        if(p_is(p,TOK_DOT)){
            /* let p.x = v mutates a struct field (a dictionary key). */
            p_adv(p);
            char *f=p_ident(p,"field name");
            p_expect(p,TOK_ASSIGN,"=");
            Expr *target=new_expr(pos,EX_IDENT); target->as.ident=xstrdup(name);
            Expr *set=builtin_call3(pos,"index_set",target,new_str_expr(pos,f),parse_expr(p));
            Stmt *st=new_stmt(pos,ST_EXPR); st->as.expr=set; return st;
        }
        if(p_is(p,TOK_LBRACKET)){
            p_adv(p);
            Expr *idx=parse_expr(p);
            p_expect(p,TOK_RBRACKET,"]");
            p_expect(p,TOK_ASSIGN,"=");
            Expr *target=new_expr(pos,EX_IDENT); target->as.ident=xstrdup(name);
            Expr *set=builtin_call3(pos,"index_set",target,idx,parse_expr(p));
            Stmt *st=new_stmt(pos,ST_EXPR); st->as.expr=set; return st;
        }
        /* let i += e, -=, *=, /=, %= desugar to let i = i <op> e */
        BinaryOp cop;
        bool compound=false;
        if(p_two_adjacent(p,TOK_PLUS,TOK_ASSIGN)){cop=B_ADD;compound=true;}
        else if(p_two_adjacent(p,TOK_MINUS,TOK_ASSIGN)){cop=B_SUB;compound=true;}
        else if(p_two_adjacent(p,TOK_STAR,TOK_ASSIGN)){cop=B_MUL;compound=true;}
        else if(p_two_adjacent(p,TOK_SLASH,TOK_ASSIGN)){cop=B_DIV;compound=true;}
        else if(p_two_adjacent(p,TOK_PERCENT,TOK_ASSIGN)){cop=B_MOD;compound=true;}
        if(compound){
            p_adv(p); p_adv(p);
            Expr *rhs=parse_expr(p);
            Stmt *s=new_stmt(pos,ST_LET); s->as.assign.name=name; s->as.assign.value=mk_self_op_expr(pos,name,cop,rhs);
            return s;
        }
        /* let i++ / let i-- */
        if(p_two_adjacent(p,TOK_PLUS,TOK_PLUS) || p_two_adjacent(p,TOK_MINUS,TOK_MINUS)){
            bool inc=p_is(p,TOK_PLUS); p_adv(p); p_adv(p);
            Stmt *s=new_stmt(pos,ST_LET); s->as.assign.name=name; s->as.assign.value=mk_self_op(pos,name,inc?B_ADD:B_SUB);
            return s;
        }
        p_expect(p,TOK_ASSIGN,"="); Expr *v=parse_expr(p); Stmt *s=new_stmt(pos,ST_LET);s->as.assign.name=name;s->as.assign.value=v;return s;
    }
    if(p_is(p,TOK_RETURN)){
        SourcePos pos=p_adv(p).pos; Expr *v=NULL; if(!p_is(p,TOK_RPAREN) && !p_is(p,TOK_RBRACE))v=parse_expr(p); Stmt *s=new_stmt(pos,ST_RETURN);s->as.ret=v;return s;
    }
    if(p_is(p,TOK_IF)){
        SourcePos pos=p_adv(p).pos; IfStmt ifs={0};
        p_expect(p,TOK_LPAREN,"if condition start");
        ifs.cond=parse_expr(p);
        p_expect(p,TOK_RPAREN,"if condition end");
        ifs.then_block=parse_block(p);
        while(p_is(p,TOK_ELIF)){
            p_adv(p); Elif e={0};
            p_expect(p,TOK_LPAREN,"elif condition start");
            e.cond=parse_expr(p);
            p_expect(p,TOK_RPAREN,"elif condition end");
            e.body=parse_block(p);
            elif_push(&ifs,e);
        }
        if(p_is(p,TOK_ELSE)){p_adv(p);ifs.has_else=true;ifs.else_block=parse_block(p);}
        Stmt *s=new_stmt(pos,ST_IF);s->as.ifs=ifs;return s;
    }
    if(p_is(p,TOK_FOR)){
        SourcePos pos=p_adv(p).pos;
        p_expect(p,TOK_LPAREN,"for header start");
        char *var=NULL;
        if(p_is(p,TOK_IDENT) && p->pos+1<p->len && p->toks[p->pos+1].kind==TOK_IN){var=p_ident(p,"loop variable");p_expect(p,TOK_IN,"in");}
        Expr *range = var ? parse_range(p) : parse_expr(p);
        p_expect(p,TOK_RPAREN,"for header end");
        StmtVec body=parse_block(p);
        Stmt *s=new_stmt(pos,ST_FOR);s->as.fors.var=var;s->as.fors.range=range;s->as.fors.body=body;return s;
    }
    if(p_is(p,TOK_BREAK)){SourcePos pos=p_adv(p).pos;return new_stmt(pos,ST_BREAK);}
    if(p_is(p,TOK_CONTINUE)){SourcePos pos=p_adv(p).pos;return new_stmt(pos,ST_CONTINUE);}
    /* 'switch' is a contextual keyword: only at statement start does it start a
       switch, so it stays usable as an ordinary identifier elsewhere. */
    if(p_is(p,TOK_IDENT) && !strcmp(p_peek(p)->text,"switch")) return parse_switch(p);
    /* 'struct' is contextual in the same way. */
    if(p_is(p,TOK_IDENT) && !strcmp(p_peek(p)->text,"struct")) return parse_struct(p);
    Expr *e=parse_expr(p);
    /* `a[i] = v` is element assignment, not a rebinding: it becomes list_set. */
    if(p_is(p,TOK_ASSIGN) && e->kind==EX_CALL && e->as.call.callee && !strcmp(e->as.call.callee,"index_get") && e->as.call.args->len==2){
        p_adv(p);
        Expr *v=parse_expr(p);
        Expr *set=builtin_call3(e->pos,"index_set",e->as.call.args->data[0],e->as.call.args->data[1],v);
        Stmt *st=new_stmt(e->pos,ST_EXPR); st->as.expr=set; return st;
    }
    Stmt *s=new_stmt(e->pos,ST_EXPR);s->as.expr=e;return s;
}

static StmtVec parse_block(Parser *p) {
    /* Parentheses are the only block delimiter in RBL 0.8. Braces are reserved
       for dictionary literals, so '{' is reported explicitly instead of being
       silently accepted as legacy syntax. */
    if (p_is(p,TOK_LBRACE)) fatal_at(p_peek(p)->pos,"braces are not a block delimiter in RBL 0.8; use '(' and ')' (braces are reserved for dictionary literals)");
    p_expect(p,TOK_LPAREN,"block start");
    StmtVec v=stmtvec_new();
    while(!p_is(p,TOK_RPAREN)) {
        if(p_is(p,TOK_EOF)) fatal_at(p_peek(p)->pos,"unterminated block");
        stmtvec_push(&v,parse_stmt(p));
    }
    p_expect(p,TOK_RPAREN,"block end"); return v;
}

static Function parse_function(Parser *p) {
    SourcePos pos=p_adv(p).pos;
    char *name=NULL; ParamVec params=paramvec_new();
    /* Receiver method: `func (p Point) move(dx int) (...)`. The receiver becomes
       the first parameter, so the call `p.move(dx)` is simply `move(p, dx)` and no
       method tables or type knowledge are needed. */
    if(p_is(p,TOK_LPAREN)){
        p_adv(p);
        Param recv={0}; recv.pos=p_peek(p)->pos;
        recv.name=p_ident(p,"receiver name");
        char *rtype=p_ident(p,"receiver type");
        (void)rtype; /* recorded for readers; a struct instance is a dictionary today */
        recv.type=xstrdup(""); /* no runtime type check: the receiver is dispatched by name */
        paramvec_push(&params,recv);
        p_expect(p,TOK_RPAREN,"receiver end");
    }
    name=p_ident(p,"function name"); p_expect(p,TOK_LPAREN,"function parameters");
    if(!p_is(p,TOK_RPAREN)){
        for(;;){Param q={0};q.pos=p_peek(p)->pos;q.name=p_ident(p,"parameter name");q.type=p_ident(p,"parameter type");paramvec_push(&params,q);if(p_is(p,TOK_COMMA)){p_adv(p);continue;}break;}
    }
    p_expect(p,TOK_RPAREN,"function parameters end"); Function f={0};f.pos=pos;f.name=name;f.params=params;
    if(!p_is(p,TOK_LBRACE) && !p_is(p,TOK_LPAREN)){f.has_return_type=true;f.return_type=p_ident(p,"return type");}
    f.body=parse_block(p); return f;
}

static Program parse_program(Token *toks, size_t n, const char *file) {
    Parser p={toks,n,0,file}; Program prog={0};
    while(!p_is(&p,TOK_EOF)){
        if(p_is(&p,TOK_IDENT) && !strcmp(p_peek(&p)->text,"struct")){ (void)parse_struct(&p); continue; }
        if(!p_is(&p,TOK_FUNC)) fatal_at(p_peek(&p)->pos,"top-level declaration must start with func or struct");
        funcvec_push(&prog.funcs,parse_function(&p));
    }
    return prog;
}

/* ========================== CODEGEN SUPPORT ========================== */

typedef enum { ST_UNKNOWN, ST_INT, ST_FLOAT, ST_BOOL, ST_STR, ST_UNIT } StaticType;

typedef struct {
    char *name;
    StaticType type;
    bool definitely_bound;
} Var;
typedef struct { Var *data; size_t len, cap; } VarVec;

static int var_find(VarVec *v,const char *name){for(size_t i=0;i<v->len;i++)if(!strcmp(v->data[i].name,name))return (int)i;return -1;}
static int var_get_or_add(VarVec *v,const char *name){
    int i=var_find(v,name);if(i>=0)return i;
    if(v->len==v->cap){v->cap=v->cap?v->cap*2:8;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));}
    v->data[v->len].name=xstrdup(name);v->data[v->len].type=ST_UNKNOWN;v->data[v->len].definitely_bound=false;
    return (int)v->len++;
}
static StaticType type_from_name(const char *s){
    if(!s)return ST_UNKNOWN;
    if(!strcmp(s,"Int")||!strcmp(s,"int")||!strcmp(s,"integer"))return ST_INT;
    if(!strcmp(s,"Float")||!strcmp(s,"float"))return ST_FLOAT;
    if(!strcmp(s,"Bool")||!strcmp(s,"bool"))return ST_BOOL;
    if(!strcmp(s,"String")||!strcmp(s,"string")||!strcmp(s,"Str")||!strcmp(s,"str"))return ST_STR;
    if(!strcmp(s,"Unit")||!strcmp(s,"unit"))return ST_UNIT;
    return ST_UNKNOWN;
}
static StaticType function_return_static(Program *p,const char *name){
    int fi=-1;for(size_t i=p->funcs.len;i>0;i--)if(!strcmp(p->funcs.data[i-1].name,name)){fi=(int)(i-1);break;}
    if(fi<0)return ST_UNKNOWN;
    Function *f=&p->funcs.data[fi];return f->has_return_type?type_from_name(f->return_type):ST_UNKNOWN;
}

static StaticType infer_expr_type(Program *p, Expr *e, VarVec *vars);

static StaticType builtin_static_type(Program *p, Expr *e, VarVec *vars){
    (void)p;
    if(!e || e->kind!=EX_CALL) return ST_UNKNOWN;
    const char *n=e->as.call.callee;
    size_t argc=e->as.call.args->len;
    if(!strcmp(n,"print")) return ST_UNIT;
    if(!strcmp(n,"len")) return argc==1 ? ST_INT : ST_UNKNOWN;
    if(!strcmp(n,"input") || !strcmp(n,"read_file")) return ST_STR;
    if(!strcmp(n,"write_file")) return ST_UNIT;
    if(!strcmp(n,"int")) return argc==1 ? ST_INT : ST_UNKNOWN;
    if(!strcmp(n,"float")) return argc==1 ? ST_FLOAT : ST_UNKNOWN;
    if(!strcmp(n,"str")) return argc==1 ? ST_STR : ST_UNKNOWN;
    if(!strcmp(n,"sign")) return argc==1 ? ST_INT : ST_UNKNOWN;
    if(!strcmp(n,"is_nan") || !strcmp(n,"is_inf")) return argc==1 ? ST_BOOL : ST_UNKNOWN;
    if(!strcmp(n,"sqrt") || !strcmp(n,"floor") || !strcmp(n,"ceil") || !strcmp(n,"round") || !strcmp(n,"sin") || !strcmp(n,"cos") || !strcmp(n,"tan") || !strcmp(n,"log") || !strcmp(n,"exp") || !strcmp(n,"log10") || !strcmp(n,"log2") || !strcmp(n,"trunc")) return argc==1 ? ST_FLOAT : ST_UNKNOWN;
    if(!strcmp(n,"pow") || !strcmp(n,"fmod") || !strcmp(n,"hypot")) return argc==2 ? ST_FLOAT : ST_UNKNOWN;
    if(!strcmp(n,"abs")) {
        if(argc!=1) return ST_UNKNOWN;
        return infer_expr_type(p,e->as.call.args->data[0],vars);
    }
    if(!strcmp(n,"min") || !strcmp(n,"max")) {
        if(argc==0) return ST_UNKNOWN;
        StaticType t=infer_expr_type(p,e->as.call.args->data[0],vars);
        for(size_t i=1;i<argc;i++){ StaticType q=infer_expr_type(p,e->as.call.args->data[i],vars); if(q!=t) return ST_UNKNOWN; }
        return (t==ST_INT||t==ST_FLOAT) ? t : ST_UNKNOWN;
    }
    if(!strcmp(n,"random_int")) return argc==2 ? ST_INT : ST_UNKNOWN;
    if(!strcmp(n,"random_float")) return argc==0 ? ST_FLOAT : ST_UNKNOWN;
    if(!strcmp(n,"random_bool")) return argc==0 ? ST_BOOL : ST_UNKNOWN;
    if(!strcmp(n,"pow")) return argc==2 ? ST_FLOAT : ST_UNKNOWN;
    return ST_UNKNOWN;
}

static StaticType builtin_method_type(Program *p, Expr *e, VarVec *vars){
    if(!e || e->kind!=EX_METHOD) return ST_UNKNOWN;
    const char *o=e->as.method.object, *m=e->as.method.method;
    size_t argc=e->as.method.args->len;
    if((!strcmp(o,"warn")||!strcmp(o,"error")) && !strcmp(m,"log")) return ST_UNIT;
    if(!strcmp(o,"rbl.io")){
        if(!strcmp(m,"input")||!strcmp(m,"prompt")) return ST_STR;
        if(!strcmp(m,"read_file")) return ST_STR;
        if(!strcmp(m,"write_file")) return ST_UNIT;
    }
    if(!strcmp(o,"rbl.math")){
        if(!strcmp(m,"sqrt")||!strcmp(m,"pow")||!strcmp(m,"sin")||!strcmp(m,"cos")||!strcmp(m,"tan")||!strcmp(m,"log")||!strcmp(m,"exp")||!strcmp(m,"log10")||!strcmp(m,"log2")||!strcmp(m,"trunc")) return ST_FLOAT;
        if(!strcmp(m,"abs")){ if(argc!=1) return ST_UNKNOWN; return infer_expr_type(p,e->as.method.args->data[0],vars); }
        if(!strcmp(m,"floor")||!strcmp(m,"ceil")||!strcmp(m,"round")){ if(argc!=1) return ST_UNKNOWN; return infer_expr_type(p,e->as.method.args->data[0],vars); }
        if(!strcmp(m,"min")||!strcmp(m,"max")){ if(argc==0)return ST_UNKNOWN; StaticType t=infer_expr_type(p,e->as.method.args->data[0],vars); for(size_t i=1;i<argc;i++) if(infer_expr_type(p,e->as.method.args->data[i],vars)!=t)return ST_UNKNOWN; return (t==ST_INT||t==ST_FLOAT)?t:ST_UNKNOWN; }
    }
    if(!strcmp(o,"rbl.string")){
        if(!strcmp(m,"len")) return argc==1?ST_INT:ST_UNKNOWN;
        if(!strcmp(m,"upper")||!strcmp(m,"lower")||!strcmp(m,"trim")) return argc==1?ST_STR:ST_UNKNOWN;
        if(!strcmp(m,"contains")||!strcmp(m,"starts_with")||!strcmp(m,"ends_with")) return argc==2?ST_BOOL:ST_UNKNOWN;
    }
    if(!strcmp(o,"rbl.fs")){
        if(!strcmp(m,"exists")||!strcmp(m,"delete")) return argc==1?ST_BOOL:ST_UNKNOWN;
        if(!strcmp(m,"cwd")) return argc==0?ST_STR:ST_UNKNOWN;
    }
    if(!strcmp(o,"rbl.time")){
        if(!strcmp(m,"now_ms")) return argc==0?ST_INT:ST_UNKNOWN;
        if(!strcmp(m,"sleep_ms")) return ST_UNIT;
    }
    if(!strcmp(o,"rbl.random")){
        if(!strcmp(m,"int")) return argc==2?ST_INT:ST_UNKNOWN;
        if(!strcmp(m,"float")) return argc==0?ST_FLOAT:ST_UNKNOWN;
        if(!strcmp(m,"bool")) return argc==0?ST_BOOL:ST_UNKNOWN;
    }
    return ST_UNKNOWN;
}

static StaticType infer_expr_type(Program *p, Expr *e, VarVec *vars);
static void infer_stmt_seq(Program *p, StmtVec *v, VarVec *vars, bool controlled);
static bool function_is_fast_int(Program *p, int fi, int depth);

static StaticType infer_expr_type(Program *p, Expr *e, VarVec *vars){
    if(!e)return ST_UNIT;
    switch(e->kind){
        case EX_INT:return ST_INT;
        case EX_FLOAT:return ST_FLOAT;
        case EX_STR:return ST_STR;
        case EX_BOOL:return ST_BOOL;
        case EX_IDENT:{int i=var_find(vars,e->as.ident);return i>=0?vars->data[i].type:ST_UNKNOWN;}
        case EX_UNARY:{StaticType t=infer_expr_type(p,e->as.unary.expr,vars);if(e->as.unary.op==U_NEG)return (t==ST_INT||t==ST_FLOAT)?t:ST_UNKNOWN;return t==ST_BOOL?ST_BOOL:ST_UNKNOWN;}
        case EX_BINARY:{
            StaticType a=infer_expr_type(p,e->as.binary.left,vars), b=infer_expr_type(p,e->as.binary.right,vars);
            switch(e->as.binary.op){
                case B_ADD: if(a==ST_INT&&b==ST_INT)return ST_INT; if(a==ST_FLOAT&&b==ST_FLOAT)return ST_FLOAT; if(a==ST_STR&&b==ST_STR)return ST_STR; return ST_UNKNOWN;
                case B_SUB: case B_MUL: case B_DIV: if(a==ST_INT&&b==ST_INT)return ST_INT; if(a==ST_FLOAT&&b==ST_FLOAT)return ST_FLOAT; return ST_UNKNOWN;
                /* % is integer-only by language decision: float % int and float % float stay
                   ST_UNKNOWN, so they take the tagged path and fail at runtime with a message. */
                case B_MOD: return (a==ST_INT&&b==ST_INT)?ST_INT:ST_UNKNOWN;
                case B_EQ: case B_NE: case B_IS: case B_LT: case B_GT: case B_LE: case B_GE: return (a!=ST_UNKNOWN&&a==b)?ST_BOOL:ST_UNKNOWN;
                case B_AND: case B_OR: return (a==ST_BOOL&&b==ST_BOOL)?ST_BOOL:ST_UNKNOWN;
            }
            return ST_UNKNOWN;
        }
        case EX_CALL:{
            StaticType bt=builtin_static_type(p,e,vars);
            if(bt!=ST_UNKNOWN || !strcmp(e->as.call.callee,"input") || !strcmp(e->as.call.callee,"read_file") || !strcmp(e->as.call.callee,"write_file")) return bt;
            return function_return_static(p,e->as.call.callee);
        }
        case EX_METHOD:{ StaticType mt=builtin_method_type(p,e,vars); return mt==ST_UNKNOWN?ST_UNIT:mt; }
        case EX_TERNARY:{(void)infer_expr_type(p,e->as.tern.cond,vars);StaticType t1=infer_expr_type(p,e->as.tern.then_expr,vars),t2=infer_expr_type(p,e->as.tern.else_expr,vars);return t1==t2?t1:ST_UNKNOWN;}
        case EX_RANGE:return ST_UNKNOWN;
    }
    return ST_UNKNOWN;
}

static void infer_stmt_seq(Program *p, StmtVec *v, VarVec *vars, bool controlled){
    for(size_t i=0;i<v->len;i++){
        Stmt *s=v->data[i];
        switch(s->kind){
            case ST_SET:{
                int idx=var_get_or_add(vars,s->as.assign.name);StaticType t=infer_expr_type(p,s->as.assign.value,vars);
                if(s->as.assign.value && s->as.assign.value->kind==EX_CALL){
                    int cfi=-1;for(size_t z=p->funcs.len;z>0;z--)if(!strcmp(p->funcs.data[z-1].name,s->as.assign.value->as.call.callee)){cfi=(int)(z-1);break;}
                    if(cfi<0||!function_is_fast_int(p,cfi,0))t=ST_UNKNOWN;
                }
                vars->data[idx].type=t;
                if(!controlled)vars->data[idx].definitely_bound=true;
                break;
            }
            case ST_LET:{
                int idx=var_get_or_add(vars,s->as.assign.name);StaticType t=infer_expr_type(p,s->as.assign.value,vars);
                if(s->as.assign.value && s->as.assign.value->kind==EX_CALL){
                    int cfi=-1;for(size_t z=p->funcs.len;z>0;z--)if(!strcmp(p->funcs.data[z-1].name,s->as.assign.value->as.call.callee)){cfi=(int)(z-1);break;}
                    if(cfi<0||!function_is_fast_int(p,cfi,0))t=ST_UNKNOWN;
                }
                if(vars->data[idx].type==ST_UNKNOWN)vars->data[idx].type=t;
                else if(t!=ST_UNKNOWN&&vars->data[idx].type!=t)vars->data[idx].type=ST_UNKNOWN;
                break;
            }
            case ST_BREAK: case ST_CONTINUE: break;
            case ST_RETURN: (void)infer_expr_type(p,s->as.ret,vars); break;
            case ST_EXPR: (void)infer_expr_type(p,s->as.expr,vars); break;
            case ST_IF:
                (void)infer_expr_type(p,s->as.ifs.cond,vars);
                infer_stmt_seq(p,&s->as.ifs.then_block,vars,true);
                for(size_t j=0;j<s->as.ifs.elif_len;j++){(void)infer_expr_type(p,s->as.ifs.elifs[j].cond,vars);infer_stmt_seq(p,&s->as.ifs.elifs[j].body,vars,true);}
                if(s->as.ifs.has_else)infer_stmt_seq(p,&s->as.ifs.else_block,vars,true);
                break;
            case ST_FOR:{
                /* Condition form (for (cond)): `var` is NULL and `range` holds the
                   condition, so there is no loop variable to bind. */
                if(!s->as.fors.var){
                    (void)infer_expr_type(p,s->as.fors.range,vars);
                    infer_stmt_seq(p,&s->as.fors.body,vars,true);
                    break;
                }
                int idx=var_get_or_add(vars,s->as.fors.var);
                StaticType old_type=vars->data[idx].type; bool old_bound=vars->data[idx].definitely_bound;
                vars->data[idx].type=ST_INT;
                (void)infer_expr_type(p,s->as.fors.range,vars);
                vars->data[idx].definitely_bound=true;
                infer_stmt_seq(p,&s->as.fors.body,vars,true);
                vars->data[idx].type=old_type;
                vars->data[idx].definitely_bound=old_bound;
                break;
            }
        }
    }
}

static void collect_expr_vars(Expr *e, VarVec *vars) {
    if(!e)return;
    switch(e->kind){
        case EX_IDENT: var_get_or_add(vars,e->as.ident); break;
        case EX_UNARY: collect_expr_vars(e->as.unary.expr,vars); break;
        case EX_BINARY: collect_expr_vars(e->as.binary.left,vars); collect_expr_vars(e->as.binary.right,vars); break;
        case EX_CALL: for(size_t i=0;i<e->as.call.args->len;i++)collect_expr_vars(e->as.call.args->data[i],vars); break;
        case EX_METHOD: for(size_t i=0;i<e->as.method.args->len;i++)collect_expr_vars(e->as.method.args->data[i],vars); break;
        case EX_TERNARY: collect_expr_vars(e->as.tern.cond,vars); collect_expr_vars(e->as.tern.then_expr,vars); collect_expr_vars(e->as.tern.else_expr,vars); break;
        case EX_RANGE: collect_expr_vars(e->as.range.start,vars); collect_expr_vars(e->as.range.end,vars); break;
        default: break;
    }
}
static void collect_stmt_vars(Stmt *s, VarVec *vars){
    switch(s->kind){
        case ST_SET: case ST_LET: var_get_or_add(vars,s->as.assign.name); collect_expr_vars(s->as.assign.value,vars); break;
        case ST_BREAK: case ST_CONTINUE: break;
        case ST_RETURN: collect_expr_vars(s->as.ret,vars); break;
        case ST_EXPR: collect_expr_vars(s->as.expr,vars); break;
        case ST_IF:
            collect_expr_vars(s->as.ifs.cond,vars);
            for(size_t i=0;i<s->as.ifs.then_block.len;i++)collect_stmt_vars(s->as.ifs.then_block.data[i],vars);
            for(size_t j=0;j<s->as.ifs.elif_len;j++){collect_expr_vars(s->as.ifs.elifs[j].cond,vars);for(size_t i=0;i<s->as.ifs.elifs[j].body.len;i++)collect_stmt_vars(s->as.ifs.elifs[j].body.data[i],vars);}
            if(s->as.ifs.has_else)for(size_t i=0;i<s->as.ifs.else_block.len;i++)collect_stmt_vars(s->as.ifs.else_block.data[i],vars);
            break;
        case ST_FOR:
            if(s->as.fors.var){var_get_or_add(vars,s->as.fors.var);}
            collect_expr_vars(s->as.fors.range,vars);
            for(size_t i=0;i<s->as.fors.body.len;i++)collect_stmt_vars(s->as.fors.body.data[i],vars);
            break;
    }
}

/* ======================== DIRECT X86-64 ASM BACKEND ========================
 * Target object format: ELF64, System V AMD64 ABI (Linux) or PE/COFF with the
 * Microsoft x64 entry convention (Windows). The RBL value ABI and every RBL
 * function keep the System V convention on both targets, so one code generator
 * serves both; only the process entry/exit glue and a few assembler directives
 * differ. On Windows the runtime is compiled from C with __attribute__((sysv_abi))
 * and the same generated instructions call into it unchanged.
 */
typedef enum { TARGET_LINUX_ELF, TARGET_WINDOWS_PE } TargetKind;
static TargetKind g_target = TARGET_LINUX_ELF;
static bool target_windows(void) { return g_target == TARGET_WINDOWS_PE; }

typedef struct { char *text; char *label; } StrConst;
typedef struct { StrConst *data; size_t len, cap; } StrPool;
static void pool_push(StrPool *p, const char *text, const char *label) { if(p->len==p->cap){p->cap=p->cap?p->cap*2:16;p->data=xrealloc(p->data,p->cap*sizeof(*p->data));}p->data[p->len].text=xstrdup(text);p->data[p->len].label=xstrdup(label);p->len++; }
static const char *pool_find(StrPool *p,const char *text){for(size_t i=0;i<p->len;i++)if(!strcmp(p->data[i].text,text))return p->data[i].label;return NULL;}
static const char *pool_intern(StrPool *p,const char *text){const char*old=pool_find(p,text);if(old)return old;char label[64];snprintf(label,sizeof(label),".Lrbl_str_%zu",p->len);pool_push(p,text,label);return p->data[p->len-1].label;}
static void pool_free(StrPool *p){for(size_t i=0;i<p->len;i++){free(p->data[i].text);free(p->data[i].label);}free(p->data);}
static int align16(int n){return (n+15)&~15;}
static int slot_off(int idx){return -40 - 24*(idx+1);}
static int fast_slot_off(int idx){return -8*(idx+1);}
static int function_last_index_asm(Program *p,const char *name){for(size_t i=p->funcs.len;i>0;i--)if(!strcmp(p->funcs.data[i-1].name,name))return(int)(i-1);return-1;}
static void emit_asm_bytes(FILE *o,const char*s){const unsigned char*p=(const unsigned char*)s;if(!*p){fputs("    .byte 0\n",o);return;}fputs("    .byte ",o);bool first=true;while(*p){if(!first)fputs(", ",o);fprintf(o,"0x%02X",*p++);first=false;}fputs(", 0\n",o);}

typedef struct {
    FILE *out; Program *prog; Function *fn; VarVec *vars; StrPool *pool; size_t fi; unsigned long label_counter; bool fast_slots;
    int reg_slots[3];
    /* Nesting depth of the in-flight fast-int analysis. This is what stops
       (mutually) recursive single-return int functions from recursing forever
       inside function_is_fast_int(). */
    int fast_depth;
    /* Innermost loop targets for break/continue (0 means "not inside a loop"). */
    unsigned long break_label, continue_label;
} ACG;
static unsigned long aid(ACG*g){return g->label_counter++;}
/* Translation-unit-wide label sequence. Every function starts from this value and
   writes its final counter back, so labels can never be reused anywhere in the
   file - not inside a loop body, and not between the normal and the fast variant
   of a function. */
static unsigned long g_label_seq = 0;
static int cur_slot_off(const ACG*g,int idx){return g->fast_slots?fast_slot_off(idx):slot_off(idx);}
static const char *reg_name_for_index(int k){static const char *names[] = {"r14","r15"}; return (k>=0&&k<2)?names[k]:NULL;}
static int reg_for_slot(const ACG *g,int idx){for(int k=0;k<2;k++)if(g->reg_slots[k]==idx)return k;return -1;}
static bool stmt_assigns_var(Stmt*s,int idx,VarVec*vars){
    if(!s)return false;
    switch(s->kind){
        case ST_SET: case ST_LET:{int i=var_find(vars,s->as.assign.name);return i==idx;}
        case ST_IF:
            for(size_t i=0;i<s->as.ifs.then_block.len;i++)if(stmt_assigns_var(s->as.ifs.then_block.data[i],idx,vars))return true;
            for(size_t j=0;j<s->as.ifs.elif_len;j++)for(size_t i=0;i<s->as.ifs.elifs[j].body.len;i++)if(stmt_assigns_var(s->as.ifs.elifs[j].body.data[i],idx,vars))return true;
            if(s->as.ifs.has_else)for(size_t i=0;i<s->as.ifs.else_block.len;i++)if(stmt_assigns_var(s->as.ifs.else_block.data[i],idx,vars))return true;
            return false;
        case ST_FOR:
            if(s->as.fors.var && var_find(vars,s->as.fors.var)==idx)return true;
            for(size_t i=0;i<s->as.fors.body.len;i++)if(stmt_assigns_var(s->as.fors.body.data[i],idx,vars))return true;
            return false;
        default:return false;
    }
}
static bool block_has_nested_for(StmtVec*v){
    for(size_t i=0;i<v->len;i++){Stmt*s=v->data[i];if(s->kind==ST_FOR)return true; if(s->kind==ST_IF){if(block_has_nested_for(&s->as.ifs.then_block))return true;for(size_t j=0;j<s->as.ifs.elif_len;j++)if(block_has_nested_for(&s->as.ifs.elifs[j].body))return true;if(s->as.ifs.has_else&&block_has_nested_for(&s->as.ifs.else_block))return true;}}
    return false;
}
static void emit_expr_asm(ACG*g,Expr*e);
static void emit_stmt_asm(ACG*g,Stmt*s);
static void emit_int_expr(ACG*g,Expr*e);
static void emit_bool_expr(ACG*g,Expr*e);
static bool expr_fast_int(ACG*g,Expr*e);
static bool expr_fast_bool(ACG*g,Expr*e);

static const char *bin_runtime(BinaryOp op){switch(op){case B_ADD:return"rbl_add";case B_SUB:return"rbl_sub";case B_MUL:return"rbl_mul";case B_DIV:return"rbl_div";case B_MOD:return"rbl_mod";case B_EQ:return"rbl_eq";case B_NE:return"rbl_ne";case B_LT:return"rbl_lt";case B_GT:return"rbl_gt";case B_LE:return"rbl_le";case B_GE:return"rbl_ge";case B_AND:return"rbl_and";case B_OR:return"rbl_or";case B_IS:return"rbl_is";}return"rbl_fail";}

static bool function_is_fast_int(Program*p,int fi,int depth){
    if(depth>8||fi<0)return false;
    Function*f=&p->funcs.data[fi];
    if(!f->has_return_type||type_from_name(f->return_type)!=ST_INT||f->params.len>6||f->body.len!=1||f->body.data[0]->kind!=ST_RETURN)return false;
    VarVec vars={0};
    for(size_t i=0;i<f->params.len;i++){int idx=var_get_or_add(&vars,f->params.data[i].name);vars.data[idx].type=type_from_name(f->params.data[i].type);vars.data[idx].definitely_bound=true;}
    for(size_t i=0;i<f->body.len;i++)collect_stmt_vars(f->body.data[i],&vars);
    infer_stmt_seq(p,&f->body,&vars,false);
    Expr*e=f->body.data[0]->as.ret;
    StaticType t=infer_expr_type(p,e,&vars);
    if(t!=ST_INT){for(size_t i=0;i<vars.len;i++)free(vars.data[i].name);free(vars.data);return false;}
    ACG fake={0};fake.prog=p;fake.fn=f;fake.vars=&vars;fake.fi=(size_t)fi;fake.reg_slots[0]=fake.reg_slots[1]=fake.reg_slots[2]=-1;fake.fast_depth=depth;
    bool ok=expr_fast_int(&fake,e);
    for(size_t i=0;i<vars.len;i++){free(vars.data[i].name);}free(vars.data);
    return ok;
}

static bool expr_fast_int(ACG*g,Expr*e){
    if(!e||infer_expr_type(g->prog,e,g->vars)!=ST_INT)return false;
    switch(e->kind){
        case EX_INT:return true;
        case EX_IDENT:{int i=var_find(g->vars,e->as.ident);return i>=0&&g->vars->data[i].type==ST_INT&&g->vars->data[i].definitely_bound;}
        case EX_UNARY:return e->as.unary.op==U_NEG&&expr_fast_int(g,e->as.unary.expr);
        case EX_BINARY:
            if(e->as.binary.op==B_ADD||e->as.binary.op==B_SUB||e->as.binary.op==B_MUL||e->as.binary.op==B_DIV||e->as.binary.op==B_MOD)return expr_fast_int(g,e->as.binary.left)&&expr_fast_int(g,e->as.binary.right);
            return false;
        case EX_CALL:{int fi=function_last_index_asm(g->prog,e->as.call.callee);if(fi<0||!function_is_fast_int(g->prog,fi,g->fast_depth+1))return false;Function*f=&g->prog->funcs.data[fi];if(f->params.len!=e->as.call.args->len)return false;for(size_t i=0;i<e->as.call.args->len;i++)if(!expr_fast_int(g,e->as.call.args->data[i]))return false;return true;}
        default:return false;
    }
}
static bool expr_fast_bool(ACG*g,Expr*e){
    if(!e||infer_expr_type(g->prog,e,g->vars)!=ST_BOOL)return false;
    switch(e->kind){
        case EX_BOOL:return true;
        case EX_IDENT:{int i=var_find(g->vars,e->as.ident);return i>=0&&g->vars->data[i].type==ST_BOOL&&g->vars->data[i].definitely_bound;}
        case EX_UNARY:return e->as.unary.op==U_NOT&&expr_fast_bool(g,e->as.unary.expr);
        case EX_BINARY:
            if(e->as.binary.op==B_AND||e->as.binary.op==B_OR)return expr_fast_bool(g,e->as.binary.left)&&expr_fast_bool(g,e->as.binary.right);
            if(e->as.binary.op==B_EQ||e->as.binary.op==B_IS||e->as.binary.op==B_NE||e->as.binary.op==B_LT||e->as.binary.op==B_GT||e->as.binary.op==B_LE||e->as.binary.op==B_GE)return expr_fast_int(g,e->as.binary.left)&&expr_fast_int(g,e->as.binary.right);
            return false;
        default:return false;
    }
}

static void emit_call_args(ACG*g,ExprVec*args){size_t n=args->len,bytes=((n*16+15)/16)*16;if(bytes)fprintf(g->out,"    sub rsp, %zu\n",bytes);for(size_t i=0;i<n;i++){emit_expr_asm(g,args->data[i]);fprintf(g->out,"    mov QWORD PTR [rsp+%zu], rax\n    mov QWORD PTR [rsp+%zu], rdx\n",i*16,i*16+8);}}

static bool expr_fast_leaf_int(ACG*g,Expr*e){
    if(!e || infer_expr_type(g->prog,e,g->vars)!=ST_INT) return false;
    if(e->kind==EX_INT) return true;
    if(e->kind==EX_IDENT){
        int i=var_find(g->vars,e->as.ident);
        return i>=0 && g->vars->data[i].type==ST_INT && g->vars->data[i].definitely_bound;
    }
    return false;
}

static void emit_fast_leaf_int(ACG*g,Expr*e,const char *dst){
    FILE *o=g->out;
    if(e->kind==EX_INT){
        fprintf(o,"    mov %s,%lld\n",dst,(long long)e->as.i);
        return;
    }
    int idx=var_find(g->vars,e->as.ident);
    if(idx<0) fatal("internal: unknown integer identifier %s",e->as.ident);
    int rk=reg_for_slot(g,idx);
    if(rk>=0){
        fprintf(o,"    mov %s,%s\n",dst,reg_name_for_index(rk));
        return;
    }
    if(g->fast_slots){
        fprintf(o,"    mov %s,QWORD PTR [rbp%d]\n",dst,cur_slot_off(g,idx));
    } else {
        fprintf(o,"    mov %s,QWORD PTR [rbp%d+16]\n",dst,cur_slot_off(g,idx));
    }
}

static void emit_fast_call(ACG*g,int fi,ExprVec*args){
    size_t n=args->len, bytes=align16((int)(n*8));
    if(n)fprintf(g->out,"    sub rsp, %zu\n",bytes);
    for(size_t i=0;i<n;i++){emit_int_expr(g,args->data[i]);fprintf(g->out,"    mov QWORD PTR [rsp+%zu], rax\n",i*8);}
    static const char*regs[]={"rdi","rsi","rdx","rcx","r8","r9"};
    for(size_t i=0;i<n;i++)fprintf(g->out,"    mov %s, QWORD PTR [rsp+%zu]\n",regs[i],i*8);
    if(n)fprintf(g->out,"    add rsp, %zu\n",bytes);
    fprintf(g->out,"    call fast_fn_%d\n",fi);
}

static void emit_int_expr(ACG*g,Expr*e){
    FILE*o=g->out;
    if(!e){fputs("    xor eax,eax\n",o);return;}
    switch(e->kind){
        case EX_INT:fprintf(o,"    mov rax,%lld\n",(long long)e->as.i);return;
        case EX_IDENT:{int idx=var_find(g->vars,e->as.ident);if(idx<0){fatal("internal: unknown int variable %s",e->as.ident);}int rk=reg_for_slot(g,idx);if(rk>=0){fprintf(o,"    mov rax,%s\n",reg_name_for_index(rk));return;}if(g->fast_slots)fprintf(o,"    mov rax,QWORD PTR [rbp%d]\n",cur_slot_off(g,idx));else fprintf(o,"    mov rax,QWORD PTR [rbp%d+16]\n",cur_slot_off(g,idx));return;}
        case EX_UNARY:
            emit_int_expr(g,e->as.unary.expr);if(e->as.unary.op==U_NEG){fprintf(o,"    neg rax\n    jo .L_int_overflow_%lu\n",aid(g));fprintf(o,"    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",g->label_counter-1,g->label_counter-1,g->label_counter-1);}return;
        case EX_BINARY:{
            BinaryOp op=e->as.binary.op;if(op!=B_ADD&&op!=B_SUB&&op!=B_MUL&&op!=B_DIV&&op!=B_MOD){fatal("internal: non-int binary in raw path");}
            emit_int_expr(g,e->as.binary.left);fputs("    push rax\n",o);emit_int_expr(g,e->as.binary.right);fputs("    pop rcx\n",o);
            if(op==B_ADD||op==B_SUB||op==B_MUL){
                if(op==B_ADD)fputs("    add rcx,rax\n",o);else if(op==B_SUB)fputs("    sub rcx,rax\n",o);else fputs("    imul rcx,rax\n",o);
                unsigned long n=aid(g);fprintf(o,"    jo .L_int_overflow_%lu\n    mov rax,rcx\n    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",n,n,n,n);
            }else{
                unsigned long nzero=aid(g),nov=aid(g),done=aid(g);
                fputs("    mov r8,rax\n    mov rax,rcx\n    test r8,r8\n",o);fprintf(o,"    jz .L_int_divzero_%lu\n    mov rdx,0x8000000000000000\n    cmp rax,rdx\n    jne .L_int_divdo_%lu\n    cmp r8,-1\n    je .L_int_divov_%lu\n.L_int_divdo_%lu:\n",nzero,done,nov,done);
                fputs("    cqo\n    idiv r8\n",o);
                /* idiv leaves the quotient in rax and the remainder in rdx. */
                if(op==B_MOD){fputs("    mov rax,rdx\n",o);}
                fprintf(o,"    jmp .L_int_done_%lu\n.L_int_divzero_%lu:\n    call rbl_int_divzero\n    jmp .L_int_done_%lu\n.L_int_divov_%lu:\n    call rbl_int_div_overflow\n.L_int_done_%lu:\n",done,nzero,done,nov,done);
            }
            return;
        }
        case EX_CALL:{int fi=function_last_index_asm(g->prog,e->as.call.callee);if(fi<0||!function_is_fast_int(g->prog,fi,0))fatal("internal: call not eligible for raw int path");emit_fast_call(g,fi,e->as.call.args);return;}
        default:fatal("internal: unsupported raw int expression");
    }
}

static bool emit_fast_leaf_compare(ACG*g,Expr*left,Expr*right){
    if(!expr_fast_leaf_int(g,left) || !expr_fast_leaf_int(g,right)) return false;
    FILE *o=g->out;
    if(left->kind==EX_IDENT && right->kind==EX_INT && right->as.i>=INT32_MIN && right->as.i<=INT32_MAX){
        emit_fast_leaf_int(g,left,"rcx");
        fprintf(o,"    cmp rcx,%lld\n",(long long)right->as.i);
        return true;
    }
    if(left->kind==EX_IDENT && right->kind==EX_IDENT){
        emit_fast_leaf_int(g,left,"rcx");
        emit_fast_leaf_int(g,right,"rax");
        fputs("    cmp rcx,rax\n",o);
        return true;
    }
    emit_fast_leaf_int(g,left,"rcx");
    emit_fast_leaf_int(g,right,"rax");
    fputs("    cmp rcx,rax\n",o);
    return true;
}

static void emit_bool_expr(ACG*g,Expr*e){
    FILE*o=g->out;
    switch(e->kind){
        case EX_BOOL:fprintf(o,"    mov eax,%d\n",e->as.b?1:0);return;
        case EX_IDENT:{int idx=var_find(g->vars,e->as.ident);int rk=reg_for_slot(g,idx);if(rk>=0){fprintf(o,"    mov rax,%s\n",reg_name_for_index(rk));return;}if(g->fast_slots)fprintf(o,"    mov rax,QWORD PTR [rbp%d]\n",cur_slot_off(g,idx));else fprintf(o,"    mov rax,QWORD PTR [rbp%d+16]\n",cur_slot_off(g,idx));return;}
        case EX_UNARY:emit_bool_expr(g,e->as.unary.expr);fputs("    xor eax,1\n",o);return;
        case EX_BINARY:{
            BinaryOp op=e->as.binary.op;
            if(op==B_AND||op==B_OR){emit_bool_expr(g,e->as.binary.left);fputs("    push rax\n",o);emit_bool_expr(g,e->as.binary.right);fputs("    pop rcx\n",o);if(op==B_AND)fputs("    and eax,ecx\n",o);else fputs("    or eax,ecx\n",o);return;}
            if(!emit_fast_leaf_compare(g,e->as.binary.left,e->as.binary.right)){
                emit_int_expr(g,e->as.binary.left);fputs("    push rax\n",o);emit_int_expr(g,e->as.binary.right);fputs("    pop rcx\n    cmp rcx,rax\n",o);
            }
            const char*cc="e";switch(op){case B_EQ:cc="e";break;case B_IS:cc="e";break;case B_NE:cc="ne";break;case B_LT:cc="l";break;case B_GT:cc="g";break;case B_LE:cc="le";break;case B_GE:cc="ge";break;default:fatal("internal: invalid bool op");}
            fprintf(o,"    set%s al\n    movzx eax,al\n",cc);return;
        }
        default:fatal("internal: unsupported raw bool expression");
    }
}

static bool is_builtin_name(const char *n){
    return !strcmp(n,"print") || !strcmp(n,"len") || !strcmp(n,"input") || !strcmp(n,"read_file") ||
           !strcmp(n,"write_file") || !strcmp(n,"abs") || !strcmp(n,"sqrt") || !strcmp(n,"min") ||
           !strcmp(n,"max") || !strcmp(n,"int") || !strcmp(n,"float") || !strcmp(n,"str") ||
           !strcmp(n,"pow") || !strcmp(n,"floor") || !strcmp(n,"ceil") || !strcmp(n,"round") ||
           !strcmp(n,"sin") || !strcmp(n,"cos") || !strcmp(n,"tan") || !strcmp(n,"log") || !strcmp(n,"exp") ||
           !strcmp(n,"log10") || !strcmp(n,"log2") || !strcmp(n,"trunc") || !strcmp(n,"fmod") || !strcmp(n,"hypot") || !strcmp(n,"sign") || !strcmp(n,"is_nan") || !strcmp(n,"is_inf") ||
           !strcmp(n,"list_new") || !strcmp(n,"list_push") || !strcmp(n,"list_get") || !strcmp(n,"list_set") ||
           !strcmp(n,"index_get") || !strcmp(n,"index_set") || !strcmp(n,"dict_new") || !strcmp(n,"dict_set") ||
           !strcmp(n,"contains") || !strcmp(n,"tuple_new") || !strcmp(n,"tuple_push") || !strcmp(n,"null_value") ||
           !strcmp(n,"random_int") || !strcmp(n,"random_float") || !strcmp(n,"random_bool");
}

static void emit_builtin_call(ACG*g, Expr *e){
    FILE *o=g->out;
    const char *name=e->as.call.callee;
    size_t n=e->as.call.args->len;

    if(!strcmp(name,"print")){
        size_t bytes=((n*16+15)/16)*16; emit_call_args(g,e->as.call.args);
        if(n) fprintf(o,"    mov rdi,rsp\n    mov esi,%zu\n    call rbl_print_values\n    add rsp,%zu\n",n,bytes);
        else fputs("    xor edi,edi\n    xor esi,esi\n    call rbl_print_values\n",o);
        fputs("    xor eax,eax\n    xor edx,edx\n",o); return;
    }

    if(!strcmp(name,"len")){
        if(n!=1) fatal_at(e->pos,"len() expects 1 argument, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_len\n",o); return;
    }
    if(!strcmp(name,"input")){
        if(n==0){ fputs("    call rbl_input0\n",o); return; }
        if(n!=1) fatal_at(e->pos,"input() expects 0 or 1 argument, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_input1\n",o); return;
    }
    if(!strcmp(name,"read_file")){
        if(n!=1) fatal_at(e->pos,"read_file() expects 1 argument, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_read_file\n",o); return;
    }
    if(!strcmp(name,"write_file")){
        if(n!=2) fatal_at(e->pos,"write_file() expects 2 arguments, got %zu",n);
        size_t bytes=((n*16+15)/16)*16; emit_call_args(g,e->as.call.args);
        fprintf(o,"    mov rdi,rsp\n    mov esi,%zu\n    call rbl_write_file_values\n    add rsp,%zu\n",n,bytes); return;
    }
    if(!strcmp(name,"abs")){
        if(n!=1) fatal_at(e->pos,"abs() expects 1 argument, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_abs\n",o); return;
    }
    if(!strcmp(name,"sqrt")){
        if(n!=1) fatal_at(e->pos,"sqrt() expects 1 argument, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_sqrt\n",o); return;
    }
    if(!strcmp(name,"min") || !strcmp(name,"max")){
        if(n==0) fatal_at(e->pos,"%s() expects at least 1 argument",name);
        size_t bytes=((n*16+15)/16)*16; emit_call_args(g,e->as.call.args);
        fprintf(o,"    mov rdi,rsp\n    mov esi,%zu\n    call %s_values\n    add rsp,%zu\n",n,!strcmp(name,"min")?"rbl_min":"rbl_max",bytes); return;
    }
    if(!strcmp(name,"int") || !strcmp(name,"float") || !strcmp(name,"str")){
        if(n!=1) fatal_at(e->pos,"%s() expects 1 argument, got %zu",name,n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n",o);
        fprintf(o,"    call rbl_%s\n",name); return;
    }
    if(!strcmp(name,"list_new")){
        if(n!=0) fatal_at(e->pos,"list_new() takes no arguments");
        fputs("    call rbl_list_new\n",o);
        return;
    }
    if(!strcmp(name,"list_push")){
        if(n!=2) fatal_at(e->pos,"list_push() expects 2 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    mov rcx,rdx\n    mov rdx,rax\n    pop rsi\n    pop rdi\n    call rbl_list_push\n",o);
        return;
    }
    if(!strcmp(name,"list_get")){
        if(n!=2) fatal_at(e->pos,"list_get() expects 2 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    pop rsi\n    pop rdi\n    call rbl_list_get\n",o);
        return;
    }
    if(!strcmp(name,"list_set")){
        if(n!=3) fatal_at(e->pos,"list_set() expects 3 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[2]);
        fputs("    mov r8,rdx\n    mov rcx,rax\n    pop rdx\n    pop rax\n    pop rsi\n    pop rdi\n    call rbl_list_set\n",o);
        return;
    }
    if(!strcmp(name,"index_get")){
        if(n!=2) fatal_at(e->pos,"index_get() expects 2 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    mov rcx,rdx\n    mov rdx,rax\n    pop rsi\n    pop rdi\n    call rbl_index_get\n",o);
        return;
    }
    if(!strcmp(name,"index_set")){
        if(n!=3) fatal_at(e->pos,"index_set() expects 3 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[2]);
        fputs("    mov r9,rdx\n    mov r8,rax\n    pop rcx\n    pop rdx\n    pop rsi\n    pop rdi\n    call rbl_index_set\n",o);
        return;
    }
    if(!strcmp(name,"dict_new")){
        if(n!=0) fatal_at(e->pos,"dict_new() takes no arguments");
        fputs("    call rbl_dict_new\n",o);
        return;
    }
    if(!strcmp(name,"dict_set")){
        if(n!=3) fatal_at(e->pos,"dict_set() expects 3 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[2]);
        fputs("    mov r9,rdx\n    mov r8,rax\n    pop rcx\n    pop rdx\n    pop rsi\n    pop rdi\n    call rbl_dict_set\n",o);
        return;
    }
    if(!strcmp(name,"null_value")){
        if(n!=0) fatal_at(e->pos,"null takes no arguments");
        fputs("    mov eax,8\n    xor edx,edx\n",o);
        return;
    }
    if(!strcmp(name,"tuple_new")){
        if(n!=0) fatal_at(e->pos,"tuple_new() takes no arguments");
        fputs("    call rbl_tuple_new\n",o);
        return;
    }
    if(!strcmp(name,"tuple_push")){
        if(n!=2) fatal_at(e->pos,"tuple_push() expects 2 arguments");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    mov rcx,rdx\n    mov rdx,rax\n    pop rsi\n    pop rdi\n    call rbl_tuple_push\n",o);
        return;
    }
    if(!strcmp(name,"contains")){
        if(n!=2) fatal_at(e->pos,"'in' expects 2 operands");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    mov rcx,rdx\n    mov rdx,rax\n    pop rsi\n    pop rdi\n    call rbl_contains\n",o);
        return;
    }
    if(!strcmp(name,"pow") || !strcmp(name,"floor") || !strcmp(name,"ceil") || !strcmp(name,"round") || !strcmp(name,"sin") || !strcmp(name,"cos") || !strcmp(name,"tan") || !strcmp(name,"log") || !strcmp(name,"exp") || !strcmp(name,"log10") || !strcmp(name,"log2") || !strcmp(name,"trunc") || !strcmp(name,"fmod") || !strcmp(name,"hypot") || !strcmp(name,"sign") || !strcmp(name,"is_nan") || !strcmp(name,"is_inf")){
        if(!strcmp(name,"fmod") || !strcmp(name,"hypot")){
            if(n!=2) fatal_at(e->pos,"%s() expects 2 arguments, got %zu",name,n);
            emit_expr_asm(g,e->as.call.args->data[0]);
            fputs("    push rax\n    push rdx\n",o);
            emit_expr_asm(g,e->as.call.args->data[1]);
            fprintf(o,"    mov r8,rax\n    mov r9,rdx\n    pop rdx\n    pop rdi\n    mov rsi,rdx\n    mov rdx,r8\n    mov rcx,r9\n    call rbl_math_%s\n",name);
            return;
        }
        if(!strcmp(name,"pow")){
            if(n!=2) fatal_at(e->pos,"pow() expects 2 arguments, got %zu",n);
            emit_expr_asm(g,e->as.call.args->data[0]);
            fputs("    push rax\n    push rdx\n",o);
            emit_expr_asm(g,e->as.call.args->data[1]);
            fputs("    mov r8,rax\n    mov r9,rdx\n    pop rdx\n    pop rdi\n    mov rsi,rdx\n    mov rdx,r8\n    mov rcx,r9\n    call rbl_math_pow\n",o);
            return;
        }
        if(n!=1) fatal_at(e->pos,"%s() expects 1 argument, got %zu",name,n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    mov rdi,rax\n    mov rsi,rdx\n    call ",o);
        fprintf(o,"rbl_math_%s\n",name);
        return;
    }
    if(!strcmp(name,"random_int")){
        if(n!=2) fatal_at(e->pos,"random_int() expects 2 arguments, got %zu",n);
        emit_expr_asm(g,e->as.call.args->data[0]);
        fputs("    push rax\n    push rdx\n",o);
        emit_expr_asm(g,e->as.call.args->data[1]);
        fputs("    mov r8,rax\n    mov r9,rdx\n    pop rsi\n    pop rdi\n    mov rdx,r8\n    mov rcx,r9\n    call rbl_random_int\n",o);
        return;
    }
    if(!strcmp(name,"random_float")){
        if(n!=0) fatal_at(e->pos,"random_float() expects 0 arguments, got %zu",n);
        fputs("    call rbl_random_float\n",o); return;
    }
    if(!strcmp(name,"random_bool")){
        if(n!=0) fatal_at(e->pos,"random_bool() expects 0 arguments, got %zu",n);
        fputs("    call rbl_random_bool\n",o); return;
    }
    fatal("internal: unknown builtin %s",name);
}

static void emit_builtin_method(ACG *g, Expr *e){
    FILE *o=g->out; const char *obj=e->as.method.object, *m=e->as.method.method; size_t n=e->as.method.args->len;
    if((!strcmp(obj,"warn")||!strcmp(obj,"error")) && !strcmp(m,"log")){
        size_t bytes=((n*16+15)/16)*16; emit_call_args(g,e->as.method.args);
        if(n) fprintf(o,"    mov rdi,rsp\n"); else fputs("    xor edi,edi\n",o);
        fprintf(o,"    mov esi,%zu\n    mov edx,%d\n    call rbl_log_values\n",n,!strcmp(obj,"warn")?1:2);
        if(n) fprintf(o,"    add rsp,%zu\n",bytes);
        fputs("    xor eax,eax\n    xor edx,edx\n",o); return;
    }
    if(!strcmp(obj,"rbl.io")){
        if(!strcmp(m,"input")||!strcmp(m,"prompt")){
            if(n==0){fputs("    call rbl_input0\n",o);return;}
            if(n!=1) fatal_at(e->pos,"rbl.io.%s() expects 0 or 1 args",m);
            emit_expr_asm(g,e->as.method.args->data[0]); fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_input1\n",o); return;
        }
        if(!strcmp(m,"read_file")){if(n!=1)fatal_at(e->pos,"rbl.io.read_file() expects 1 arg");emit_expr_asm(g,e->as.method.args->data[0]);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_read_file\n",o);return;}
        if(!strcmp(m,"write_file")){if(n!=2)fatal_at(e->pos,"rbl.io.write_file() expects 2 args");size_t bytes=((n*16+15)/16)*16;emit_call_args(g,e->as.method.args);fprintf(o,"    mov rdi,rsp\n    mov esi,2\n    call rbl_write_file_values\n    add rsp,%zu\n",bytes);return;}
    }
    if(!strcmp(obj,"rbl.math")){
        const char *fn=NULL;
        if(!strcmp(m,"sqrt"))fn="rbl_sqrt"; else if(!strcmp(m,"abs"))fn="rbl_abs"; else if(!strcmp(m,"pow"))fn="rbl_math_pow";
        else if(!strcmp(m,"floor"))fn="rbl_math_floor"; else if(!strcmp(m,"ceil"))fn="rbl_math_ceil"; else if(!strcmp(m,"round"))fn="rbl_math_round";
        else if(!strcmp(m,"log10"))fn="rbl_math_log10"; else if(!strcmp(m,"log2"))fn="rbl_math_log2"; else if(!strcmp(m,"trunc"))fn="rbl_math_trunc"; else if(!strcmp(m,"sign"))fn="rbl_math_sign"; else if(!strcmp(m,"is_nan"))fn="rbl_math_is_nan"; else if(!strcmp(m,"is_inf"))fn="rbl_math_is_inf";
        else if(!strcmp(m,"sin"))fn="rbl_math_sin"; else if(!strcmp(m,"cos"))fn="rbl_math_cos"; else if(!strcmp(m,"tan"))fn="rbl_math_tan"; else if(!strcmp(m,"log"))fn="rbl_math_log"; else if(!strcmp(m,"exp"))fn="rbl_math_exp";
        if(fn){
            if(!strcmp(m,"pow")){
                if(n!=2)fatal_at(e->pos,"rbl.math.pow() expects 2 args");
                emit_expr_asm(g,e->as.method.args->data[0]);
                fputs("    push rax\n    push rdx\n",o);
                emit_expr_asm(g,e->as.method.args->data[1]);
                fputs("    mov r8,rax\n    mov r9,rdx\n    pop rdx\n    pop rdi\n    mov rsi,rdx\n    mov rdx,r8\n    mov rcx,r9\n    call rbl_math_pow\n",o);
                return;
            }
            if(n!=1) fatal_at(e->pos,"rbl.math.%s() expects 1 arg",m);
            emit_expr_asm(g,e->as.method.args->data[0]);
            fputs("    mov rdi,rax\n    mov rsi,rdx\n    call ",o);
            fprintf(o,"%s\n",fn);
            return;
        }
        if(!strcmp(m,"min")||!strcmp(m,"max")){if(n==0)fatal_at(e->pos,"rbl.math.%s() expects args",m);size_t bytes=((n*16+15)/16)*16;emit_call_args(g,e->as.method.args);fprintf(o,"    mov rdi,rsp\n    mov esi,%zu\n    call %s_values\n    add rsp,%zu\n",n,!strcmp(m,"min")?"rbl_min":"rbl_max",bytes);return;}
    }
    if(!strcmp(obj,"rbl.string")){
        if(!strcmp(m,"len")){if(n!=1)fatal_at(e->pos,"rbl.string.len() expects 1 arg");emit_expr_asm(g,e->as.method.args->data[0]);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_len\n",o);return;}
        const char *fn=NULL; if(!strcmp(m,"upper"))fn="rbl_string_upper";else if(!strcmp(m,"lower"))fn="rbl_string_lower";else if(!strcmp(m,"trim"))fn="rbl_string_trim";else if(!strcmp(m,"contains"))fn="rbl_string_contains";else if(!strcmp(m,"starts_with"))fn="rbl_string_starts_with";else if(!strcmp(m,"ends_with"))fn="rbl_string_ends_with";
        if(fn){size_t expected=(strstr(m,"contains")||strstr(m,"starts_with")||strstr(m,"ends_with"))?2:1;if(n!=expected)fatal_at(e->pos,"rbl.string.%s() expects %zu args",m,expected);emit_expr_asm(g,e->as.method.args->data[0]);fputs("    push rax\n    push rdx\n",o);if(expected==2){emit_expr_asm(g,e->as.method.args->data[1]);fputs("    mov r8,rax\n    mov r9,rdx\n    pop rdx\n    pop rdi\n    mov rsi,rdx\n    mov rdx,r8\n    mov rcx,r9\n",o);}else{fputs("    pop rsi\n    pop rdi\n",o);}fprintf(o,"    call %s\n",fn);return;}
    }
    if(!strcmp(obj,"rbl.fs")){
        if(!strcmp(m,"cwd")){if(n!=0)fatal_at(e->pos,"rbl.fs.cwd() expects no args");fputs("    call rbl_fs_cwd\n",o);return;}
        const char*fn=!strcmp(m,"exists")?"rbl_fs_exists":(!strcmp(m,"delete")?"rbl_fs_delete":NULL);
        if(fn){if(n!=1)fatal_at(e->pos,"rbl.fs.%s() expects 1 arg",m);emit_expr_asm(g,e->as.method.args->data[0]);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call ",o);fprintf(o,"%s\n",fn);return;}
    }
    if(!strcmp(obj,"rbl.time")){
        if(!strcmp(m,"now_ms")){if(n!=0)fatal_at(e->pos,"rbl.time.now_ms() expects no args");fputs("    call rbl_time_now_ms\n",o);return;}
        if(!strcmp(m,"sleep_ms")){if(n!=1)fatal_at(e->pos,"rbl.time.sleep_ms() expects 1 arg");emit_expr_asm(g,e->as.method.args->data[0]);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_time_sleep_ms\n",o);return;}
    }
    if(!strcmp(obj,"rbl.random")){
        if(!strcmp(m,"int")){
            if(n!=2)fatal_at(e->pos,"%s.int() expects 2 args",obj);
            emit_expr_asm(g,e->as.method.args->data[0]);
            fputs("    push rax\n    push rdx\n",o);
            emit_expr_asm(g,e->as.method.args->data[1]);
            fputs("    mov r8,rax\n    mov r9,rdx\n    pop rsi\n    pop rdi\n    mov rdx,r8\n    mov rcx,r9\n    call rbl_random_int\n",o);
            return;
        }
        if(!strcmp(m,"float")){if(n!=0)fatal_at(e->pos,"%s.float() expects no args",obj);fputs("    call rbl_random_float\n",o);return;}
        if(!strcmp(m,"bool")){if(n!=0)fatal_at(e->pos,"%s.bool() expects no args",obj);fputs("    call rbl_random_bool\n",o);return;}
    }
    fprintf(o,"    lea rdi,[rip+%s]\n    lea rsi,[rip+%s]\n    call rbl_unknown_method\n",pool_intern(g->pool,obj),pool_intern(g->pool,m));
    fputs("    xor eax,eax\n    xor edx,edx\n",o);
}

static void emit_expr_asm(ACG*g,Expr*e){
    FILE*o=g->out;if(!e){fputs("    xor eax,eax\n    xor edx,edx\n",o);return;}
    if(expr_fast_int(g,e)){emit_int_expr(g,e);fputs("    mov rdx,rax\n    mov eax,1\n",o);return;}
    if(expr_fast_bool(g,e)){emit_bool_expr(g,e);fputs("    mov rdx,rax\n    mov eax,4\n",o);return;}
    switch(e->kind){
        case EX_INT:fprintf(o,"    mov eax,1\n    mov rdx,%lld\n",(long long)e->as.i);return;
        case EX_FLOAT:{uint64_t bits=0;memcpy(&bits,&e->as.f,8);fprintf(o,"    mov eax,2\n    mov rdx,0x%016"PRIx64"\n",bits);return;}
        case EX_STR:fprintf(o,"    mov eax,3\n    lea rdx,[rip+%s]\n",pool_intern(g->pool,e->as.s));return;
        case EX_BOOL:fprintf(o,"    mov eax,4\n    mov edx,%d\n",e->as.b?1:0);return;
        case EX_IDENT:{int idx=var_get_or_add(g->vars,e->as.ident);fprintf(o,"    lea rdi,[rbp%d]\n    lea rsi,[rip+%s]\n    call rbl_get_slot\n",slot_off(idx),pool_intern(g->pool,e->as.ident));return;}
        case EX_UNARY:emit_expr_asm(g,e->as.unary.expr);fputs("    mov rdi,rax\n    mov rsi,rdx\n",o);fputs(e->as.unary.op==U_NEG?"    call rbl_neg\n":"    call rbl_not\n",o);return;
        case EX_BINARY:
            emit_expr_asm(g,e->as.binary.left);fputs("    sub rsp,16\n    mov [rsp],rax\n    mov [rsp+8],rdx\n",o);emit_expr_asm(g,e->as.binary.right);fputs("    mov r8,rax\n    mov r9,rdx\n    mov rdi,[rsp]\n    mov rsi,[rsp+8]\n    mov rdx,r8\n    mov rcx,r9\n    add rsp,16\n",o);fprintf(o,"    call %s\n",bin_runtime(e->as.binary.op));return;
        case EX_TERNARY:{
            unsigned long lelse=aid(g),lend=aid(g);
            Expr*c=e->as.tern.cond;
            if(expr_fast_bool(g,c)){emit_bool_expr(g,c);fputs("    test rax,rax\n",o);}
            else{emit_expr_asm(g,c);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_bool\n    test rdx,rdx\n",o);}
            fprintf(o,"    jz .L_tern_else_%lu\n",lelse);
            emit_expr_asm(g,e->as.tern.then_expr);
            fprintf(o,"    jmp .L_tern_end_%lu\n.L_tern_else_%lu:\n",lend,lelse);
            emit_expr_asm(g,e->as.tern.else_expr);
            fprintf(o,".L_tern_end_%lu:\n",lend);
            return;
        }
        case EX_RANGE:fprintf(o,"    lea rdi,[rip+%s]\n    call rbl_fail\n",pool_intern(g->pool,"range is only valid in for"));return;
        case EX_CALL:{
            const char*name=e->as.call.callee; size_t n=e->as.call.args->len;
            if(is_builtin_name(name)){ emit_builtin_call(g,e); return; }
            int fi=function_last_index_asm(g->prog,name);
            if(fi<0){ fprintf(o,"    lea rdi,[rip+%s]\n    call rbl_unknown_function\n",pool_intern(g->pool,name)); return; }
            if(function_is_fast_int(g->prog,fi,0)&&expr_fast_int(g,e)){ emit_int_expr(g,e); fputs("    mov rdx,rax\n    mov eax,1\n",o); return; }
            fprintf(o,"    lea rdi,[rip+%s]\n    mov esi,%zu\n    mov edx,%zu\n    call rbl_check_arity\n",pool_intern(g->pool,name),g->prog->funcs.data[fi].params.len,n);
            size_t bytes=((n*16+15)/16)*16; emit_call_args(g,e->as.call.args);
            if(n) fprintf(o,"    mov rdi,rsp\n    mov esi,%zu\n    call fn_%d\n    add rsp,%zu\n",n,fi,bytes);
            else fprintf(o,"    xor edi,edi\n    xor esi,esi\n    call fn_%d\n",fi);
            return;
        }
        case EX_METHOD: { emit_builtin_method(g,e); return; }
    }
}

static void emit_store_slot(FILE*o,int idx){fprintf(o,"    mov [rbp%d+8],rax\n    mov [rbp%d+16],rdx\n    mov QWORD PTR [rbp%d],1\n",slot_off(idx),slot_off(idx),slot_off(idx));}
static void emit_store_int_slot(ACG*g,int idx){int rk=reg_for_slot(g,idx);if(rk>=0){fprintf(g->out,"    mov %s,rax\n",reg_name_for_index(rk));return;}fprintf(g->out,"    mov QWORD PTR [rbp%d+16],rax\n    mov QWORD PTR [rbp%d+8],1\n    mov QWORD PTR [rbp%d],1\n",slot_off(idx),slot_off(idx),slot_off(idx));}
static bool emit_fast_inplace_int_update(ACG*g,int idx,Expr*e){
    if(idx<0 || reg_for_slot(g,idx)<0 || !e || e->kind!=EX_BINARY) return false;
    BinaryOp op=e->as.binary.op;
    if(op!=B_ADD && op!=B_SUB && op!=B_MUL) return false;
    Expr *lhs=e->as.binary.left, *rhs=e->as.binary.right;
    if(!lhs || lhs->kind!=EX_IDENT || strcmp(lhs->as.ident,g->vars->data[idx].name)!=0) return false;
    if(!expr_fast_leaf_int(g,rhs)) return false;
    const char *dst=reg_name_for_index(reg_for_slot(g,idx));
    if(rhs->kind==EX_INT && rhs->as.i>=INT32_MIN && rhs->as.i<=INT32_MAX){
        if(op==B_ADD) fprintf(g->out,"    add %s,%lld\n",dst,(long long)rhs->as.i);
        else if(op==B_SUB) fprintf(g->out,"    sub %s,%lld\n",dst,(long long)rhs->as.i);
        else fprintf(g->out,"    imul %s,%lld\n",dst,(long long)rhs->as.i);
    }else{
        emit_fast_leaf_int(g,rhs,"rax");
        if(op==B_ADD) fprintf(g->out,"    add %s,rax\n",dst);
        else if(op==B_SUB) fprintf(g->out,"    sub %s,rax\n",dst);
        else fprintf(g->out,"    imul %s,rax\n",dst);
    }
    unsigned long n=aid(g);
    fprintf(g->out,"    jo .L_int_overflow_%lu\n",n);
    fprintf(g->out,"    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",n,n,n);
    return true;
}

static void emit_block_asm(ACG*g,StmtVec*v){for(size_t i=0;i<v->len;i++)emit_stmt_asm(g,v->data[i]);}

static bool stmt_fast_int(ACG*g,Stmt*s){
    switch(s->kind){
        case ST_SET:{int i=var_find(g->vars,s->as.assign.name);return i>=0&&g->vars->data[i].type==ST_INT&&expr_fast_int(g,s->as.assign.value);}
        case ST_LET:{int i=var_find(g->vars,s->as.assign.name);return i>=0&&g->vars->data[i].type==ST_INT&&g->vars->data[i].definitely_bound&&expr_fast_int(g,s->as.assign.value);}
        case ST_IF:{if(!expr_fast_bool(g,s->as.ifs.cond))return false;for(size_t i=0;i<s->as.ifs.then_block.len;i++)if(!stmt_fast_int(g,s->as.ifs.then_block.data[i]))return false;for(size_t j=0;j<s->as.ifs.elif_len;j++){if(!expr_fast_bool(g,s->as.ifs.elifs[j].cond))return false;for(size_t i=0;i<s->as.ifs.elifs[j].body.len;i++)if(!stmt_fast_int(g,s->as.ifs.elifs[j].body.data[i]))return false;}if(s->as.ifs.has_else)for(size_t i=0;i<s->as.ifs.else_block.len;i++)if(!stmt_fast_int(g,s->as.ifs.else_block.data[i]))return false;return true;}
        default:return false;
    }
}
static bool block_fast_int(ACG*g,StmtVec*v){for(size_t i=0;i<v->len;i++)if(!stmt_fast_int(g,v->data[i]))return false;return true;}

static void emit_fast_for(ACG*g,Stmt*s){
    FILE*o=g->out; Expr*r=s->as.fors.range; int idx=var_find(g->vars,s->as.fors.var); unsigned long loop=aid(g), end=aid(g), noexec=aid(g);
    StaticType old_loop_type = (idx>=0)?g->vars->data[idx].type:ST_UNKNOWN;
    bool old_loop_bound = (idx>=0)?g->vars->data[idx].definitely_bound:false;
    bool loop_reg_ok = idx>=0 && !block_has_nested_for(&s->as.fors.body);
    for(size_t i=0;i<s->as.fors.body.len;i++) if(stmt_assigns_var(s->as.fors.body.data[i],idx,g->vars)) loop_reg_ok=false;

    /* Compute bounds once. For exclusive ranges, materialize end-1. */
    fputs("    sub rsp,16\n",o);
    emit_int_expr(g,r->as.range.start); fputs("    mov [rsp],rax\n",o);
    emit_int_expr(g,r->as.range.end);
    if(r->as.range.inclusive) fputs("    mov [rsp+8],rax\n",o);
    else { fputs("    sub rax,1\n",o); unsigned long n=aid(g); fprintf(o,"    jo .L_int_overflow_%lu\n",n); fprintf(o,"    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",n,n,n); fputs("    mov [rsp+8],rax\n",o); }

    ACG loopg=*g;
    loopg.reg_slots[0]=loop_reg_ok?idx:-1;
    int next_reg=1;
    for(size_t vi=0;vi<g->vars->len && next_reg<2;vi++){
        if((int)vi==idx) continue;
        if(g->vars->data[vi].type!=ST_INT || !g->vars->data[vi].definitely_bound) continue;
        bool used=false;
        /* Allocate only variables participating in the loop body. */
        for(size_t si=0;si<s->as.fors.body.len;si++){
            Stmt *bs=s->as.fors.body.data[si];
            (void)bs;
            /* The conservative whole-body fast check guarantees reads/writes are integer. */
            used=true;
        }
        if(!used) continue;
        loopg.reg_slots[next_reg++]=(int)vi;
    }
    while(next_reg<2) loopg.reg_slots[next_reg++]=-1;

    /* Load allocated locals into callee-saved registers. */
    for(int rk=1;rk<2;rk++) if(loopg.reg_slots[rk]>=0)
        fprintf(o,"    mov %s,QWORD PTR [rbp%d+16]\n",reg_name_for_index(rk),slot_off(loopg.reg_slots[rk]));

    fputs("    mov r13,QWORD PTR [rsp+8]\n",o);
    if(loop_reg_ok) fputs("    mov r14,QWORD PTR [rsp]\n",o);
    fprintf(o,".L_fast_for_%lu:\n",loop);
    if(loop_reg_ok) fprintf(o,"    cmp r14,r13\n    jg .L_fast_for_end_%lu\n",end);
    else fprintf(o,"    mov rax,QWORD PTR [rsp]\n    cmp rax,r13\n    jg .L_fast_for_end_%lu\n",end);

    if(loop_reg_ok){
        loopg.vars->data[idx].type=ST_INT; loopg.vars->data[idx].definitely_bound=true;
        emit_block_asm(&loopg,&s->as.fors.body);
        /* Labels consumed while emitting the body live in the ACG copy. Without
           this sync the parent context keeps handing out ids that were already
           emitted inside the loop, and GNU as rejects the program with
           "symbol ... is already defined". */
        g->label_counter=loopg.label_counter;
        fputs("    inc r14\n",o);
    }else{
        fputs("    mov rax,QWORD PTR [rsp]\n",o);
        fprintf(o,"    mov QWORD PTR [rbp%d+16],rax\n    mov QWORD PTR [rbp%d+8],1\n    mov QWORD PTR [rbp%d],1\n",slot_off(idx),slot_off(idx),slot_off(idx));
        loopg.vars->data[idx].type=ST_INT; loopg.vars->data[idx].definitely_bound=true;
        emit_block_asm(&loopg,&s->as.fors.body);
        g->label_counter=loopg.label_counter;
        fputs("    add QWORD PTR [rsp],1\n",o);
    }
    /* The tagged loop checks its increment through rbl_inc_i64. The fast loop must
       not silently wrap instead. An exclusive range can never overflow here: its
       bound is end-1, so the final increment lands exactly on end. An inclusive
       range can overflow only when the last index reaches INT64_MAX, so the check
       is emitted only when that is reachable - the common loops stay unchecked. */
    if(r->as.range.inclusive && !(r->as.range.end->kind==EX_INT && r->as.range.end->as.i<INT64_MAX)){
        unsigned long ovf=aid(g);
        fprintf(o,"    jo .L_int_overflow_%lu\n    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",ovf,ovf,ovf,ovf);
    }
    fprintf(o,"    jmp .L_fast_for_%lu\n.L_fast_for_end_%lu:\n",loop,end);

    /* The loop variable is bound only when at least one iteration executed. */
    if(loop_reg_ok){
        fprintf(o,"    mov rax,QWORD PTR [rsp]\n    cmp rax,QWORD PTR [rsp+8]\n    jg .L_fast_for_noexec_%lu\n",noexec);
        fprintf(o,"    mov rax,QWORD PTR [rsp+8]\n    mov QWORD PTR [rbp%d+16],rax\n    mov QWORD PTR [rbp%d+8],1\n    mov QWORD PTR [rbp%d],1\n.L_fast_for_noexec_%lu:\n",slot_off(idx),slot_off(idx),slot_off(idx),noexec);
    }
    for(int rk=1;rk<2;rk++) if(loopg.reg_slots[rk]>=0)
        fprintf(o,"    mov QWORD PTR [rbp%d+16],%s\n    mov QWORD PTR [rbp%d+8],1\n    mov QWORD PTR [rbp%d],1\n",slot_off(loopg.reg_slots[rk]),reg_name_for_index(rk),slot_off(loopg.reg_slots[rk]),slot_off(loopg.reg_slots[rk]));
    fputs("    add rsp,16\n",o);
    if(idx>=0){g->vars->data[idx].type=old_loop_type;g->vars->data[idx].definitely_bound=old_loop_bound;}
}
static void emit_stmt_asm(ACG*g,Stmt*s){
    FILE*o=g->out;
    switch(s->kind){
        case ST_SET:{int idx=var_get_or_add(g->vars,s->as.assign.name);fprintf(o,"    lea rdi,[rbp%d]\n    lea rsi,[rip+%s]\n    call rbl_require_unbound\n",slot_off(idx),pool_intern(g->pool,s->as.assign.name));if(g->vars->data[idx].type==ST_INT&&expr_fast_int(g,s->as.assign.value)){if(!emit_fast_inplace_int_update(g,idx,s->as.assign.value)){emit_int_expr(g,s->as.assign.value);emit_store_int_slot(g,idx);}return;}else{emit_expr_asm(g,s->as.assign.value);emit_store_slot(o,idx);}return;}
        case ST_LET:{int idx=var_get_or_add(g->vars,s->as.assign.name);if(g->vars->data[idx].type==ST_INT&&g->vars->data[idx].definitely_bound&&expr_fast_int(g,s->as.assign.value)){if(!emit_fast_inplace_int_update(g,idx,s->as.assign.value)){emit_int_expr(g,s->as.assign.value);emit_store_int_slot(g,idx);}return;}emit_expr_asm(g,s->as.assign.value);fputs("    sub rsp,16\n    mov [rsp],rax\n    mov [rsp+8],rdx\n",o);fprintf(o,"    lea rdi,[rbp%d]\n    mov rsi,[rsp]\n    lea rdx,[rip+%s]\n    call rbl_require_rebind\n    mov rax,[rsp]\n    mov rdx,[rsp+8]\n    add rsp,16\n",slot_off(idx),pool_intern(g->pool,s->as.assign.name));emit_store_slot(o,idx);return;}
        case ST_BREAK:if(!g->break_label)fatal_at(s->pos,"break outside of a for loop");fprintf(o,"    jmp .L_for_end_%lu\n",g->break_label);return;
        case ST_CONTINUE:if(!g->continue_label)fatal_at(s->pos,"continue outside of a for loop");fprintf(o,"    jmp .L_for_cont_%lu\n",g->continue_label);return;
        case ST_RETURN:if(s->as.ret)emit_expr_asm(g,s->as.ret);else fputs("    xor eax,eax\n    xor edx,edx\n",o);fprintf(o,"    jmp .L_return_%zu\n",g->fi);return;
        case ST_EXPR:emit_expr_asm(g,s->as.expr);return;
        case ST_IF:{unsigned long end=aid(g),next=aid(g);bool fast=expr_fast_bool(g,s->as.ifs.cond);if(fast)emit_bool_expr(g,s->as.ifs.cond);else{emit_expr_asm(g,s->as.ifs.cond);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_bool\n",o);}fputs(fast?"    test rax,rax\n":"    test rdx,rdx\n",o);fprintf(o,"    jz .L_if_next_%lu\n",next);emit_block_asm(g,&s->as.ifs.then_block);fprintf(o,"    jmp .L_if_end_%lu\n.L_if_next_%lu:\n",end,next);for(size_t i=0;i<s->as.ifs.elif_len;i++){unsigned long n=aid(g);fast=expr_fast_bool(g,s->as.ifs.elifs[i].cond);if(fast)emit_bool_expr(g,s->as.ifs.elifs[i].cond);else{emit_expr_asm(g,s->as.ifs.elifs[i].cond);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_bool\n",o);}fputs(fast?"    test rax,rax\n":"    test rdx,rdx\n",o);fprintf(o,"    jz .L_if_next_%lu\n",n);emit_block_asm(g,&s->as.ifs.elifs[i].body);fprintf(o,"    jmp .L_if_end_%lu\n.L_if_next_%lu:\n",end,n);}if(s->as.ifs.has_else)emit_block_asm(g,&s->as.ifs.else_block);fprintf(o,".L_if_end_%lu:\n",end);return;}
        case ST_FOR:{
            /* Unified condition loop `for (cond)`: there is no loop variable, and
               `range` holds the condition. break/continue use the same labels as
               the range form. */
            if(!s->as.fors.var){
                Expr*c=s->as.fors.range;
                unsigned long loop=aid(g),end=aid(g),cont=aid(g),prev_b=g->break_label,prev_c=g->continue_label;
                g->break_label=end; g->continue_label=cont;
                fprintf(o,".L_for_%lu:\n",loop);
                if(expr_fast_bool(g,c)){emit_bool_expr(g,c);fputs("    test rax,rax\n",o);}
                else{emit_expr_asm(g,c);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_bool\n    test rdx,rdx\n",o);}
                fprintf(o,"    jz .L_for_end_%lu\n",end);
                emit_block_asm(g,&s->as.fors.body);
                fprintf(o,".L_for_cont_%lu:\n    jmp .L_for_%lu\n.L_for_end_%lu:\n",cont,loop,end);
                g->break_label=prev_b; g->continue_label=prev_c;
                return;
            }
            /* `for (x in <list>)`: a list is not a range, so iterate it by index.
               The iterable is evaluated once; break/continue use the usual labels. */
            if(s->as.fors.var && s->as.fors.range->kind!=EX_RANGE){
                int idx=var_find(g->vars,s->as.fors.var);
                unsigned long loop=aid(g),end=aid(g),cont=aid(g),prev_b=g->break_label,prev_c=g->continue_label;
                g->break_label=end; g->continue_label=cont;
                fputs("    sub rsp,32\n",o);
                emit_expr_asm(g,s->as.fors.range);
                /* 5 is TAG_LIST, 7 is TAG_TUPLE on both back ends; a clear message
                   beats the generic "expected a list" coming out of the helpers. */
                unsigned long ok=aid(g);
                fprintf(o,"    cmp rax,5\n    je .L_for_iter_%lu\n    cmp rax,7\n    je .L_for_iter_%lu\n    lea rdi,[rip+%s]\n    call rbl_fail\n.L_for_iter_%lu:\n",
                        ok,ok,pool_intern(g->pool,"for (x in ...) expects a range or a list"),ok);
                fputs("    mov [rsp+8],rax\n    mov [rsp+16],rdx\n    mov QWORD PTR [rsp],0\n",o);
                fprintf(o,".L_for_%lu:\n",loop);
                fputs("    mov rdi,[rsp+8]\n    mov rsi,[rsp+16]\n    call rbl_list_len\n    cmp rdx,[rsp]\n    jle ",o);
                fprintf(o,".L_for_end_%lu\n",end);
                fputs("    mov rdi,[rsp+8]\n    mov rsi,[rsp+16]\n    mov rdx,[rsp]\n    call rbl_list_get\n",o);
                emit_store_slot(o,idx);
                emit_block_asm(g,&s->as.fors.body);
                fprintf(o,".L_for_cont_%lu:\n",cont);
                fputs("    mov rax,[rsp]\n    inc rax\n    mov [rsp],rax\n",o);
                fprintf(o,"    jmp .L_for_%lu\n.L_for_end_%lu:\n    add rsp,32\n",loop,end);
                g->break_label=prev_b; g->continue_label=prev_c;
                return;
            }
            if(s->as.fors.range->kind==EX_RANGE){
                Expr*r=s->as.fors.range;int idx=var_find(g->vars,s->as.fors.var);ACG probe=*g;
                StaticType rt1=infer_expr_type(g->prog,r->as.range.start,g->vars),rt2=infer_expr_type(g->prog,r->as.range.end,g->vars);
                if(idx>=0&&rt1==ST_INT&&rt2==ST_INT&&expr_fast_int(g,r->as.range.start)&&expr_fast_int(g,r->as.range.end)){
                    StaticType old_type=g->vars->data[idx].type;bool old_bound=g->vars->data[idx].definitely_bound;g->vars->data[idx].type=ST_INT;g->vars->data[idx].definitely_bound=true;bool fast_body=block_fast_int(g,&s->as.fors.body);g->vars->data[idx].type=old_type;g->vars->data[idx].definitely_bound=old_bound;
                    if(fast_body){(void)probe;emit_fast_for(g,s);return;}
                }
            }
            if(s->as.fors.range->kind!=EX_RANGE){fprintf(o,"    lea rdi,[rip+%s]\n    call rbl_fail\n",pool_intern(g->pool,"range is only valid in for"));return;}
            int idx=var_get_or_add(g->vars,s->as.fors.var);unsigned long loop=aid(g),end=aid(g);fputs("    sub rsp,32\n",o);emit_expr_asm(g,s->as.fors.range->as.range.start);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_int\n    mov [rsp],rdx\n",o);emit_expr_asm(g,s->as.fors.range->as.range.end);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_int\n    mov [rsp+8],rdx\n",o);if(s->as.fors.range->as.range.inclusive)fputs("    mov rax,[rsp+8]\n    mov [rsp+16],rax\n",o);else fputs("    mov rdi,[rsp+8]\n    call rbl_dec_i64\n    mov [rsp+16],rax\n",o);fputs("    mov rax,[rsp]\n    mov [rsp+24],rax\n",o);fprintf(o,".L_for_%lu:\n    mov rax,[rsp+24]\n    cmp rax,[rsp+16]\n    jg .L_for_end_%lu\n",loop,end);fputs("    mov eax,1\n    mov rdx,[rsp+24]\n",o);emit_store_slot(o,idx);
            /* break/continue targets for this loop (nested loops save and restore). */
            unsigned long cont=aid(g),prev_b=g->break_label,prev_c=g->continue_label;
            g->break_label=end; g->continue_label=cont;
            emit_block_asm(g,&s->as.fors.body);
            g->break_label=prev_b; g->continue_label=prev_c;
            fprintf(o,".L_for_cont_%lu:\n",cont);
            fputs("    mov rdi,[rsp+24]\n    call rbl_inc_i64\n    mov [rsp+24],rax\n",o);fprintf(o,"    jmp .L_for_%lu\n.L_for_end_%lu:\n    add rsp,32\n",loop,end);return;
        }
    }
}

static void emit_function_asm(FILE*o,Program*p,size_t fi,StrPool*pool){
    Function*f=&p->funcs.data[fi];VarVec vars={0};for(size_t i=0;i<f->params.len;i++)var_get_or_add(&vars,f->params.data[i].name);for(size_t i=0;i<f->body.len;i++)collect_stmt_vars(f->body.data[i],&vars);for(size_t i=0;i<f->params.len;i++){int idx=var_find(&vars,f->params.data[i].name);vars.data[idx].type=type_from_name(f->params.data[i].type);vars.data[idx].definitely_bound=true;}infer_stmt_seq(p,&f->body,&vars,false);
    int frame=align16((int)(40 + vars.len*24));
    if(!target_windows())fprintf(o,"\n.type fn_%zu,@function",fi);
    fprintf(o,"\nfn_%zu:\n    push rbp\n    mov rbp,rsp\n    push r12\n    push r13\n    push r14\n    push r15\n",fi);if(frame)fprintf(o,"    sub rsp,%d\n",frame);for(size_t zi=0;zi<vars.len;zi++)fprintf(o,"    mov QWORD PTR [rbp%d],0\n",slot_off((int)zi));fputs("    mov r12,rdi\n    mov r13,rsi\n",o);fputs("    call rbl_gc_init\n",o);const char*fl=pool_intern(pool,f->name);fprintf(o,"    lea rdi,[rip+%s]\n    mov esi,%zu\n    mov edx,r13d\n    call rbl_check_arity\n",fl,f->params.len);for(size_t i=0;i<f->params.len;i++){Param*q=&f->params.data[i];const char*expected=pool_intern(pool,q->type),*param=pool_intern(pool,q->name);int idx=var_find(&vars,q->name);if(q->type[0])fprintf(o,"    lea rdi,[r12+%zu]\n    lea rsi,[rip+%s]\n    lea rdx,[rip+%s]\n    lea rcx,[rip+%s]\n    call rbl_check_type\n",i*16,expected,param,fl);fprintf(o,"    mov rax,[r12+%zu]\n    mov [rbp%d+8],rax\n    mov rdx,[r12+%zu]\n    mov [rbp%d+16],rdx\n    mov QWORD PTR [rbp%d],1\n",i*16,slot_off(idx),i*16+8,slot_off(idx),slot_off(idx));}
    ACG g={0}; g.out=o; g.prog=p; g.fn=f; g.vars=&vars; g.pool=pool; g.fi=fi; g.label_counter=g_label_seq; g.fast_slots=false; g.reg_slots[0]=g.reg_slots[1]=g.reg_slots[2]=-1;emit_block_asm(&g,&f->body);g_label_seq=g.label_counter+1;fputs("    xor eax,eax\n    xor edx,edx\n",o);fprintf(o,".L_return_%zu:\n",fi);fputs("    lea rsp,[rbp-32]\n    pop r15\n    pop r14\n    pop r13\n    pop r12\n    pop rbp\n    ret\n",o);for(size_t i=0;i<vars.len;i++)free(vars.data[i].name);free(vars.data);
}

static void emit_fast_function_asm(FILE*o,Program*p,size_t fi,StrPool*pool){
    Function*f=&p->funcs.data[fi];VarVec vars={0};for(size_t i=0;i<f->params.len;i++)var_get_or_add(&vars,f->params.data[i].name);for(size_t i=0;i<f->body.len;i++)collect_stmt_vars(f->body.data[i],&vars);for(size_t i=0;i<f->params.len;i++){int idx=var_find(&vars,f->params.data[i].name);vars.data[idx].type=type_from_name(f->params.data[i].type);vars.data[idx].definitely_bound=true;}infer_stmt_seq(p,&f->body,&vars,false);
    int frame=align16((int)(vars.len*8));
    if(!target_windows())fprintf(o,"\n.type fast_fn_%zu,@function",fi);
    fprintf(o,"\nfast_fn_%zu:\n    push rbp\n    mov rbp,rsp\n",fi);if(frame)fprintf(o,"    sub rsp,%d\n",frame);
    static const char*regs[]={"rdi","rsi","rdx","rcx","r8","r9"};for(size_t i=0;i<f->params.len;i++){int idx=var_find(&vars,f->params.data[i].name);fprintf(o,"    mov QWORD PTR [rbp%d],%s\n",fast_slot_off(idx),regs[i]);}
    ACG g={0}; g.out=o; g.prog=p; g.fn=f; g.vars=&vars; g.pool=pool; g.fi=fi; g.label_counter=g_label_seq; g.fast_slots=true; g.reg_slots[0]=g.reg_slots[1]=g.reg_slots[2]=-1;emit_int_expr(&g,f->body.data[0]->as.ret);g_label_seq=g.label_counter+1;fputs("    leave\n    ret\n",o);for(size_t i=0;i<vars.len;i++)free(vars.data[i].name);free(vars.data);
}

static void emit_program(FILE*o,Program*p,const char*source){
    StrPool pool={0};int main_id=function_last_index_asm(p,"main");if(main_id<0)fatal("function main not found");
    fprintf(o,"# RBL direct x86-64 ASM generated from %s (%s)\n.intel_syntax noprefix\n.text\n",source,target_windows()?"windows x86-64 / PE":"linux x86-64 / ELF");
    for(size_t i=0;i<p->funcs.len;i++)emit_function_asm(o,p,i,&pool);
    for(size_t i=0;i<p->funcs.len;i++)if(function_is_fast_int(p,(int)i,0))emit_fast_function_asm(o,p,i,&pool);
    if(target_windows()){
        /* The C runtime enters main with the Microsoft x64 convention: 16-byte
           stack alignment, 32 bytes of shadow space, and no return to the CRT
           because the runtime terminates the process. Every RBL function keeps
           the System V convention, which is what the C runtime is compiled for. */
        fputs("\n.globl main\nmain:\n    sub rsp, 40\n    xor edi, edi\n    xor esi, esi\n",o);
        fprintf(o,"    call fn_%d\n    xor edi, edi\n    call rbl_process_exit\n    ud2\n",main_id);
        fputs("\n.section .rdata,\"dr\"\n.p2align 3\n",o);
    }else{
        fputs("\n.globl _start\n.type _start,@function\n_start:\n    and rsp,-16\n    xor edi,edi\n    xor esi,esi\n",o);
        fprintf(o,"    call fn_%d\n    xor edi,edi\n    call rbl_process_exit\n    ud2\n",main_id);
        fputs("\n.section .rodata\n.p2align 3\n",o);
    }
    for(size_t i=0;i<pool.len;i++){fprintf(o,"%s:\n",pool.data[i].label);emit_asm_bytes(o,pool.data[i].text);}
    if(!target_windows())fputs(".section .note.GNU-stack,\"\",@progbits\n",o);
    pool_free(&pool);
}

static char *read_file_asm(const char*path){FILE*f=fopen(path,"rb");if(!f){perror(path);exit(1);}fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0)fatal("cannot stat input");char*s=xmalloc((size_t)n+1);if(fread(s,1,(size_t)n,f)!=(size_t)n){perror("read");exit(1);}s[n]=0;fclose(f);return s;}
static void usage_asm(const char*a){fprintf(stderr,"rblc-asm 0.7.0-stdlib-paren-blocks-x86_64\nUsage: %s input.rbl [-S output.s] [--check] [--target linux|win]\n",a);}
int main(int argc,char**argv){
    if(argc<2){usage_asm(argv[0]);return 2;}const char*input=NULL,*outpath=NULL;bool check=false;
    for(int i=1;i<argc;i++){if((!strcmp(argv[i],"-S")||!strcmp(argv[i],"-o"))&&i+1<argc){outpath=argv[++i];continue;}if(!strcmp(argv[i],"--check")){check=true;continue;}if(!strcmp(argv[i],"--target")&&i+1<argc){const char*t=argv[++i];if(!strcmp(t,"win")||!strcmp(t,"win-x86_64")||!strcmp(t,"windows")||!strcmp(t,"windows-x86_64"))g_target=TARGET_WINDOWS_PE;else if(!strcmp(t,"linux")||!strcmp(t,"linux-x86_64")||!strcmp(t,"elf"))g_target=TARGET_LINUX_ELF;else fatal("unknown target '%s' (expected linux or win)",t);continue;}if(!strcmp(argv[i],"--version")){puts("rblc-asm 0.7.0-stdlib-paren-blocks-x86_64");return 0;}if(argv[i][0]=='-'){usage_asm(argv[0]);return 2;}if(!input)input=argv[i];else{usage_asm(argv[0]);return 2;}}
    if(!input){usage_asm(argv[0]);return 2;}char*src=read_file_asm(input);size_t nt=0;Token*toks=lex_all(src,input,&nt);Program prog=parse_program(toks,nt,input);
    /* Some diagnostics are produced by code generation (break/continue outside a
       loop, for example), so --check must run the back end too instead of stopping
       after parsing; the assembly itself goes to a throwaway stream. */
    if(check){FILE*sink=tmpfile();if(!sink){fprintf(stderr,"rblc-asm: cannot create a temporary file for --check\n");return 1;}emit_program(sink,&prog,input);fclose(sink);printf("OK: %s\n",input);free_tokens(toks,nt);free(src);return 0;}
    char buf[4096];if(!outpath){snprintf(buf,sizeof(buf),"%s.s",input);outpath=buf;}FILE*out=fopen(outpath,"wb");if(!out){perror(outpath);return 1;}emit_program(out,&prog,input);fclose(out);fprintf(stderr,"ASM emitted: %s\n",outpath);free_tokens(toks,nt);free(src);return 0;
}
