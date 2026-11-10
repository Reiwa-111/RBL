# last verified: v0.8.0 | 2026.11.09 | EXAMPLE AND TEST DOCUMENTATION

## How-to: тестирование RBL

Текущий checkout 0.8.0 содержит несколько дополняющих друг друга уровней проверки: regression suite, smoke-тест стандартной библиотеки, проверку исходников примеров и differential runner для двух backend.

## Регрессионные тесты

Из корня checkout:

```bash
python3 tests/run_tests.py
```

В рабочей версии 0.8.0 набор содержит 61 прямой тестовый сценарий RBL. Скрипт запускает каждый тест через активный backend и сравнивает код завершения, stdout и заданные диагностические фрагменты. Дополнительно проверяется расстановка GC safe points в assembly: доказанно не выделяющие память scalar functions пропускают лишние проверки, allocating functions сохраняют их, а в доказанно fast-integer loops нет `rbl_gc_safepoint` на каждой итерации. Отдельный codegen regression test проверяет это различие и во время исполнения.

## Стандартная библиотека и ввод

```bash
python3 tests/test_stdlib.py
```

Этот тест отдельно проверяет глобальные функции и пространства имён стандартной библиотеки, файловый ввод-вывод и интерактивный `input(prompt)`.

## Изоляция build cache

```bash
python3 tests/test_build_cache.py
```

Тест создаёт два временных RBL-исходника с одинаковым именем, но разным содержимым, и намеренно задаёт одному более старое время изменения. Проверяется, что для них создаются разные build directories и каждая программа запускает именно свой код, а не чужой cached binary. Основная команда `python rbl.py test` включает эту проверку.

## Выравнивание вызовов Linux Runtime

```bash
python3 tests/test_asm_call_alignment.py
```

Тест собирает инструментированную копию Linux Runtime. Перед каждым текстовым assembly `call` проверяется, что `rsp` выровнен на 16 байт согласно SysV AMD64; проверка сохраняет исходные регистры и flags. Текущий исходник содержит 158 текстовых call sites; собранный Runtime object содержит 178 инструкций `call` после раскрытия assembler. Затем выполняется прямая regression suite и дополнительные сценарии сравнения/конкатенации строк с этим Runtime. В Windows используется WSL; без WSL тест сообщает о пропуске. Это проверка вызовов из Runtime в helpers/libc, отдельная от теста входного выравнивания generated fast functions в Runtime ABI suite.

## Совместимость Runtime ABI

```bash
python3 tests/test_runtime_abi.py
```

Тест компилирует небольшой native probe и сгенерированные RBL-программы. Он проверяет layout/offsets ABI, соответствие major-версии versioned symbols, совпадение tag values между C/compiler и Linux assembly, совместимые minor/patch-версии и расширения descriptor, отклонение несовместимых layout/features, отказ линковки при отсутствии versioned ABI symbol, диагностическое сообщение и код завершения, отказ до входа в RBL `main` с несовместимым встроенным descriptor и SysV stack alignment для вложенных fast-integer/boolean calls. Основная команда `python rbl.py test` включает эту проверку.

## Владение динамическими строками и OOM сборщика

```bash
python3 tests/test_string_ownership.py
```

Native probe проверяет stack roots, строки в элементах list и ключах/значениях dictionary, освобождение недостижимых строк, учёт heap bytes после роста list/dictionary и повторного подсчёта после GC, детерминированные отказы выделения памяти в начальной и растущей string registry, при её уплотнении, в container registry, mark stack, survivor vector и новой registry до sweep, отсутствие частичной сборки при ошибках (включая неизменность container-byte accounting) и успешный retry. Также проверяется идемпотентность внутреннего освобождения и native C borrowed pointer в stack-local после очистки копии в вызывающем frame. Основная команда `python rbl.py test` включает эту проверку.

## Повторные input/read_file под GC pressure

```bash
python3 tests/test_string_io_gc.py
```

Интеграционный тест 96 раз читает файл размером 64 КиБ и выполняет 12 000 вызовов `input()`, чередуя варианты с prompt и без него. Также проверяется, что пустой spec и `s` без ширины сохраняют произвольные байты, а форматирование с шириной отклоняет некорректный UTF-8 вместо неверного подсчёта символов. Проверяются ранние и последние сохранённые результаты и вывод prompt при пересечении автоматических порогов сборщика. Основная команда `python rbl.py test` включает эту проверку.

## Диагностика компилятора

```bash
python3 tests/test_diagnostics.py
```

Набор из 63 сценариев состоит из 45 ошибок исходника, 8 корректных контрольных программ, 4 ожидаемых runtime errors и 6 тестов некорректных UTF-8 byte sequences. Проверяются точные file/line/column и stage labels, source excerpts и точное выравнивание caret, UTF-8 columns включая многобайтовые символы и combining marks, tabs, LF/CRLF/одиночный CR, numeric separators, некорректные Unicode escapes, отклонение embedded NUL и классы malformed UTF-8, незакрытые и многострочные литералы, вложенные f-string expressions/комментарии, синтаксис и ограничения format specs, явное отклонение dynamic/nested specs в начале и после width, корректный разбор форматов, включая использование alignment-символов в качестве fill, runtime type mismatches, ошибки точности для integer и недопустимую точность с `s`, parser namespace/constant errors и ограничения вызовов, обнаруживаемые codegen. Тесты выполняются через выбранный backend. Команды `python3 rbl.py test` и `make test` включают этот набор.

Прямой regression case `tests/cases/fstrings.rbl` проверяет integer bases, включая signed 64-bit minimum, стили преобразования и точность float до 17 цифр, width до предела 1 000 000 code points, alignment/fill (включая alignment-символы в роли fill и явный zero fill), ширину Unicode в code points, пустую/default-интерполяцию и преобразование через `s` для всех runtime value tags. `tests/cases/gc_strings.rbl` также проверяет выделение форматированных строк при многократных GC. `tests/cases/gc_fast_loop.rbl` проверяет не выделяющий память fast-loop path при сохранении managed string root. Полный контракт форматов описан в syntax reference.

## Полный набор тестов через CLI

```bash
python3 rbl.py test
```

Команда последовательно запускает полный набор проверок checkout: direct regression, standard library/input, изоляцию build cache и безопасную очистку, инструментальную проверку Linux Runtime call alignment, Runtime ABI, GC safe-point codegen, владение динамическими строками/OOM, stress-тест повторных input/read_file под GC pressure, потоковый вывод tool, bundled-toolchain и manifest checks, Linux launcher, POSIX metadata, wx filesystem-security и worker-lifecycle checks, DPI static checks и диагностику компилятора. В Windows дополнительно выполняются registry-policy и save-cleanup tests. Проверки, зависящие от платформы, могут явно сообщать о пропуске при отсутствии prerequisites. Команда не ищет автоматически пользовательские тесты в каталоге каждого RBL-проекта.

`make test` запускает меньший compiler/Runtime subset (direct regression, standard library, cache isolation, call alignment, Runtime ABI, ownership, I/O-under-GC, toolchain manifest и diagnostics). Он не эквивалентен `python rbl.py test`.

## Сравнить Linux и Windows backend

На Windows при наличии WSL и native MinGW-w64:

```powershell
python tests\differential.py --a wsl --b native
```

По умолчанию выбираются 83 source cases: 68 файлов из `tests/cases/`, standard-library smoke source, 8 RBL-файлов прямо в `examples/` и 6 прямо в `benchmarks/`. Все примеры рекурсивно не запускаются. Для 61 успешно завершающейся детерминированной программы и 11 ожидаемых runtime-error программ сравниваются exit code/stdout/stderr (включая ожидаемый ненулевой exit code); 7 ожидаемо отрицательных fixtures должны отклоняться с одинаковыми compiler diagnostics; 4 программы со случайным или платформозависимым выводом должны успешно собраться и завершиться на обоих backend, но их вывод не сравнивается. Каждая программа запускается в отдельном временном рабочем каталоге, поэтому относительные файлы не перезаписывают файлы репозитория. Параметры `--a` и `--b` выбирают backend; типичные значения — `wsl` и `native`. После параметров можно передать конкретные пути.

Для отдельного сравнения диагностик компилятора между платформами (source positions, excerpts, malformed UTF-8 и вложенные f-string) запустите:

```powershell
python tests\differential_diagnostics.py
```

## Пересобрать bootstrap toolchain

```bash
python3 rbl.py bootstrap
```

Команда принудительно пересобирает bootstrap compiler/runtime artifacts. Обычно это не требуется: драйвер отслеживает файлы и использует доступные актуальные бинарные компоненты.

## Пример поиска в компиляторе

Рефакторинг компилятора — внутреннее архитектурное изменение; отдельно от него в RBL 0.8.0 добавлены ограниченные спецификаторы форматирования f-строк, но пользовательской системы source modules нет. Обычный RBL-пример ниже повторно обращается к переменным и функциям и использует одинаковые строковые значения; hash-индексы остаются приватной деталью реализации компилятора:

```bash
python3 rbl.py check examples/compiler/lookup_demo.rbl
python3 rbl.py run examples/compiler/lookup_demo.rbl
```

Ожидаемый вывод приведён в [описании примера](../../../examples/compiler/README.md). Это функциональная демонстрация, а не стресс-тест коллизий или benchmark времени.

Проверить все исходники примеров из корня репозитория в Windows PowerShell можно так:

```powershell
Get-ChildItem examples -Recurse -Filter *.rbl | ForEach-Object {
    python rbl.py check $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "RBL check failed: $($_.FullName)" }
}
```

Полная проверка всех исходников `examples/` и `tests/`, включая семь ожидаемо отрицательных файлов и assembler acceptance, доступна в tracked-скрипте:

```powershell
python tests\check_corpus.py
$env:RBL_TOOLCHAIN = 'wsl'
python tests\check_corpus.py
Remove-Item Env:RBL_TOOLCHAIN
```

Первый запуск проверяет native Windows/PE backend, второй — Linux/ELF через WSL. Команда `python tests\check_documentation.py` проверяет локальные Markdown-ссылки, якоря и структуру RU/EN страниц.

## После изменений языка или компилятора

1. Добавьте минимальный `.rbl` сценарий в `tests/cases/` и зарегистрируйте его в таблице `CASES` файла `tests/run_tests.py`, указав ожидаемые exit code, stdout и stderr. Runner использует явный список и не ищет новые файлы автоматически.
2. Если поведение связано со стандартной библиотекой или вводом — расширьте `tests/stdlib_smoke.rbl` и/или проверки `tests/test_stdlib.py`.
3. Запустите `python3 rbl.py test` и `python3 tests/check_corpus.py`.
4. На Windows выполните program и diagnostic differential suites против WSL.
5. Обновите соответствующие страницы reference и ограничения, если изменился публичный контракт.
6. При внутренних изменениях компилятора пересоберите оба platform backend, запустите полный corpus и differential checks, обновите описания архитектуры и оптимизаций. Не представляйте приватные детали компилятора как возможности языка RBL.

Дополнительный статический анализ GCC всех семи compiler translation units и трёх общих Runtime C modules запускается командой `bash tests/run_gcc_analyzer.sh` в Linux/WSL или `make analyze`. Он использует `-fanalyzer` и строгие проверки прототипов, а промежуточные object files создаёт только во временном каталоге.
