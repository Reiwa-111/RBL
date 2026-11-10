# last verified: v0.8.0 | 2026.11.10 | RELEASE READINESS RECHECK

## Назначение и статус

Это основной план развития RBL. В нём отдельно описаны реализованная база 0.8.0, незавершённая инженерная работа и будущие предложения. Roadmap задаёт порядок и критерии готовности, а не календарные обещания.

**Текущее состояние исходников:** корневой VERSION, CLI и compiler binaries сообщают 0.8.0. Этот roadmap описывает реализацию в текущем дереве исходников и оставшиеся инженерные этапы. Публикация релиза выполняется отдельно.

## Принципы

1. **Сначала контракт, затем реализация.** У каждой возможности должны быть грамматика, семантика, порядок вычислений, ошибки и тесты.
2. **Корректность важнее скорости.** Ни одна оптимизация не должна менять наблюдаемое поведение, побочные эффекты или момент возникновения ошибки.
3. **Общий язык — два backend.** Linux ELF64 и Windows PE/COFF используют общий frontend и должны совпадать по детерминированному поведению. Платформенные различия документируются.
4. **Слои не смешиваются.** AST не знает о физических регистрах; IR не зависит от формата объектного файла; source import не является загрузчиком плагинов.
5. **Совместимость проверяется.** Рефакторинг не должен менять value ABI, синтаксис или evaluation order без отдельного решения и migration note.
6. **Нет функций «на бумаге».** Парсинг или прототип не означают готовность. Нужны реализация, позитивные и негативные тесты, документация и проверки обеих платформ.
7. **Релиз закрывается доказательствами.** Чистая сборка, regression, smoke и differential tests, обновлённые RU/EN docs и release notes.

## Текущая база — RBL 0.8.0

RBL 0.8.0 — нативный однофайловый язык и toolchain для x86-64 Linux ELF64 и x86-64 Windows PE/COFF. Общий C frontend и code generator напрямую создают assembly; пользовательский RBL-код не транслируется в C.

Компилятор разделён на `common.c`, `lexer.c`, `ast.c`, `parser.c`, `sema.c`, `codegen.c` и `driver.c`; внутренние интерфейсы находятся в `rblc_internal.h`. Поиск переменных, функций и строковых констант использует open-addressing hash indexes. Обе платформы используют общую реализацию frontend и семантики.

Язык включает типизированные параметры функций, рекурсию, условия, циклы, `switch`, списки, словари, кортежи, record-like structs, строки, f-string interpolation и ограниченные спецификаторы форматирования f-строк. Стандартная библиотека предоставляет глобальные built-ins и namespaces `rbl.io`, `rbl.math`, `rbl.string`, `rbl.fs`, `rbl.time` и `rbl.random`.

Runtime ABI `1.2.0` описывает tagged value representation, calling convention и обязательные функции Runtime. Runtime управляет динамическими строками и контейнерами с помощью консервативного неподвижного mark-and-sweep collector. Диагностика сохраняет исходные координаты на этапах lexer, parser, semantic analysis и codegen. В toolchain входят CLI, необязательная IDE RBL Studio, тестовые инструменты и benchmark suites.

Языковой и runtime-контракт определён в справочниках [синтаксиса](../reference/syntax.md), [стандартной библиотеки](../reference/standard-library.md), [модели выполнения](execution-model.md), [Runtime ABI](../reference/runtime-abi.md) и [ограничений](../reference/limitations.md).

## Следующий утверждённый этап

Следующий пакет работ над языком и компилятором — **v0.9.1: спецификация IR и verifier**. Текущий compiler по-прежнему строит assembly непосредственно из AST; IR, CFG и IR backend ещё не реализованы. Начинать следует с RFC/контракта IR и тестов verifier для некорректного IR. Понижение AST, построение CFG и перенос backend нельзя начинать до ревью и фиксации этого контракта. Система source modules для v1.0 — более поздний этап и также требует отдельного RFC до реализации.

## План релизов

## RBL 0.8.0 — стабилизация компилятора и runtime

**Цель:** стабилизировать архитектуру компилятора/runtime, добавить ограниченные спецификаторы форматирования f-строк и сохранить существующее поведение языка за пределами этого явного синтаксического дополнения. Source-module system в RBL 0.8.0 не входит.

### 0.8.0 — модульная архитектура compiler source и ускорение поиска

**Статус: реализовано.**

- Исходный код компилятора разделён на `common.c`, `lexer.c`, `ast.c`, `parser.c`, `sema.c`, `codegen.c` и `driver.c`; внутренние контракты находятся в `rblc_internal.h`.
- Для двух платформ используются общий frontend и единый контракт семантики; fast-path eligibility определяется semantic analysis, а не assembly emitter.
- Поиск переменных, функций и строковых констант выполняется через open-addressing hash indexes с сохранением порядка массивов и правила last-definition-wins.
- Makefile и Windows/WSL bootstrap paths отслеживают зависимости от compiler source, headers и VERSION.
- Измерения compiler-only latency изолируют накладные расходы модулей; сборки one-TU/seven-TU выдают побайтно идентичный assembly на детерминированных нагрузках.

**Завершение:** проходят полные regression и negative-diagnostics suites; чистая сборка работает на обеих платформах; компилируются все примеры; frontend общий; CLI работает согласованно; Runtime ABI явно версионирован и проверяется; benchmark воспроизводим.

### 0.8.0 - Runtime ABI и диагностика

- Value ABI определяет tags, payload, arguments, returns, calling convention и обязанности caller/callee.
- Compiler/runtime ABI имеет отдельную версию и проверку совместимости, независимую от версии языка.
- Source positions (файл, UTF-8 byte offset, строка и столбец с единицы) сохраняются через lexer, parser, AST, semantic validation и codegen.
- Диагностики Linux/Windows используют общий контракт расположения исходной ошибки, кроме необходимых платформенных различий.

**Текущий контракт:** Runtime ABI `1.2.0` включает tagged values, System V AMD64 helper convention, stack value arrays, containers, GC-managed strings и helper форматирования f-строк. Diagnostic pipeline сохраняет координаты исходника на всех этапах компилятора.

**Завершение:** несовместимые compiler/runtime пары отклоняются при линковке или до входа в RBL main; диагностики lexer/parser/semantic/codegen содержат корректные source positions и excerpts на обоих targets. Это не означает общей compile-time проверки типов или source spans/ranges.

### 0.8.0 - модель памяти строк

- Пути выделения строк охватывают literals, concatenation, `str()`, file I/O, formatting и преобразования контейнеров.
- Статические строки программы отделены от динамических heap strings.
- Поиск корней, достижимость и освобождение динамических строк интегрированы с container collector.
- Ownership и stress tests охватывают живые строки, сборку мусора, смешанные графы строк/контейнеров и ошибки аллокации.
- Длительные тесты проверяют повторное выделение памяти и I/O под GC pressure.

**Критерии завершения:** lifetime-модель всех динамических строк описана и тестируется.

**Статус: реализовано и проверено.** Native Windows и WSL test suites проходят, включая fault injection начальной/растущей/уплотняемой registry, mark stack/survivor vector/replacement registry, rollback/retry, native stack-borrowed roots, идемпотентность внутреннего освобождения и многократные `input()`/`read_file()` под GC pressure. Differential runner проверяет 83 выбранных source cases: для 61 успешно завершающейся программы и 11 ожидаемых runtime-error программ сравниваются exit code/stdout/stderr, 7 ожидаемо отрицательных fixtures отклоняются с одинаковыми diagnostics, для 4 программ с платформенным/случайным выводом требуется успешная сборка и завершение без сравнения output. Все 30 diagnostic differential cases проходят. В репозитории находится 121 исходный файл RBL в `examples/` и среди compiler-test исходников в `tests/` (53 примера и 68 тестовых исходников; один uppercase `.RBL` fixture для IDE намеренно исключён): 114 ожидаемо успешно компилируемых и 7 ожидаемых negative cases. Все 114 положительных исходников компилируются и проходят assembler на Windows и WSL; все 53 примера проходят команду `check` на обеих платформах. Консервативное удержание объектов из-за stale stack words и отсутствие native pinning/root-handle API остаются документированными ограничениями.

### 0.8.0 - спецификаторы форматирования f-строк

**Статус: реализовано и проверено.** Syntax reference описывает `[[fill]align][width][.precision][type]`, ширину 0..1 000 000 code points, точность 0..17, integer bases, float conversion types, преобразование `s`, значения по умолчанию и runtime-несовместимости. Parser diagnostics проверяют некорректные спецификаторы, явный отказ от dynamic/nested specs в начале и после width, а также ограничения; общий runtime formatter использует фиксированные форматы преобразования и managed RBL string allocation. Regression case покрывает основания, включая signed 64-bit minimum, точность до 17 цифр, ширину до 1 000 000, выравнивание, fill (включая `<`, `>` и `^` в роли fill), Unicode width и явный zero fill; GC stress case проверяет повторные выделения форматированных строк. Все 61 прямой regression case и 63 diagnostic cases проходят; f-string case выдаёт одинаковый результат в WSL/Linux и native Windows.

**Состояние 0.8.0:** модульный compiler, версионированный ABI, документированная lifetime-модель строк, ограниченные f-string format specs и межплатформенные критерии проверки реализованы. Оставшиеся ограничения перечислены в справочнике языка и manifest.

### Release checklist RBL 0.8.0

Этот checklist фиксирует реализацию и проверочное состояние RBL 0.8.0. Сам по себе он не является разрешением на публикацию.

**Уточнение области:** 6/6 означает завершение пакетов compiler/Runtime 0.8.0 и зафиксированных для них проверок. Это не означает готовность всего desktop-продукта к релизу. Интерактивная приёмка IDE и проверка corresponding source/source-offer для встроенного Windows toolchain — отдельные незакрытые release gates; они отслеживаются в [`RELEASE_MANIFEST.txt`](../../../RELEASE_MANIFEST.txt).

1. [x] **Архитектура** — модульный compiler, приватные границы модулей, lookup indexes, сохранённая семантика и воспроизводимые compiler/build measurements.
2. [x] **Runtime ABI** — версионированный descriptor, link/startup compatibility checks, layout значений, calling convention и обязательные feature bits.
3. [x] **Диагностика** — source positions и excerpts через lexer/parser/semantic/codegen, включая UTF-8 и межплатформенную совместимость.
4. [x] **Память** — документированное владение динамическими строками/контейнерами, детерминированный OOM rollback/retry сборщика, ограничения native C borrow и повторный I/O под GC pressure.
5. [x] **Форматирование f-строк** — ограниченная грамматика, согласованность parser/runtime, runtime type errors, managed output и граничные/differential tests.
6. [x] **Финальная проверка** — проходят 61 direct regression case; все 114 положительных исходников в наборе из 53 примеров и 68 тестовых файлов компилируются и проходят assembler, 7 отрицательных отклоняются; проходит 63-сценарный diagnostic suite; differential runner из 83 source cases проходит с 61 успешным output comparison, 11 expected-runtime-error comparisons, 7 expected-negative diagnostic и 4 nondeterministic build/exit comparisons, все 30 diagnostic differential cases совпадают. Проходят Runtime ABI, инструментальный Linux Runtime call alignment, build-cache isolation, deterministic OOM/GC/ownership и повторный I/O под GC. Tracked corpus и documentation validators проходят. Записаны controlled runtime (2 warmup/10 samples), module-isolation (3/10) и Makefile build (2/12 в disposable sandbox) benchmarks. Чистые source-only compiler/Runtime builds и запуск программ проходят в Windows и WSL sandboxes. Документация EN/RU, README, VERSION и manifest синхронизированы. Runtime benchmark был привязан к CPU, но выполнялся под задокументированной фоновой нагрузкой.

[`RELEASE_MANIFEST.txt`](../../../RELEASE_MANIFEST.txt) — единственный manifest кандидата RBL 0.8.0. Его предрелизный статус должен оставаться явно указанным до разрешённой публикации.

## v0.9 — RBL IR, CFG и оптимизатор

**Цель:** отделить семантику языка от машинного codegen. IR должен стать проверяемым промежуточным контрактом между frontend и backend.

### 0.9.1 — спецификация и verifier IR

**Текущий артефакт:** [предложение по дизайну IR v0.1](ir-rfc.md) (proposed, контракт не зафиксирован). Предлагаются SSA-подобные immutable temporaries с block arguments и явные mutable local slots; открытые решения RFC требуют review до фиксации стабильного контракта.

- Определить значения, инструкции, operands/results, функции, параметры, локальные и временные значения.
- Определить basic blocks и terminators, правила control-flow edges и представление вызовов.
- Явно описать side effects, runtime checks, ошибки и операции с контейнерами.
- Добавить IR verifier: арность/типы инструкций, ссылки на значения, корректность terminators, достижимость и переходы.
- Переносить source location metadata в IR.
- Не навязывать SSA/phi nodes до выбора и обоснования конкретной модели IR.

**Готово, когда:** весь IR проверяется verifier; некорректные IR-фрагменты ловятся отдельными negative tests.

### 0.9.2 — AST lowering и CFG

- Реализовать AST → IR без знаний о регистрах и x86-64.
- Представить if/switch/loops через basic blocks и явные переходы.
- Сохранить порядок вычислений, short-circuit rules и точки возникновения runtime errors.
- На переходном этапе сравнивать IR backend со старым путём на одном regression corpus.

**Готово, когда:** все конструкции текущего языка корректно представлены в CFG; тесты проверяют рёбра, циклы, выходы и недостижимые блоки.

### 0.9.3 — backend из IR

- Перевести x86-64 ASM codegen на вход IR, сохранив runtime value ABI.
- Сохранить Linux ELF64 и Windows PE/COFF как target-specific emission/link paths.
- Удалять AST-to-ASM путь только после parity, чистой сборки и прохождения differential suite.

### 0.9.4 — анализ и оптимизации

Порядок внедрения:

1. Liveness analysis для virtual values.
2. Global/whole-function register allocation, включая caller/callee-saved registers и spill slots.
3. Constant folding с сохранением ошибок и evaluation order.
4. Dead-code и dead-store elimination с учётом side effects.
5. Common-subexpression elimination только для выражений с доказанной чистотой и корректной aliasing-моделью.

**Критерии выхода 0.9:** IR verifier и CFG tests; эквивалентность optimized/unoptimized execution; Linux/Windows differential parity; отдельные regression tests для каждой оптимизации; воспроизводимый benchmark на одинаковых workloads. Прирост скорости не заменяет проверки корректности.

## v1.0 — модульная система языка

**Цель:** перейти от модели «один .rbl файл — одна программа» к многофайловым проектам с явными source modules и детерминированной сборкой. Модульная система — центральная пользовательская возможность 1.0.

### 1.0.1 — спецификация модуля и импорта

До реализации создать RFC и зафиксировать:

- **Единица модуля:** один .rbl файл с module identity; у executable project один entry module.
- **Формы импорта:** согласованные ранее варианты import module, from module import name и path import вида import "path.rbl". Aliases, re-export и точные правила относительных путей фиксируются в RFC, а не угадываются из реализации.
- **Поиск:** детерминированные правила относительно importing file и project root; неоднозначные совпадения — ошибка. Не выполнять неявный поиск по произвольному process working directory.
- **Идентичность:** нормализованный project-relative path, чтобы один физический файл не загружался дважды под разными путями.
- **Экспорт и видимость:** определить доступные извне объявления, private/public границу, qualified names и правила конфликтов.
- **Граф:** каждый import — dependency edge; модуль загружается, парсится и компилируется один раз на build graph.
- **Циклы:** начальная безопасная политика — обнаруживать циклические импорты и показывать полный цикл. Разрешать циклы можно только после спецификации инициализации и частично готовых модулей.
- **Инициализация:** определить, разрешены ли исполняемые module-level expressions и когда они выполняются. Базовый вариант — декларативные модули без неявного выполнения произвольного кода при import.
- **Ошибки:** missing module/export, duplicate identity/symbol, invalid path и cycle должны показывать import chain и source location.

### 1.0.2 — компиляция графа модулей

- Отделить module resolver/loader от lexer/parser каждого файла.
- Сначала строить и проверять dependency graph, затем выполнять semantic analysis и codegen.
- Ввести module symbol tables и детерминированное разрешение локальных и импортированных имён.
- Объединять модули в один program IR или согласованные IR units с единым entry point.
- Разрешить межмодульные вызовы без runtime dynamic loading.
- Сохранять source locations всех файлов для diagnostics и debug metadata.
- Не менять runtime value ABI только ради source import.

### 1.0.3 — project model и build system

- Зафиксировать минимальный rbl.toml: project name/version, entry module, source roots и target settings.
- Версия проекта не равна версии компилятора RBL.
- Команды check/build/run должны работать со всем dependency graph.
- Cache key должен учитывать содержимое исходников, зависимости, compiler version, ABI version и target.
- Изменение импортированного файла обязано инвалидировать кэш всех зависимых модулей.
- В 1.0 включить локальные project modules. Registry, загрузка внешних пакетов и semver dependency resolver — отдельный этап после 1.0.

### 1.0.4 — критерии готовности

- Старые однофайловые программы продолжают собираться без обязательной миграции.
- Тестируются direct/transitive/repeated imports, aliases, visibility, name conflicts, missing modules/exports, cycles, relative paths и cache invalidation.
- Каждый модуль компилируется один раз в рамках сборки.
- Linux и Windows дают эквивалентное поведение.
- Неизменённый проект использует корректный cache; изменение dependency пересобирает нужных потребителей.
- Ошибки содержат путь и позицию первопричины, а не только общий linker failure.
- Документированы resolution, visibility, initialization, cycle policy, layout проекта и переход с одного файла на модули.
- Публичный module contract закреплён integration/regression tests.

**Не входит в 1.0:** native plugin host для IDE/compiler; внешний package registry; гарантированный binary ABI между независимо собранными модулями; async scheduler и сложные closures до определения их memory/lifetime semantics.

## После 1.0

- Package manager, registry, lockfile и воспроизводимое получение внешних зависимостей.
- Native extensions/plugins с отдельными ABI, permissions, lifecycle, versioning и failure isolation.
- Closures/lambdas, default/variadic/named arguments и overload resolution.
- async/await, task scheduler и интеграция heap-resident frames с GC.
- try/catch, raise и defer после спецификации unwinding и cleanup semantics.
- Source debugger, breakpoints, variable inspection и Windows PE line tables.
- Дополнительные архитектуры, bytecode VM для debug/development и self-hosted frontend.

## Сквозной процесс для каждой возможности

1. RFC: проблема, примеры, grammar, semantics, ошибки и ограничения.
2. Модель: evaluation order, side effects, aliasing, ownership/lifetime и ABI.
3. Tests first: positive, negative, boundary, regression и differential tests.
4. Реализация в нужных слоях frontend/IR/backend/runtime без лишнего дублирования.
5. Validation: clean build обеих платформ, regression, smoke, differential и при необходимости fuzz/property tests.
6. Документация: tutorial/how-to/reference/explanation на русском и английском.
7. Release: Added/Changed/Fixed, version, manifest и проверка поставляемых бинарников.

## Управление roadmap

Этот файл — единственный актуальный release roadmap. design.md описывает принципы и ссылается сюда. Перевод пункта в «готово» допускается только после реализации и выполнения критериев выхода. При изменении приоритетов сначала обновляются обе языковые редакции roadmap. Работу над кандидатом можно отражать в release notes; дата публикации добавляется только после фактической публикации.
