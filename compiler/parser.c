#include "rblc_internal.h"

static Token *p_peek(Parser *p);
static Token p_adv(Parser *p);
static bool p_is(Parser *p, TokenKind k);
static void p_expect(Parser *p, TokenKind k, const char *ctx);
static char *p_ident(Parser *p, const char *ctx);
static bool p_two_adjacent(Parser *p, TokenKind a, TokenKind b);
static Expr *mk_binary(SourcePos pos, BinaryOp op, Expr *l, Expr *r);
static Expr *mk_self_op_expr(SourcePos pos, const char *name, BinaryOp op, Expr *rhs);
static Expr *mk_self_op(SourcePos pos, const char *name, BinaryOp op);
static Stmt *parse_switch(Parser *p);
static Expr *fstring_literal_expr(SourcePos pos, const char *raw, size_t len);
static Expr *fstring_expr(SourcePos pos, SourcePos raw_pos, const char *raw);
static void validate_fstring_spec(SourcePos pos, const char *spec, size_t len);
static Expr *new_str_expr(SourcePos pos, const char *s);
static int struct_lookup(const char *name);
static Stmt *parse_struct(Parser *p);
static Expr *builtin_call0(SourcePos pos, const char *name);
static Expr *builtin_call1(SourcePos pos, const char *name, Expr *a);
static Expr *builtin_call2(SourcePos pos, const char *name, Expr *a, Expr *b);
static Expr *builtin_call3(SourcePos pos, const char *name, Expr *a, Expr *b, Expr *c3);
static Expr *parse_primary(Parser *p);
static Expr *parse_atom(Parser *p);
static Expr *parse_unary(Parser *p);
static Expr *parse_mul(Parser *p);
static Expr *parse_add(Parser *p);
static Expr *parse_range(Parser *p);
static Expr *parse_cmp(Parser *p);
static Expr *parse_eq(Parser *p);
static Expr *parse_and(Parser *p);
static Expr *parse_or(Parser *p);
static Expr *parse_expr(Parser *p);
static Stmt *parse_stmt(Parser *p);
static StmtVec parse_block(Parser *p);
static Function parse_function(Parser *p);

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
    /* The synthetic opening quote is not present in the original source. Map it
       to the byte/column immediately before this literal run so the first real
       character (and any escape diagnostic) retains its original location. */
    SourcePos synthetic_quote = pos;
    if (synthetic_quote.pos) synthetic_quote.pos--;
    /* Column zero is intentional when the literal starts at column one: the
       synthetic quote advances the lexer back to the first real source column. */
    synthetic_quote.col--;
    diagnostic_set_stage(DIAG_STAGE_LEXER);
    Token *toks = lex_all_at(src, pos.file, &nt, synthetic_quote);
    diagnostic_set_stage(DIAG_STAGE_PARSER);
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

/* Validate the deliberately small, static format grammar. */
static void validate_fstring_spec(SourcePos pos, const char *spec, size_t len) {
    size_t i = 0;
    for (size_t j = 0; j < len; j++) {
        if (spec[j] == '{')
            fatal_at(source_pos_advance(pos, spec, j), "dynamic or nested f-string format specifications are not supported");
    }
    if (i < len) {
        size_t cp = utf8_sequence_length((const unsigned char *)spec, len, i);
        if (!cp) fatal_at(source_pos_advance(pos, spec, i), "invalid UTF-8 in f-string format spec");
        /* A fill+alignment pair takes precedence, even when fill is itself
           '<', '>' or '^'. Otherwise a leading alignment character stands alone. */
        if (i + cp < len && (spec[i + cp] == '<' || spec[i + cp] == '>' || spec[i + cp] == '^'))
            i += cp + 1;
        else if (spec[i] == '<' || spec[i] == '>' || spec[i] == '^')
            i++;
    }
    size_t width = 0;
    size_t width_start = i;
    while (i < len && spec[i] >= '0' && spec[i] <= '9') {
        unsigned digit = (unsigned)(spec[i++] - '0');
        if (width > 1000000u / 10u || width * 10u + digit > 1000000u)
            fatal_at(source_pos_advance(pos, spec, width_start), "f-string format width exceeds 1000000");
        width = width * 10u + digit;
    }
    if (i < len && spec[i] == '.') {
        size_t precision_start = i++;
        size_t precision = 0, digits = 0;
        while (i < len && spec[i] >= '0' && spec[i] <= '9') {
            unsigned digit = (unsigned)(spec[i++] - '0');
            if (precision > 17u / 10u || precision * 10u + digit > 17u)
                fatal_at(source_pos_advance(pos, spec, precision_start), "f-string precision must be between 0 and 17");
            precision = precision * 10u + digit;
            digits++;
        }
        if (!digits) fatal_at(source_pos_advance(pos, spec, precision_start), "f-string precision requires digits");
        if (precision > 17) fatal_at(source_pos_advance(pos, spec, precision_start), "f-string precision must be between 0 and 17");
    }
    if (i < len) {
        size_t type_pos = i;
        char type = spec[i++];
        if (!strchr("dboxXfFeEgGs", type))
            fatal_at(source_pos_advance(pos, spec, type_pos), "unsupported f-string format type '%c'", type);
    }
    if (i != len) fatal_at(source_pos_advance(pos, spec, i), "invalid character in f-string format spec");
}

/* Interpolated string -> string concatenation. Literal runs become string
   literals, unformatted {expression} parts become str(expression) calls, and
   formatted parts become internal Runtime-format calls. Parts are joined with
   '+'. '{{' and '}}' produce literal braces. */
 static Expr *fstring_expr(SourcePos pos, SourcePos raw_pos, const char *raw) {
    Expr *result = NULL;
    size_t cap = 32, n = 0;
    char *lit = xmalloc(cap);
    size_t i = 0;
    size_t lit_start = 0;
    while (raw[i]) {
        if ((raw[i] == '{' && raw[i + 1] == '{') ||
            (raw[i] == '}' && raw[i + 1] == '}')) {
            if (n) {
                Expr *s = fstring_literal_expr(source_pos_advance(raw_pos, raw, lit_start), lit, n);
                result = result ? mk_binary(pos, B_ADD, result, s) : s;
                n = 0;
            }
            char brace[2] = { raw[i], '\0' };
            Expr *s = new_str_expr(source_pos_advance(raw_pos, raw, i), brace);
            result = result ? mk_binary(pos, B_ADD, result, s) : s;
            i += 2;
            lit_start = i;
            continue;
        }
        if (raw[i] == '}') fatal_at(source_pos_advance(raw_pos, raw, i), "single '}' is not allowed in an f-string; write '}}'");
        if (raw[i] != '{') { str_push(&lit, &n, &cap, raw[i]); i++; continue; }

        if (n) {
            Expr *s = fstring_literal_expr(source_pos_advance(raw_pos, raw, lit_start), lit, n);
            result = result ? mk_binary(pos, B_ADD, result, s) : s;
            n = 0;
        }
        size_t j = i + 1, brace_depth = 0, paren_depth = 0, bracket_depth = 0;
        size_t colon = (size_t)-1;
        char quote = 0;
        bool escaped = false;
        while (raw[j]) {
            char c = raw[j];
            if (quote) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == quote) quote = 0;
                j++;
                continue;
            }
            if (c == '"') { quote = c; j++; continue; }
            /* Match the normal lexer's line-comment rules while searching for
               the interpolation terminator. Braces and format colons inside
               comments are source text, not expression delimiters. */
            if (c == '#' || (c == '/' && raw[j + 1] == '/')) {
                while (raw[j] && raw[j] != '\n' && raw[j] != '\r') j++;
                continue;
            }
            if (c == '{') brace_depth++;
            else if (c == '}') { if (!brace_depth) break; brace_depth--; }
            else if (c == '(') paren_depth++;
            else if (c == ')' && paren_depth) paren_depth--;
            else if (c == '[') bracket_depth++;
            else if (c == ']' && bracket_depth) bracket_depth--;
            else if (c == ':' && !brace_depth && !paren_depth && !bracket_depth && colon == (size_t)-1) {
                /* Once the top-level format separator is found, the remainder
                   is a static format spec, not RBL expression syntax. Scan it
                   to the interpolation's closing brace without treating '#',
                   quotes or nested delimiters as comments/string expressions.
                   Nested/dynamic specs are rejected by validate_fstring_spec. */
                colon = j++;
                while (raw[j] && raw[j] != '}') j++;
                break;
            }
            j++;
        }
        if (!raw[j]) fatal_at(source_pos_advance(raw_pos, raw, i), "unterminated '{' in an f-string");
        size_t expr_end = colon == (size_t)-1 ? j : colon;
        size_t expr_len = expr_end - i - 1;
        if (!expr_len) fatal_at(source_pos_advance(raw_pos, raw, i + 1), "empty expression in an f-string");

        char *spec = NULL;
        if (colon != (size_t)-1) {
            size_t spec_len = j - colon - 1;
            SourcePos spec_pos = source_pos_advance(raw_pos, raw, colon + 1);
            spec = xstrndup(raw + colon + 1, spec_len);
            validate_fstring_spec(spec_pos, spec, spec_len);
        }

        char *text = xstrndup(raw + i + 1, expr_len);
        SourcePos expr_pos = source_pos_advance(raw_pos, raw, i + 1);
        size_t nt = 0;
        diagnostic_set_stage(DIAG_STAGE_LEXER);
        Token *toks = lex_all_at(text, expr_pos.file, &nt, expr_pos);
        diagnostic_set_stage(DIAG_STAGE_PARSER);
        Parser sub = { .toks = toks, .len = nt, .pos = 0, .file = expr_pos.file };
        Expr *inner = parse_expr(&sub);
        if (!p_is(&sub, TOK_EOF)) fatal_at(p_peek(&sub)->pos, "unexpected token in an f-string expression");
        free_tokens(toks, nt);
        free(text);

        ExprVec args = exprvec_new();
        exprvec_push(&args, inner);
        Expr *call = new_expr(expr_pos, EX_CALL);
        call->as.call.callee = xstrdup(spec ? "__rbl_format_value" : "str");
        if (spec) exprvec_push(&args, new_str_expr(source_pos_advance(raw_pos, raw, colon + 1), spec));
        call->as.call.args = xmalloc(sizeof(ExprVec));
        *call->as.call.args = args;
        free(spec);
        result = result ? mk_binary(pos, B_ADD, result, call) : call;
        i = j + 1;
        lit_start = i;
    }
    if (n) {
        Expr *s = fstring_literal_expr(source_pos_advance(raw_pos, raw, lit_start), lit, n);
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
    /*idk shit*/
    switch(t.kind) {
        case TOK_INT: { Expr *e=new_expr(t.pos,EX_INT); e->as.i=t.i; return e; }
        case TOK_FLOAT: { Expr *e=new_expr(t.pos,EX_FLOAT); e->as.f=t.f; return e; }
        case TOK_STR: { Expr *e=new_expr(t.pos,EX_STR); e->as.s=xstrdup(t.text); return e; }
        case TOK_FSTR: return fstring_expr(t.pos, t.content_pos, t.text);
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
                    double cv = 0.0;
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
    /* Parentheses are the only block delimiter in RBL 0.7.2. Braces are reserved
       for dictionary literals, so '{' is reported explicitly instead of being
       silently accepted as legacy syntax. */
    if (p_is(p,TOK_LBRACE)) fatal_at(p_peek(p)->pos,"braces are not a block delimiter; use '(' and ')' (braces are reserved for dictionary literals)");
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

 Program parse_program(Token *toks, size_t n, const char *file) {
    Parser p={toks,n,0,file}; Program prog={0};
    while(!p_is(&p,TOK_EOF)){
        if(p_is(&p,TOK_IDENT) && !strcmp(p_peek(&p)->text,"struct")){ (void)parse_struct(&p); continue; }
        if(!p_is(&p,TOK_FUNC)) fatal_at(p_peek(&p)->pos,"top-level declaration must start with func or struct");
        funcvec_push(&prog.funcs,parse_function(&p));
    }
    return prog;
}

/* ========================== CODEGEN SUPPORT ========================== */

