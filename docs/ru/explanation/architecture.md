# last verified: v0.8.0 | 2026.11.09 | ARCHITECTURE GUIDE RECHECK

## Explanation: архитектура компилятора и backend

RBL 0.8.0 использует модульный bootstrap-компилятор на C, который генерирует x86-64 assembly непосредственно из синтаксического дерева. Пользовательская программа не транслируется в C.

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

Компилятор разделён на независимые C translation units с одним приватным общим заголовком:

| Модуль | Ответственность |
|---|---|
| `common.c` | Диагностика, allocation helpers и общая строковая hash-функция |
| `lexer.c` | UTF-8 токенизация и освобождение токенов |
| `ast.c` | Создание AST-узлов и vector helpers |
| `parser.c` | Recursive-descent parser и compile-time объявления struct |
| `sema.c` | Индексы переменных/функций, type inference и анализ fast-path eligibility |
| `codegen.c` | Scalar/loop specialization и прямая генерация x86-64 assembly |
| `driver.c` | CLI, чтение исходника и orchestration pipeline |
| `rblc_internal.h` | Приватные структуры данных и межмодульные объявления; это не публичный RBL API |

Порядок compilation pipeline: общие утилиты → lexing/создание AST → parsing → semantic analysis → code generation → orchestration в driver. Если описывать именно зависимости исходных файлов, driver использует frontend и code generator; parser зависит от AST/common utilities; semantic analysis — от AST/common utilities; code generator — от semantic analysis/common utilities; lexer и AST helpers — от common utilities. Этот граф ацикличен. Проверка допустимости fast-integer path принадлежит semantic analysis и не вызывает assembly emitter. Общий header раскрывает межмодульные объявления; внутренние helpers объявлены `static` и остаются внутри своих модулей. Build paths компилятора включают диагностику отсутствующих/нестрогих прототипов, повторных объявлений и неявных объявлений функций, чтобы граница интерфейса контролировалась сборкой. Один frontend и один code generator обслуживают Linux ELF64 и Windows PE/COFF. В 0.8.0 по-прежнему нет отдельного RBL IR, CFG/basic-block pipeline или whole-function liveness allocator.

Makefile компилирует каждый модуль в отдельный object file и затем линкует объекты. Это позволяет выполнять полную сборку параллельно и перекомпилировать только изменённый модуль. Make и native/WSL bootstrap paths в `tools/rbltool.py` отслеживают исходники компилятора, приватный header и каноническую версию из `VERSION` при проверке необходимости пересборки. Отдельный Linux-скрипт `tools/rebuild_compiler.sh` пересобирает те же семь исходников компилятора и все четыре Linux Runtime objects (assembly, containers, ABI и formatting), читая `VERSION` при запуске.

## Исходные позиции и диагностика

`SourcePos` хранит исходный файл, нулевой UTF-8 byte offset и номера строки/столбца с единицы. До lexer driver проверяет корректность UTF-8 и отклоняет embedded NUL bytes. Токены сохраняют позиции; AST-выражения, statements, параметры и функции получают позицию исходной конструкции. Контекст диагностики сохраняет исходный буфер до завершения компилятора, поэтому `fatal_at()` выводит этап, сообщение, строку исходника и каретку. Столбцы считают Unicode code points; tab считается одним исходным столбцом, но в excerpt раскрывается до четырёх terminal cells. Padding каретки приближённо учитывает широкие CJK/emoji символы и combining marks нулевой ширины; source coordinates от этого не меняются. LF, CRLF и одиночный CR считаются переводами строки.

Driver помечает этапы lexer, parser, semantic validation и codegen. Выражения f-string повторно лексируются с origin offset в исходном файле; перед поиском форматного двоеточия учитываются вложенные фигурные, круглые и квадратные скобки. После разделителя остаток интерполяции читается как статический format spec и проверяется отдельно. Форматированные выражения понижаются во внутренний helper call; общая реализация `runtime/rbl_format.c` использует фиксированные conversion formats и выделяет Runtime-managed строки для обеих платформ. Semantic validation сейчас проверяет расположение `break`/`continue`, повторные имена параметров и наличие `main`. Codegen сообщает об ограничениях числа аргументов builtin на позиции вызова. Это не полноценная статическая система типов: проверки типов аргументов и runtime failures остаются частью runtime-поведения. Точный контракт и негативные тесты описаны в [справочнике диагностики](../reference/diagnostics.md).

### Пример на RBL

Разделение компилятора на C-модули не является системой модулей исходного языка. Программы RBL 0.8.0 по-прежнему компилируются из одного `.rbl`-файла и не могут импортировать C-модули компилятора. [`examples/compiler/lookup_demo.rbl`](../../../examples/compiler/lookup_demo.rbl) — небольшой запускаемый пример, который повторно использует локальные переменные, функции и строковые значения. Он показывает обычную нагрузку, обрабатываемую приватными индексами, но не предоставляет к ним доступ из RBL. Подробности и ожидаемый вывод — в [описании примера](../../../examples/compiler/README.md).

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

Компилятор встраивает требуемый Runtime ABI descriptor в каждый executable и вызывает версионированную функцию `rbl_runtime_abi_check_v1` до входа в RBL `main`. Линкер отклоняет Runtime без symbol требуемой major-версии; startup handshake проверяет minor-версию descriptor, layout value/указателя, calling convention и feature bits. Нормативное описание находится в [справочнике Runtime ABI](../reference/runtime-abi.md).

## Runtime components и память

- `runtime/rbl_runtime.s` — Linux scalar operations, строки, I/O, math и process support.
- `runtime/rbl_runtime_win.c` — Windows-эквивалент platform runtime.
- `runtime/rbl_containers.c` — общие операции со списками, словарями, tuple и collector.
- `runtime/rbl_abi.c` / `runtime/rbl_abi.h` — общий ABI descriptor, проверка совместимости версий и константы контракта.

Shared runtime реализует консервативный non-moving mark-and-sweep collector для контейнеров и динамических строк. Он сканирует слова стека до `rbp`, сохранённого из сгенерированной функции `main`, а затем обходит строковые/list/tuple/dictionary values внутри достижимых контейнеров. Статические литералы не входят в managed heap. Сгенерированный код вызывает `rbl_gc_safepoint` перед операторами, способными выделять память или вызывать код с неизвестным поведением аллокаций, а также перед повторной проверкой условия цикла. Для доказанно не выделяющих память integer/Boolean scalar statements избыточные safe points пропускаются; allocating operations и неизвестные вызовы сохраняют проверки. Runtime allocators не запускают сборку посреди выражения. Это сохраняет видимость живых локальных значений и loop temporaries без whole-function liveness analysis. Из-за консервативного характера collector случайные pointer-shaped данные на стеке могут продлить жизнь недостижимого объекта.

## Почему нет импорта модулей

В текущей версии программа компилируется как один исходный файл. Встроенные namespace (`rbl.math`, `rbl.io` и другие) — имена, распознаваемые compiler/runtime, а не файлы модулей и не механизм `import`. Пакетная система и module resolution отсутствуют.
