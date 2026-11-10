#include "rblc_internal.h"

static StaticType function_return_static(Program *p,const char *name);
static StaticType builtin_static_type(Program *p, Expr *e, VarVec *vars);
static StaticType builtin_method_type(Program *p, Expr *e, VarVec *vars);
static void collect_expr_vars(Expr *e, VarVec *vars);

static void var_index_insert(VarVec *v,size_t index){
    size_t slot=rbl_hash_string(v->data[index].name)&(v->index_cap-1);
    while(v->index[slot]) slot=(slot+1)&(v->index_cap-1);
    v->index[slot]=index+1;
}
static void var_index_rebuild(VarVec *v,size_t capacity){
    size_t *next=xcalloc(capacity,sizeof(*next));
    free(v->index);
    v->index=next;
    v->index_cap=capacity;
    for(size_t i=0;i<v->len;i++) var_index_insert(v,i);
}
 int var_find(VarVec *v,const char *name){
    if(!v->index_cap) return -1;
    size_t slot=rbl_hash_string(name)&(v->index_cap-1);
    while(v->index[slot]){
        size_t index=v->index[slot]-1;
        if(!strcmp(v->data[index].name,name)) return (int)index;
        slot=(slot+1)&(v->index_cap-1);
    }
    return -1;
}
 int var_get_or_add(VarVec *v,const char *name){
    int i=var_find(v,name);if(i>=0)return i;
    if(v->len==v->cap){if(v->cap>SIZE_MAX/2/sizeof(*v->data))fatal("too many variables in function");v->cap=v->cap?v->cap*2:8;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));}
    v->data[v->len].name=xstrdup(name);v->data[v->len].type=ST_UNKNOWN;v->data[v->len].definitely_bound=false;
    size_t added=v->len++;
    if(!v->index_cap) var_index_rebuild(v,16);
    else if(v->len>v->index_cap-v->index_cap/3){if(v->index_cap>SIZE_MAX/2/sizeof(*v->index))fatal("variable index capacity overflow");var_index_rebuild(v,v->index_cap*2);}
    else var_index_insert(v,added);
    return (int)added;
}
void varvec_free(VarVec *v){
    for(size_t i=0;i<v->len;i++) free(v->data[i].name);
    free(v->data);
    free(v->index);
    memset(v,0,sizeof(*v));
}
 StaticType type_from_name(const char *s){
    if(!s)return ST_UNKNOWN;
    if(!strcmp(s,"Int")||!strcmp(s,"int")||!strcmp(s,"integer"))return ST_INT;
    if(!strcmp(s,"Float")||!strcmp(s,"float"))return ST_FLOAT;
    if(!strcmp(s,"Bool")||!strcmp(s,"bool"))return ST_BOOL;
    if(!strcmp(s,"String")||!strcmp(s,"string")||!strcmp(s,"Str")||!strcmp(s,"str"))return ST_STR;
    if(!strcmp(s,"Unit")||!strcmp(s,"unit"))return ST_UNIT;
    return ST_UNKNOWN;
}
/* Program.funcs is complete and immutable by the time semantic/codegen lookup
   begins, so this lazy index is built once and remains valid until cleanup. */
 int function_last_index(Program *p,const char *name){
    if(!p->function_index_cap){
        size_t cap=16;
        while(p->funcs.len>cap/2){if(cap>SIZE_MAX/2/sizeof(*p->function_index))fatal("function index capacity overflow");cap*=2;}
        p->function_index=xcalloc(cap,sizeof(*p->function_index));
        p->function_index_cap=cap;
        for(size_t i=0;i<p->funcs.len;i++){
            size_t slot=rbl_hash_string(p->funcs.data[i].name)&(cap-1);
            while(p->function_index[slot]){
                size_t old=p->function_index[slot]-1;
                if(!strcmp(p->funcs.data[old].name,p->funcs.data[i].name)) break;
                slot=(slot+1)&(cap-1);
            }
            p->function_index[slot]=i+1; /* Later definitions intentionally win. */
        }
    }
    size_t slot=rbl_hash_string(name)&(p->function_index_cap-1);
    while(p->function_index[slot]){
        size_t index=p->function_index[slot]-1;
        if(!strcmp(p->funcs.data[index].name,name)) return (int)index;
        slot=(slot+1)&(p->function_index_cap-1);
    }
    return -1;
}
void program_index_free(Program *p){free(p->function_index);p->function_index=NULL;p->function_index_cap=0;}

static void validate_stmt_locations(Stmt *s, unsigned loop_depth) {
    switch (s->kind) {
        case ST_BREAK:
            if (!loop_depth) fatal_at(s->pos, "break outside of a for loop");
            break;
        case ST_CONTINUE:
            if (!loop_depth) fatal_at(s->pos, "continue outside of a for loop");
            break;
        case ST_IF:
            for (size_t i = 0; i < s->as.ifs.then_block.len; i++)
                validate_stmt_locations(s->as.ifs.then_block.data[i], loop_depth);
            for (size_t j = 0; j < s->as.ifs.elif_len; j++)
                for (size_t i = 0; i < s->as.ifs.elifs[j].body.len; i++)
                    validate_stmt_locations(s->as.ifs.elifs[j].body.data[i], loop_depth);
            if (s->as.ifs.has_else)
                for (size_t i = 0; i < s->as.ifs.else_block.len; i++)
                    validate_stmt_locations(s->as.ifs.else_block.data[i], loop_depth);
            break;
        case ST_FOR:
            for (size_t i = 0; i < s->as.fors.body.len; i++)
                validate_stmt_locations(s->as.fors.body.data[i], loop_depth + 1);
            break;
        default:
            break;
    }
}

void validate_program(Program *program, SourcePos eof_pos) {
    bool has_main = false;
    for (size_t i = 0; i < program->funcs.len; i++) {
        Function *fn = &program->funcs.data[i];
        if (!strcmp(fn->name, "main")) has_main = true;

        for (size_t a = 0; a < fn->params.len; a++) {
            for (size_t b = a + 1; b < fn->params.len; b++) {
                if (!strcmp(fn->params.data[a].name, fn->params.data[b].name))
                    fatal_at(fn->params.data[b].pos,
                             "parameter '%s' is declared more than once in function '%s'",
                             fn->params.data[b].name, fn->name);
            }
        }

        for (size_t j = 0; j < fn->body.len; j++)
            validate_stmt_locations(fn->body.data[j], 0);
    }
    if (!has_main) fatal_at(eof_pos, "function main not found");
}

 static StaticType function_return_static(Program *p,const char *name){
    int fi=function_last_index(p,name);
    if(fi<0)return ST_UNKNOWN;
    Function *f=&p->funcs.data[fi];return f->has_return_type?type_from_name(f->return_type):ST_UNKNOWN;
}

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

 static bool semantic_fast_int_expr(Program *p,Expr *e,VarVec *vars,int depth);

 StaticType infer_expr_type(Program *p, Expr *e, VarVec *vars){
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

 void infer_stmt_seq(Program *p, StmtVec *v, VarVec *vars, bool controlled){
    for(size_t i=0;i<v->len;i++){
        Stmt *s=v->data[i];
        switch(s->kind){
            case ST_SET:{
                int idx=var_get_or_add(vars,s->as.assign.name);StaticType t=infer_expr_type(p,s->as.assign.value,vars);
                if(s->as.assign.value && s->as.assign.value->kind==EX_CALL){
                    int cfi=function_last_index(p,s->as.assign.value->as.call.callee);
                    if(cfi<0||!function_is_fast_int(p,cfi,0))t=ST_UNKNOWN;
                }
                vars->data[idx].type=t;
                if(!controlled)vars->data[idx].definitely_bound=true;
                break;
            }
            case ST_LET:{
                int idx=var_get_or_add(vars,s->as.assign.name);StaticType t=infer_expr_type(p,s->as.assign.value,vars);
                if(s->as.assign.value && s->as.assign.value->kind==EX_CALL){
                    int cfi=function_last_index(p,s->as.assign.value->as.call.callee);
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

/* The integer fast-path eligibility check belongs to semantic analysis: it
   describes a proven expression subset and must not depend on the ASM emitter. */
static bool semantic_fast_int_expr(Program *p,Expr *e,VarVec *vars,int depth){
    if(!e || infer_expr_type(p,e,vars)!=ST_INT) return false;
    switch(e->kind){
        case EX_INT: return true;
        case EX_IDENT:{int i=var_find(vars,e->as.ident);return i>=0&&vars->data[i].type==ST_INT&&vars->data[i].definitely_bound;}
        case EX_UNARY: return e->as.unary.op==U_NEG&&semantic_fast_int_expr(p,e->as.unary.expr,vars,depth);
        case EX_BINARY:
            if(e->as.binary.op==B_ADD||e->as.binary.op==B_SUB||e->as.binary.op==B_MUL||e->as.binary.op==B_DIV||e->as.binary.op==B_MOD)
                return semantic_fast_int_expr(p,e->as.binary.left,vars,depth)&&semantic_fast_int_expr(p,e->as.binary.right,vars,depth);
            return false;
        case EX_CALL:{
            int fi=function_last_index(p,e->as.call.callee);
            if(fi<0||!function_is_fast_int(p,fi,depth+1)) return false;
            Function *f=&p->funcs.data[fi];
            if(f->params.len!=e->as.call.args->len) return false;
            for(size_t i=0;i<e->as.call.args->len;i++) if(!semantic_fast_int_expr(p,e->as.call.args->data[i],vars,depth)) return false;
            return true;
        }
        default: return false;
    }
}

bool function_is_fast_int(Program *p,int fi,int depth){
    if(depth>8||fi<0) return false;
    Function *f=&p->funcs.data[fi];
    if(!f->has_return_type||type_from_name(f->return_type)!=ST_INT||f->params.len>6||f->body.len!=1||f->body.data[0]->kind!=ST_RETURN) return false;
    VarVec vars={0};
    for(size_t i=0;i<f->params.len;i++){
        int idx=var_get_or_add(&vars,f->params.data[i].name);
        vars.data[idx].type=type_from_name(f->params.data[i].type);
        vars.data[idx].definitely_bound=true;
    }
    collect_stmt_vars(f->body.data[0],&vars);
    infer_stmt_seq(p,&f->body,&vars,false);
    bool ok=infer_expr_type(p,f->body.data[0]->as.ret,&vars)==ST_INT &&
            semantic_fast_int_expr(p,f->body.data[0]->as.ret,&vars,depth);
    varvec_free(&vars);
    return ok;
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
 void collect_stmt_vars(Stmt *s, VarVec *vars){
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
