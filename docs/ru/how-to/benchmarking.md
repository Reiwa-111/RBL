# last verified: v0.8.0 | 2026.11.09 | BENCHMARKING GUIDE

## How-to: измерение производительности RBL

В репозитории есть два разных benchmark suite. Не смешивайте их результаты: они используют разные workloads и модели измерения.

## Портативный RBL-only benchmark

```bash
python3 rbl.py bench
```

Этот suite использует активный backend и выводит build time, полное время запуска процесса, оценку `net` (runtime минус baseline запуска пустой программы), диапазон времени и размер исполняемого файла. Для коротких нагрузок startup overhead может доминировать.

## Сравнение пяти реализаций

Запуск на Linux или в WSL:

```bash
python3 benchmarks/compare_all.py --warmups 2 --reps 10
```

Тот же suite доступен через CLI:

```bash
python3 rbl.py bench5way --warmups 2 --reps 10
```

Он сравнивает RBL, C `-O2`, C++ `-O2`, CPython и написанный вручную x86-64 assembly на трёх синтетических целочисленных нагрузках. Требуются GNU `as`, GNU `ld`, C compiler и C++ compiler. ASM baseline использует Linux syscalls, поэтому suite запускается в Linux/WSL, а не как native Windows executable. Старое имя команды `bench3way` оставлено как alias для обратной совместимости.

Runner измеряет полный запуск дочернего процесса, включая startup; время сборки RBL выводится отдельно. Если ОС предоставляет `sched_setaffinity`, benchmark-процесс закрепляется за одним CPU.

## Измерение модульности компилятора и сборки

Чтобы сравнить один translation unit с семью модулями компилятора, выполните в WSL/Linux:

```bash
python3 benchmarks/module_arch_perf.py --warmups 3 --reps 10 --build-warmups 2 --build-reps 12
```

Чтобы измерить полную, параллельную, инкрементальную сборку и no-op:

```bash
python3 benchmarks/make_build_perf.py --jobs 1 6 12 --warmups 2 --reps 12
```

**Makefile benchmark запускайте только в disposable source copy/worktree.** Он вызывает `make -B`, перезаписывает compiler build outputs и меняет timestamp копии `compiler/lexer.c`. Не запускайте его в рабочем checkout, если нужно сохранить сгенерированные бинарники и object files.

Обе команды измеряют поведение compiler/build, а не runtime сгенерированных программ. Полная методика и результаты приведены в [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md).

## Как сделать результаты сопоставимыми

1. Используйте одну машину, ОС/среду и версии toolchain.
2. Не меняйте workloads и optimization flags между запусками.
3. Используйте достаточное число прогревов и измерений; записывайте параметры.
4. Фиксируйте CPU, ОС/ядро, Python, GCC/G++, binutils, backend и сырые samples.
5. Повторяйте тест после изменения benchmark-кода, исходников компилятора или окружения.

C и C++ собираются с `-O2`; компилятор может преобразовывать циклы, сохраняя результат. Runtime-счётчики итераций и `noinline` для workload вызовов не позволяют native compiler полностью удалить предназначенную измеряемую работу. Это сравнение конкретных программ, а не универсальный рейтинг языков.

## Сохранённые результаты

Текущие runtime-измерения RBL 0.8.0, окружение и интерпретация приведены в [`benchmarks/RESULTS.md`](../../../benchmarks/RESULTS.md). Модульность компилятора, накладные расходы translation units и время сборки описаны в [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md).
