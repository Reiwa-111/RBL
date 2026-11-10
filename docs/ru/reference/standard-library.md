# last verified: v0.8.0 | 2026.11.09 | LANGUAGE CONTRACT RECHECK

## Reference: стандартная библиотека RBL 0.8.0

Встроенные функции реализованы компилятором/runtime. Имена namespace доступны напрямую — оператор `import` для них не нужен. Это не пользовательская модульная система: source imports и загрузки сторонних пакетов нет.

## Глобальные функции

| Функция | Назначение и контракт |
|---|---|
| `print(value, ...)` | Выводит аргументы через один пробел и добавляет перевод строки; возвращает `Unit`. |
| `len(value)` | Число UTF-8 code points в строке; число элементов list/dictionary/tuple. |
| `input()` | Читает одну строку стандартного ввода. |
| `input(prompt)` | Сначала выводит и flush-ит prompt, затем читает одну строку. |
| `read_file(path)` | Читает текстовый файл. |
| `write_file(path, content)` | Записывает текст в файл. |
| `abs(x)`, `sqrt(x)` | Абсолютное значение и квадратный корень. |
| `min(x, ...)`, `max(x, ...)` | Минимум/максимум одного или нескольких числовых аргументов общего типа. |
| `int(x)`, `float(x)`, `str(x)` | Поддерживаемые runtime преобразования скалярных значений. |
| `pow`, `floor`, `ceil`, `round` | Степень и округление. |
| `sin`, `cos`, `tan`, `log`, `exp` | Математические операции; тригонометрические функции принимают радианы. |
| `random_int(a, b)` | Псевдослучайное целое в включительном интервале `[a, b]`. |
| `random_float()` | Псевдослучайное float-значение в `[0, 1]`. |
| `random_bool()` | Псевдослучайное булево значение. |

Строки, возвращаемые `input()`, `read_file()` и преобразованиями, являются значениями RBL под управлением Runtime. Код RBL не должен вручную освобождать их payload; время жизни определяется [моделью владения динамическими строками](../explanation/execution-model.md).

Также существуют глобальные aliases `log10`, `log2`, `trunc`, `fmod`, `hypot`, `degrees`, `radians`, `clamp`, `sign`, `is_nan` и `is_inf`.

Преобразования не означают, что любой произвольный контейнер/объект можно конвертировать в любой scalar. Для неверных типов runtime выдаёт диагностическую ошибку.

## Namespace `rbl.io`

| Функция | Назначение |
|---|---|
| `rbl.io.input()` | Читает строку из stdin. |
| `rbl.io.prompt(text)` | Выводит prompt и читает строку. |
| `rbl.io.read_file(path)` | Читает текстовый файл. |
| `rbl.io.write_file(path, content)` | Записывает текстовый файл. |

Пример:

```rbl
set text = rbl.io.read_file("notes.txt")
rbl.io.write_file("copy.txt", text)
```

## Namespace `rbl.math`

Реализованные методы namespace: `abs`, `sqrt`, `pow`, `min`, `max`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`, `log10`, `log2`, `trunc`, `sign`, `is_nan`, `is_inf`.

Константы `rbl.math.pi` и `rbl.math.e` — значения, не функции: круглые скобки после них не ставятся.

Особенности:

- `fmod(a, b)` выполняет float remainder с поведением C `fmod`: знак результата соответствует знаку делимого.
- Оператор `%` предназначен только для `int`.
- Глобальная функция `clamp(x, low, high)` требует совместимые числовые типы; метода `rbl.math.clamp` в реализации 0.8.0 нет.
- `fmod`, `hypot`, `degrees` и `radians` доступны как глобальные функции, но не как методы `rbl.math`.
- `sign(NaN)` возвращает `0` в текущей реализации.
- `is_nan` и `is_inf` принимают числовые значения; целые числа не являются NaN/Inf.

## Namespace `rbl.string`

| Функция | Назначение |
|---|---|
| `rbl.string.len(text)` | Длина строки в UTF-8 code points. |
| `rbl.string.upper(text)` | Преобразование ASCII букв в верхний регистр. |
| `rbl.string.lower(text)` | Преобразование ASCII букв в нижний регистр. |
| `rbl.string.trim(text)` | Удаляет пробелы, tab, LF и CR по краям. |
| `rbl.string.contains(text, part)` | Проверяет наличие подстроки. |
| `rbl.string.starts_with(text, prefix)` | Проверяет префикс. |
| `rbl.string.ends_with(text, suffix)` | Проверяет суффикс. |

`upper` и `lower` не выполняют Unicode case folding: байты UTF-8 вне ASCII копируются без изменения.

## Namespace `rbl.fs`

| Функция | Назначение |
|---|---|
| `rbl.fs.exists(path)` | Проверяет существование пути. |
| `rbl.fs.delete(path)` | Удаляет файл/путь и возвращает признак успеха. |
| `rbl.fs.cwd()` | Возвращает текущий рабочий каталог. |

Путь `cwd()` зависит от host OS и не должен сравниваться как идентичная строка между Windows и Linux.

## Namespace `rbl.time`

| Функция | Назначение |
|---|---|
| `rbl.time.now_ms()` | Возвращает миллисекундное значение часов платформы. |
| `rbl.time.sleep_ms(duration)` | Приостанавливает выполнение на неотрицательное число миллисекунд. |

Источник `now_ms()` различается: Linux использует `clock_gettime(CLOCK_REALTIME)` (wall clock), Windows — `GetTickCount64()` (монотонное время с момента загрузки). Абсолютные результаты и тип часов между платформами не сопоставимы. Для измерения длительности используйте внешний монотонный таймер benchmark runner.

## Namespace `rbl.random`

| Функция | Назначение |
|---|---|
| `rbl.random.int(min, max)` | Псевдослучайное целое из включительного интервала. |
| `rbl.random.float()` | Псевдослучайное float в `[0, 1]`. |
| `rbl.random.bool()` | Псевдослучайный bool. |

Глобальные aliases `random_int`, `random_float`, `random_bool` также доступны. Генератор не криптографический. Linux использует C library `rand()`, Windows — внутренний linear congruential generator с 31-битным выходом; последовательности между платформами не совпадают и не должны использоваться для криптографии, токенов или секретов.

## Logging methods

```rbl
warn.log("message")
error.log("message")
```

Они печатают сообщения с префиксами `[WARN]` и `[ERROR]`. Неизвестный метод вызывает runtime error после вычисления аргументов.

## Платформенная реализация

| Компонент | Исходник / назначение |
|---|---|
| Linux scalar runtime | `runtime/rbl_runtime.s` |
| Windows scalar runtime | `runtime/rbl_runtime_win.c` |
| Lists/dictionaries/tuples и collector | `runtime/rbl_containers.c` |

Общий интерфейс языка не означает полной идентичности всех системных наблюдаемых значений: `cwd`, источник часов и случайные последовательности намеренно платформозависимы.
