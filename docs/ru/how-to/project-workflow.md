# last verified: v0.8.1 | 2026.11.17 | CURRENT DOCUMENTATION REVIEW

## How-to: работа с проектом

## Создать новый проект

Из корня checkout:

```bash
python3 rbl.py new my_app
```

Указать другой путь:

```bash
python3 rbl.py new my_app --path ../my_app
```

Целевой каталог должен быть пустым или отсутствовать. NAME: 1-64 ASCII-буквы, цифры, `_` или `-`, первый символ - буква или цифра. Новый проект содержит:

```text
my_app/
├── rbl.toml
├── README.md
├── src/
│   └── main.rbl
├── tests/
└── benchmarks/
```

## Инициализировать существующий каталог

```bash
python3 /path/to/RBLStudio/rbl.py init
```

Команда создаёт `rbl.toml`, `src/`, `tests/`, `benchmarks/` и стартовый `src/main.rbl`, если их ещё нет. Имя текущего каталога должно соответствовать правилу имени проекта. Существующий манифест не перезаписывается.

## Собирать и запускать

CLI не устанавливает глобальную команду. Укажите `RBL_STUDIO_ROOT` как путь к checkout RBL Studio и выполняйте команды из каталога проекта:

```bash
export RBL_STUDIO_ROOT="/path/to/RBLStudio"
python3 "$RBL_STUDIO_ROOT/rbl.py" check src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" build src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" run src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" asm src/main.rbl
```

Windows PowerShell:

```powershell
$env:RBL_STUDIO_ROOT = "C:\path\to\RBLStudio"
python "$env:RBL_STUDIO_ROOT\rbl.py" check src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" build src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" run src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" asm src\main.rbl
```

Путь к исходнику можно указать явно после команды. Без аргумента используется `package.entry` из `rbl.toml`; по умолчанию это `src/main.rbl`. Если такого файла нет, драйвер также проверяет `main.rbl` в корне проекта.

## Отформатировать файл

```bash
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl --check
```

Форматтер нормализует отступы блоков по скобкам в начале/конце строк. Это не полноценный форматтер с настройками стиля и не проверка всех соглашений оформления.

## Сгенерировать простой список функций

```bash
python3 /path/to/RBLStudio/rbl.py doc src/main.rbl -o target/api.md
```

`rbl doc` — простой генератор таблицы объявлений функций, извлекающий имя, параметры и возвращаемый тип по текстовому шаблону. Это не генератор полной документации, не анализатор типов и не генератор документации стандартной библиотеки.

## Очистить результаты сборки

Disposable-артефакты в checkout разделены по назначению:

- `build/cache/sources/` — скомпилированные пользовательские программы и
  скачанные upstream-архивы исходников. Дорого пересобирать.
- `build/output/` — выходы IDE, worker, Cargo и bootstrap из текущего checkout.

```bash
python3 /path/to/RBLStudio/rbl.py clean   # только compiler cache
make clean-cache                          # тот же cache, из корня checkout
make clean-build                          # выходы IDE/worker/bootstrap
make clean                                # alias для clean-build
```

`clean` удаляет только compiler source cache `build/cache/sources/` внутри
checkout RBL Studio. `build/output/`, каталог сборки текущего проекта и
остальные build evidence не удаляются. `make clean` не трогает cache; для него
есть `make clean-cache`.

## Проверить окружение

```bash
python3 /path/to/RBLStudio/rbl.py doctor
python3 /path/to/RBLStudio/rbl.py env
```

`doctor` проверяет доступность toolchain. `env` выводит корень установки RBL, текущий каталог проекта и путь к Python.

## Что означает `rbl.py test`

`rbl.py test` последовательно запускает наборы самого RBL Studio; эта команда не запускает тесты текущего проекта:

Текущий runner включает:

- Прямые compiler regression, standard library/input, build-cache isolation, проверки scaffold нового проекта и безопасной очистки.
- Инструментальную проверку Linux Runtime call alignment, совместимость/startup Runtime ABI, GC safe-point codegen, владение managed strings/OOM и повторный I/O под GC pressure.
- Проверки streaming output, bundled toolchain и manifest, Linux launcher, POSIX metadata, wx filesystem security и worker lifecycle, а также статические DPI checks.
- Windows-only проверки registry policy и save cleanup, после которых запускаются compiler source diagnostics.

Точный порядок и platform gates определены функцией `run_tests()` в `rbl.py`; контракты отдельных проверок описаны в [руководстве по тестированию](testing.md).

Это не универсальный тестовый runner для произвольных проектов и команда не выполняет автоматически файлы из `my_app/tests/`. Контракты отдельных наборов и межплатформенные проверки описаны в [руководстве по тестированию](testing.md) и [справочнике CLI](../reference/cli.md).
