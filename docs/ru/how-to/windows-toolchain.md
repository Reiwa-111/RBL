# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: Windows, MinGW-w64 и WSL

RBL 0.7.2 умеет собирать нативные исполняемые файлы x86-64 Windows (PE/COFF). На Windows можно выбрать нативный MinGW-w64 toolchain или WSL backend.

## Проверить доступность toolchain

Из PowerShell в корне checkout:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

Рекомендуемый источник MinGW-w64 — MSYS2. Драйвер проверяет `RBL_MINGW`, стандартные каталоги установки MSYS2/MinGW и инструменты в `PATH`.

## Выбрать backend

Переменная окружения `RBL_TOOLCHAIN` имеет приоритет над сохранённой настройкой Studio:

```powershell
$env:RBL_TOOLCHAIN = "native"
python rbl.py doctor
```

Поддерживаются режимы:

| Значение | Поведение |
|---|---|
| `auto` | Предпочитает нативный MinGW-w64; использует WSL, если native недоступен и WSL настроен. |
| `native` | Требует нативный Windows toolchain. |
| `wsl` | Использует настроенный дистрибутив WSL. |

Указать каталог `bin` MinGW-w64 можно через `RBL_MINGW`:

```powershell
$env:RBL_MINGW = "C:\msys64\ucrt64\bin"
$env:RBL_TOOLCHAIN = "native"
python rbl.py doctor
```

## Запустить через WSL

Убедитесь, что установлен дистрибутив WSL и в нём доступны Python, GNU `as`, GNU `ld` и C compiler для пересборки компонентов при необходимости. Режим `wsl` выбирается так:

```powershell
$env:RBL_TOOLCHAIN = "wsl"
python rbl.py doctor
python rbl.py run examples\test.rbl
```

Дистрибутив по умолчанию — `Ubuntu-24.04`; конкретная конфигурация WSL задаётся настройками RBL Studio/toolchain.

## Проверить различия двух backend

При наличии одновременно WSL и нативного MinGW-w64:

```powershell
python tests\differential.py --a wsl --b native
```

Проверка сравнивает код завершения и, для детерминированных сценариев, stdout/stderr побайтно. Случайные результаты и текущий рабочий каталог платформозависимы и не сравниваются как фиксированные строки.

## Частые проблемы

| Симптом | Что проверить |
|---|---|
| Native toolchain не найден | Установлен ли MinGW-w64; указывает ли `RBL_MINGW` на каталог `bin`; доступен ли `gcc` и GNU `as`. |
| Не запускается WSL или конвертация путей | Доступен ли выбранный дистрибутив; работает ли `wslpath`; корректно ли смонтирован диск проекта. |
| Сборка использует не тот backend | Значение `RBL_TOOLCHAIN` и сохранённая настройка `wsl_mode`. |
| Не хватает runtime/compiler object | Запустите `python rbl.py doctor`; при необходимости — `python rbl.py bootstrap`. |
| Ошибка запуска IDE | Сначала проверьте `python tools/ide_doctor.py` и `python tools/ide_smoke.py`; журнал — `ide_startup.log`. |

## Ограничения

Наличие MinGW-пути у драйвера не означает, что протестирована каждая комбинация Windows, CRT и версии компилятора. Benchmark с ручными Linux syscall-программами не запускается как native Windows benchmark; используйте WSL или Linux. См. [измерение производительности](benchmarking.md).
