# last verified: v0.8.0 | 2026.11.09 | PROJECT WORKFLOW RECHECK

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

Целевой каталог должен быть пустым или отсутствовать. Новый проект содержит:

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

Команда создаёт `rbl.toml`, `src/`, `tests/`, `benchmarks/` и стартовый `src/main.rbl`, если их ещё нет. Существующий манифест не перезаписывается.

## Собирать и запускать

Команды выполняются из каталога проекта, а `rbl.py` указывается путём до checkout:

```bash
python3 /path/to/RBLStudio/rbl.py check
python3 /path/to/RBLStudio/rbl.py build
python3 /path/to/RBLStudio/rbl.py run
python3 /path/to/RBLStudio/rbl.py asm
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

```bash
python3 /path/to/RBLStudio/rbl.py clean
```

Команда удаляет `build/` в текущем каталоге проекта. Не запускайте её из каталога, содержащего данные, которые вы хотите сохранить в `build/`.

## Проверить окружение

```bash
python3 /path/to/RBLStudio/rbl.py doctor
python3 /path/to/RBLStudio/rbl.py env
```

`doctor` проверяет доступность toolchain. `env` выводит корень установки RBL, текущий каталог проекта и путь к Python.

## Что означает `rbl test`

`rbl test` последовательно запускает следующие наборы из checkout RBL:

1. Прямые compiler regression cases (`tests/run_tests.py`).
2. Smoke-тесты standard library и input (`tests/test_stdlib.py`).
3. Изоляция build cache для одинаковых имён исходников (`tests/test_build_cache.py`).
4. Проверка SysV call alignment в Linux Runtime (`tests/test_asm_call_alignment.py`; в Windows запускается через WSL).
5. Проверки совместимости и startup Runtime ABI (`tests/test_runtime_abi.py`).
6. Тесты владения managed strings и детерминированных OOM collector (`tests/test_string_ownership.py`).
7. Повторные `input()`/`read_file()` под нагрузкой сборщика мусора (`tests/test_string_io_gc.py`).
8. Тесты source diagnostics компилятора (`tests/test_diagnostics.py`).

Это не универсальный тестовый runner для произвольных проектов и команда не выполняет автоматически файлы из `my_app/tests/`. Контракты отдельных наборов и межплатформенные проверки описаны в [руководстве по тестированию](testing.md) и [справочнике CLI](../reference/cli.md).
