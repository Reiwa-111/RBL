# last verified: v0.8.0 | 2026.11.11 | BUNDLED WINDOWS TOOLCHAIN

## How-to: Windows, MinGW-w64 и WSL

RBL 0.8.0 умеет собирать нативные исполняемые файлы x86-64 Windows (PE/COFF). На Windows можно выбрать нативный MinGW-w64 toolchain или WSL backend.

## Проверить доступность toolchain

Из PowerShell в корне checkout:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

В portable-дистрибутиве RBL сначала проверяется собственная минимальная toolchain в `toolchain/windows-x86_64/msys64/ucrt64/`. Это не полный MSYS2: внутри только GCC driver, GNU assembler, linker, нужные startup/static libraries и рекурсивно проверенные DLL. Если встроенного набора нет, драйвер проверяет `RBL_MINGW`, стандартные каталоги MSYS2/MinGW и инструменты в `PATH`.

Инструменты для сборки самой RBL Studio (полный MSYS2/GCC C++ toolchain, Rust, CMake и desktop-разработка) не входят в проект и устанавливаются отдельно на машине разработчика; точные версии, ссылки, хэши и инструкции указаны в [`toolchain/README.md`](../../../toolchain/README.md) и [`toolchain/requirements.json`](../../../toolchain/requirements.json). Пользователь получает уже собранное приложение и не собирает IDE из исходников.

Это отдельно от backend-компонентов, нужных самому языку. В checkout версии 0.8.0 находится урезанный MinGW runtime bundle из 43 файлов (44 файла на диске вместе с manifest; около 21,9 МБ) для компиляции пользовательских RBL-программ; готовое Windows-приложение включает такой же backend. В нём есть GCC driver, assembler, linker и нужные runtime-файлы, но нет C frontend GCC и headers, поэтому он не может пересобрать IDE/bootstrap-компилятор. Backend намеренно оставлен, чтобы сборка RBL-программ работала на чистом ПК без внешней toolchain; удалять его можно только после замены, прошедшей acceptance установленного приложения без внешних инструментов.

Обычная сборка пользовательских программ на Windows не требует frontend C-компилятора: release-дистрибутив содержит RBL compiler и готовые Windows Runtime objects. Native pipeline собирает `.s` через GNU `as`, затем линкует объект программы и Runtime через GCC driver. Для RBL отключён GCC linker plugin (`-fno-use-linker-plugin`): RBL не создаёт LTO objects, поэтому LTO plugin и `lto-wrapper` не поставляются. Полная C toolchain (включая `cc1.exe` и headers) нужна только разработчику для пересборки bootstrap-компилятора/Runtime (`python rbl.py bootstrap`). Обычные команды не пересобирают готовые бинарники из-за времён файлов; при отсутствии обязательного артефакта потребуется developer toolchain.

Для developer rebuild явно укажите полный внешний MinGW-w64, чтобы он имел приоритет над встроенным runtime bundle:

```powershell
$env:RBL_MINGW = "C:\msys64\ucrt64\bin"
python rbl.py bootstrap
Remove-Item Env:RBL_MINGW
```

Чтобы собрать или проверить bundle из установленного MSYS2 UCRT64:

```powershell
python tools\package_windows_toolchain.py --source C:\msys64\ucrt64
python tools\package_windows_toolchain.py --check
```

Скрипт формирует SHA-256 manifest и завершает работу ошибкой при отсутствующем linker input или неразрешённой DLL. Встроенная поставка — около 21 MiB для проверенной UCRT64 сборки; фактический размер записан в manifest.

Bundle не становится частью Apache-2.0 лицензии RBL. Сверяйте точные версии пакетов и метаданные лицензий в [third-party notices](../../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [реестре corresponding source](../../../toolchain/windows-x86_64/SOURCE_CODE.md) и [аудите архивов исходников](../../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Собраны и проверены подписи 20 точных MSYS2 source archives: 9 для toolchain и 11 для wx/runtime dependencies. Проверены upstream-хеши, git commit CRT/winpthreads и хеш исходника default-manifest. Они собраны в локальный source-material bundle размером 331 187 540 байт: `build/source-material/RBL-0.8.0-Windows-Source-Materials-20261111-final-reviewed.tar.zst` (SHA-256 `2ca37857afbda2d34d67379b5c12a02ca10f07e12d6d8df2423e00addb708066`). Он не входит в runtime bundle или Git snapshot и не приложен к опубликованному pre-release. Способ доставки corresponding sources и юридическая проверка условий распространения остаются незавершёнными. Binutils обновлён до 2.47-4; linker формирует PE/COFF, readelf отсутствует, но BFD распознаёт ELF input vectors. Source-level review подтвердил достижимость `cache_bwrite` при обычной записи PE output; уязвимое условие чтения за границами буфера для стандартного RBL workflow остаётся неразрешённым upstream. Владелец принял остаточный риск только для поддерживаемого compiler-generated COFF workflow (без произвольного object input и `--gc-sections`), но это не исправление и не заявление о неприменимости. Этот inventory не является юридическим заключением.
У Rust worker отдельный inventory активных default-feature зависимостей на 28 crates и corresponding-source archive; обновлённые wx build scripts включают его в build payload, а installers проверяют SHA-256 и сохраняют архив в установленном приложении. См. [Rust worker notices](../../../rbl-ide/wx-ui/RUST_DEPENDENCY_NOTICES.md). Опциональный legacy-egui feature имеет отдельный dependency graph и не входит в поставляемый worker.

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
| Ошибка запуска IDE | Проверьте наличие wx GUI executable и соседнего `rbl-studio-worker.exe`. Пересоберите native frontend через `tools\build_wx_ide.ps1`; для backend-задач нужны Python 3.9+ и выбранный GNU toolchain; worker сначала ищет app-local interpreter в `runtime/python/`, затем `RBL_PYTHON`, затем системный Python. Legacy egui GUI — отдельный developer-only fallback (`RBL_STUDIO_ALLOW_LEGACY=1`). |

## Ограничения

Наличие MinGW-пути у драйвера не означает, что протестирована каждая комбинация Windows, CRT и версии компилятора. Benchmark с ручными Linux syscall-программами не запускается как native Windows benchmark; используйте WSL или Linux. См. [измерение производительности](benchmarking.md).
