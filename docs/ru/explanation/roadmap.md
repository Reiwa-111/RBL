# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Назначение и статус

Это основной план развития RBL от версии **0.7.2** до **1.0**. Он заменяет прежний план из исторического архива. Здесь разделены подтверждённая реализация, незавершённая работа и будущие предложения. Roadmap задаёт порядок и критерии готовности, а не календарные обещания.

**Текущая база:** корневой VERSION содержит 0.7.2; CLI и Linux/Windows-компиляторы показывают 0.7.2. В текущем checkout проходят 59 direct regression tests и standard-library smoke test. Это проверка рабочего дерева, а не доказательство публикации тега v0.7.2.

## Принципы

1. **Сначала контракт, затем реализация.** У каждой возможности должны быть грамматика, семантика, порядок вычислений, ошибки и тесты.
2. **Корректность важнее скорости.** Ни одна оптимизация не должна менять наблюдаемое поведение, побочные эффекты или момент возникновения ошибки.
3. **Общий язык — два backend.** Linux ELF64 и Windows PE/COFF используют общий frontend и должны совпадать по детерминированному поведению. Платформенные различия документируются.
4. **Слои не смешиваются.** AST не знает о физических регистрах; IR не зависит от формата объектного файла; source import не является загрузчиком плагинов.
5. **Совместимость проверяется.** Рефакторинг не должен менять value ABI, синтаксис или evaluation order без отдельного решения и migration note.
6. **Нет функций «на бумаге».** Парсинг или прототип не означают готовность. Нужны реализация, позитивные и негативные тесты, документация и проверки обеих платформ.
7. **Релиз закрывается доказательствами.** Чистая сборка, regression, smoke и differential tests, обновлённые RU/EN docs и release notes.

## Уже выполнено

### v0.7.1 — выпущенная основа

**Статус: выполнено.** Релиз закрепил прямую генерацию x86-64 ASM из общего C frontend для Linux ELF64 и Windows PE/COFF. Пользовательский RBL-код не транслируется в C. Реализованы платформенные runtime и общий C runtime контейнеров, scalar fast paths и ограниченный loop-local register allocation.

К языковой базе относятся функции и рекурсия, условия и циклы, break/continue, switch, тернарное выражение, строки и f-string interpolation, списки, словари, кортежи, null, in/is, record-like struct и receiver-method syntax. Есть CLI, RBL Studio, стандартная библиотека и тестовая инфраструктура.

Коллектор использует conservative mark-and-sweep для контейнерных объектов. Это не полноценное освобождение всех динамических строк.

### v0.7.2 — текущая рабочая база

**Статус: изменения присутствуют в checkout; публикация релизного тега — отдельная операция.**

- VERSION — единый источник версии репозитория; CLI и оба бинарника rblc-asm показывают 0.7.2.
- Документация разделена на полноценные RU и EN редакции по Diátaxis.
- Release notes ведутся по Keep a Changelog; история 0.7.1 сохранена, запись 0.7.2 добавлена.
- Пятисторонний benchmark RBL/C/C++/CPython/ASM исправлен против полного удаления C/C++ workload оптимизатором; методика, результаты и ограничения описаны.
- Уточнены Windows runtime diagnostics и платформенные оговорки.
- Проверено: 59/59 regression cases, standard-library smoke test, CLI/compiler version checks для обеих целей.

**Не входит в 0.7.2:** разбиение компилятора, RBL IR, CFG, global register allocation, source imports и multi-file compilation.

## План релизов

## v0.8 — стабилизация компилятора и runtime

**Цель:** подготовить внутреннюю архитектуру к росту, не меняя семантику языка. 0.8 — инженерный релиз, не релиз модульной системы.

### 0.8.1 — разделение compiler source

- Разделить монолитный compiler source на common/diagnostics, lexer, AST, parser, semantic analysis, codegen и driver/CLI.
- Определить владение AST, токенами, строками и временными буферами; исключить неявное совместное владение.
- Вынести общие структуры в заголовки, определить небольшие внутренние интерфейсы.
- Оставить один frontend и один контракт семантики для двух платформ.
- Не совмещать механический перенос с переписыванием parser, изменением syntax или введением IR.

**Готово, когда:** все текущие тесты проходят без смены ожидаемого поведения; чистая сборка из исходников работает на обеих платформах; нет дублирования frontend; CLI и value ABI сохранены.

### 0.8.2 — runtime ABI и диагностика

- Формально описать value ABI: tags, payload, аргументы, возвраты, calling convention и обязанности caller/callee.
- Ввести отдельную версию compiler/runtime ABI и проверку совместимости. Версия языка и ABI version — разные сущности.
- Сохранять source spans (файл, строка, колонка, диапазон) до этапа диагностики.
- Согласовать диагностики Linux/Windows, оставив только необходимые платформенные различия.

**Готово, когда:** несовместимые compiler/runtime объекты обнаруживаются при сборке; ошибки frontend содержат корректное местоположение на обоих targets.

### 0.8.3 — модель памяти строк

- Инвентаризировать выделение строк: literals, concatenation, str(), file I/O, formatting и преобразования контейнеров.
- Разделить статические строки программы и динамические heap strings.
- Определить корни, достижимость и освобождение динамических строк в связи с container GC.
- Проверить, что достижимые строки не освобождаются, а недостижимые не удерживаются бесконечно.
- Добавить длительные stress tests для строк и смешанных графов строк/контейнеров.

**Готово, когда:** lifetime-модель всех динамических строк описана и тестируется. До этого нельзя заявлять полный garbage collection.

### 0.8.4 — завершение начатой поверхности языка

- Специфицировать и реализовать f-string format specs: числовая точность, основания чисел, ширина и выравнивание.
- Определить допустимые сочетания типа и спецификатора, поведение некорректных спецификаторов и правила Unicode/alignment.
- Реализовать одинаковое поведение Linux/Windows; не передавать пользовательский формат напрямую в небезопасный varargs-вызов.
- Добавить positive/negative tests для всех поддерживаемых форматов.

**Критерии выхода 0.8:** модульный compiler source; проверяемый ABI; завершённая и покрытая тестами lifetime-модель строк; реализованный согласованный набор f-string format specs; чистые build, regression, smoke и differential tests. Незавершённые пункты не объявляются shipped.

## v0.9 — RBL IR, CFG и оптимизатор

**Цель:** отделить семантику языка от машинного codegen. IR должен стать проверяемым промежуточным контрактом между frontend и backend.

### 0.9.1 — спецификация и verifier IR

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

Этот файл — единственный актуальный release roadmap. design.md описывает принципы и ссылается сюда; archive/ROADMAP.md и archive/NEXT_STEPS.md остаются историческими снимками. Перевод пункта в «готово» допускается только после реализации и выполнения критериев выхода. При изменении приоритетов сначала обновляются обе языковые редакции roadmap; release notes меняются при фактическом выпуске.
