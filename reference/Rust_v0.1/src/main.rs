mod ast;
mod interpreter;
mod lexer;
mod parser;

use interpreter::Interpreter;
use lexer::Lexer;
use parser::Parser;
use std::env;
use std::fs;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Использование: {} <файл>", args[0]);
        std::process::exit(1);
    }

    let path = &args[1];

    if !path.ends_with(".rbl") {
        eprintln!("предупреждение: ожидается файл с расширением .rbl");
    }

    let source = fs::read_to_string(path).expect("не смог прочитать файл");

    let mut lexer = Lexer::new(&source);
    let tokens = lexer.tokenize();

    let mut parser = Parser::new(tokens);
    let program = parser.parse_program();

    let mut interpreter = Interpreter::new(program);
    interpreter.run();
}
