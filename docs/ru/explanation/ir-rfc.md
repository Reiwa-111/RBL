# RBL IR v0.1 — предложение по дизайну

**Статус: PROPOSED; контракт не зафиксирован.** Это кандидат внутреннего представления для v0.9.1. Документ не меняет язык RBL, Runtime ABI или текущий AST-to-assembly compiler. Открытые решения нужно проверить до объявления стабильного контракта.

## Область действия

IR становится контрактом между текущим frontend и будущим backend. В нём должны быть явны порядок вычислений, control flow, runtime checks, side effects и source locations, чтобы verifier отклонял некорректные программы до codegen.

Первая версия представляет один source file на compilation unit, как в RBL 0.8.0. Она не добавляет imports, project modules, closures, exceptions, async execution, публичный bytecode format или binary IR ABI между версиями. Runtime ABI 1.2.0 не меняется.

## Модель представления

### Module, функции, locals и values

- `IRModule` владеет таблицей исходных файлов, constant pool и упорядоченным списком функций одного source file.
- Функции сохраняют исходный порядок и получают module-local `FunctionId`. Разрешение имён сохраняет текущую семантику last-definition-wins для повторных function names; исходный список не схлопывается.
- `LocalId` обозначает изменяемый function-wide local slot. Блоки `if`/`elif`/`else` и циклов не создают lexical scopes. Source bindings остаются слотами, поскольку порядок `set`/`let`, привязка loop variable и runtime bound/type checks наблюдаемы.
- `ValueId` обозначает неизменяемый результат выражения. У каждого значения ровно одно определение: результат инструкции либо параметр basic block. ID уникален в пределах функции.
- Факты о типах IR: `unit`, `int`, `float`, `bool`, `string`, `list`, `dictionary`, `tuple`, `null` или `dynamic`. `dynamic` означает, что Runtime checks остаются необходимыми; это не новая static type system.
- Source values используют существующий 16-байтовый tagged Runtime value. Unboxed integer/Boolean registers — оптимизация backend, наблюдаемо эквивалентная tagged-value поведению.
- Объявленный return type функции сейчас является static inference hint, а не Runtime-проверкой возвращаемого значения. Type annotations параметров проверяются при входе в функцию. IR должен сохранять это различие.

### CFG и merge values

Функция состоит из basic blocks. У каждого блока есть уникальный `BlockId`, ноль или более типизированных параметров, упорядоченный список инструкций и ровно один terminator. Неявного fall-through нет; entry block указывается явно.

**Предложение: SSA-подобные immutable temporaries с block arguments, без `phi` instructions.** Каждый predecessor передаёт выбранное на этом ребре значение аргументом целевого блока. Back-edges циклов передают loop-carried values тем же способом. Изменяемые source locals остаются явными операциями `LOAD_LOCAL`/`STORE_LOCAL` и не переводятся в SSA в v0.1.

Terminator-ы:

- `BR target(args...)` — безусловное ребро.
- `COND_BR condition, true_target(true_args...), false_target(false_args...)` — условное ребро.
- `RETURN value` — возврат значения; source `return` без выражения возвращает `unit`.
- `UNREACHABLE` — блок, недостижимый при корректном выполнении, обычно после гарантированного trap.

### Семейства инструкций

| Семейство | Кандидатные операции | Семантический контракт |
|---|---|---|
| Константы | `CONST_INT`, `CONST_FLOAT`, `CONST_BOOL`, `CONST_STRING`, `CONST_NULL`, `CONST_UNIT` | Определяют типизированное значение; строки ссылаются на module constant pool. |
| Locals | `LOAD_LOCAL`, `CHECK_UNBOUND`, `CHECK_REBIND`, `CHECK_LOCAL_TYPE`, `STORE_LOCAL` | Сохраняют порядок `set`/`let` и runtime binding/type failures. |
| Unary/binary | `UNARY_NEG`, `UNARY_NOT`, `BINARY_*`, `COMPARE_*`, `IDENTITY`, `LOGICAL_AND`, `LOGICAL_OR` | Сохраняют порядок операндов и Runtime errors; `and`/`or` вычисляют оба операнда. |
| Checks | `EXPECT_BOOL`, `EXPECT_INT`, `CHECK_ARITY`, `CHECK_CALL_TYPE` | Явные упорядоченные проверки с source locations. |
| Calls | `CALL_FUNCTION`, `CALL_BUILTIN`, `CALL_UNKNOWN` | Содержат resolved function/builtin IDs либо unresolved function/method name, упорядоченные аргументы и result type. Runtime helper ABI выбирает backend. |
| Containers | `LIST_NEW/PUSH/GET/SET`, `DICT_NEW/SET`, `TUPLE_NEW/PUSH`, `INDEX_GET/SET`, `CONTAINS` | Явные allocation, mutation, bounds/type checks и поведение отсутствующего key (`unit`). |
| Strings | `STRING_CONCAT`, `STRING_METHOD`, `FORMAT_VALUE` | Сохраняют immutable string semantics, managed allocation и f-string metadata. |
| Runtime | `GC_SAFEPOINT`, `TRAP` | Safepoint — ordering barrier; trap хранит тип ошибки и source location. |

Точные enum names и operand structs — детали API verifier; семантические различия выше являются предложенным контрактом. Builtin-name resolution выполняется раньше user-function lookup, поэтому builtin перекрывает пользовательскую функцию с тем же именем; среди user functions действует last-definition-wins. Неизвестные function names и unknown namespace methods вызывают trap до вычисления аргументов. Конструктор struct сейчас понижается до dictionary operations, методы receiver — до обычных calls с receiver первым аргументом. `switch` parser уже понижает до `if`/`elif`; отдельные opcode им не нужны.

## Порядок вычислений и наблюдаемое поведение

IR обязан точно сохранить текущую execution model:

1. `set name = rhs`: проверить, что `name` unbound, **до** вычисления `rhs`.
2. `let name = rhs`: вычислить `rhs` **до** проверки существования `name` и совместимости runtime type.
   - `let object.field = value` и `let object[index] = value` — container mutations, а не rebindings local slot.
   - `let name += value`, `let name++` и подобные compound/increment forms parser преобразует в read-modify-write `let`.
3. Binary operands и function arguments вычисляются слева направо. Arity check resolved call происходит до вычисления аргументов. Unresolved function name вызывает trap до вычисления любого argument expression; поэтому `CALL_UNKNOWN` хранит имя/позицию, а не вычисленные аргументы.
4. `and` и `or` вычисляют оба операнда; short-circuit отсутствует.
5. Условия `if` и ternary вычисляются один раз и проверяются как Boolean до выбора ребра.
6. Condition-form `for` вычисляет/проверяет условие на каждой итерации. List/tuple iterable вычисляется один раз; текущая длина запрашивается на каждой итерации, поэтому append/remove в теле могут менять количество итераций. Element lookup выполняется на каждой итерации. Range start/end вычисляются слева направо один раз и проверяются как integers. Переменные обоих видов циклов становятся bound после выполненной итерации; цикл с нулём итераций оставляет переменную unbound.
7. `break`, `continue`, return и выходы из циклов становятся явными CFG edges/terminators без переноса точки остановки последующего кода.
8. Expression statement сохраняет side effects и может завершиться ошибкой, даже если результат не используется.

Нельзя сворачивать, дублировать, поднимать или переупорядочивать операции, если меняются side effects, allocation/lifetime roots, runtime errors или порядок ошибок.

## Effects, allocation и ошибки

Effects выводятся из opcode/builtin descriptors, а не принимаются как произвольные flags вызывающего кода. Начальные измерения: local read/write, container read/write, I/O, nondeterminism, allocation, may-fail, завершение процесса и GC barrier.

String concatenation/formatting, создание/рост containers и allocating Runtime/library calls могут выделять память. Collector консервативный и non-moving; IR сохраняет явные `GC_SAFEPOINT` на текущих safe-point boundaries. Размещение зависит от формы цикла: condition-form и list/tuple loops выполняют safepoint на каждой итерации; range loops — при входе в statement и используют checks внутри body, а доказанно fast integer range loops могут опускать избыточные per-iteration checks. Safepoint нельзя пересекать transform-ом, меняющим live managed values или их stack roots.

Потенциально failing operations сохраняют source location и error contract: dynamic arithmetic/comparison, Boolean/integer checks, local binding/type checks, arity checks, unknown calls, container indexing/mutation, I/O и f-string formatting. Backend может выбрать platform-specific Runtime helper, но не переносит ошибку через более ранний side effect/error операнда.

## Source locations

Каждая инструкция и terminator содержит `IRLocation`: source-file ID, нулевой UTF-8 byte offset, номер строки с единицы и Unicode-code-point column с единицы. Это совпадает с `SourcePos`. В v0.1 нужна начальная позиция, не end span. Synthetic instructions наследуют location source construct; generated bookkeeping может иметь явный synthetic-location kind вместо выдуманной координаты.

## Контракт verifier

Verifier отклоняет module до backend emission при нарушении инвариантов:

1. Structural arrays/counts, IDs, opcode/terminator tags и source-file references корректны; function/block/local/value IDs уникальны в своей области.
2. У каждого value ровно одно определение; каждый operand ссылается на value той же функции.
3. Arity и operand/result types совпадают с opcode contract. `dynamic` допустим только там, где явно разрешён Runtime check; verifier не придумывает более сильный type fact.
4. У каждого block ровно один последний terminator; после него нет инструкций.
5. Каждый branch target существует; число и типы edge arguments совпадают с block parameters целевого блока.
6. У каждого non-entry block есть хотя бы один predecessor, и он достижим из entry. `UNREACHABLE` завершает достижимый блок после guaranteed trap; это не исключение для мёртвых блоков из проверки reachability.
7. SSA-подобные values доминируют над каждым use. Block parameters определяются при входе в блок и доступны в нём и доминируемых successor blocks.
8. Calls ссылаются на корректные function/builtin descriptors, имеют допустимую arity и сохраняют result type. Неизвестные имена используют `CALL_UNKNOWN`, а не отсутствующий function ID.
9. Local IDs, type facts и check/store operations согласованы с local table функции.
10. Source locations ссылаются на известный файл и offset/line/column внутри него.
11. Effect metadata согласованы с opcode descriptors; potentially failing/allocating operation нельзя пометить как pure/non-allocating.

Verifier возвращает structured error с function/block/instruction identity и ближайшим `IRLocation`. Он не завершает процесс и не генерирует assembly. Negative tests проверяют error category и location, а не только ненулевой exit code.

## Тесты и критерии завершения

Positive fragments: constants/arithmetic, locals и binding checks, eager Boolean evaluation, вложенные branches, block arguments, loops/back-edges, function/builtin calls, dynamic values, containers, strings/f-strings, safepoints и source locations. Negative fragments: duplicate IDs, undefined/duplicate values, неверные operand types/arity, invalid local/function/builtin IDs, missing/duplicate terminators, инструкции после terminator, missing branch targets, bad edge arguments, unreachable blocks, non-dominating uses, malformed call descriptors, invalid source locations и inconsistent effect flags.

`v0.9.1` завершён, когда контракт прошёл review и зафиксирован, in-memory verifier встроен в compiler tests, все positive fragments проходят, все negative fragments отклоняются со structured diagnostics, а verifier не зависит от target assembly или Runtime implementation. `v0.9.2` добавляет AST-to-IR lowering и проверяет каждую generated function. Текущий AST-to-assembly backend остаётся behavioral oracle до достижения differential parity.

## Открытые решения для review

- Подтвердить block arguments как единственный механизм слияния immutable temporaries (без `phi`).
- Подтвердить, что source variables остаются явными mutable local slots в первой версии IR.
- Зафиксировать opcode inventory и builtin descriptor IDs после проверки всей поверхности standard library/Runtime.
- Решить, выводятся ли effects только из opcode descriptors или частично уточняются frontend analysis; непроверенные effect claims принимать нельзя.
- Решить, ограничивается ли `UNREACHABLE` блоками после guaranteed traps или включает другие доказанно unreachable blocks.

До review RFC остаётся предложением, а не frozen IR ABI.
