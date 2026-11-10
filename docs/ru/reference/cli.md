# last verified: v0.8.0 | 2026.11.10 | CLI AND TEST SUITE DOCUMENTATION

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
| `rbl.py test` | Запускает compiler/Runtime regression и diagnostics, а также build-cache/cleanup, GC, toolchain, launcher, filesystem-security, worker-lifecycle и DPI checks; в Windows добавляются registry/save-cleanup tests. |
| `rbl.py bench` | Запускает переносимый RBL-only benchmark. |
| `rbl.py bench5way [--reps N] [--warmups N]` | Запускает сравнение пяти реализаций на Linux/WSL; `bench3way` оставлена как legacy alias. |
| `rbl.py diff [--a MODE] [--b MODE] [PATH ...]` | Сравнивает два backend. По умолчанию `wsl` и `native`. |
| `rbl.py fmt [FILE] [--check]` | Форматирует файл либо проверяет, что форматирование уже применено. |
| `rbl.py doc [FILE] [-o FILE]` | Генерирует простую таблицу объявлений функций. |
| `rbl.py clean` | Безопасно удаляет только compiler cache `build/sources/`, сохраняя остальные build evidence и пользовательские данные. |

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

## `bench` и `bench5way`

Эти команды запускают разные scripts:

- `bench` → `benchmarks/run_benchmarks.py`, RBL-only измерения, доступные через выбранный backend.
- `bench5way` → `benchmarks/compare_all.py`, пять реализаций, Linux syscall ASM baseline, только Linux/WSL.
- `bench3way` остаётся alias для обратной совместимости.

`bench` в текущем CLI не принимает `--reps` и `--warmups`; эти параметры относятся к `bench5way`. См. [how-to benchmark](../how-to/benchmarking.md).

## Низкоуровневый driver

Для диагностики и скриптов существует `tools/rbltool.py`. Он предоставляет команды `doctor`, `check`, `build`, `run`, `asm`; операции создания проектов, benchmark, форматирования, API listing и очистки добавляет верхнеуровневый `rbl.py`. Этапы компиляции, исходные координаты и покрытие тестами описаны в [справочнике диагностики](diagnostics.md).
