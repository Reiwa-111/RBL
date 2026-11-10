#include "rblc_internal.h"

 ExprVec exprvec_new(void) { ExprVec v={0}; return v; }
 StmtVec stmtvec_new(void) { StmtVec v={0}; return v; }
 ParamVec paramvec_new(void) { ParamVec v={0}; return v; }
 void exprvec_push(ExprVec *v, Expr *e) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=e; }
 void stmtvec_push(StmtVec *v, Stmt *s) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:8;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=s; }
 void paramvec_push(ParamVec *v, Param p) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=p; }
 void funcvec_push(FuncVec *v, Function f) { if(v->len==v->cap){v->cap=v->cap?v->cap*2:4;v->data=xrealloc(v->data,v->cap*sizeof(*v->data));} v->data[v->len++]=f; }
 void elif_push(IfStmt *ifs, Elif e) { if(ifs->elif_len==ifs->elif_cap){ifs->elif_cap=ifs->elif_cap?ifs->elif_cap*2:2;ifs->elifs=xrealloc(ifs->elifs,ifs->elif_cap*sizeof(*ifs->elifs));} ifs->elifs[ifs->elif_len++]=e; }

 Expr *new_expr(SourcePos p, ExprKind k) { Expr *e=xcalloc(1,sizeof(*e)); e->pos=p; e->kind=k; return e; }
 Stmt *new_stmt(SourcePos p, StmtKind k) { Stmt *s=xcalloc(1,sizeof(*s)); s->pos=p; s->kind=k; return s; }
