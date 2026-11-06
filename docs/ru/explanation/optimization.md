# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Explanation: оптимизации компилятора

Оптимизации RBL 0.7.2 консервативные и встроены в AST analysis/code generation. Это не отдельный высокоуровневый optimizer pipeline над IR.

## Scalar fast paths

Когда анализ доказывает, что выражение или состояние локальной переменной имеет целочисленный/булевый тип, компилятор может использовать unboxed machine values вместо общего tagged representation (`rax` tag + `rdx` payload). Это снижает число переходов в runtime helpers и стоимость работы с тегами.

К fast paths относятся поддерживаемые целочисленные арифметические операции и сравнения, boolean operations, некоторые целочисленные функции и подходящие integer loops. При неопределённости в типе, binding или control flow codegen возвращается к общему tagged runtime path.

## Loop-local register allocation

Для ограниченного класса циклов compiler может удерживать горячие целочисленные значения в callee-saved registers. Реализация локальная и специализированная; например, `r14` используется для подходящей induction variable, `r15` — для выбранного hot integer local/accumulator. Сохранение регистров должно соблюдать ABI.

Fast loop path имеет строгие условия допуска. Переназначение induction variable, вложенные циклы, динамические значения и сложный/неподдерживаемый control flow приводят к обычной генерации кода.

## Чего optimizer не делает

В текущей реализации нет общего:

- RBL IR с явно выделенными basic blocks;
- CFG и data-flow/liveness analysis;
- global graph-coloring register allocation;
- общего constant folding pipeline;
- dead-store/dead-code elimination на IR;
- common-subexpression elimination pipeline.

Не следует путать специализированные fast paths с полноценным глобальным оптимизатором. Будущая архитектура AST → IR → CFG → liveness → register allocation — планируемое направление, не свойство релиза 0.7.2.
