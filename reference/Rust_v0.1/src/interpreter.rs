// Интерпретатор: обходит AST и реально исполняет программу.
// Пока это самый наивный вариант — просто клонирует значения при каждом обращении
// к переменной. Move-семантика (кто владеет значением, когда старое становится
// невалидным) здесь ещё НЕ проверяется — это отдельный будущий шаг (анализ
// перед исполнением), см. открытые вопросы в спеке. Задача этого шага — чтобы
// код на языке реально считался и давал верный результат.

use crate::ast::*;
use std::collections::HashMap;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    Unit, // отсутствие значения — у функций без return_type (как main)
}

impl Value {
    fn type_name(&self) -> &'static str {
        match self {
            Value::Int(_) => "int",
            Value::Float(_) => "float",
            Value::Str(_) => "string",
            Value::Bool(_) => "bool",
            Value::Unit => "()",
        }
    }
}

impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Value::Int(v) => write!(f, "{}", v),
            Value::Float(v) => write!(f, "{}", v),
            Value::Str(v) => write!(f, "{}", v),
            Value::Bool(v) => write!(f, "{}", v),
            Value::Unit => write!(f, ""),
        }
    }
}

type Env = HashMap<String, Value>;

enum Flow {
    Normal,
    Return(Value),
}

pub struct Interpreter {
    functions: HashMap<String, FuncDecl>,
}

impl Interpreter {
    pub fn new(program: Program) -> Self {
        let mut functions = HashMap::new();
        for f in program.functions {
            functions.insert(f.name.clone(), f);
        }
        Interpreter { functions }
    }

    pub fn run(&mut self) {
        let main_fn = self
            .functions
            .get("main")
            .expect("функция main не найдена")
            .clone();

        let mut env = Env::new();
        self.exec_block(&main_fn.body, &mut env);
    }

    fn exec_block(&mut self, stmts: &[Stmt], env: &mut Env) -> Flow {
        for stmt in stmts {
            match self.exec_stmt(stmt, env) {
                Flow::Normal => continue,
                Flow::Return(v) => return Flow::Return(v),
            }
        }
        Flow::Normal
    }

    fn exec_stmt(&mut self, stmt: &Stmt, env: &mut Env) -> Flow {
        match stmt {
            Stmt::Set { name, value } => {
                if env.contains_key(name) {
                    panic!("переменная '{}' уже объявлена — используй let для изменения", name);
                }
                let v = self.eval_expr(value, env);
                env.insert(name.clone(), v);
                Flow::Normal
            }
            Stmt::Let { name, value } => {
                let new_val = self.eval_expr(value, env);
                match env.get(name) {
                    None => panic!("переменная '{}' не объявлена — сначала используй set", name),
                    Some(old_val) => {
                        if old_val.type_name() != new_val.type_name() {
                            panic!("error: {} ≠ {}", old_val.type_name(), new_val.type_name());
                        }
                    }
                }
                env.insert(name.clone(), new_val);
                Flow::Normal
            }
            Stmt::Return(expr) => {
                let v = match expr {
                    Some(e) => self.eval_expr(e, env),
                    None => Value::Unit,
                };
                Flow::Return(v)
            }
            Stmt::Expr(expr) => {
                self.eval_expr(expr, env);
                Flow::Normal
            }
            Stmt::If { condition, then_block, elifs, else_block } => {
                if self.eval_bool(condition, env, "if") {
                    return self.exec_block(then_block, env);
                }
                for (elif_cond, elif_body) in elifs {
                    if self.eval_bool(elif_cond, env, "elif") {
                        return self.exec_block(elif_body, env);
                    }
                }
                if let Some(else_body) = else_block {
                    return self.exec_block(else_body, env);
                }
                Flow::Normal
            }
            Stmt::For { var, range, body } => {
                let (start, end, inclusive) = self.eval_range(range, env);
                let last = if inclusive { end } else { end - 1 };
                let mut i = start;
                while i <= last {
                    env.insert(var.clone(), Value::Int(i));
                    match self.exec_block(body, env) {
                        Flow::Normal => {}
                        Flow::Return(v) => return Flow::Return(v),
                    }
                    i += 1;
                }
                Flow::Normal
            }
        }
    }

    fn eval_bool(&mut self, expr: &Expr, env: &mut Env, context: &str) -> bool {
        match self.eval_expr(expr, env) {
            Value::Bool(b) => b,
            other => panic!("условие {} должно быть bool, получено {}", context, other.type_name()),
        }
    }

    fn eval_range(&mut self, expr: &Expr, env: &mut Env) -> (i64, i64, bool) {
        match expr {
            Expr::Range { start, end, inclusive } => {
                let s = self.eval_expr(start, env);
                let e = self.eval_expr(end, env);
                match (s, e) {
                    (Value::Int(s), Value::Int(e)) => (s, e, *inclusive),
                    _ => panic!("границы диапазона должны быть int"),
                }
            }
            _ => panic!("ожидался диапазон (0..10 или 0..=10) в for"),
        }
    }

    fn eval_expr(&mut self, expr: &Expr, env: &mut Env) -> Value {
        match expr {
            Expr::Int(v) => Value::Int(*v),
            Expr::Float(v) => Value::Float(*v),
            Expr::Str(v) => Value::Str(v.clone()),
            Expr::Bool(v) => Value::Bool(*v),
            Expr::Ident(name) => env
                .get(name)
                .unwrap_or_else(|| panic!("переменная '{}' не найдена", name))
                .clone(),
            Expr::Unary { op, expr } => {
                let v = self.eval_expr(expr, env);
                match (op, v) {
                    (UnaryOp::Neg, Value::Int(i)) => Value::Int(-i),
                    (UnaryOp::Neg, Value::Float(f)) => Value::Float(-f),
                    (UnaryOp::Not, Value::Bool(b)) => Value::Bool(!b),
                    (op, v) => panic!("нельзя применить {:?} к {}", op, v.type_name()),
                }
            }
            Expr::Binary { left, op, right } => {
                let l = self.eval_expr(left, env);
                let r = self.eval_expr(right, env);
                self.eval_binary(op, l, r)
            }
            Expr::Call { callee, args } => self.eval_call(callee, args, env),
            Expr::MethodCall { object, method, args } => {
                self.eval_method_call(object, method, args, env)
            }
            Expr::Range { .. } => panic!("диапазон можно использовать только в for"),
        }
    }

    fn eval_binary(&mut self, op: &BinaryOp, l: Value, r: Value) -> Value {
        use BinaryOp::*;
        match op {
            Add | Sub | Mul | Div => match (l, r) {
                (Value::Int(a), Value::Int(b)) => Value::Int(match op {
                    Add => a + b,
                    Sub => a - b,
                    Mul => a * b,
                    Div => a / b,
                    _ => unreachable!(),
                }),
                (Value::Float(a), Value::Float(b)) => Value::Float(match op {
                    Add => a + b,
                    Sub => a - b,
                    Mul => a * b,
                    Div => a / b,
                    _ => unreachable!(),
                }),
                (Value::Str(a), Value::Str(b)) if matches!(op, Add) => Value::Str(a + &b),
                (a, b) => panic!(
                    "нельзя применить {:?} к {} и {}",
                    op, a.type_name(), b.type_name()
                ),
            },
            Eq => Value::Bool(l == r),
            NotEq => Value::Bool(l != r),
            Lt | Gt | LtEq | GtEq => match (l, r) {
                (Value::Int(a), Value::Int(b)) => Value::Bool(match op {
                    Lt => a < b,
                    Gt => a > b,
                    LtEq => a <= b,
                    GtEq => a >= b,
                    _ => unreachable!(),
                }),
                (Value::Float(a), Value::Float(b)) => Value::Bool(match op {
                    Lt => a < b,
                    Gt => a > b,
                    LtEq => a <= b,
                    GtEq => a >= b,
                    _ => unreachable!(),
                }),
                (a, b) => panic!("нельзя сравнить {} и {}", a.type_name(), b.type_name()),
            },
            And => match (l, r) {
                (Value::Bool(a), Value::Bool(b)) => Value::Bool(a && b),
                (a, b) => panic!("and требует bool, получено {} и {}", a.type_name(), b.type_name()),
            },
            Or => match (l, r) {
                (Value::Bool(a), Value::Bool(b)) => Value::Bool(a || b),
                (a, b) => panic!("or требует bool, получено {} и {}", a.type_name(), b.type_name()),
            },
        }
    }

    fn eval_call(&mut self, callee: &str, args: &[Expr], env: &mut Env) -> Value {
        if callee == "print" {
            let values: Vec<String> = args.iter().map(|a| self.eval_expr(a, env).to_string()).collect();
            println!("{}", values.join(" "));
            return Value::Unit;
        }

        let func = self
            .functions
            .get(callee)
            .unwrap_or_else(|| panic!("функция '{}' не найдена", callee))
            .clone();

        if func.params.len() != args.len() {
            panic!(
                "функция '{}' ожидает {} аргумент(ов), передано {}",
                callee, func.params.len(), args.len()
            );
        }

        let mut local_env = Env::new();
        for (param, arg_expr) in func.params.iter().zip(args.iter()) {
            let value = self.eval_expr(arg_expr, env);
            let Type::Named(expected) = &param.ty;
            if expected.as_str() != value.type_name() {
                panic!(
                    "параметр '{}' функции '{}': ожидался {}, передан {}",
                    param.name, callee, expected, value.type_name()
                );
            }
            local_env.insert(param.name.clone(), value);
        }

        match self.exec_block(&func.body, &mut local_env) {
            Flow::Return(v) => v,
            Flow::Normal => Value::Unit,
        }
    }

    fn eval_method_call(&mut self, object: &str, method: &str, args: &[Expr], env: &mut Env) -> Value {
        let values: Vec<String> = args.iter().map(|a| self.eval_expr(a, env).to_string()).collect();
        let text = values.join(" ");

        match (object, method) {
            ("warn", "log") => println!("[WARN] {}", text),
            ("error", "log") => println!("[ERROR] {}", text),
            _ => panic!("неизвестный метод '{}.{}'", object, method),
        }

        Value::Unit
    }
}
