# last verified: v0.8.0 | 2026.11.10 | IDE ENGINEERING AUDIT

# RBL Studio 0.8.0 — инженерный аудит и статус готовности

Дата фиксации: 2026-11-10. Статус документа: промежуточный инженерный отчёт, не разрешение на публикацию.

## Итоговый статус

**IDE 0.8.0: NOT RELEASE-READY / UNRELEASED.** Основной launcher/install path использует C++20/wxWidgets 3.2 + Scintilla и отдельный Rust worker. Windows SaveDocument byte-roundtrip acceptance — 27/27 PASS на текущем GUI candidate. Предварительный resource run на 480 секунд прошёл resource/UI thresholds, но использовал старый harness и не проверял graceful shutdown. Вторая попытка 7 200 секунд завершилась досрочно после task #45 без CSV. Следующий запуск на 480 секунд с усиленным harness завершился с кодом 1 после начального dispatch Run Tests; сохранённый log содержит только startup output и не содержит CSV/verdict. PASS для двухчасового soak текущего RC не заявляется. Windows registered install/upgrade/uninstall acceptance проходит в изолированном HKCU GUID subtree. DPI static checks проходят 12/12, но live 125-200% multi-monitor и интерактивная Linux GUI-приёмка остаются непроверенными. MSYS2 corresponding-source/legal review, пересборка RC с Rust source payload и Binutils CVE-2026-90801 остаются release blockers.

Этот документ намеренно отделяет проверенное от непроверенного. Успешная компиляция, unit-тесты и `--self-test` не доказывают корректность реального GUI-сценария или соблюдение общего лимита памяти под нагрузкой.

## Архитектура

- Основной UI реализован на C++20 + wxWidgets 3.2 + `wxStyledTextCtrl`/Scintilla в `native/wx-ui/`; отдельный Rust `rbl-studio-worker` запускает backend-задачи.
- Прежний Rust + `eframe/egui` UI сохранён в `src/app.rs` и `src/editor.rs` как rollback/reference и выбирается только при `RBL_STUDIO_ALLOW_LEGACY=1`.
- Python/Tkinter GUI удалён. `tools/rbltool.py` остаётся backend-драйвером компилятора и не отвечает за отображение интерфейса.
- В Cargo dependency scan ранее не обнаруживались Wry/WebView/CEF/Chromium/Electron/WebKit engine packages. Внешние ссылки могут открываться системным браузером; это не встраивает браузерный движок.
- IDE поддерживает обычные текстовые файлы независимо от расширения. Компиляторные операции ограничены `.rbl` и дополнительно проверяются на границе backend.
- Проектное дерево, поиск, вывод процессов, вкладки и история редактора имеют программные ограничения. Эти ограничения не являются жёсткой квотой allocator/OS для всего процесса.

### Начальный snapshot перехода на нативный интерфейс (2026-11-09; исторический)

На этом начальном snapshot wx frontend существовал параллельно Cargo/egui приложению. Сейчас он является основным launcher/install target; прежний Rust/egui UI сохранён для отката и сравнения. wx frontend предоставляет Run/Build/Check/ASM/Tests/Benchmarks/Doctor, потоковый stdin/EOF/cancel, вкладки и проектное дерево, UTF-8/BOM и UTF-16 LE/BE, поиск/замену, Find in Files, список Problems с переходом к позиции, настройки редактора, командную палитру и quick reference. Обход поиска ограничен по числу entries/bytes/results; дерево не следует по symlink. Worker использует ограниченный NDJSON протокол и повторно использует `src/process.rs`.

Начальная проверка на тот момент: Windows UCRT64 и Linux/WSL CMake Release сборки; worker unit tests (20 Windows / 21 Linux), Clippy `-D warnings`, интерактивный smoke `unit_converter.rbl` с двумя `input()` на обеих платформах. Один Windows idle sample: GUI ~30.5 MiB Working Set + worker ~9.8 MiB. Это историческая базовая точка, а не текущая приёмка; актуальные проверки и остаточные gaps приведены ниже. Подробности и команды сборки находятся в [`native/wx-ui/README.md`](../../../native/wx-ui/README.md).

### Архитектурный обзор legacy egui frontend (исторический)

Компоновка следует модели egui: верхние/нижние панели создаются до боковой панели, а `CentralPanel` — последней. Project использует ограниченную по ширине изменяемую `SidePanel`; docking framework не оправдан текущей схемой «один Project / один Editor / одна Output-панель» и добавил бы зависимости и состояние, не устраняя сами найденные дефекты.

Для редактора сопоставлены [DisplayMap в Zed](https://zed.dev/blog/zed-decoded-rope-sumtree), централизующий folding, wrapping и другие преобразования отображения, и [архитектура Helix](https://github.com/helix-editor/helix/blob/master/docs/architecture.md), разделяющая rope-backed core, selections/transactions/history и view layer. Также изучен [Lapce](https://docs.lapce.dev/) как пример нативного Rust/GPU-редактора. Эти решения показывают, почему полноценное редактируемое сворачивание требует явной карты координат source-to-display, а не временной строки-проекции. RBL Studio пока сохраняет существующий ограниченный текстовый буфер и документную Undo-историю; полная миграция на Rope/DisplayMap — отдельная архитектурная задача, а не безопасный косметический патч. Браузерный движок и тяжёлая editor-зависимость не добавлялись.

## Базовая реализация legacy egui frontend — исторические сведения

| Область | Текущее состояние по исходникам/тестам |
|---|---|
| Текстовые документы и вкладки | Реализованы; есть тесты кодировок, BOM, переводов строк и бюджетов |
| UTF-8 / UTF-16 LE/BE | Реализованы; ANSI и бинарные данные не декодируются автоматически |
| Большие документы | Виртуализированные окна редактора; лимит файла 16 MiB |
| Undo/Redo и Replace All | Документная история и отдельный swap для Replace All; тесты границ UTF-8 и истории |
| `.rbl` compiler gating | Реализован и покрыт тестами |
| Процессы | Фоновый запуск, ограниченный потоковый вывод, отмена и интерактивный stdin для Run; Windows дочерний процесс создаётся приостановленным и включается в Job Object до выполнения первой инструкции |
| Project tree/search | Ограниченный фоновый обход, объединение повторных обновлений, отображение ошибок файловой системы и поиск по обнаруженному набору файлов; UX больших реальных деревьев ещё требует приёмки |
| Геометрия панелей | Project изменяем пользователем в пределах 190–360 egui points (245 по умолчанию); Output ограничен 45% и расположен над нижней строкой состояния; GUI-приёмка всё ещё нужна |
| Ввод и прокрутка | Enter передаётся как нативное Key event egui; editor обёрнут в двунаправленный ScrollArea и заполняет viewport; same-frame paste при первом фокусе проходит через лимиты |
| Run stdin | Отдельное поле STDIN с автофокусом; строки передаются в stdin процесса, `print()`/`input(prompt)` сбрасывают stdout, EOF закрывает stdin дочернего процесса; интеграционный тест запускает unit converter с двумя вводами |
| Gutter и структура кода | Gutter использует фон редактора и разделитель; направляющие RBL и folding используют общие найденные пары блочных скобок; обычный текст использует ограниченные участки отступов |
| Взаимодействия редактора | Текстовые поля диалогов сохраняют ввод; Save As предотвращает дублирующих владельцев открытого файла; Replace All восстанавливает пересчитанную позицию курсора |
| Настройки редактора | Сохраняемые настройки подсветки синтаксиса и soft wrap, плюс существующие тема/шрифт/табуляция/номера строк |
| Native GUI | Реализован; unit/self-test не заменяет ручную приёмку GUI |

## Ресурсы: исторические измерения egui (не относятся к wx)

Точные цифры, hashes, методика и ограничения находятся в [отчёте ресурсов](../../../benchmarks/IDE_RESOURCE_USAGE.md). Краткая сводка существующих принятых измерений:

| Платформа / сценарий | Память IDE | Дополнительные метрики | Статус |
|---|---:|---|---|
| Windows idle | Working Set avg 131.7 / peak 131.9 MiB | Private Bytes avg 196.3 MiB; CPU 0.00% при секундном sampling | Измерено |
| Windows, один 16 MiB TXT | Working Set avg 172.5 / peak 172.8 MiB | Private Bytes avg 235.7 MiB | Измерено |
| Windows, один 16 MiB RBL | Working Set avg 172.9 / peak 173.2 MiB | Private Bytes avg 236.2 MiB | Измерено |
| WSL idle | RSS avg/peak 131.3 MiB | VmData avg 274.5 MiB | Измерено |
| WSL, один 16 MiB TXT | RSS avg/peak 176.8 MiB | VmData avg 322.4 MiB | Измерено |
| WSL, один 16 MiB RBL | RSS avg/peak 174.6 MiB | VmData avg 321.5 MiB | Измерено |
| 32 вкладки (32 × 1 MiB документов) | Working Set peak 194.7 MiB | Private Bytes IDE + descendants peak 254.6 MiB; один процесс | Измерено (только память) |
| Длительное редактирование, Find/Replace/Undo, активная компиляция + дети | — | Нет принятого интерактивного/process-tree профиля | Не измерено |

Секундный sampling может пропустить кратковременные пики. Windows Working Set, Private Bytes, Linux RSS, VmData и PSS — разные метрики. Не складывайте RSS каждого процесса как точный физический footprint при наличии shared pages; для Linux process-tree сравнения предпочтителен PSS, если доступен. Для Windows нужно явно фиксировать IDE и descendants раздельно, а затем суммарную оценку дерева процессов.

Эти цифры относятся только к прежнему egui frontend и не являются доказательством
для основного wx frontend. Нельзя использовать 64 MiB бюджета редакторских буферов
как доказательство общего ограничения; приёмка wx описана отдельно ниже.

## Независимые аудиты

Независимые read-only аудиты охватили layout/порядок панелей, редактор/folding/scrolling и lifecycle процессов/stdin. По итогам исправлен порядок нижних панелей (строка состояния теперь у нижней границы окна), Project стал ограниченно изменяемым по ширине, ID прокрутки редактора привязан к странице документа, исправлен расчёт направляющих и добавлена явная команда EOF для stdin. Новый process-level тест запускает `examples/utilities/unit_converter.rbl`, дожидается обоих flushed prompts, отправляет `1` и `10` и проверяет форматированный результат конвертации. EOF проверяется отдельным тестом дочернего процесса. Ранее подтверждённые проблемы с перехватом горячих клавиш, дублирующим Save As, позицией курсора после Replace All, фоновым обходом/ограничениями поиска, ошибками файловой системы, статусом ошибок чтения потоков и порядком запуска Windows Job Object также остаются покрыты regression-тестами.

Известное ограничение редактора: folding по-прежнему использует ограниченный лексический сканер, а не AST компилятора. Пока свёрнут хотя бы один блок, проекция read-only; щелчок в область кода раскрывает блоки текущей страницы перед редактированием. Это защищает исходные координаты от повреждения, но пока не эквивалентно редактору с source-aware display map. Сообщённая полоса около 30 px количественно согласуется с зарезервированным 18-point RBL gutter для маркеров folding при отключённых номерах строк; точное положение на экране всё ещё требует GUI-подтверждения.

При интеграции каждый finding должен содержать файл и строки, воспроизводимое доказательство, severity, минимальное исправление и результат повторного теста. Предположения и UX-пожелания не должны маркироваться как подтверждённые дефекты.

## Проверки и критерии завершения

Для release readiness требуются:

1. Завершённые независимые аудиты и исправление подтверждённых blocker/high findings.
2. Проверка текстовых форматов: неизвестные расширения, UTF-8/UTF-16, BOM, LF/CRLF/CR, invalid encoding, NUL/binary rejection, Save/Save As и `.rbl`-only commands.
3. Проверенная интерактивная GUI автоматизация (или ручной протокол) для tabs, edit, find/replace, undo/redo, save, process output и cancellation.
4. Ресурсная матрица: idle, обычный документ, 16 MiB, 32 tabs, sustained multi-document editing, mass Replace/Undo, active compiler/tests; IDE, descendants и aggregate memory; average, peak, sample interval, duration, repetitions и CPU.
5. Полная Windows GNU и Linux/WSL проверка после последнего изменения: fmt, tests, Clippy `-D warnings`, release build, version/self-test, launcher/bootstrap без бинарника, runtime dependencies и реальный GUI startup.
6. `git diff --check`, документационные ссылки, отсутствие browser engines и устаревших ссылок на удалённый Python GUI.

До выполнения критериев статус остаётся **NOT RELEASE-READY**. Commit, push, tag и публикация требуют отдельного решения владельца проекта.

## Связанные документы

- [Руководство RBL Studio](../how-to/ide.md)
- [Измерения памяти и CPU](../../../benchmarks/IDE_RESOURCE_USAGE.md)
- [Release notes](../release-notes.md)
- [Manifest](../../../RELEASE_MANIFEST.txt)

## Native wxWidgets IDE — основной frontend (с 2026-11-09)

Этот раздел относится к новой реализации `native/wx-ui/` на wxWidgets 3.2 и
`wxStyledTextCtrl`/Scintilla с Rust worker. Предыдущие таблицы ресурсов и
интерактивные проверки выше относятся к старому egui frontend и не являются
измерениями или доказательством готовности wx-кандидата.

Подтверждено последней проверкой:

- Windows UCRT64/MinGW и Linux/WSL CMake builds прошли.
- Windows standalone startup прошёл с PATH, содержащим только каталоги Windows;
  соседний Rust worker запустился и подключился. Windows worker smoke прошёл.
- Linux/WSL worker: 23/23 теста, Clippy `-D warnings`, `cargo fmt -- --check`,
  stdin/EOF, output backpressure и shutdown-order integration прошли.
- Windows GNU worker: 22/22 теста, Clippy `-D warnings`, fmt, stdin/EOF,
  backpressure и shutdown-order integration прошли.
- Полный Windows `python rbl.py test` прошёл: 61 direct ASM regression,
  63 diagnostics, Runtime ABI, SysV call alignment, GC pressure, stdin и
  streaming проверки.
- В GUI проверено восстановление после некорректного AUI perspective,
  отображение Project/editor/Output, создание 32 пустых вкладок и сообщение
  `Tab limit reached (32)` при дальнейших запросах.
- В одном post-load измерении с раскрытым project tree GUI использовал около
  86.9 MiB Working Set, worker — около 10.0 MiB. Активного compiler child не
  было; это не peak-профиль дерева процессов.

Windows Rust worker suite теперь отдельно прошёл через GNU toolchain (22/22);
прежняя проблема relink под MSVC-профилем является исторической. Остаточные
пункты wx-кандидата: визуальные DPI-проверки выше 100%, многочасовой leak test,
непрерывный compiler/search stress, большие diagnostics, filesystem
permission/error edge cases и финальное cross-platform check-to-replace TOCTOU
окно. Бюджет 512 MiB прошёл измеренные нагрузки, включая текущий 200.5 s sample,
10 000 search results и directory-heavy tree expansion, но не является жёсткой
allocator quota или доказательством для любого workload. wx frontend теперь
выбирается основными repository launchers и user-scope installers. Это не
означает завершения release acceptance. Commit, push, tag и release не выполнялись.

Последний visual pass добавил компактный toolbar с группами File/Run/Tools,
иконки дерева проекта и Problems/Console/Search, отдельную RBL-иконку,
собственный значок приложения и нарисованные glyphs папок/файлов, согласованную
тёмную палитру, активные вкладки, статус проекта/задачи/позиции курсора и
автоматическое восстановление старого AUI layout без нового toolbar.

Для референса непосредственно на Windows был запущен PyCharm Community Edition
2024.3 с этим репозиторием; снимок окна сохранён как
`build/wx-ui-win/reference-pycharm.png`. Новое окно wx IDE снято и просмотрено
(`build/wx-ui-win/ide-final-visual.png`). После первого скриншота исправлены
светлая полоса toolbar, цвет текста консоли и отображение тире в stdin hint.

Проведена реальная GUI-проверка: через Open открыт
`examples/utilities/unit_converter.rbl`, через toolbar запущен Run, в GUI stdin
отправлены `1` и `10`; консоль показала `10.000 km = 6.214 mi`, процесс завершился
с кодом 0, статус стал Finished (`build/wx-ui-win/ide-run-complete.png`). Через
GUI также вызван Check; для корректной программы Problems остался пустым
(`build/wx-ui-win/ide-check-pass.png`). Windows и Linux builds после изменений
темы и иконок прошли.

## Дополнительная проверка wx frontend — 2026-11-10

wxWidgets frontend является основным launcher/install path; Cargo/egui frontend
и его исходники не удалялись и остаются явным legacy fallback. Последние
изменения wx-кода:

- Перед `SetText` режим новых переводов строк Scintilla выбирается по первому
  найденному CRLF/CR/LF исходного файла. Обработчик автоотступа также принимает
  CR-only newline. Windows UCRT64 и WSL CMake builds прошли после этих двух
  изменений. Точная побайтовая GUI-приёмка round-trip ещё не выполнена; нельзя
  считать все encoding/EOL случаи прошедшими.
- При закрытии IDE во время Find in Files теперь запрашивается отмена, а
  окончательное закрытие откладывается до обработки search completion event.
  Это убирает обычный сценарий блокировки окна на unbounded join в destructor.
  Реальный GUI-тест закрытия во время поиска ещё нужен; системный файловый
  вызов, заблокированный ОС, всё ещё может задержать завершение.
- Windows `python rbl.py test` завершился с кодом 0: 61 direct ASM tests, 63
  diagnostics tests, Runtime ABI/call alignment, GC/string I/O pressure,
  streaming и toolchain/build-cache checks. Два developer-only native C probes
  пропущены, поскольку в минимальном Windows bundle отсутствуют GCC
  `cc1`/headers.
- Windows corpus check завершился с кодом 0 (121 candidate, 114 positive
  assembled, 7 expected rejects). Documentation checker завершился с кодом 0
  (66 Markdown, 474 links, без missing targets/fragments). `git diff --check`
  также завершился с кодом 0.
- WSL пересобрал wx CMake target после close-during-search patch; после
  последнего worker stream-reader fix прошли `cargo fmt -- --check`, 23/23
  worker tests, Clippy `-D warnings`, stdin/EOF smoke и backpressure integration.
- Windows UCRT64 и Ubuntu 24.04/WSL wx CMake targets пересобраны после
  последнего Save revalidation patch.
- `tools/build_wx_ide.ps1` теперь закрепляет поиск GCC assembler/linker за
  signature-verified Binutils 2.47-4 из репозитория через `COMPILER_PATH` и
  завершается с ошибкой, если `gcc -print-prog-name` выбирает другой инструмент.
  Для каждого build directory создаётся отдельный Cargo target directory, чтобы
  worker действительно перелинковывался с этой настройкой. GCC compile/link smoke прошёл.
- Windows GNU worker build и 22/22 unit tests прошли; Windows stdin/EOF smoke
  и backpressure integration также прошли на пересобранном worker.

Историческая попытка автоматизации Save/Save As через системный файловый диалог
была inconclusive и не засчитывалась. Она superseded последующим тестом ниже;
её статус не описывает текущую приёмку.

Позднее отдельный реальный GUI-тест `python tests/wx_gui_file_roundtrip.py`
прошёл 21/21. Он проверил Save и Save As для новых и существующих документов;
точные байты UTF-8 LF/CRLF/CR, UTF-8 BOM, UTF-16 LE/BE, смешанных EOL,
Cyrillic/non-BMP текста и Cyrillic filenames; сохранение изменённого буфера;
решения для external modification/deletion; dirty tab/window close Cancel/No/Yes;
и отклонение обоих malformed surrogate fixtures без изменения исходных байтов.

Независимый read-only аудит wx frontend (worker-19) дополнительно выявил:

- MEDIUM (mitigated, not eliminated): Save повторно хэширует destination после
  flush/close временного файла и повторно спрашивает подтверждение, если файл
  изменился после первой проверки. POSIX Save закрепляет родительский каталог и
  использует descriptor-relative operations на протяжении транзакции. На Linux
  с RENAME_EXCHANGE временный файл атомарно меняется местами с destination;
  displaced identity и bytes проверяются, при несовпадении выполняется обратный
  exchange. На DrvFS и других filesystems без exchange сохранение существующего
  target завершается fail-closed с operation_not_supported; unconditional
  renameat fallback удалён. Первое сохранение использует exclusive linkat.
  DrvFS может отклонять relative operations через pinned directory FD после
  rename родительского каталога; такие операции fail-closed. Windows удерживает
  no-delete handles для parent chain, но destination leaf может измениться до
  path-based ReplaceFileW. Backup и identity checks сохраняют displaced object
  при несовпадении, но не дают compare-and-swap. Recovery artifacts повторно
  открываются с reparse-point handles, проверяются по file ID и очищаются по
  handle, а не через повторный pathname lookup.
- POSIX metadata: `fchown` сохраняет owner/group, `fchmod` — mode bits, включая
  special bits. Linux копирует access ACL и поддерживаемые xattrs; унаследованная
  destination-only access ACL удаляется. `security.*`/`trusted.*` намеренно не
  копируются и вызывают warning; на non-Linux POSIX ACL/xattr preservation
  недоступно и также показывается warning. Windows `ReplaceFileW` по документам
  сохраняет существующую DACL и named streams, но owner/group и SACL не проверены.
- Атомарная замена одного имени multiply hard-linked файла разрывает набор
  hardlinks: новые байты получает только выбранный pathname, остальные имена
  сохраняют прежние байты. Save теперь показывает предупреждение с default No.
- Временный файл создаётся эксклюзивно (`CREATE_NEW` на Windows, `O_EXCL` на
  POSIX), а не старым неэксклюзивным `ofstream`. POSIX open использует
  `O_NONBLOCK`, чтобы raced FIFO не мог заблокировать GUI до проверки типа.
- POSIX Save синхронизирует тот же закреплённый directory FD до replacement
  (чтобы recovery links стали durable), после replacement до cleanup и ещё раз
  после cleanup. Ошибка первого fsync отменяет сохранение; ошибка post-replace
  sync оставляет recovery links. Остаётся только финальная гонка конечного leaf.
- Учёт undo budget суммирует длины edit events, а не измеряет реальное
  выделение памяти Scintilla для истории. Порог очищает историю консервативно,
  но не является точным лимитом retained allocation.
- POSIX Find in Files работает от pinned directory FD через `fdopendir`/`readdir`,
  `fstatat(AT_SYMLINK_NOFOLLOW)` и `openat`; identity файла переносится в search
  result и проверяется повторно при открытии. Security tests — 4/4. POSIX Project
  Tree использует один shared root FD, relative component walks и expected
  identities (12/12); Save dirfd tests — 4/4. POSIX metadata harness прошёл все
  16 именованных сценариев.
- Diagnostics разбираются только из worker events с `stderr=true`. При выходе
  worker без `finished` уже полученный diagnostic stream финализируется; GUI
  acceptance на 10 000 строк и аварийный выход ожидает запуска на новой Windows-сборке.

Изменение search-close снижает риск блокировки при обычном закрытии окна, но
не доказывает отмену блокирующего системного filesystem read.

Последний независимый read-only аудит worker/backend (worker-17) признал
согласованными по исходнику high-water pause/resume очереди вывода, bounded
writer batching, немедленный flush control events, очистку snapshot и порядок
Finished. Игнорирование ошибки отправки последнего неполного UTF-8 фрагмента
исправлено в `src/process.rs` и покрыто тестом
`output_reader_reports_disconnected_receiver_for_final_utf8_fragment`.
Текущие WSL worker tests: 23/23; Windows GNU worker tests: 22/22 (один
platform-specific тест отсутствует). Windows и WSL stdin/EOF smoke и
backpressure integration на 6.1 MB вывода прошли.

Оставшиеся backend lifecycle риски по этому аудиту:

- MEDIUM Unix: при аварийном panic/crash/SIGKILL worker обычная очистка process
  group не выполняется, и прямой дочерний процесс может остаться orphan. Нет
  Linux parent-death signal или process-group RAII guard.
- LOW/MEDIUM Unix: потомок, вызвавший `setsid()` и сохранивший stdout/stderr,
  может выйти из исходной process group. Join reader threads тогда способен
  зависнуть и задержать `Finished`/shutdown.
- LOW protocol semantics: повторный `stdin_eof` и корректный `cancel` не имеют
  индивидуального acknowledgement; это должно оставаться явно fire-and-forget
  поведением протокола.

Первый отчёт worker-20 описывал устаревший snapshot coordinator. Повторная
проверка актуального src/bin/rbl-studio-worker.rs и свежая Linux-сборка подтвердили,
что обычный shutdown дренирует pending output и отправляет bye через тот же FIFO
channel. Shutdown-order test прошёл: 385 output events, Finished перед bye, exit 0.

Второй проход выявил отдельный P2 edge с потерей control events: process-event
intake останавливался на output high-water mark, но protocol-request intake
продолжался и мог занять восемь зарезервированных слотов. Исправлено: coordinator
применяет одинаковый high-water gate к request_rx и event_rx, создавая bounded
backpressure на stdin до расходования резерва. Добавлен unit test; все 27 текущих
worker unit tests проходят. Новый integration test с 10 000 protocol frames и
приостановленным stdout сохранил все protocol_error responses, отправил bye
последним и завершился с exit 0.

Если parent навсегда прекращает читать stdout, доставка всё ещё не гарантируется:
после ограниченных 100 ms send и 250 ms ожидания writer использует описанный
abandonment path. Это bounded transport-failure policy, а не обычная FIFO
ошибка порядка. Остальные backend caveats: очистка Unix process group при
аварийном завершении worker, потомки с setsid(), fire-and-forget семантика
stdin_eof/cancel и очень узкая PGID reuse race после reap.

Измерения wx вынесены отдельно в
[`benchmarks/IDE_RESOURCE_USAGE.md`](../../../benchmarks/IDE_RESOURCE_USAGE.md).
Ранний запуск 200.4 s теперь исторический. Позднее выполнен 319.5 s run на GUI
hash `3735FC7C08608E464DD0313C586BE82E7C39F0625AA6826A66C1E2E61F501AE2`:
32 документа / 64 MiB, Scintilla append/undo/redo, реальный Tests task и закрытие
вкладок. На 313 process-tree samples peak Working Set / Private Bytes дерева
составили 257.2 / 191.1 MiB; GUI-only peaks — 192.2 / 169.9 MiB. Итог после
закрытия вкладок — 57.6 / 17.7 MiB. Измеренный сценарий остался ниже 512 MiB,
но дочерние процессы Tests были активны только небольшую часть времени; это не
непрерывный compiler stress и не worst-case allocator bound.

Последние GUI/worker hashes:
`F84AEE828DD9E9F5A782C46623BF6B32E21FDBF06CA9D88FE763DA6F7E5665CB` и
`05B144E3AF0FE31D2D7E9696DED305F0AA9E1B7443D6BA642F84EDDE504790AE`. На них
выполнен sustained sample 319.9 s / 313 samples: peak Working Set / Private
Bytes дерева процессов — 263.0 / 192.4 MiB, mean Working Set 205.5 MiB, после
закрытия вкладок — 57.9 / 16.2 MiB.

Отдельный project-tree stress раскрыл 34 × 1 500 файлов до ровно 50 000 узлов,
не превысив глобальный предел. Peak памяти дерева процессов составил 276.3
MiB RSS / 224.7 MiB USS. Отдельная проверка каталога из 1 501 элемента подтвердила
лимит 1 500 элементов плюс marker усечения.

Миграция числовых настроек из legacy `settings.json` больше не использует
исключения `std::stof`/`std::stoi` для чрезмерно больших значений: integer
prefix разбирается через `std::from_chars`, а out-of-range значения оставляют
безопасные defaults. GUI-тест в изолированном профиле прошёл 4/4 для корректных,
слишком больших, отрицательных значений и malformed JSON. Второй запуск GUI
подтвердил загрузку сохранённых значений в настоящем Editor Settings dialog.
Переменная `RBL_STUDIO_CONFIG_DIR` задаёт отдельный конфигурационный каталог для
portable deployment и изолированных тестов.

### GUI-приёмка — дополнительный прогон 2026-11-10

- Save/Save As и файловые round-trip: 21/21 PASS, включая UTF-8/BOM/UTF-16,
  LF/CRLF/CR/mixed EOL, external modification/deletion, dirty-close и malformed
  surrogate rejection.
- Граница размера файла: 2/2 PASS; ровно 16 MiB открывается, 16 MiB + 1 byte
  отклоняется настоящим warning dialog без закрытия IDE.
- Навигация диагностики: сохранённый файл, unsaved snapshot и безопасная
  активация устаревшей диагностики после закрытия вкладки прошли.
- Find in Files: позиции CR-only/CRLF/LF прошли 3/3; отмена, повторный запуск и
  закрытие IDE во время поиска прошли GUI-проверку. Stress на 12 000 hits
  ограничен 10 000 результатами и выводится batch-ами по 50 строк. Последний
  изолированный прогон: 3.72 s, peak 76.9 MiB RSS / 29.2 MiB USS, UI ping 92.0 ms.
  В комбинированном batch под параллельной нагрузкой однажды сработал timeout
  1 s; изолированный повтор прошёл.
- GUI Stop отключает stdin/Send/EOF сразу после отмены; два последовательных Run
  отменены с exit 130. Отдельный GUI-тест подтвердил FIFO: строка, отправленная
  непосредственно перед EOF, дошла до дочернего процесса до закрытия stdin.
- Worker shutdown: при искусственно заполненном stdout pipe принятые события
  были доставлены, `finished` пришёл раньше `bye` на Windows и WSL.
- Восстановление workspace запускает ровно один worker. GUI-команды Check,
  Build, Run и Tests завершились статусом `Finished` (4/4). Отдельный process-
  level тест подтвердил запуск реального `python.exe .../tests/run_tests.py`
  дочернего процесса командой Tests (ID 6020) и успешный `Finished`.
- Project tree: подтверждены предел 1 500 элементов на каталог и 50 000 узлов
  глобально. Find in Files ограничен 100 000 записями, 128 MiB, 2 MiB на файл,
  10 000 результатов и глубиной 32.
- Directory-heavy tree stress: 35 × 1 500 подкаталогов достигли 49 999 узлов;
  lazy `...` placeholders учитываются как отдельные узлы. Peak RSS — 88.5 MiB.
- Windows `python rbl.py test`: exit 0 (61 direct ASM, 63 diagnostics и проверки
  ABI, GC, I/O, streaming, manifest/cache); corpus: 121 candidate, 114 positive,
  7 expected negative; документация: 66 Markdown / 474 links. WSL wx build,
  worker 23/23, fmt, Clippy, stdin/EOF и backpressure прошли.

Новый worker audit обнаружил и помог закрыть shutdown ordering bug: раньше
worker мог выйти до отправки `pending_output` или пропустить `finished` перед
`bye`. Остаются ограничения: 2-секундный backpressure failsafe намеренно
прерывает worker при длительно заблокированном stdout; 8 control slots — конечный
резерв, не безлимитный при flood запросов; на Unix остаётся очень узкая гонка
повторного использования PGID после reap.

В follow-up от 2026-11-10 закрыты несколько точечных acceptance items:

- Save/Save As round-trip: 27/27, включая UTF-8/BOM/UTF-16, LF/CRLF/CR,
  отказ и повтор после read-only, внешнее удаление, подтверждение перезаписи и
  повторные внешние записи во время модального conflict prompt, а также
  предупреждение/отказ/подтверждение для hard-linked destination.
- POSIX metadata harness: PASS для permission bits, owner/group, user xattrs,
  копирования явной ACL, удаления унаследованной default ACL, hard-link count,
  отказа при symlink, FIFO без блокировки, статической подмены пути и
  same-directory backup/recovery hard-links. Это не доказывает полностью
  атомарный compare-and-swap.
- Diagnostics: cap 10 000 строк и сортировка PASS; в synthetic scale test
  измерены UI ping 14 мс и process-tree RSS 66,4 MiB. Oversized-line discard,
  навигация и stale invalidation при редактировании до первого diagnostic PASS.
- Find in Files: EOL 3/3, обработка недоступных каталогов, ограничение глубины,
  отказ следовать directory symlink/junction и external-junction containment
  PASS. Создание file symlink — INCONCLUSIVE из-за ограничений host policy.
- Актуальные Windows GNU Release и Linux/WSL Release сборки завершились с exit 0.

Восстановленный `wx-resource-soak-20261110-070042.csv` содержит 313 samples за
примерно 321 секунду. Peak process-tree Working Set / Private Bytes составили
261,8 / 192,2 MiB; UI ping p95/p99/max — 0,1 / 0,3 / 106,9 мс. Для этого sample
прошли пороги 512 MiB и 500 мс, но он не длился запрошенные два часа. Acceptance
script после этого усилен: теперь он должен завершаться ошибкой при раннем
выходе или недостаточной длительности и собирать process handles вместе с
GDI/USER objects. Позднее усиленная попытка 7 200 секунд завершилась досрочно после task #45 без CSV.

Smoke-test исправленного script на 60 секунд (`wx-resource-soak-20261110-080424.csv`)
завершился с 80 samples за 60,5 секунды. Peak Working Set / Private Bytes —
265,2 / 193,4 MiB; peak process-tree handles — 1 218; peak GDI/USER objects —
355 / 110; UI ping p95/p99/max — 0,1 / 0,4 / 109,7 мс. `DURATION=PASS`; это
подтверждает новые метрики и duration guard, но не заменяет многочасовой leak test.

Остаётся NOT TESTED: DPI 125/150/200%, завершённый многочасовой GUI/worker leak
soak, длительный compiler/search stress с реальными дочерними процессами и
интерактивная Linux GUI-приёмка. Вторая усиленная попытка 7 200 секунд
завершилась досрочно после task #45 без CSV. Следующий запуск на 480 секунд с
усиленным harness завершился с кодом 1 после начального dispatch Run Tests и
не создал CSV/verdict. PASS для длительного soak текущего RC не заявляется.

POSIX existing-target Save завершается fail-closed без RENAME_EXCHANGE;
unconditional renameat fallback удалён. Первое сохранение использует exclusive
linkat. Windows сохраняет path-based ReplaceFileW leaf race и LOW cleanup edge,
который может оставить orphan temp при ошибке получения handle metadata сразу
после exclusive creation. Для включения нового Rust worker license payload и
installer marker checks требуется новая package build.

20 exact-version source archives для compiler toolchain и wx/runtime dependency
closure собраны и signature-verified; upstream hashes и Git source commits
проверены. Rust worker dependency closure отдельно описан как 28 активных
default-feature crates; legacy eframe/rfd dependencies включаются только opt-in
и не входят в worker build. Полный worker source bundle:
build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261110-1955.tar.zst
(8 099 357 bytes; SHA-256 df5881aa72dc556d9fff6217fd436cec9ad9cb345ac899058a12d23bbdf40da5).
Оба build scripts копируют его в source/Rust-worker-source.tar.zst; оба installers
требуют архив, проверяют SHA-256 и сохраняют его в установленном приложении.
Существующие RC-каталоги созданы до этого изменения и должны быть пересобраны.
Отдельный 330 067 308-byte MSYS2 toolchain/wx source bundle не входит в runtime
package. Юридическая проверка способа распространения исходников или written
offer не завершена.

Bundled Binutils обновлён до подписанного MSYS2 2.47-4. Shipped linker формирует
PE/COFF (i386pep/i386pe), readelf не входит в bundle, но BFD input target vector
также распознаёт ELF objects. Source-level review точного подписанного archive
подтвердил достижимость `cache_bwrite` при обычной записи PE output через
foreign-section path в COFF final-link. Default RBL build линкует
compiler-generated COFF objects и не принимает произвольные object inputs и не
передаёт `--gc-sections -w`; это сужает exposure, но не доказывает, что уязвимое
условие размера записи недостижимо. CVE-2026-90801 остаётся unresolved и
release-blocking; это не заявление об upstream fix.

**Текущий статус: ENGINEERING TESTS PASS / GUI ACCEPTANCE SUBSTANTIAL WITH
REMAINING GAPS / RELEASE BLOCKED / UNRELEASED.** Commit, push, tag и публикация
не выполнялись.
