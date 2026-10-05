// Рекурсивный спуск: превращает поток токенов в AST.
// Приоритет операторов (от низкого к высокому):
// or > and > == != > < > <= >= > .. ..= > + - > * / > unary (not ! -) > primary

use crate::ast::*;
use crate::lexer::Token;

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Parser { tokens, pos: 0 }
    }

    fn peek(&self) -> &Token {
        &self.tokens[self.pos]
    }

    fn advance(&mut self) -> Token {
        let t = self.tokens[self.pos].clone();
        if self.pos < self.tokens.len() - 1 {
            self.pos += 1;
        }
        t
    }

    fn check(&self, expected: &Token) -> bool {
        std::mem::discriminant(self.peek()) == std::mem::discriminant(expected)
    }

    fn expect(&mut self, expected: Token, context: &str) -> Token {
        if self.check(&expected) {
            self.advance()
        } else {
            panic!(
                "ожидался {:?} ({}), но найден {:?}",
                expected, context, self.peek()
            );
        }
    }

    fn expect_ident(&mut self, context: &str) -> String {
        match self.advance() {
            Token::Ident(name) => name,
            other => panic!("ожидался идентификатор ({}), но найден {:?}", context, other),
        }
    }

    pub fn parse_program(&mut self) -> Program {
        let mut functions = Vec::new();
        while !matches!(self.peek(), Token::Eof) {
            functions.push(self.parse_func_decl());
        }
        Program { functions }
    }

    fn parse_func_decl(&mut self) -> FuncDecl {
        self.expect(Token::Func, "объявление функции");
        let name = self.expect_ident("имя функции");

        self.expect(Token::LParen, "параметры функции");
        let mut params = Vec::new();
        if !matches!(self.peek(), Token::RParen) {
            loop {
                let pname = self.expect_ident("имя параметра");
                let ptype = self.parse_type();
                params.push(Param { name: pname, ty: ptype });

                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                } else {
                    break;
                }
            }
        }
        self.expect(Token::RParen, "конец списка параметров");

        // тип возврата опционален: если сразу '{' — функция ничего не возвращает (как main)
        let return_type = if matches!(self.peek(), Token::LBrace) {
            None
        } else {
            Some(self.parse_type())
        };

        let body = self.parse_block();

        FuncDecl { name, params, return_type, body }
    }

    fn parse_type(&mut self) -> Type {
        let name = self.expect_ident("тип");
        Type::Named(name)
    }

    fn parse_block(&mut self) -> Vec<Stmt> {
        self.expect(Token::LBrace, "начало блока");
        let mut stmts = Vec::new();
        while !matches!(self.peek(), Token::RBrace) {
            stmts.push(self.parse_stmt());
        }
        self.expect(Token::RBrace, "конец блока");
        stmts
    }

    fn parse_stmt(&mut self) -> Stmt {
        match self.peek() {
            Token::Set => self.parse_set_stmt(),
            Token::Let => self.parse_let_stmt(),
            Token::Return => self.parse_return_stmt(),
            Token::If => self.parse_if_stmt(),
            Token::For => self.parse_for_stmt(),
            _ => Stmt::Expr(self.parse_expr()),
        }
    }

    fn parse_set_stmt(&mut self) -> Stmt {
        self.expect(Token::Set, "set");
        let name = self.expect_ident("имя переменной");
        self.expect(Token::Assign, "=");
        let value = self.parse_expr();
        Stmt::Set { name, value }
    }

    fn parse_let_stmt(&mut self) -> Stmt {
        self.expect(Token::Let, "let");
        let name = self.expect_ident("имя переменной");
        self.expect(Token::Assign, "=");
        let value = self.parse_expr();
        Stmt::Let { name, value }
    }

    fn parse_return_stmt(&mut self) -> Stmt {
        self.expect(Token::Return, "return");
        if matches!(self.peek(), Token::RBrace) {
            Stmt::Return(None)
        } else {
            Stmt::Return(Some(self.parse_expr()))
        }
    }

    fn parse_if_stmt(&mut self) -> Stmt {
        self.expect(Token::If, "if");
        let condition = self.parse_expr();
        let then_block = self.parse_block();

        let mut elifs = Vec::new();
        while matches!(self.peek(), Token::Elif) {
            self.advance();
            let elif_cond = self.parse_expr();
            let elif_block = self.parse_block();
            elifs.push((elif_cond, elif_block));
        }

        let else_block = if matches!(self.peek(), Token::Else) {
            self.advance();
            Some(self.parse_block())
        } else {
            None
        };

        Stmt::If { condition, then_block, elifs, else_block }
    }

    fn parse_for_stmt(&mut self) -> Stmt {
        self.expect(Token::For, "for");
        let var = self.expect_ident("переменная цикла");
        self.expect(Token::In, "in");
        let range = self.parse_range_expr();
        let body = self.parse_block();
        Stmt::For { var, range, body }
    }

    // ==== выражения ====

    fn parse_expr(&mut self) -> Expr {
        self.parse_or_expr()
    }

    fn parse_or_expr(&mut self) -> Expr {
        let mut left = self.parse_and_expr();
        while matches!(self.peek(), Token::Or) {
            self.advance();
            let right = self.parse_and_expr();
            left = Expr::Binary { left: Box::new(left), op: BinaryOp::Or, right: Box::new(right) };
        }
        left
    }

    fn parse_and_expr(&mut self) -> Expr {
        let mut left = self.parse_equality_expr();
        while matches!(self.peek(), Token::And) {
            self.advance();
            let right = self.parse_equality_expr();
            left = Expr::Binary { left: Box::new(left), op: BinaryOp::And, right: Box::new(right) };
        }
        left
    }

    fn parse_equality_expr(&mut self) -> Expr {
        let mut left = self.parse_comparison_expr();
        loop {
            let op = match self.peek() {
                Token::Eq => BinaryOp::Eq,
                Token::NotEq => BinaryOp::NotEq,
                _ => break,
            };
            self.advance();
            let right = self.parse_comparison_expr();
            left = Expr::Binary { left: Box::new(left), op, right: Box::new(right) };
        }
        left
    }

    fn parse_comparison_expr(&mut self) -> Expr {
        let mut left = self.parse_range_expr();
        loop {
            let op = match self.peek() {
                Token::Lt => BinaryOp::Lt,
                Token::Gt => BinaryOp::Gt,
                Token::LtEq => BinaryOp::LtEq,
                Token::GtEq => BinaryOp::GtEq,
                _ => break,
            };
            self.advance();
            let right = self.parse_range_expr();
            left = Expr::Binary { left: Box::new(left), op, right: Box::new(right) };
        }
        left
    }

    fn parse_range_expr(&mut self) -> Expr {
        let start = self.parse_additive_expr();
        match self.peek() {
            Token::DotDot => {
                self.advance();
                let end = self.parse_additive_expr();
                Expr::Range { start: Box::new(start), end: Box::new(end), inclusive: false }
            }
            Token::DotDotEq => {
                self.advance();
                let end = self.parse_additive_expr();
                Expr::Range { start: Box::new(start), end: Box::new(end), inclusive: true }
            }
            _ => start,
        }
    }

    fn parse_additive_expr(&mut self) -> Expr {
        let mut left = self.parse_multiplicative_expr();
        loop {
            let op = match self.peek() {
                Token::Plus => BinaryOp::Add,
                Token::Minus => BinaryOp::Sub,
                _ => break,
            };
            self.advance();
            let right = self.parse_multiplicative_expr();
            left = Expr::Binary { left: Box::new(left), op, right: Box::new(right) };
        }
        left
    }

    fn parse_multiplicative_expr(&mut self) -> Expr {
        let mut left = self.parse_unary_expr();
        loop {
            let op = match self.peek() {
                Token::Star => BinaryOp::Mul,
                Token::Slash => BinaryOp::Div,
                _ => break,
            };
            self.advance();
            let right = self.parse_unary_expr();
            left = Expr::Binary { left: Box::new(left), op, right: Box::new(right) };
        }
        left
    }

    fn parse_unary_expr(&mut self) -> Expr {
        match self.peek() {
            Token::Minus => {
                self.advance();
                let expr = self.parse_unary_expr();
                Expr::Unary { op: UnaryOp::Neg, expr: Box::new(expr) }
            }
            Token::Not | Token::Bang => {
                self.advance();
                let expr = self.parse_unary_expr();
                Expr::Unary { op: UnaryOp::Not, expr: Box::new(expr) }
            }
            _ => self.parse_primary_expr(),
        }
    }

    fn parse_primary_expr(&mut self) -> Expr {
        match self.advance() {
            Token::Int(v) => Expr::Int(v),
            Token::Float(v) => Expr::Float(v),
            Token::Str(v) => Expr::Str(v),
            Token::True => Expr::Bool(true),
            Token::False => Expr::Bool(false),
            Token::LParen => {
                let expr = self.parse_expr();
                self.expect(Token::RParen, "закрывающая скобка");
                expr
            }
            Token::Ident(name) => self.parse_ident_expr(name),
            other => panic!("неожиданный токен в выражении: {:?}", other),
        }
    }

    fn parse_ident_expr(&mut self, name: String) -> Expr {
        match self.peek() {
            // вызов функции: name(args)
            Token::LParen => {
                self.advance();
                let args = self.parse_args();
                self.expect(Token::RParen, "конец аргументов вызова");
                Expr::Call { callee: name, args }
            }
            // вызов метода: name.method(args) — warn.log(...), error.log(...)
            Token::Dot => {
                self.advance();
                let method = self.expect_ident("имя метода");
                self.expect(Token::LParen, "аргументы метода");
                let args = self.parse_args();
                self.expect(Token::RParen, "конец аргументов метода");
                Expr::MethodCall { object: name, method, args }
            }
            _ => Expr::Ident(name),
        }
    }

    fn parse_args(&mut self) -> Vec<Expr> {
        let mut args = Vec::new();
        if !matches!(self.peek(), Token::RParen) {
            loop {
                args.push(self.parse_expr());
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                } else {
                    break;
                }
            }
        }
        args
    }
}
