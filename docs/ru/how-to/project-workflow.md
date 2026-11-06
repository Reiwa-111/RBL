# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

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

`rbl test` запускает regression suite и отдельную проверку стандартной библиотеки из checkout RBL. Это не универсальный тестовый runner для тестов произвольного проекта и не выполняет автоматически файлы из `my_app/tests/`.
