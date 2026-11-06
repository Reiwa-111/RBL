# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Reference: CLI RBL

Основная точка входа — Python-скрипт `rbl.py` в репозитории RBL. Он не устанавливает глобальную команду `rbl` в систему. В командах ниже `python3` используется в Linux/WSL, а `python` — в PowerShell.

## Команды

| Команда | Назначение |
|---|---|
| `rbl.py version` | Печатает версию из корневого файла `VERSION`. |
| `rbl.py doctor` | Проверяет toolchain через низкоуровневый driver. |
| `rbl.py bootstrap` | Принудительно перестраивает bootstrap compiler/runtime artifacts. |
| `rbl.py env` | Печатает RBL root, project root и Python executable. |
| `rbl.py new NAME [--path DIR]` | Создаёт новый проект. |
| `rbl.py init` | Инициализирует текущий каталог. |
| `rbl.py check [FILE]` | Выполняет frontend/analysis/codegen check без assembler и linker. |
| `rbl.py build [FILE]` | Компилирует, ассемблирует и линкует executable. |
| `rbl.py run [FILE]` | Собирает и запускает программу. |
| `rbl.py asm [FILE]` | Запускает assembly preview и показывает сгенерированный ASM. |
| `rbl.py test` | Запускает repository regression suite и stdlib smoke test. |
| `rbl.py bench` | Запускает переносимый RBL-only benchmark. |
| `rbl.py bench3way [--reps N] [--warmups N]` | Запускает сравнение пяти реализаций на Linux/WSL. Имя команды оставлено для совместимости. |
| `rbl.py diff [--a MODE] [--b MODE] [PATH ...]` | Сравнивает два backend. По умолчанию `wsl` и `native`. |
| `rbl.py fmt [FILE] [--check]` | Форматирует файл либо проверяет, что форматирование уже применено. |
| `rbl.py doc [FILE] [-o FILE]` | Генерирует простую таблицу объявлений функций. |
| `rbl.py clean` | Удаляет каталог `build/` в текущем каталоге. |

Файл `FILE` для `check`, `build`, `run`, `asm`, `fmt` и `doc` необязателен: если он не передан, CLI берёт `package.entry` из манифеста, иначе `src/main.rbl` по умолчанию. Если этого пути нет и в корне текущего проекта существует `main.rbl`, используется он.

## Примеры

Linux:

```bash
python3 rbl.py new demo
cd demo
python3 ../rbl.py check
python3 ../rbl.py build
python3 ../rbl.py run
python3 ../rbl.py test
```

Windows PowerShell:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

## `check` — что он делает

`check` не является только parser check: он выполняет обработку исходника, анализ и генерацию assembly во временный поток. Но он не вызывает assembler/linker, не создаёт исполняемый файл и не сохраняет обычный assembly output.

## `bench` и `bench3way`

Эти команды запускают разные scripts:

- `bench` → `benchmarks/run_benchmarks.py`, RBL-only измерения, доступные через выбранный backend.
- `bench3way` → `benchmarks/compare_all.py`, пять реализаций, Linux syscall ASM baseline, только Linux/WSL.

`bench` в текущем CLI не принимает `--reps` и `--warmups`; эти параметры относятся к `bench3way`. См. [how-to benchmark](../how-to/benchmarking.md).

## Низкоуровневый driver

Для диагностики и скриптов существует `tools/rbltool.py`. Он предоставляет команды `doctor`, `check`, `build`, `run`, `asm`; операции создания проектов, benchmark, форматирования, API listing и очистки добавляет верхнеуровневый `rbl.py`.
