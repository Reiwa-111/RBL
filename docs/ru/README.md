# last verified: v0.8.0 | 2026.11.10 | DOCUMENTATION RELEASE READINESS

## Документация RBL

Актуальная документация описывает **рабочую версию RBL 0.8.0** и организована по [Diátaxis](https://diataxis.fr/): обучение, практические инструкции, справочник и объяснения. Она ориентирована на фактически реализованный компилятор и runtime, а не на предполагаемый будущий язык.

## С чего начать

| Раздел | Для чего нужен | Основные материалы |
|---|---|---|
| **Курс для начинающих** | Пройти 20 уроков с запускаемыми примерами | [Курс для начинающих](../../examples/beginner/README_RUS.md) |
| **How-to** | Решить конкретную практическую задачу | [Работа с проектом](how-to/project-workflow.md), [Windows и WSL](how-to/windows-toolchain.md), [Тестирование](how-to/testing.md), [RBL Studio](how-to/ide.md), [Измерение скорости](how-to/benchmarking.md) |
| **Reference** | Найти точное описание синтаксиса, функций или команды | [Синтаксис](reference/syntax.md), [Диагностика](reference/diagnostics.md), [Стандартная библиотека](reference/standard-library.md), [CLI](reference/cli.md), [Манифест](reference/manifest.md), [Ограничения](reference/limitations.md), [Runtime ABI](reference/runtime-abi.md) |
| **Explanation** | Понять устройство и причины технических решений | [Архитектура](explanation/architecture.md), [Модель выполнения](explanation/execution-model.md), [Оптимизации](explanation/optimization.md), [Производительность](explanation/performance.md), [Результаты benchmark](explanation/performance-results.md), [Принципы проектирования](explanation/design.md), [Дорожная карта](explanation/roadmap.md) |

План обновления графического интерфейса, тем, настроек и developer experience: [RBL Studio — план обновления интерфейса и UX](IDE_DESIGN_REWORK_PLAN.md).

## Поддерживаемая версия и область действия

- Версия репозитория/компилятора: `0.8.0` — источник истины: файл [`../../VERSION`](../../VERSION).
- Целевые платформы: x86-64 Linux (ELF64) и x86-64 Windows (PE/COFF).
- Пользовательские программы компилируются в машинный код через прямую генерацию x86-64 assembly.
- RBL Studio — необязательная графическая IDE; CLI и компилятор могут использоваться отдельно.
- Основной launcher/install path RBL Studio использует нативный wxWidgets 3.2 + Scintilla frontend из `native/wx-ui/` и Rust `rbl-studio-worker`. Исходники прежнего Rust/egui frontend сохранены как legacy reference; `RBL_STUDIO_ALLOW_LEGACY=1` разрешает переход на отдельно собранный executable из developer checkout, если wx executable недоступен, но legacy binary не входит в wx installer payload. wx-приложение пока имеет статус UNRELEASED до завершения приёмки и проверок распространения.
- Source imports, многомодульная компиляция и менеджер зависимостей пока не реализованы.

RBL 0.8.0 включает ограниченные спецификаторы форматирования f-строк, Runtime ABI 1.2.0, управляемые динамические строки, диагностику исходных позиций и модульную реализацию компилятора. Маркеры `last verified` указывают на последнюю техническую проверку страницы.

Встроенная Windows MinGW-w64 toolchain и wx runtime имеют отдельные сторонние лицензии. См. [toolchain notices](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [wx notices](../../native/wx-ui/THIRD_PARTY_NOTICES.md) и [аудит corresponding sources](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Apache-2.0 применяется к файлам RBL и не перелицензирует эти бинарные компоненты. Архив исходных материалов собран локально, но распространение остаётся заблокировано, пока материалы не будут приложены к бинарному пакету либо письменное предложение не пройдёт юридическую проверку.

## Релизы

Подготовленные пользовательские изменения RBL 0.8.0 описаны в [release notes](release-notes.md) в разделах **Added**, **Changed** и **Fixed**.

## Другой язык

[English edition](../eng/README.md) содержит ту же актуальную документацию на английском языке.
