# RBL 0.8.1 / 0.8.0 — заметки о релизе

Последняя проверка: 2026-11-17

## RBL 0.8.1 — подготовка релиза (ещё не опубликован)

RBL 0.8.1 — текущее дерево подготовки релиза. Опубликованный source snapshot
`v0.8.0-r2` остаётся неизменным; в рамках этой работы тег 0.8.1 и GitHub Release
не создавались.

### Добавлено

- Нативный first-run setup/installer wizard в wxWidgets IDE. Он проверяет
application root, worker, Python runtime и compiler toolchain, сохраняет
per-user настройки, поддерживает явный development path из source checkout и
может асинхронно запускать платформенный user-scope Install / Repair при наличии
полного installer payload. Wizard также можно повторно открыть из Settings.
- Нативный Linux build wxWidgets GUI + Rust worker и shell install path,
проверенные на Ubuntu 24.04 с системным wxWidgets 3.2.4
(`RBL_USE_SYSTEM_WX=1`). Для этого варианта GTK 3 и display stack остаются
системными зависимостями.
- Подготовка Flatpak payload с Linux app-local CPython; Windows runtime-файлы и
временный Python bytecode исключаются из staging payload. Локальный Flatpak
bundle собран и установлен; sandbox CLI-команды `version`, `doctor`, `check` и
`run` прошли.
- Восемь небольших программ RBL 0.8.x в `examples/v081/` и десять уроков на
  русском и английском: форматирование f-строк, managed strings/containers,
  диагностика, Runtime ABI, setup wizard, native IDE и Linux/Flatpak. Исходные
  двадцать beginner-уроков серии 0.7.2 повторно проверены компилятором 0.8.1.

### Изменено

- Модель GitHub release — tagged repository source tree. Отдельные ZIP/portable
archives, binary release assets и corresponding-source archives не являются
release deliverables или publication gates. Third-party notices, тексты лицензий,
provenance URLs и hashes остаются в репозитории; эта политика не отменяет
обязательств при распространении bundled third-party binaries.
- Windows DPI acceptance дополнена подтверждением пользователя, что реальный
mixed-DPI сценарий проверен успешно. В журнале отдельно записаны проверки
перемещения между мониторами, восстановления floating pane, миграции сохранённого
layout и восстановления окон из координат за пределами экрана.

### Проверки и явно указанные ограничения

- `python rbl.py test`: PASS; 63/63 direct ASM cases и 63/63 compiler
diagnostics. Windows CTest: 15/15. Linux CTest: 14/14. DPI static tests: 12/12.
- Flatpak payload tests: 6/6. Проверка документации: 103 Markdown-файла,
538 относительных ссылок, битых ссылок нет, иерархия заголовков EN/RU совпадает.
- Пользователь подтвердил успешное завершение двухчасового (7 200 секунд) Windows resource soak. Отмечен PASS по подтверждению пользователя; raw CSV/verdict и финальные хэши GUI/worker в этой рабочей сессии независимо не перепроверялись. Проверка на pristine VM не зафиксирована.
- Flatpak GUI/display/portal path и Flathub publication не объявляются принятыми.
  Нативная Linux GUI/install проверка ограничена Ubuntu 24.04.
- Binutils 2.47-4 остаётся отмеченным для CVE-2026-90801; документированная
область остаточного риска ограничена поддерживаемым compiler-generated COFF
workflow и не означает исправления upstream finding. Windows
`ReplaceFileW` destination-leaf race остаётся задокументированной задачей для
0.9. POSIX existing-target save работает fail-closed, если atomic exchange
недоступен.
- Third-party notices и provenance присутствуют, но эти release notes не являются
юридическим заключением или общим разрешением на распространение bundled binaries.

Подготовка учебных материалов, очистка и regression для 0.8.1 завершены. Двухчасовой soak отмечен PASS по подтверждению пользователя; pristine-VM и интерактивный Flatpak GUI/portal остаются непроверенными. Остаются git commit, push, tag и публикация GitHub Release.

## RBL 0.8.0 — полноценный релиз

RBL Studio 0.8.0 — полный релиз языка RBL, его нативного компилятора/runtime и
IDE на wxWidgets. Windows x86-64 payload поставляется как готовый к запуску
checkout: в корне проекта лежат GUI, Rust worker, app-local CPython и
assembler/linker runtime, необходимые для сборки и запуска программ на RBL.
Запускайте `RBLStudio.cmd` (GUI) или `rbl.cmd` (CLI) прямо из корня проекта;
отдельный архив для распространения не создаётся. Из тех же исходников
собирается Linux x86-64 payload.

**Статус публикации:** checkout 0.8.0 опубликован 2026-11-15 как **полный
релиз `v0.8.0-r2`**
(`https://github.com/Reiwa-111/RBL/releases/tag/v0.8.0-r2`). Бинарные assets не
прикреплены: распространяемый payload — это исходный checkout на этом теге.
Более ранняя запись GitHub `v0.8.0` от 2026-11-10 остаётся pre-release
placeholder без assets и всё ещё содержит повреждённый пример; её не двигали.
Открытые acceptance- и legal-условия перечислены в разделе
[Ограничения и условия публикации](#ограничения-и-условия-публикации).

## Состав поставки

Payload 0.8.0 — это корень репозитория (checkout). Это переносимая папка
приложения, а не база installer'а: внутри нет install manifest, uninstaller'а и
интеграции с registry/XDG. Отдельные ZIP/tar.zst архивы не создаются. Прежние
архивы (Windows 578 файлов / Linux 5 149 файлов) удалены 2026-11-15 при смене
модели поставки; их хэши сохранены в исторических audit- и verification-
материалах.

### Что входит в checkout

- `bin/win-x86_64/studio/rbl-studio-wx.exe` — нативная IDE на wxWidgets 3.2 +
  Scintilla вместе с runtime-DLL wxWidgets/MinGW.
- `bin/win-x86_64/studio/rbl-studio-worker.exe` — Rust backend компилятора и
  инструментов.
- App-local CPython 3.14.10 в `runtime/python`, урезанный до модулей, которые
  используют backend RBL и regression suite (~9 МБ; необязательные расширения
  TLS/SQLite/zstd/decimal/XML удалены).
- Компилятор, runtime и CLI RBL (`rbl`, `rbl.py`, `rbl.cmd`, `RBLStudio.cmd`,
  `RBLStudio.sh`, `RBLStudio.vbs`).
- Замыкание assembler/linker для сборки пользовательских программ: GNU `as`/`ld`
  из закреплённого MinGW-w64 UCRT64 bundle в Windows; встроенный Clang assembler
  и LLD в Linux.
- Сторонние notices в `THIRD_PARTY_NOTICES.txt` и лицензии компонентов в
  `licenses/`, `runtime/python/licenses/` и `toolchain/*/`.

### Быстрый старт в Windows

1. Откройте checkout репозитория; распаковывать архив не нужно.
2. Запустите `RBLStudio.cmd` из корня проекта (или
   `bin\win-x86_64\studio\rbl-studio-wx.exe` напрямую).
3. Launcher задаёт application root; GUI находит worker рядом со своим
   executable и app-local Python в `runtime/python`. Переменные `RBL_*`,
   системный Python, MSYS2 и компилятор не нужны.

### Быстрый старт в Linux

Готовый Linux-архив удалён 2026-11-15 при смене модели поставки. Соберите
нативный payload из тех же исходников через `tools/build_wx_ide.sh`, затем
запустите:

```bash
./RBLStudio.sh          # GUI
./rbl version           # CLI
```

Нативная Linux-сборка выполнена и проверена на Ubuntu 24.04. Текущий ELF floor —
GLIBC 2.39 / GLIBCXX 3.4.32 (см. `LINUX_ABI_AUDIT.txt`); для более старых
дистрибутивов нужна пересборка под старый sysroot. Нативный shell-пакет
использует host GTK 3/display stack. Flatpak-путь поставляет GTK через
`org.gnome.Platform` 51.

## Added

- Ограниченные спецификаторы форматирования f-строк: символ заполнения,
  выравнивание, минимальная ширина, точность чисел с плавающей точкой и явные
  типы преобразования целых, вещественных значений и строк. Грамматика и
  границы определены в [справочнике синтаксиса](reference/syntax.md).
- Диагностика с именем файла, строкой, столбцом, меткой этапа компилятора,
  фрагментом исходного кода и кареткой для лексического и синтаксического
  анализа, семантической проверки и генерации кода.
- Управляемое Runtime время жизни динамических строк, интегрированное с
  консервативным неподвижным mark-and-sweep collector для строк и контейнеров.
  Ограничения заимствования строк из нативного C и границы collector
  документированы явно.
- Дескриптор Runtime ABI и проверка совместимости. Версия ABI независима от
  версии языка/компилятора; текущая версия Runtime ABI — `1.2.0`.
- Готовый пакет IDE на wxWidgets с app-local Python backend и встроенным
  assembler/linker замыканием: установленные действия check/build/run/format
  работают без developer checkout и системного Python.

## Changed

- Компилятор разделён на специализированные C translation units с приватными
  интерфейсами модулей. Поиск переменных, функций и строковых констант
  использует hash-индексы, сохраняя детерминированный порядок исходных данных и
  поведение разрешения имён функций.
- Динамические строки стали объектами, которыми владеет Runtime. Программы на
  RBL не должны вручную освобождать payload управляемых строк; см.
  [модель выполнения](explanation/execution-model.md).
- Добавлены safe points для сборки мусора. Для быстрых целочисленных циклов
  вызовы safe point пропускаются внутри итераций только тогда, когда компилятор
  доказывает отсутствие аллокаций и изменения указательных корней.
- Система сборки отслеживает compiler modules, приватные заголовки, ABI-исходники
  и зависимость от версии; отдельные object-файлы поддерживают инкрементальную
  и параллельную сборку.
- C entry points общего container Runtime централизованы в `runtime/rbl_abi.h`;
  Linux/Windows bootstrap paths теперь строго проверяют отсутствующие и
  повторные прототипы.
- Для сравнения пяти реализаций добавлена корректно названная команда/Makefile
  target `bench5way`; `bench3way` сохранена как alias обратной совместимости.
- Основной launcher/install target RBL Studio переведён на C++20/wxWidgets 3.2 +
  Scintilla с отдельным процессом Rust `rbl-studio-worker`. Прежнее приложение
  Rust/`eframe`/egui сохранено как legacy reference/fallback и не входит в wx
  installer. IDE открывает текстовые документы разных расширений, сохраняя
  поддержку компиляции только для `.rbl`.
- Уточнены бюджеты памяти редактора, резервирование истории страниц и проверки
  границ Replace All; обход дерева проекта получил общий лимит сканирования.
- Одноразовое состояние разделено на `build/cache/` (собранные пользовательские
  программы и скачанные upstream sources) и `build/output/` (IDE, Cargo,
  bootstrap и упаковка). `rbl.py clean`/`make clean-cache` трогают только кэш;
  `make clean-build`/`make clean` — только выходы.

## Fixed

- Улучшено отслеживание позиций в UTF-8, для LF/CRLF/CR и фрагментов
  интерполяции f-строк: ошибки указывают на исходный файл, а не временный
  фрагмент.
- Ошибки выделения памяти collector стали транзакционными: неудачная сборка
  мусора сохраняет существующую кучу и допускает повторную попытку.
  Структуры аллокации проверяются детерминированными OOM-тестами.
- Согласовано форматирование f-строк в Linux и Windows, включая ширину Unicode
  code points и граничные случаи fill/alignment.
- Исправлены зависимости сборки: Linux rebuild script и обе платформенные
  toolchain-ветки включают ABI- и formatting-runtime objects.
- Исправлено SysV AMD64 stack alignment в местах вызова libc/helpers из Linux
  Runtime, включая error exits и общие arithmetic overflow paths; добавлены
  инструментальный regression harness и статическая проверка control-flow.
- Артефакты сборки изолированы по identity исходника и target backend: одинаковые
  имена файлов и одновременные WSL/native-сборки больше не используют общие
  outputs.
- Исправлен POSIX save-rollback gap, когда символическая ссылка могла подменить
  destination в последнем окне exchange. Вытесненная запись теперь
  идентифицируется без следования по ссылке; rollback выполняется только пока
  обе записи сохраняют проверенные identities. Windows orphan-temp cleanup теперь
  удаляет по открытому handle.

## Запись о проверке

Все результаты ниже перепроверены 2026-11-15 на текущем checkout.

- Проходят 63/63 direct-assembly regression теста RBL (`tests/run_tests.py`).
- Диагностический suite из 63 сценариев: 45 ошибок исходника, 8 корректных
  контрольных программ, 4 ожидаемые runtime errors и 6 тестов некорректных UTF-8
  последовательностей.
- Windows wx CTest: **12/12 passed**. Linux wx CTest: **11/11 passed**.
- Корень checkout собран из portable installer staging tree; install state,
  build-кэши и CMake build directories отсутствуют.
- Нативный запуск Windows из корня checkout с PATH только из System32 и без
  переменных `RBL_*`: `rbl.cmd version` → `RBL 0.8.0`, `rbl.cmd check` и
  `rbl.cmd run` проходят; `rbl-studio-wx.exe` открывает окно `RBL Studio` и
  запускает worker.
- Прежний нативный запуск Linux из удалённого `tar.zst` на ext4 под WSLg
  (историческое свидетельство): окно GUI живо, дочерний worker-процесс
  присутствует.
- Linux installed CLI/worker acceptance проходит в минимальном PATH только из
  coreutils и `bash`/`dirname` — без системных `python3`, `cc`, `gcc` и `make`.
  App-local CPython 3.14.10 выполняет `initialize`, `check`, `run`, worker и
  `clean`.
- Corresponding source в checkout: дерево Rust worker (`rbl-ide/worker`),
  исходники wxWidgets IDE (`rbl-ide/wx-ui`) и закреплённый Windows toolchain
  bundle с `SOURCE_CODE.md` и манифестом. Крупные upstream-архивы corresponding
  source (MSYS2 toolchain, CPython, python-build-standalone, wxWidgets) в
  checkout не входят; их точные source-локации и хэши записаны в
  `toolchain/windows-x86_64/SOURCE_CODE.md`. Публичный маршрут поставки source
  всё ещё требует одобрения владельца/юриста.

## Ограничения и условия публикации

Это измеренные ограничения payload, а не формулировки pre-release:

1. **Интерактивная приёмка IDE не завершена.** Полный ручной GUI checklist, live
   DPI 125–200% на нескольких мониторах, приёмка всех тем и 7 200-секундный
   resource soak не выполнены. Startup smoke и статические GUI-тесты проходят.
2. **Приёмка в pristine environment не завершена.** Пакеты проверены на build
   host и в изолированных каталогах, но не на свежей VM без developer caches.
3. **Юридическое согласование source delivery ожидается.** Архивы corresponding
   source включены и проверены по хэшам, но текст публичного раскрытия и
   written offer для release page не получили одобрения владельца/юриста.
4. **Базовая линия Linux.** Нативный Linux binary требует GLIBC 2.39 / GLIBCXX
   3.4.32 и host GTK 3. Это не универсальный Linux binary.
5. **Windows save race.** У `ReplaceFileW` нет destination-identity
   compare-and-swap; оставшаяся path-based leaf race документирована для 0.9.
   Поддерживаемый workflow принимает остаточный риск CVE-2026-90801 только для
   compiler-generated COFF objects, без произвольного object input и без
   `--gc-sections`.
6. **История тега.** Публичный тег `v0.8.0` всё ещё содержит повреждённый
   `examples/utilities/source-WUDMB5.rbl`. Рабочее дерево и оба релизных архива
   содержат исправленный файл размером 1 093 байта. Финальный релиз должен быть
   выпущен из нового commit/tag; существующий тег нельзя двигать задним числом.

Во время подготовки этих заметок публикация, upload, перемещение тега и смена
release-статуса не выполнялись.

## Правила ведения заметок

- Записывайте только реализованное пользовательское поведение в категориях
  **Added**, **Changed** или **Fixed**.
- Не описывайте идеи и задачи roadmap как доступные функции.
- Добавляйте дату публикации и финальные ссылки на assets только после
  фактической публикации релиза.
