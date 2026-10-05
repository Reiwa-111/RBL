// AST — дерево, которое парсер строит из токенов.
// Смысл программы не зависит от способа исполнения (интерпретатор/компилятор) —
// и интерпретатор, и будущий компилятор в LLVM должны работать с одним и тем же деревом.

#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    Named(String), // int, float, string, bool — пока просто имя, без проверки набора
}

#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub ty: Type,
}

#[derive(Debug, Clone)]
pub struct FuncDecl {
    pub name: String,
    pub params: Vec<Param>,
    pub return_type: Option<Type>, // None = функция ничего не возвращает (как main)
    pub body: Vec<Stmt>,
}

#[derive(Debug, Clone)]
pub struct Program {
    pub functions: Vec<FuncDecl>,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    Set {
        name: String,
        value: Expr,
    },
    Let {
        name: String,
        value: Expr,
    },
    Return(Option<Expr>),
    Expr(Expr), // например print(a) как самостоятельная строка
    If {
        condition: Expr,
        then_block: Vec<Stmt>,
        elifs: Vec<(Expr, Vec<Stmt>)>,
        else_block: Option<Vec<Stmt>>,
    },
    For {
        var: String,
        range: Expr, // всегда Expr::Range
        body: Vec<Stmt>,
    },
}

#[derive(Debug, Clone, PartialEq)]
pub enum BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Eq,
    NotEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    And,
    Or,
}

#[derive(Debug, Clone, PartialEq)]
pub enum UnaryOp {
    Neg, // -x
    Not, // not x / !x
}

#[derive(Debug, Clone)]
pub enum Expr {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    Ident(String),
    Unary {
        op: UnaryOp,
        expr: Box<Expr>,
    },
    Binary {
        left: Box<Expr>,
        op: BinaryOp,
        right: Box<Expr>,
    },
    Call {
        callee: String,
        args: Vec<Expr>,
    },
    // warn.log(...), error.log(...) — объект и метод через точку
    MethodCall {
        object: String,
        method: String,
        args: Vec<Expr>,
    },
    Range {
        start: Box<Expr>,
        end: Box<Expr>,
        inclusive: bool, // true для ..=, false для ..
    },
}
