# last verified: v0.8.1 | 2026.11.17 | ПОДГОТОВКА РЕЛИЗА И СТАТУС ДОКУМЕНТАЦИИ

## Документация RBL

Актуальная документация описывает **язык RBL 0.8.1, компилятор, runtime и toolchain** и организована по [Diátaxis](https://diataxis.fr/): обучение, практические инструкции, справочник и объяснения. Она ориентирована на фактически реализованный компилятор и runtime, а не на предполагаемый будущий язык.

## С чего начать

- [уроки нововведений 0.8.x](tutorials/README.md) — десять уроков о нововведениях 0.8.x и IDE.

| Раздел | Для чего нужен | Основные материалы |
|---|---|---|
| **Курс для начинающих** | Пройти 20 уроков с запускаемыми примерами | [Курс для начинающих](../../examples/beginner/README_RUS.md) |
| **How-to** | Решить конкретную практическую задачу | [Работа с проектом](how-to/project-workflow.md), [Windows и WSL](how-to/windows-toolchain.md), [Упаковка Linux](how-to/linux-packaging.md), [Тестирование](how-to/testing.md), [RBL Studio](how-to/ide.md), [Измерение скорости](how-to/benchmarking.md) |
| **Reference** | Найти точное описание синтаксиса, функций или команды | [Синтаксис](reference/syntax.md), [Диагностика](reference/diagnostics.md), [Стандартная библиотека](reference/standard-library.md), [CLI](reference/cli.md), [Манифест](reference/manifest.md), [Ограничения](reference/limitations.md), [Runtime ABI](reference/runtime-abi.md) |
| **Explanation** | Понять устройство и причины технических решений | [Архитектура](explanation/architecture.md), [Модель выполнения](explanation/execution-model.md), [Оптимизации](explanation/optimization.md), [Производительность](explanation/performance.md), [Результаты benchmark](explanation/performance-results.md), [Принципы проектирования](explanation/design.md), [Дорожная карта](explanation/roadmap.md) |

## Поддерживаемая версия и область действия

- Версия репозитория/компилятора: `0.8.1` — источник истины: файл [`../../VERSION`](../../VERSION).
- Целевые платформы: x86-64 Linux (ELF64) и x86-64 Windows (PE/COFF).
- Пользовательские программы компилируются в машинный код через прямую генерацию x86-64 assembly.
- RBL Studio — необязательная графическая IDE; CLI и компилятор могут использоваться отдельно.
- Основной launcher/install path RBL Studio использует нативный wxWidgets 3.2 + Scintilla frontend из `rbl-ide/wx-ui/` и Rust `rbl-studio-worker`. Нативные Linux build и shell install проверены на Ubuntu 24.04 с системным wxWidgets 3.2.4 и host GTK 3. Flatpak payload и sandbox CLI проверены локально; интерактивный GUI/portal и Flathub acceptance не заявляются. Нативный first-run setup wizard выполняет диагностику prerequisites, сохраняет per-user setup state и запускает Install / Repair асинхронно при наличии полного installer payload. Пользователь подтвердил успешную проверку Windows mixed-DPI. Дерево 0.8.1 подготовлено к релизу, но ещё не опубликовано; `v0.8.0-r2` остаётся опубликованным source snapshot. Более ранняя запись GitHub `v0.8.0` остаётся pre-release placeholder.
- Source imports, многомодульная компиляция и менеджер зависимостей пока не реализованы.

RBL 0.8.1 сохраняет языковой и компиляторный контракт 0.8.0: ограниченные спецификаторы форматирования f-строк, Runtime ABI 1.2.0, управляемые динамические строки, диагностику исходных позиций и модульную реализацию компилятора. Маркеры `last verified` указывают на последнюю техническую проверку страницы.

Встроенные Windows MinGW-w64, Linux LLVM, wx runtime, CPython и Rust worker имеют отдельные сторонние лицензии и source obligations. См. [Windows toolchain notices](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [wx notices](../../rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md), [Linux packaging inventory](how-to/linux-packaging.md) и [аудит corresponding sources](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Apache-2.0 применяется к файлам RBL и не перелицензирует эти компоненты. GitHub Release публикует исходное дерево репозитория, которое уже содержит исходники Rust worker и wxWidgets IDE, а также source inventory toolchain; отдельно собранные upstream-архивы corresponding source не являются release deliverables или publication gates. Сторонние лицензионные обязательства при распространении bundled binaries сохраняются и требуют отдельной проверки.

## Релизы

Подготовка релиза RBL 0.8.1 и история изменений версии 0.8.0 описаны в [release notes](release-notes.md) в разделах **Added**, **Changed** и **Fixed**.

## Другой язык

[English edition](../eng/README.md) содержит ту же актуальную документацию на английском языке.
