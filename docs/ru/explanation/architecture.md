# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Explanation: архитектура компилятора и backend

RBL 0.7.2 использует bootstrap-компилятор на C, который генерирует x86-64 assembly непосредственно из синтаксического дерева. Пользовательская программа не транслируется в C.

## Путь от исходника до executable

```text
source.rbl
   │
   ▼
UTF-8 lexer → parser → AST → static analysis
                              │
                              ▼
                      scalar/loop fast paths
                              │
                              ▼
                    x86-64 assembly output
                              │
                              ▼
                       GNU assembler
                              │
                              ▼
                 linker + platform runtime
                              │
                              ▼
                       native executable
```

Frontend, AST, analysis и code generator сейчас находятся в `compiler/rblc_asm.c`. Отдельного RBL IR, CFG/basic-block pipeline или whole-function liveness allocator в этой версии нет.

## Поддерживаемые targets

| Target | Формат | Platform runtime | Shared containers |
|---|---|---|---|
| Linux x86-64 | ELF64 | `runtime/rbl_runtime.s` | `runtime/rbl_containers.c` |
| Windows x86-64 | PE/COFF | `runtime/rbl_runtime_win.c` | `runtime/rbl_containers.c` |

Frontend и code generator общие. Platform entry/exit glue, системные функции, object format directives, runtime и toolchain различаются. На Linux используются GNU `as` и `ld`, системные библиотеки и runtime objects. На Windows используется MinGW-w64: GNU assembler и GCC как linker driver.

## Что компилируется на C

C используется для bootstrap compiler, Windows scalar runtime и shared container runtime. Компилятор может вызывать GCC при сборке этих собственных компонентов. Это не означает, что исходные `.rbl` программы переводятся в C: пользовательский код генерируется в assembly.

## Runtime value representation

Универсальное значение в runtime — пара tag/payload:

| Tag | Значение |
|---:|---|
| 0 | `Unit` |
| 1 | `Int` (i64) |
| 2 | `Float` (IEEE-754 binary64 bits) |
| 3 | `String` (pointer) |
| 4 | `Bool` |
| 5 | `List` |
| 6 | `Dictionary` |
| 7 | `Tuple` |
| 8 | `null` |

В общем runtime-представлении `rax` несёт tag, `rdx` — payload. Отдельные доказуемо безопасные integer/boolean code paths могут работать с unboxed machine values.

Runtime helper calls используют System V AMD64 argument registers: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`. Windows runtime functions экспортируются с `sysv_abi`, чтобы сохранить этот контракт для сгенерированного кода, несмотря на Windows PE entry point. Пользовательские RBL function calls используют сгенерированную компилятором argument-array convention; не следует выводить их stack frame из ABI обычной C-функции.

## Runtime components и память

- `runtime/rbl_runtime.s` — Linux scalar operations, строки, I/O, math и process support.
- `runtime/rbl_runtime_win.c` — Windows-эквивалент platform runtime.
- `runtime/rbl_containers.c` — общие операции со списками, словарями, tuple и collector.

Container runtime реализует conservative mark-and-sweep collector для list/tuple/dictionary объектов: отслеживает контейнерные allocation, ищет корни среди кандидатов на стеке и обходит ссылки из контейнеров. Это не полный tracing GC для всех объектов. Строковые allocations, включая результаты конкатенации и форматирования контейнеров, не освобождаются этим collector и могут жить до завершения процесса.

## Почему нет импорта модулей

В текущей версии программа компилируется как один исходный файл. Встроенные namespace (`rbl.math`, `rbl.io` и другие) — имена, распознаваемые compiler/runtime, а не файлы модулей и не механизм `import`. Пакетная система и module resolution отсутствуют.
