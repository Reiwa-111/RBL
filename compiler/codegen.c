#include "rblc_internal.h"
#include "../runtime/rbl_abi.h"

#define RBL_STRINGIFY_INNER(value) #value
#define RBL_STRINGIFY(value) RBL_STRINGIFY_INNER(value)

static bool target_windows(void);
static void pool_push(StrPool *p, const char *text, const char *label);
static const char *pool_find(StrPool *p,const char *text);
static const char *pool_intern(StrPool *p,const char *text);
static void pool_free(StrPool *p);
static int align16(int n);
static int slot_off(int idx);
static int fast_slot_off(int idx);
static void emit_asm_bytes(FILE *o,const char*s);
static unsigned long aid(ACG*g);
static int cur_slot_off(const ACG*g,int idx);
static const char *reg_name_for_index(int k);
static int reg_for_slot(const ACG *g,int idx);
static bool stmt_assigns_var(Stmt*s,int idx,VarVec*vars);
static bool block_has_nested_for(StmtVec*v);
static const char *bin_runtime(BinaryOp op);
static bool expr_fast_int(ACG*g,Expr*e);
static bool expr_fast_bool(ACG*g,Expr*e);
static void emit_call_args(ACG*g,ExprVec*args);
static bool expr_fast_leaf_int(ACG*g,Expr*e);
static void emit_fast_leaf_int(ACG*g,Expr*e,const char *dst);
static void emit_fast_call(ACG*g,int fi,ExprVec*args);
static void emit_int_expr(ACG*g,Expr*e);
static bool emit_fast_leaf_compare(ACG*g,Expr*left,Expr*right);
static void emit_bool_expr(ACG*g,Expr*e);
static bool is_builtin_name(const char *n);
static void emit_builtin_call(ACG*g, Expr *e);
static void emit_builtin_method(ACG *g, Expr *e);
static void emit_expr_asm(ACG*g,Expr*e);
static void emit_store_slot(FILE*o,int idx);
static void emit_store_int_slot(ACG*g,int idx);
static bool emit_fast_inplace_int_update(ACG*g,int idx,Expr*e);
static void emit_block_asm(ACG*g,StmtVec*v);
static bool stmt_fast_int(ACG*g,Stmt*s);
static bool block_fast_int(ACG*g,StmtVec*v);
static void emit_fast_for(ACG*g,Stmt*s);
static void emit_stmt_asm(ACG*g,Stmt*s);
static void emit_function_asm(FILE*o,Program*p,size_t fi,StrPool*pool);
static void emit_fast_function_asm(FILE*o,Program*p,size_t fi,StrPool*pool);

TargetKind g_target = TARGET_LINUX_ELF;
 static bool target_windows(void) { return g_target == TARGET_WINDOWS_PE; }

static void pool_index_insert(StrPool *p,size_t index){
    size_t slot=rbl_hash_string(p->data[index].text)&(p->index_cap-1);
    while(p->index[slot]) slot=(slot+1)&(p->index_cap-1);
    p->index[slot]=index+1;
}
static void pool_index_rebuild(StrPool *p,size_t capacity){
    size_t *next=xcalloc(capacity,sizeof(*next));
    free(p->index);
    p->index=next;
    p->index_cap=capacity;
    for(size_t i=0;i<p->len;i++) pool_index_insert(p,i);
}
static bool pool_index_needs_growth(size_t entries,size_t capacity){return entries>capacity-capacity/3;}
 static void pool_push(StrPool *p, const char *text, const char *label) {
    if(p->len==p->cap){if(p->cap>SIZE_MAX/2/sizeof(*p->data))fatal("string pool capacity overflow");p->cap=p->cap?p->cap*2:16;p->data=xrealloc(p->data,p->cap*sizeof(*p->data));}
    if(!p->index_cap) pool_index_rebuild(p,16);
    else if(pool_index_needs_growth(p->len+1,p->index_cap)){if(p->index_cap>SIZE_MAX/2/sizeof(*p->index))fatal("string index capacity overflow");pool_index_rebuild(p,p->index_cap*2);}
    p->data[p->len].text=xstrdup(text);
    p->data[p->len].label=xstrdup(label);
    pool_index_insert(p,p->len);
    p->len++;
 }
 static const char *pool_find(StrPool *p,const char *text){
    if(!p->index_cap) return NULL;
    size_t slot=rbl_hash_string(text)&(p->index_cap-1);
    while(p->index[slot]){
        size_t index=p->index[slot]-1;
        if(!strcmp(p->data[index].text,text)) return p->data[index].label;
        slot=(slot+1)&(p->index_cap-1);
    }
    return NULL;
 }
 static const char *pool_intern(StrPool *p,const char *text){const char*old=pool_find(p,text);if(old)return old;char label[64];snprintf(label,sizeof(label),".Lrbl_str_%zu",p->len);pool_push(p,text,label);return p->data[p->len-1].label;}
 static void pool_free(StrPool *p){for(size_t i=0;i<p->len;i++){free(p->data[i].text);free(p->data[i].label);}free(p->data);free(p->index);memset(p,0,sizeof(*p));}
 static int align16(int n){return (n+15)&~15;}
 static int slot_off(int idx){return -40 - 24*(idx+1);}
 static int fast_slot_off(int idx){return -8*(idx+1);}
 static void emit_asm_bytes(FILE *o,const char*s){const unsigned char*p=(const unsigned char*)s;if(!*p){fputs("    .byte 0\n",o);return;}fputs("    .byte ",o);bool first=true;while(*p){if(!first)fputs(", ",o);fprintf(o,"0x%02X",*p++);first=false;}fputs(", 0\n",o);}

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
 static const char *bin_runtime(BinaryOp op){switch(op){case B_ADD:return"rbl_add";case B_SUB:return"rbl_sub";case B_MUL:return"rbl_mul";case B_DIV:return"rbl_div";case B_MOD:return"rbl_mod";case B_EQ:return"rbl_eq";case B_NE:return"rbl_ne";case B_LT:return"rbl_lt";case B_GT:return"rbl_gt";case B_LE:return"rbl_le";case B_GE:return"rbl_ge";case B_AND:return"rbl_and";case B_OR:return"rbl_or";case B_IS:return"rbl_is";}return"rbl_fail";}

 static bool expr_fast_int(ACG*g,Expr*e){
    if(!e||infer_expr_type(g->prog,e,g->vars)!=ST_INT)return false;
    switch(e->kind){
        case EX_INT:return true;
        case EX_IDENT:{int i=var_find(g->vars,e->as.ident);return i>=0&&g->vars->data[i].type==ST_INT&&g->vars->data[i].definitely_bound;}
        case EX_UNARY:return e->as.unary.op==U_NEG&&expr_fast_int(g,e->as.unary.expr);
        case EX_BINARY:
            if(e->as.binary.op==B_ADD||e->as.binary.op==B_SUB||e->as.binary.op==B_MUL||e->as.binary.op==B_DIV||e->as.binary.op==B_MOD)return expr_fast_int(g,e->as.binary.left)&&expr_fast_int(g,e->as.binary.right);
            return false;
        case EX_CALL:{int fi=function_last_index(g->prog,e->as.call.callee);if(fi<0||!function_is_fast_int(g->prog,fi,g->fast_depth+1))return false;Function*f=&g->prog->funcs.data[fi];if(f->params.len!=e->as.call.args->len)return false;for(size_t i=0;i<e->as.call.args->len;i++)if(!expr_fast_int(g,e->as.call.args->data[i]))return false;return true;}
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
    if(idx<0) fatal_at(e->pos,"internal codegen error: unknown integer identifier %s",e->as.ident);
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
        case EX_IDENT:{int idx=var_find(g->vars,e->as.ident);if(idx<0){fatal_at(e->pos,"internal codegen error: unknown integer variable %s",e->as.ident);}int rk=reg_for_slot(g,idx);if(rk>=0){fprintf(o,"    mov rax,%s\n",reg_name_for_index(rk));return;}if(g->fast_slots)fprintf(o,"    mov rax,QWORD PTR [rbp%d]\n",cur_slot_off(g,idx));else fprintf(o,"    mov rax,QWORD PTR [rbp%d+16]\n",cur_slot_off(g,idx));return;}
        case EX_UNARY:
            emit_int_expr(g,e->as.unary.expr);if(e->as.unary.op==U_NEG){fprintf(o,"    neg rax\n    jo .L_int_overflow_%lu\n",aid(g));fprintf(o,"    jmp .L_int_done_%lu\n.L_int_overflow_%lu:\n    call rbl_int_overflow\n.L_int_done_%lu:\n",g->label_counter-1,g->label_counter-1,g->label_counter-1);}return;
        case EX_BINARY:{
            BinaryOp op=e->as.binary.op;if(op!=B_ADD&&op!=B_SUB&&op!=B_MUL&&op!=B_DIV&&op!=B_MOD){fatal_at(e->pos,"internal codegen error: non-integer binary expression entered the raw integer path");}
            /* A leaf right operand cannot call anything, so an 8-byte push/pop
               is safe and avoids extra stack traffic. If it can contain a fast
               call or an overflow/division helper, reserve 16 bytes to preserve
               SysV alignment at every nested call site. */
            emit_int_expr(g,e->as.binary.left);
            if(expr_fast_leaf_int(g,e->as.binary.right)){
                fputs("    push rax\n",o);
                emit_int_expr(g,e->as.binary.right);
                fputs("    pop rcx\n",o);
            }else{
                fputs("    sub rsp,16\n    mov [rsp],rax\n",o);
                emit_int_expr(g,e->as.binary.right);
                fputs("    mov rcx,[rsp]\n    add rsp,16\n",o);
            }
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
        case EX_CALL:{int fi=function_last_index(g->prog,e->as.call.callee);if(fi<0||!function_is_fast_int(g->prog,fi,0))fatal_at(e->pos,"internal codegen error: call is not eligible for the raw integer path");emit_fast_call(g,fi,e->as.call.args);return;}
        default:fatal_at(e->pos,"internal codegen error: unsupported raw integer expression");
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
            if(op==B_AND||op==B_OR){emit_bool_expr(g,e->as.binary.left);fputs("    sub rsp,16\n    mov [rsp],rax\n",o);emit_bool_expr(g,e->as.binary.right);fputs("    mov rcx,[rsp]\n    add rsp,16\n",o);if(op==B_AND)fputs("    and eax,ecx\n",o);else fputs("    or eax,ecx\n",o);return;}
            if(!emit_fast_leaf_compare(g,e->as.binary.left,e->as.binary.right)){
                emit_int_expr(g,e->as.binary.left);fputs("    sub rsp,16\n    mov [rsp],rax\n",o);emit_int_expr(g,e->as.binary.right);fputs("    mov rcx,[rsp]\n    add rsp,16\n    cmp rcx,rax\n",o);
            }
            const char*cc="e";switch(op){case B_EQ:cc="e";break;case B_IS:cc="e";break;case B_NE:cc="ne";break;case B_LT:cc="l";break;case B_GT:cc="g";break;case B_LE:cc="le";break;case B_GE:cc="ge";break;default:fatal_at(e->pos,"internal codegen error: invalid boolean comparison operator");}
            fprintf(o,"    set%s al\n    movzx eax,al\n",cc);return;
        }
        default:fatal_at(e->pos,"internal codegen error: unsupported raw boolean expression");
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
           !strcmp(n,"random_int") || !strcmp(n,"random_float") || !strcmp(n,"random_bool") ||
           !strcmp(n,"__rbl_format_value");
}

 static void emit_builtin_call(ACG*g, Expr *e){
    FILE *o=g->out;
    const char *name=e->as.call.callee;
    size_t n=e->as.call.args->len;

    if(!strcmp(name,"__rbl_format_value")){
        if(n!=2 || e->as.call.args->data[1]->kind!=EX_STR)
            fatal_at(e->pos,"internal codegen error: malformed f-string format call");
        emit_expr_asm(g,e->as.call.args->data[0]);
        fprintf(o,"    mov rdi,rax\n    mov rsi,rdx\n    lea rdx,[rip+%s]\n    call rbl_format_value\n",
                pool_intern(g->pool,e->as.call.args->data[1]->as.s));
        return;
    }

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
        fprintf(o,"    mov eax,%u\n    xor edx,edx\n",(unsigned)RBL_TAG_NULL);
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
    fatal_at(e->pos,"internal codegen error: unknown builtin %s",name);
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
    if(expr_fast_int(g,e)){emit_int_expr(g,e);fprintf(o,"    mov rdx,rax\n    mov eax,%u\n",(unsigned)RBL_TAG_INT);return;}
    if(expr_fast_bool(g,e)){emit_bool_expr(g,e);fprintf(o,"    mov rdx,rax\n    mov eax,%u\n",(unsigned)RBL_TAG_BOOL);return;}
    switch(e->kind){
        case EX_INT:fprintf(o,"    mov eax,%u\n    mov rdx,%lld\n",(unsigned)RBL_TAG_INT,(long long)e->as.i);return;
        case EX_FLOAT:{uint64_t bits=0;memcpy(&bits,&e->as.f,8);fprintf(o,"    mov eax,%u\n    mov rdx,0x%016"PRIx64"\n",(unsigned)RBL_TAG_FLOAT,bits);return;}
        case EX_STR:fprintf(o,"    mov eax,%u\n    lea rdx,[rip+%s]\n",(unsigned)RBL_TAG_STRING,pool_intern(g->pool,e->as.s));return;
        case EX_BOOL:fprintf(o,"    mov eax,%u\n    mov edx,%d\n",(unsigned)RBL_TAG_BOOL,e->as.b?1:0);return;
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
            int fi=function_last_index(g->prog,name);
            if(fi<0){ fprintf(o,"    lea rdi,[rip+%s]\n    call rbl_unknown_function\n",pool_intern(g->pool,name)); return; }
            if(function_is_fast_int(g->prog,fi,0)&&expr_fast_int(g,e)){ emit_int_expr(g,e); fprintf(o,"    mov rdx,rax\n    mov eax,%u\n",(unsigned)RBL_TAG_INT); return; }
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

/* A GC safe point is only useful before a statement that may allocate or call
   code whose allocation behavior is not proven.  Scalar integer/Boolean
   expressions are fully classified by semantic analysis and cannot create or
   mutate managed roots.  Deferring collection across those statements is safe:
   the next potentially allocating statement checks the threshold before it
   evaluates any expression temporary. */
static bool stmt_needs_gc_safepoint(ACG*g,Stmt*s){
    switch(s->kind){
        case ST_SET:
        case ST_LET:
            return !expr_fast_int(g,s->as.assign.value) &&
                   !expr_fast_bool(g,s->as.assign.value);
        case ST_RETURN:
            return s->as.ret && !expr_fast_int(g,s->as.ret) &&
                   !expr_fast_bool(g,s->as.ret);
        case ST_EXPR:
            return !expr_fast_int(g,s->as.expr) &&
                   !expr_fast_bool(g,s->as.expr);
        case ST_IF:
            return !expr_fast_bool(g,s->as.ifs.cond);
        case ST_BREAK:
        case ST_CONTINUE:
            return false;
        case ST_FOR:
        default:
            return true;
    }
}

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
    /* block_fast_int proves this body cannot allocate or mutate pointer roots;
       the enclosing for statement already emitted its collection safe point. */
    loopg.suppress_gc_safepoints=true;
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
    if(!g->suppress_gc_safepoints && stmt_needs_gc_safepoint(g,s))
        fputs("    call rbl_gc_safepoint\n",o);
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
                fputs("    call rbl_gc_safepoint\n",o);
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
                /* A clear message beats the generic "expected a list" coming
                   out of the helpers. */
                unsigned long ok=aid(g);
                fprintf(o,"    cmp rax,%u\n    je .L_for_iter_%lu\n    cmp rax,%u\n    je .L_for_iter_%lu\n    lea rdi,[rip+%s]\n    call rbl_fail\n.L_for_iter_%lu:\n",
                        (unsigned)RBL_TAG_LIST,ok,(unsigned)RBL_TAG_TUPLE,ok,
                        pool_intern(g->pool,"for (x in ...) expects a range, list or tuple"),ok);
                fputs("    mov [rsp+8],rax\n    mov [rsp+16],rdx\n    mov QWORD PTR [rsp],0\n",o);
                fprintf(o,".L_for_%lu:\n",loop);
                fputs("    call rbl_gc_safepoint\n",o);
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
            int idx=var_get_or_add(g->vars,s->as.fors.var);unsigned long loop=aid(g),end=aid(g);fputs("    sub rsp,32\n",o);emit_expr_asm(g,s->as.fors.range->as.range.start);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_int\n    mov [rsp],rdx\n",o);emit_expr_asm(g,s->as.fors.range->as.range.end);fputs("    mov rdi,rax\n    mov rsi,rdx\n    call rbl_expect_int\n    mov [rsp+8],rdx\n",o);if(s->as.fors.range->as.range.inclusive)fputs("    mov rax,[rsp+8]\n    mov [rsp+16],rax\n",o);else fputs("    mov rdi,[rsp+8]\n    call rbl_dec_i64\n    mov [rsp+16],rax\n",o);fputs("    mov rax,[rsp]\n    mov [rsp+24],rax\n",o);fprintf(o,".L_for_%lu:\n    mov rax,[rsp+24]\n    cmp rax,[rsp+16]\n    jg .L_for_end_%lu\n    mov eax,%u\n    mov rdx,[rsp+24]\n",loop,end,(unsigned)RBL_TAG_INT);emit_store_slot(o,idx);
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
    fprintf(o,"\nfn_%zu:\n    push rbp\n    mov rbp,rsp\n    push r12\n    push r13\n    push r14\n    push r15\n",fi);if(frame)fprintf(o,"    sub rsp,%d\n",frame);for(size_t zi=0;zi<vars.len;zi++)fprintf(o,"    mov QWORD PTR [rbp%d],0\n",slot_off((int)zi));fputs("    mov r12,rdi\n    mov r13,rsi\n    mov rdi,rbp\n    call rbl_gc_init\n",o);const char*fl=pool_intern(pool,f->name);fprintf(o,"    lea rdi,[rip+%s]\n    mov esi,%zu\n    mov edx,r13d\n    call rbl_check_arity\n",fl,f->params.len);for(size_t i=0;i<f->params.len;i++){Param*q=&f->params.data[i];const char*expected=pool_intern(pool,q->type),*param=pool_intern(pool,q->name);int idx=var_find(&vars,q->name);if(q->type[0])fprintf(o,"    lea rdi,[r12+%zu]\n    lea rsi,[rip+%s]\n    lea rdx,[rip+%s]\n    lea rcx,[rip+%s]\n    call rbl_check_type\n",i*16,expected,param,fl);fprintf(o,"    mov rax,[r12+%zu]\n    mov [rbp%d+8],rax\n    mov rdx,[r12+%zu]\n    mov [rbp%d+16],rdx\n    mov QWORD PTR [rbp%d],1\n",i*16,slot_off(idx),i*16+8,slot_off(idx),slot_off(idx));}
    ACG g={0}; g.out=o; g.prog=p; g.fn=f; g.vars=&vars; g.pool=pool; g.fi=fi; g.label_counter=g_label_seq; g.fast_slots=false; g.reg_slots[0]=g.reg_slots[1]=g.reg_slots[2]=-1;emit_block_asm(&g,&f->body);g_label_seq=g.label_counter+1;fputs("    xor eax,eax\n    xor edx,edx\n",o);fprintf(o,".L_return_%zu:\n",fi);fputs("    lea rsp,[rbp-32]\n    pop r15\n    pop r14\n    pop r13\n    pop r12\n    pop rbp\n    ret\n",o);varvec_free(&vars);
}

 static void emit_fast_function_asm(FILE*o,Program*p,size_t fi,StrPool*pool){
    Function*f=&p->funcs.data[fi];VarVec vars={0};for(size_t i=0;i<f->params.len;i++)var_get_or_add(&vars,f->params.data[i].name);for(size_t i=0;i<f->body.len;i++)collect_stmt_vars(f->body.data[i],&vars);for(size_t i=0;i<f->params.len;i++){int idx=var_find(&vars,f->params.data[i].name);vars.data[idx].type=type_from_name(f->params.data[i].type);vars.data[idx].definitely_bound=true;}infer_stmt_seq(p,&f->body,&vars,false);
    int frame=align16((int)(vars.len*8));
    if(!target_windows())fprintf(o,"\n.type fast_fn_%zu,@function",fi);
    fprintf(o,"\nfast_fn_%zu:\n    push rbp\n    mov rbp,rsp\n",fi);if(frame)fprintf(o,"    sub rsp,%d\n",frame);
    static const char*regs[]={"rdi","rsi","rdx","rcx","r8","r9"};for(size_t i=0;i<f->params.len;i++){int idx=var_find(&vars,f->params.data[i].name);fprintf(o,"    mov QWORD PTR [rbp%d],%s\n",fast_slot_off(idx),regs[i]);}
    ACG g={0}; g.out=o; g.prog=p; g.fn=f; g.vars=&vars; g.pool=pool; g.fi=fi; g.label_counter=g_label_seq; g.fast_slots=true; g.reg_slots[0]=g.reg_slots[1]=g.reg_slots[2]=-1;emit_int_expr(&g,f->body.data[0]->as.ret);g_label_seq=g.label_counter+1;fputs("    leave\n    ret\n",o);varvec_free(&vars);
}

 void emit_program(FILE*o,Program*p,const char*source){
    StrPool pool={0};int main_id=function_last_index(p,"main");if(main_id<0)fatal("internal codegen error: semantic validation was bypassed (main is missing)");
    fprintf(o,"# RBL direct x86-64 ASM generated from %s (%s)\n.intel_syntax noprefix\n.text\n",source,target_windows()?"windows x86-64 / PE":"linux x86-64 / ELF");
    fputs(".extern " RBL_STRINGIFY(RBL_RUNTIME_ABI_CHECK_SYMBOL) "\n",o);
    for(size_t i=0;i<p->funcs.len;i++)emit_function_asm(o,p,i,&pool);
    for(size_t i=0;i<p->funcs.len;i++)if(function_is_fast_int(p,(int)i,0))emit_fast_function_asm(o,p,i,&pool);
    if(target_windows()){
        /* The C runtime enters main with the Microsoft x64 convention: 16-byte
           stack alignment, 32 bytes of shadow space, and no return to the CRT
           because the runtime terminates the process. Every RBL function keeps
           the System V convention, which is what the C runtime is compiled for. */
        fputs("\n.globl main\nmain:\n    sub rsp, 40\n",o);
        fputs("    lea rdi,[rip+.Lrbl_compiler_abi_descriptor]\n    call " RBL_STRINGIFY(RBL_RUNTIME_ABI_CHECK_SYMBOL) "\n    xor edi, edi\n    xor esi, esi\n",o);
        fprintf(o,"    call fn_%d\n    xor edi, edi\n    call rbl_process_exit\n    ud2\n",main_id);
        fputs("\n.section .rdata,\"dr\"\n.p2align 3\n",o);
    }else{
        fputs("\n.globl _start\n.type _start,@function\n_start:\n    and rsp,-16\n",o);
        fputs("    lea rdi,[rip+.Lrbl_compiler_abi_descriptor]\n    call " RBL_STRINGIFY(RBL_RUNTIME_ABI_CHECK_SYMBOL) "\n    xor edi,edi\n    xor esi,esi\n",o);
        fprintf(o,"    call fn_%d\n    xor edi,edi\n    call rbl_process_exit\n    ud2\n",main_id);
        fputs("\n.section .rodata\n.p2align 3\n",o);
    }
    fprintf(o,
        ".p2align 3\n.Lrbl_compiler_abi_descriptor:\n"
        "    .quad 0x%016llx\n    .quad 0x%016llx\n"
        "    .long %u\n    .long %u\n    .long %u\n    .long %u\n"
        "    .short %u\n    .short %u\n    .short %u\n    .short 0\n",
        (unsigned long long)RBL_RUNTIME_ABI_MAGIC,
        (unsigned long long)RBL_RUNTIME_ABI_FEATURES,
        (unsigned)RBL_RUNTIME_ABI_DESCRIPTOR_SIZE,
        (unsigned)RBL_RUNTIME_ABI_VALUE_SIZE,
        (unsigned)RBL_RUNTIME_ABI_POINTER_SIZE,
        (unsigned)RBL_RUNTIME_ABI_CALL_SYSV_AMD64,
        (unsigned)RBL_RUNTIME_ABI_MAJOR,
        (unsigned)RBL_RUNTIME_ABI_MINOR,
        (unsigned)RBL_RUNTIME_ABI_PATCH);
    for(size_t i=0;i<pool.len;i++){fprintf(o,"%s:\n",pool.data[i].label);emit_asm_bytes(o,pool.data[i].text);}
    if(!target_windows())fputs(".section .note.GNU-stack,\"\",@progbits\n",o);
    pool_free(&pool);
}

