# last verified: v0.8.0 | 2026.11.09 | RUNTIME ABI REFERENCE RECHECK

## Runtime ABI v1

**Статус:** реализовано в RBL 0.8.0. Версия ABI независима от версии языка и компилятора в `VERSION`.

## Политика версий

Текущий контракт — **Runtime ABI 1.2.0**.

- Изменение major несовместимо. Компилятор для ABI major 1 ссылается на версионированную функцию `rbl_runtime_abi_check_v1`; если в Runtime её нет, линковка завершается ошибкой.
- Minor может добавлять обратно совместимые функции и возможности. Runtime с тем же major и minor не ниже минимального, требуемого компилятором, принимается.
- Patch может исправлять реализацию, но не должен менять layouts, calling convention или наблюдаемый ABI-контракт. Patch level не участвует в проверке совместимости: различия patch в обе стороны допустимы, если major, minor и структурные требования совпадают.
- Компилятор записывает минимальную minor-версию и обязательные feature bits в descriptor внутри каждого executable.

Runtime проверяет descriptor до вызова RBL-функции `main`. При несовместимости программа завершается с кодом **78** и диагностикой, указывающей причину. Проверка дополнительно выявляет несовпадение layout, разрядности указателя, calling convention и обязательных возможностей, которое невозможно надёжно определить только по номеру версии.

## Платформы и calling convention

Поддерживаются x86-64 Linux ELF64 и x86-64 Windows PE/COFF. Runtime helper calls используют **System V AMD64 calling convention на обеих платформах**:

- Целочисленные аргументы и указатели передаются через `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, затем через стек по правилам SysV AMD64.
- Caller-saved GPR: `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`-`r11`; callee-saved GPR: `rbx`, `rbp`, `r12`-`r15` и `rsp`. Сохранение vector registers соответствует SysV AMD64 (XMM-регистры caller-saved). Перед вызовом caller выравнивает стек на 16 байт.
- Экспортируемые функции Windows C runtime используют `__attribute__((sysv_abi))`. Сам PE entry point вызывается Windows CRT по стандартному соглашению платформы, после чего переключается на соглашение RBL helpers.
- Linux executable начинает выполнение с `_start`; Windows executable входит через `main`, вызываемый CRT.

## Представление значений

Общее runtime-значение — пара 64-битных слов, **16 байт**:

| Слово | Значение |
|---|---|
| `tag` | Тег runtime-типа |
| `payload` | Биты целого/Float, указатель или boolean payload |

| Tag | Тип |
|---:|---|
| 0 | `Unit` |
| 1 | `Int` (signed 64-bit) |
| 2 | `Float` (IEEE-754 binary64) |
| 3 | `String` (указатель на NUL-terminated данные) |
| 4 | `Bool` |
| 5 | `List` |
| 6 | `Dictionary` |
| 7 | `Tuple` |
| 8 | `null` |

Числовые теги объявлены в `RBLRuntimeTag` файла `runtime/rbl_abi.h`. Для `Bool` payload равен 0 или 1; payload значений `List`, `Dictionary` и `Tuple` — указатели на Runtime objects. `Unit` и `null` различаются собственными тегами.

При обычном tagged return регистр `rax` содержит tag, а `rdx` — payload. C-функции, возвращающие двухсловную структуру значения, используют aggregate-return convention SysV AMD64 и возвращают ту же пару регистров. Некоторые доказуемо безопасные integer/boolean fast paths используют unboxed значения; это не меняет общего представления значений.

Один tagged value обычно передаётся runtime helper как `(tag, payload)` в `(rdi, rsi)`. Два значения — как `(tag1, payload1, tag2, payload2)` в `(rdi, rsi, rdx, rcx)`. Helpers для переменного числа значений получают указатель на непрерывный массив 16-байтовых пар и их количество.

Пользовательские RBL-функции используют определённое компилятором соглашение с массивом аргументов: `rdi` указывает на непрерывный массив пар по 16 байт, `rsi` содержит число аргументов. Tagged return находится в `rax:rdx`. Это внутреннее соглашение компилятора и Runtime, а не C API для внешних вызывающих программ.

## Descriptor и проверка совместимости

`runtime/rbl_abi.h` — канонический источник layout descriptor, версии ABI, идентификатора calling convention, feature bits и C-деклараций между translation units для общего container Runtime. В заголовке объявлены list, tuple, dictionary, equality, membership/indexing и GC entry points, вызываемые сгенерированным кодом, а также общие platform helpers, используемые C Runtime objects. Linux и Windows bootstrap-сборки включают проверки отсутствующих/повторных прототипов для этих общих C-модулей. `runtime/rbl_abi.c` экспортирует:

- `rbl_runtime_abi_v1` — descriptor текущего Runtime;
- `rbl_runtime_abi_check_v1` — версионированную функцию, вызываемую сгенерированной программой;
- `rbl_runtime_abi_validate` — чистую функцию проверки совместимости descriptor, используемую тестами.

Descriptor версии ABI 1.2.0 имеет размер ровно 40 байт. Смещения полей зафиксированы и проверяются во время компиляции:

| Offset | Size | Поле | Значение |
|---:|---:|---|---|
| 0 | 8 | `magic` | `0x52424c4142493031` |
| 8 | 8 | `features` | Битовая маска возможностей |
| 16 | 4 | `descriptor_size` | Размер descriptor в байтах |
| 20 | 4 | `value_size` | Размер `RBLRuntimeValue` в байтах |
| 24 | 4 | `pointer_size` | Размер указателя в байтах |
| 28 | 4 | `calling_convention` | Идентификатор соглашения (`1` = SysV AMD64) |
| 32 | 2 | `major` | ABI major version |
| 34 | 2 | `minor` | ABI minor version |
| 36 | 2 | `patch` | ABI patch version |
| 38 | 2 | `reserved` | Зарезервировано; сейчас ноль |

Layout `RBLRuntimeValue` также проверяется: `tag` находится по смещению 0, `payload` — по смещению 8. Для совместимости major должен совпадать; minor Runtime должен быть не ниже минимального, требуемого компилятором; layout value и указателя, а также calling convention должны совпадать; Runtime должен поддерживать все обязательные features. Patch level не влияет на совместимость. Более новый minor Runtime может расширить descriptor, сохранив существующий 40-байтовый префикс; проверка размера отклоняет старый descriptor, который не удовлетворяет требованиям компилятора.

Feature bits версии 1.2: `TAGGED_VALUES`, `SYSV_AMD64`, `STACK_VALUE_ARRAYS`, `CONTAINERS`, `GC_MANAGED_STRINGS` и `FSTRING_FORMAT`.

| Bit | Возможность | Mask |
|---:|---|---:|
| 0 | `TAGGED_VALUES` | `0x01` |
| 1 | `SYSV_AMD64` | `0x02` |
| 2 | `STACK_VALUE_ARRAYS` | `0x04` |
| 3 | `CONTAINERS` | `0x08` |
| 4 | `GC_MANAGED_STRINGS` | `0x10` |
| 5 | `FSTRING_FORMAT` | `0x20` |

Версионированный symbol проверяется линкером и защищает от несовместимого ABI major. Descriptor handshake выполняется при запуске процесса и проверяет minor и структурную совместимость. ABI 1.2 требует feature `FSTRING_FORMAT` и внутренний helper `rbl_format_value`, вызываемый сгенерированными f-string expressions. Build driver отслеживает formatting source/header как зависимости Runtime и линкует formatting object в executable для Linux и Windows.

## Владение и время жизни динамических строк

Payload строки по-прежнему является указателем на NUL-terminated данные, но динамически выделенные строки теперь принадлежат Runtime и управляются тем же консервативным mark-and-sweep collector, что и списки, кортежи и словари. Строковые литералы и другие статические константы Runtime не регистрируются и никогда не освобождаются сборщиком.

- Сгенерированный RBL-код не должен вызывать `free()` для string payload. Все динамические строки, возвращаемые как RBL values, принадлежат Runtime.
- Динамическая строка считается живой, пока её точный base pointer встречается как pointer-shaped machine word где-либо в сканируемом диапазоне стека либо достижим как строковый элемент/ключ/значение живого контейнера. Сканирование стека консервативное и не проверяет tag слова. Алиасы не создают отдельного владения: они ссылаются на одну аллокацию.
- Сборка выполняется в safe points, которые компилятор вставляет перед операторами, способными выделять память или вызывать код с неизвестным поведением аллокаций, а также перед проверкой условий циклов. Для доказанно не выделяющих память integer/Boolean scalar statements избыточные safe points пропускаются. В точках сборки живые локальные RBL values сохранены в stack slots, а временные значения предыдущего выражения уже не используются. Runtime allocators не запускают сборку посреди вычисления выражения.
- Сборщик сканирует слова стека текущего потока между своим активным frame и frame pointer, сохранённым из сгенерированной `main`. Поэтому pointer, материализованный в stack local native C frame внутри этого диапазона, консервативно считается root, пока frame активен; это не гарантия явной регистрации корня. Native heap/global storage и другие потоки не сканируются. Native-код не должен хранить managed pointer только в этих местах или после исчезновения всех соответствующих RBL/stack roots. `rbl_string_free` — внутренний Runtime helper, а не API передачи владения приложению; ABI не предоставляет pinning или явные root handles.
- Состояние Runtime и registry сборщика глобальны для процесса и не синхронизированы. Runtime 0.8.0 поддерживает один поток выполнения: native callers могут вызывать Runtime helpers только из потока, вошедшего в сгенерированную RBL `main`; параллельные вызовы из других потоков не поддерживаются. Протокола регистрации потоков или per-thread stack roots нет.
- Сборщик консервативный: случайное слово на стеке, похожее на указатель, может продлить жизнь объекта. Объекты не перемещаются, поэтому адрес живой строки остаётся стабильным.
- Если не удаётся выделить mark stack, survivor vector или новую таблицу container registry, сборщик сбрасывает временные mark bits и прекращает сборку до sweep; существующая куча остаётся целой, а следующий safe point может повторить попытку. Ошибка уплотнения string registry оставляет старую таблицу действительной.

Компилятор и Runtime должны согласованно расставлять safe points. ABI 1.2 требует `GC_MANAGED_STRINGS`, `FSTRING_FORMAT`, helper `rbl_gc_safepoint` и внутренний helper `rbl_format_value`. Runtime без обязательных features отклоняется startup descriptor check до выполнения RBL `main`.

## Тесты

Отдельные проверки descriptor и несовместимости запускаются командой:

```bash
python3 tests/test_runtime_abi.py
```

Набор проверяет совместимость descriptor, major guard на этапе линковки, отказ до входа в RBL `main`, совпадение tag values между C/compiler и Linux assembly, а также SysV stack alignment для вложенных fast-integer и boolean calls. Основная команда тестирования (`python rbl.py test` или `make test`) включает эти проверки. Интеграционные тесты дополнительно собирают и запускают RBL-программы, проверяя startup handshake и наличие ABI symbol при линковке.
