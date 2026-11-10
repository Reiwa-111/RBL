# last verified: v0.8.0 | 2026.11.09 | LANGUAGE CONTRACT RECHECK

## Reference: ограничения RBL 0.8.0

Этот список намеренно отделяет реализованный язык от обсуждаемых возможностей. Отсутствие конструкции — не обязательно ошибка дизайна; это означает, что текущий compiler/runtime не обещает её поддержку.

## Язык и компилятор

- Только x86-64 Linux ELF64 и x86-64 Windows PE/COFF.
- Один исходный файл на программу; нет source `import`, модулей и multi-file compilation.
- Нет глобальных переменных и констант.
- Нет исключений, `try`/`catch`/`finally`, `raise` и `defer`.
- Нет lambda expressions и closures.
- Нет default, variadic или named arguments; нет overload resolution.
- Нет async/await и task scheduler.
- Нет nullable type annotations, generic/container type expressions и развитой статической типовой системы.
- Нет named struct construction и отдельного runtime struct identity; struct хранится как dictionary.
- Нет tuple destructuring, list slices и negative indexing.
- Нет общей итерации dictionaries в циклах.
- Нет list concatenation через `+`.
- Логические `and` и `or` не short-circuit.
- Нет полноценного source-level debugger и полноценной таблицы Windows source line mapping.

## Standard library и runtime

- `rbl.string.upper/lower` обрабатывают ASCII, а не Unicode case mapping.
- Случайный генератор не является криптографически безопасным; последовательности Linux и Windows различаются.
- `rbl.time.now_ms()` имеет разные источники времени на Linux и Windows.
- `rbl.fs.cwd()` возвращает платформозависимый путь.
- Консервативный collector может удерживать недостижимые объекты, если в сканируемой памяти стека остались pointer-shaped значения; момент освобождения недетерминирован.
- Native Runtime ABI не предоставляет string pinning/root-registration API. Корнями считаются только pointer-shaped words в сканируемом диапазоне стека текущего потока и значения, достижимые через контейнеры; native heap/global storage и другие потоки не сканируются.
- Состояние Runtime глобально для процесса и не синхронизировано. Runtime helpers можно вызывать только из потока, вошедшего в сгенерированную RBL `main`; параллельные native-вызовы из других потоков не поддерживаются.
- Нет универсального пользовательского FFI/plugin ABI.

## Проекты и tooling

- Нет dependency/package manager и package registry.
- Команда `rbl test` запускает тесты самого checkout RBL, а не универсальный тестовый runner проекта.
- `rbl doc` генерирует простую таблицу функций по текстовому шаблону, не API reference.
- Форматтер нормализует отступы блоков и не пытается выполнять полный semantic formatting.

## Не смешивать с roadmap

Предлагаемые архитектурные этапы и функции описываются в [explanation/design](../explanation/design.md). Они не становятся поддерживаемой частью языка до появления реализации и регрессионных тестов.
