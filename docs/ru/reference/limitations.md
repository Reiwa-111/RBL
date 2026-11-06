# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Reference: ограничения RBL 0.7.2

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
- Нет f-string format specifications.
- Логические `and` и `or` не short-circuit.
- Нет полноценного source-level debugger и полноценной таблицы Windows source line mapping.

## Standard library и runtime

- `rbl.string.upper/lower` обрабатывают ASCII, а не Unicode case mapping.
- Случайный генератор не является криптографически безопасным; последовательности Linux и Windows различаются.
- `rbl.time.now_ms()` имеет разные источники времени на Linux и Windows.
- `rbl.fs.cwd()` возвращает платформозависимый путь.
- Collector для контейнеров не означает полного освобождения всех строк: строки, выделенные текущими runtime-путями, не освобождаются индивидуально до завершения процесса.
- Нет универсального пользовательского FFI/plugin ABI.

## Проекты и tooling

- Нет dependency/package manager и package registry.
- Команда `rbl test` запускает тесты самого checkout RBL, а не универсальный тестовый runner проекта.
- `rbl doc` генерирует простую таблицу функций по текстовому шаблону, не API reference.
- Форматтер нормализует отступы блоков и не пытается выполнять полный semantic formatting.

## Не смешивать с roadmap

Предлагаемые архитектурные этапы и функции описываются в [explanation/design](../explanation/design.md). Они не становятся поддерживаемой частью языка до появления реализации и регрессионных тестов.
