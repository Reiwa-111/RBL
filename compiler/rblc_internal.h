#ifndef RBL_COMPILER_INTERNAL_H
#define RBL_COMPILER_INTERNAL_H

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Source locations are byte offsets plus one-based line/column coordinates. */
typedef struct {
    const char *file;
    size_t pos;
    size_t line;
    size_t col;
} SourcePos;

typedef enum {
    DIAG_STAGE_NONE,
    DIAG_STAGE_LEXER,
    DIAG_STAGE_PARSER,
    DIAG_STAGE_SEMANTIC,
    DIAG_STAGE_CODEGEN
} DiagnosticStage;

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
    SourcePos content_pos; /* opening-body position for tokens with raw text, e.g. f-strings */
} Token;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    const char *file;
    size_t base_offset;
} Lexer;

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

typedef struct Stmt Stmt;
typedef struct StmtVec { Stmt **data; size_t len, cap; } StmtVec;
typedef struct { char *name; char *type; SourcePos pos; } Param;
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
typedef struct {
    FuncVec funcs;
    size_t *function_index;
    size_t function_index_cap;
} Program;
typedef struct { Token *toks; size_t len; size_t pos; const char *file; } Parser;

typedef enum { ST_UNKNOWN, ST_INT, ST_FLOAT, ST_BOOL, ST_STR, ST_UNIT } StaticType;
typedef struct { char *name; StaticType type; bool definitely_bound; } Var;
typedef struct { Var *data; size_t len, cap; size_t *index; size_t index_cap; } VarVec;

typedef enum { TARGET_LINUX_ELF, TARGET_WINDOWS_PE } TargetKind;
typedef struct { char *text; char *label; } StrConst;
typedef struct { StrConst *data; size_t len, cap; size_t *index; size_t index_cap; } StrPool;
typedef struct {
    FILE *out; Program *prog; Function *fn; VarVec *vars; StrPool *pool; size_t fi; unsigned long label_counter; bool fast_slots;
    int reg_slots[3];
    int fast_depth;
    bool suppress_gc_safepoints;
    unsigned long break_label, continue_label;
} ACG;

extern TargetKind g_target;

/* Private cross-module API. Keep translation-unit-local helpers out of this list. */
void fatal_at(SourcePos p, const char *fmt, ...);
void fatal(const char *fmt, ...);
void diagnostic_set_source(const char *file, const char *source, size_t length);
void diagnostic_set_stage(DiagnosticStage stage);
SourcePos source_pos_advance(SourcePos pos, const char *text, size_t length);
size_t utf8_sequence_length(const unsigned char *text, size_t length, size_t offset);
size_t rbl_hash_string(const char *text);
void * xmalloc(size_t n);
void * xcalloc(size_t n, size_t s);
void * xrealloc(void *p, size_t n);
char * xstrdup(const char *s);
char * xstrndup(const char *s, size_t n);
Token * lex_all(const char *src, const char *file, size_t *out_n);
Token * lex_all_at(const char *src, const char *file, size_t *out_n, SourcePos origin);
void free_tokens(Token *toks, size_t n);
const char * tok_name(TokenKind k);
void str_push(char **buf, size_t *n, size_t *cap, char c);

ExprVec exprvec_new(void);
StmtVec stmtvec_new(void);
ParamVec paramvec_new(void);
void exprvec_push(ExprVec *v, Expr *e);
void stmtvec_push(StmtVec *v, Stmt *s);
void paramvec_push(ParamVec *v, Param p);
void funcvec_push(FuncVec *v, Function f);
void elif_push(IfStmt *ifs, Elif e);
Expr * new_expr(SourcePos p, ExprKind k);
Stmt * new_stmt(SourcePos p, StmtKind k);
Program parse_program(Token *toks, size_t n, const char *file);
void validate_program(Program *program, SourcePos eof_pos);

int var_find(VarVec *v,const char *name);
int var_get_or_add(VarVec *v,const char *name);
void varvec_free(VarVec *v);
StaticType type_from_name(const char *s);
int function_last_index(Program *p,const char *name);
void program_index_free(Program *p);
bool function_is_fast_int(Program *p,int fi,int depth);
StaticType infer_expr_type(Program *p, Expr *e, VarVec *vars);
void infer_stmt_seq(Program *p, StmtVec *v, VarVec *vars, bool controlled);
void collect_stmt_vars(Stmt *s, VarVec *vars);
void emit_program(FILE*o,Program*p,const char*source);

#endif /* RBL_COMPILER_INTERNAL_H */
