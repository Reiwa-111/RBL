// Лексер: превращает сырой текст программы в поток токенов.
// Пока покрывает только то, что подтверждено в спеке (v0.1):
// set/let, func, if/elif/else, true/false, and/or/not, комментарии //,
// int/float(5f)/string литералы, диапазоны .. и ..=, базовые операторы.

#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    // Литералы
    Int(i64),
    Float(f64),
    Str(String),
    Ident(String),

    // Ключевые слова
    Set,
    Let,
    Func,
    If,
    Elif,
    Else,
    Return,
    True,
    False,
    And,
    Or,
    Not,
    For,
    In,

    // Операторы
    Plus,     // +
    Minus,    // -
    Star,     // *
    Slash,    // /
    Assign,   // =
    Eq,       // ==
    NotEq,    // !=
    Lt,       // <
    Gt,       // >
    LtEq,     // <=
    GtEq,     // >=
    Bang,     // !  (равнозначно `not`)

    // Диапазоны
    DotDot,   // ..   (не включает верхнюю границу)
    DotDotEq, // ..=  (включает верхнюю границу)
    Dot,      // .

    // Скобки и разделители
    LBrace, // {
    RBrace, // }
    LParen, // (
    RParen, // )
    Comma,  // ,

    Eof,
}

pub struct Lexer {
    chars: Vec<char>,
    pos: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Lexer {
            chars: source.chars().collect(),
            pos: 0,
        }
    }

    fn peek(&self) -> Option<char> {
        self.chars.get(self.pos).copied()
    }

    fn peek_next(&self) -> Option<char> {
        self.chars.get(self.pos + 1).copied()
    }

    fn advance(&mut self) -> Option<char> {
        let c = self.peek();
        self.pos += 1;
        c
    }

    pub fn tokenize(&mut self) -> Vec<Token> {
        let mut tokens = Vec::new();

        loop {
            self.skip_whitespace_and_comments();

            let c = match self.peek() {
                Some(c) => c,
                None => {
                    tokens.push(Token::Eof);
                    break;
                }
            };

            if c.is_ascii_digit() {
                tokens.push(self.read_number());
                continue;
            }

            if c.is_alphabetic() || c == '_' {
                tokens.push(self.read_ident_or_keyword());
                continue;
            }

            if c == '"' {
                tokens.push(self.read_string());
                continue;
            }

            let token = match c {
                '+' => { self.advance(); Token::Plus }
                '-' => { self.advance(); Token::Minus }
                '*' => { self.advance(); Token::Star }
                '/' => { self.advance(); Token::Slash }
                '{' => { self.advance(); Token::LBrace }
                '}' => { self.advance(); Token::RBrace }
                '(' => { self.advance(); Token::LParen }
                ')' => { self.advance(); Token::RParen }
                ',' => { self.advance(); Token::Comma }
                '!' => {
                    self.advance();
                    if self.peek() == Some('=') {
                        self.advance();
                        Token::NotEq
                    } else {
                        Token::Bang
                    }
                }
                '=' => {
                    self.advance();
                    if self.peek() == Some('=') {
                        self.advance();
                        Token::Eq
                    } else {
                        Token::Assign
                    }
                }
                '<' => {
                    self.advance();
                    if self.peek() == Some('=') {
                        self.advance();
                        Token::LtEq
                    } else {
                        Token::Lt
                    }
                }
                '>' => {
                    self.advance();
                    if self.peek() == Some('=') {
                        self.advance();
                        Token::GtEq
                    } else {
                        Token::Gt
                    }
                }
                '.' => {
                    self.advance();
                    if self.peek() == Some('.') {
                        self.advance();
                        if self.peek() == Some('=') {
                            self.advance();
                            Token::DotDotEq
                        } else {
                            Token::DotDot
                        }
                    } else {
                        Token::Dot 
                    }
                }
                other => panic!("UNKNOWN SYMBOL '{}' ON {}", other, self.pos),
            };

            tokens.push(token);
        }

        tokens
    }

    fn skip_whitespace_and_comments(&mut self) {
        loop {
            match self.peek() {
                Some(c) if c.is_whitespace() => {
                    self.advance();
                }
                Some('/') if self.peek_next() == Some('/') => {
                    while let Some(c) = self.peek() {
                        if c == '\n' {
                            break;
                        }
                        self.advance();
                    }
                }
                _ => break,
            }
        }
    }

    fn read_number(&mut self) -> Token {
        let start = self.pos;
        while let Some(c) = self.peek() {
            if c.is_ascii_digit() {
                self.advance();
            } else {
                break;
            }
        }

        // суффикс 'f' помечает float — но только если дальше не идёт ".." (диапазон)
        if self.peek() == Some('f') {
            let text: String = self.chars[start..self.pos].iter().collect();
            self.advance(); // съедаем 'f'
            let value: f64 = text.parse().expect("не смог распарсить float");
            return Token::Float(value);
        }

        let text: String = self.chars[start..self.pos].iter().collect();
        let value: i64 = text.parse().expect("не смог распарсить int");
        Token::Int(value)
    }

    fn read_ident_or_keyword(&mut self) -> Token {
        let start = self.pos;
        while let Some(c) = self.peek() {
            if c.is_alphanumeric() || c == '_' {
                self.advance();
            } else {
                break;
            }
        }
        let text: String = self.chars[start..self.pos].iter().collect();

        match text.as_str() {
            "set" => Token::Set,
            "let" => Token::Let,
            "func" => Token::Func,
            "if" => Token::If,
            "elif" => Token::Elif,
            "else" => Token::Else,
            "return" => Token::Return,
            "true" => Token::True,
            "false" => Token::False,
            "and" => Token::And,
            "or" => Token::Or,
            "not" => Token::Not,
            "for" => Token::For,
            "in" => Token::In,
            _ => Token::Ident(text),
        }
    }

    fn read_string(&mut self) -> Token {
        self.advance(); // съедаем открывающую "
        let start = self.pos;
        while let Some(c) = self.peek() {
            if c == '"' {
                break;
            }
            self.advance();
        }
        let text: String = self.chars[start..self.pos].iter().collect();
        self.advance(); // съедаем закрывающую "
        Token::Str(text)
    }
}
