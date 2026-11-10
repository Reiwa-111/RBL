# RBL Studio — план обновления интерфейса и UX



**Статус:** активный продуктовый план для wxWidgets frontend 0.8.0  

**Дата инвентаризации:** 2026-11-10  

**Цель:** превратить текущий функциональный редактор в цельную, настраиваемую IDE, не ломая существующие сценарии и архитектуру worker/backend.



## 1. Контекст и решение по направлению



Текущий интерфейс — нативный wxWidgets + wxAUI + wxStyledTextCtrl (Scintilla), расположенный в `native/wx-ui/`. Старый egui интерфейс в `src/` больше не является основным frontend, но содержит важную спецификацию прежних пользовательских возможностей и настроек. Его нельзя механически переносить: нужно использовать его как источник parity-требований.



Целевой ориентир — удобство современных IDE (в частности PyCharm New UI), но с сохранением нативного поведения wxWidgets и собственной идентичности RBL Studio. Не копировать внешний вид PyCharm пиксель-в-пиксель.



Основные принципы:



- **Editor first:** редактор занимает основное пространство; панели не должны отнимать его без необходимости.

- **Ясная иерархия:** меню и основные действия, навигация, editor tabs, tool windows, status bar имеют предсказуемые уровни.

- **Низкий визуальный шум:** единая палитра, ограниченный набор акцентных цветов, согласованные размеры, отступы и иконки.

- **Настраиваемая плотность:** обычный и compact режимы для разных экранов.

- **Разделение тем:** тема интерфейса и цветовая схема кода — независимые настройки.

- **Keyboard-first без keyboard-only:** палитра команд и горячие клавиши дополняют доступные меню и видимые команды.

- **Стабильность:** тема и настройки не должны менять жизненный цикл worker, блокировать UI или ломать layout persistence.

- **Доступность:** контраст, видимый focus, понятные состояния ошибок/предупреждений, масштабирование DPI.



## 2. Что уже есть в текущем wx frontend



По состоянию исходников `native/wx-ui/src/main_frame.cpp` и `include/main_frame.hpp`:



- wxAUI layout с toolbar, Project Tree, editor notebook и нижней панелью Run/Output.

- Вкладки документов, сохранение, закрытие, восстановление закрытой вкладки.

- Scintilla editor: RBL syntax styling, line numbers, folding, caret line, indentation guides, tabs/spaces, soft wrap.

- Project Tree с lazy population и ограничениями на количество узлов.

- Run, Stop, Check, Build, ASM, Tests, Benchmarks, Toolchain Doctor через отдельный Rust worker.

- Console, stdin, EOF, Problems и Search result panes.

- Find, Replace, Find in Files, Go to Line, Quick Open, Command Palette, Quick Reference.

- Persistent AUI perspective, размер окна, workspace root и несколько editor options в `ui.ini`.

- DPI-aware размеры и обработка DPI change.



Первый theme layer уже вынесен в `native/wx-ui/include/theme.hpp`: `ThemePalette` отвечает за chrome/panels, `EditorColorScheme` — отдельно за syntax tokens. При этом часть дочерних диалогов и некоторых второстепенных компонентов всё ещё использует нативные или локальные цвета; считать визуальную унификацию завершённой рано.



## 3. Parity: старый egui UI (`src/`) → wx UI



Старый UI и его `Settings` содержали следующие пользовательские параметры:



| Возможность | Старый egui | Текущий wx | Действие |

|---|---|---|---|

| Dark / Light / Nord / Monokai | Да | RBL Dark / RBL Light / Nord / Monokai / High Contrast | Сохранить aliases и добавить visual acceptance |

| Размер шрифта редактора | Да | Да | Сохранить, расширить выбором семейства шрифта |

| Размер табуляции | Да | Да | Сохранить |

| Автоматическая проверка после редактирования | Да | Да | Сохранить, сделать debounce настраиваемым при необходимости |

| Автосохранение перед Run/Build | Да | Да, перед Run/Build/Assembly | Сохранить и проверить отмену Save As |

| Номера строк | Да | Да | Сохранить |

| Подсветка синтаксиса | Да | Да, настраиваемая | Проверить toggle на больших документах |

| Перенос длинных строк | Да | Да | Сохранить |

| Направляющие отступов | Да | Да | Сохранить |

| WSL mode: auto/native/wsl | Да | Да, через worker environment | Проверить на реальном Windows/WSL окружении |

| WSL distribution | Да | Да | Проверить на реальном Windows/WSL окружении |

| Сброс настроек | Да | Нет отдельной кнопки | Вернуть |

| Apply / Cancel | Да | wx modal OK/Cancel | Сохранить транзакционное поведение |



Старый settings schema: `src/settings.rs`. Старый UI: `src/app.rs` (меню и настройки около `menu_bar`, `toolbar`, `show_settings`). Старый editor: `src/editor.rs`.



Legacy migration теперь читает поддерживаемые поля `theme`, `font_size`, `tab_size`, `show_line_numbers`, `soft_wrap`, `indent_guides`, `auto_check`, `auto_save`, `syntax_highlighting`, `wsl_mode` и `wsl_distro`, затем сохраняет их в `ui.ini` и ставит migration marker. GUI acceptance проверяет валидный, oversized, negative и malformed input. Неизвестные поля JSON не переносятся в `ui.ini`; при необходимости нужно добавить versioned schema вместо расширения ad-hoc regex.



## 4. Исследованные UX-ориентиры



### PyCharm / JetBrains IDE



- New UI снижает визуальную сложность, упрощает основную toolbar и разделяет tool windows по краям окна.

- Предусмотрены Light, Dark и high-contrast варианты; компактный режим уменьшает toolbar/tool-window headers, spacing и размеры элементов.

- UI theme отделена от editor color scheme: первая оформляет окна и controls, вторая — код, поиск и текстовые области.

- Глобальные настройки IDE отделены от project-level settings.

- Tool windows можно показывать/скрывать, перемещать и настраивать; layout должен оставаться пользовательским.



### Fluent 2 / Windows UX



- Цвет должен задавать иерархию и значение, а не быть декором.

- Layout и spacing формируют группы и отношения между элементами.

- Типографика, iconography, navigation, usability и motion должны быть системными, а не настраиваться несвязанными точечными решениями.

- Важны предсказуемые паттерны, platform conventions и минимизация визуального шума.



### wxAUI



- wxAUI поддерживает dockable/floating panes, customizable toolbars и сохранение/восстановление perspectives.

- Следует использовать эти возможности для tool-window workflow, а не строить имитацию docking вручную.



Полезные источники:



- [PyCharm New UI](https://www.jetbrains.com/help/pycharm/new-ui.html)

- [PyCharm Appearance settings](https://www.jetbrains.com/help/pycharm/settings-appearance.html)

- [PyCharm UI themes](https://www.jetbrains.com/help/pycharm/user-interface-themes.html)

- [PyCharm Colors and fonts](https://www.jetbrains.com/help/pycharm/configuring-colors-and-fonts.html)

- [PyCharm global and project settings](https://www.jetbrains.com/help/pycharm/configuring-project-and-ide-settings.html)

- [Microsoft Fluent design guidelines](https://learn.microsoft.com/en-us/windows/apps/design/guidelines-overview)

- [Fluent 2 layout](https://fluent2.microsoft.design/layout)

- [Fluent 2 design principles](https://fluent2.microsoft.design/design-principles)

- [wxAUI overview](https://www.ibaku.net/stable/overview_aui.html)

- [PyCharm New UI: reduced visual complexity, tool windows and compact mode](https://www.jetbrains.com/pycharm/whatsnew/2023-1/)

- [JetBrains 2026 New UI: themes, iconography, compact mode and tool windows](https://www.jetbrains.com/help/idea/new-ui.html)

- [JetBrains: Compact Mode](https://support.jetbrains.com/hc/en-us/articles/40254632842898-How-to-make-the-new-UI-more-compact-so-there-is-less-empty-space)

- [Nielsen Norman Group: 10 Usability Heuristics](https://www.nngroup.com/articles/ten-usability-heuristics/)

- [Don Norman: The Design of Everyday Things](https://mitpress.mit.edu/9780262525671/the-design-of-everyday-things/)

- [Alan Cooper et al.: About Face, 4th Edition](https://www.wiley.com/en-us/shop/general-introductory-computer-science/about-face-the-essentials-of-interaction-design-4th-edition-p-9781118766583)

- [W3C WCAG 2.2 — Contrast Minimum](https://www.w3.org/TR/WCAG22/#contrast-minimum)



## 5. Целевая структура интерфейса



1. **Window header / menu:** File, Edit, Selection, Navigate, Code, Run, Tools, View, Settings, Help; меню не должно разрастаться дублирующими кнопками.

2. **Main toolbar:** контекстные основные действия (Run/Stop/Check/Build), project selector, быстрый поиск; overflow для второстепенных команд.

3. **Tool window stripes:** Project слева; Structure/Services при появлении соответствующей функциональности; Problems/Run/Terminal/Build/Search снизу; дополнительные окна справа при необходимости.

4. **Editor area:** вкладки, аккуратные modified/readonly markers, контекстное меню, быстрый доступ к навигации, читаемые gutter/folding/selection states.

5. **Status bar:** текущий файл/проект, позиция курсора, encoding, line ending, indentation, состояние backend/task.

6. **Dialogs:** единый стиль, логичная группировка, адекватные минимальные размеры, клавиатурная навигация.



## 6. Дизайн-система и темы



Сделать единый Theme/Appearance слой (отдельные файлы/модуль, не россыпь цветов в `main_frame.cpp`). Минимальные семантические tokens:



- window background, surface, elevated surface, editor background;

- primary/secondary text, muted text, disabled text;

- border, separator, hover, pressed, selected, focus ring;

- accent, success, warning, error, info;

- editor foreground, selection, caret line, line numbers, whitespace, fold markers;

- syntax tokens: keyword, string, comment, number, function, boolean, operator, type, punctuation.



Первые встроенные UI themes: **RBL Dark**, **RBL Light**, **Nord**, **Monokai**, **High Contrast**. Названия старых Dark/Light сохранить как aliases при миграции. Не смешивать UI theme с editor color scheme.



Для каждого theme обязательны:



- одинаковые semantic token names;

- проверка контраста текста и интерактивных состояний;

- согласованная окраска editor, Project Tree, tabs, toolbar, output, Problems, Search, menus/dialogs/status;

- отсутствие hardcoded dark-only цветов в компонентах;

- корректный DPI scaling и системные шрифты;

- сохранение выбора после перезапуска.



Дополнительно: compact mode, UI font family/size, editor font family/size, preview темы до применения, reset, import/export color scheme — поэтапно.



## 7. Полноценные Settings



Settings должны быть отдельным понятным диалогом с категориями слева и страницей настроек справа (по образцу IDE settings, но без перегруженности):



- **Appearance:** UI theme, editor color scheme, compact mode, UI font, UI scale/system sync если поддерживается.

- **Editor / General:** font family/size, tab size, tabs vs spaces, line numbers, soft wrap, indent guides, caret line, whitespace, folding.

- **Editor / Color Scheme:** независимые цвета синтаксиса и предпросмотр.

- **Editor / Behavior:** auto-check, debounce, auto-save before Run/Build, restore tabs/session.

- **Project:** project tree visibility/behavior, default workspace behavior.

- **Toolchain:** auto/native/WSL mode и WSL distro только после проверки текущего backend contract.

- **Keymap:** отображение и настройка shortcuts — отдельный этап.



Требования: Apply применяет без закрытия, OK применяет и закрывает, Cancel откатывает изменения, Restore Defaults сбрасывает текущую категорию или всё по явному выбору. Ошибки записи конфигурации не должны молча теряться.



## 8. Приоритетный roadmap



### Phase A — Inventory и архитектурная подготовка



- [x] Сверить старый egui settings schema и основные wx panes/actions.

- [x] Зафиксировать разрывы parity и исследовательские источники.

- [ ] Проверить оставшиеся действия/горячие клавиши старого `src/app.rs` и `src/editor.rs` по полной матрице.

- [ ] Определить конфигурационную схему, версионирование и migration path.



### Phase B — Visual foundation



- [x] Вынести semantic UI tokens и отдельные editor syntax tokens в `native/wx-ui/include/theme.hpp`.

- [x] Добавить RBL Dark, RBL Light, Nord, Monokai и High Contrast.

- [x] Подключить выбор UI theme и независимой editor color scheme с сохранением.

- [ ] Визуально проверить каждую тему и устранить остаточные hardcoded/native surfaces; Settings теперь получает активную palette, но нативные Windows controls ещё не полностью theme-aware.

- [ ] Привести toolbar, tabs, panes, editor, output, diagnostics, search и status к одной системе.

- [x] Добавить базовый compact mode для toolbar, Project, output tabs и таблиц.

- [x] Исправить compact metrics при DPI transitions: tab height, Project Tree spacing/indent, toolbar margins/icons и console typography.

- [x] Привести Quick Open и Command Palette dialogs к активной UI palette; развести иконки Quick Open/Benchmarks с соседними действиями.

- [x] Сделать toolbar state-aware: compiler actions доступны только для RBL-документа при готовом backend, Stop — только во время активной задачи.

- [ ] Завершить визуально согласованные DPI-aware иконки и проверить все масштабы.



### Phase C — Settings и миграция



- [x] Перестроить modal dialog в категорийный Settings UI: Appearance, Editor, Behavior, Toolchain.

- [x] Вернуть font size, tab width, line numbers, soft wrap, indent guides, auto-check, syntax toggle, auto-save before Run/Build/Assembly.

- [x] Вернуть Windows toolchain mode и WSL distribution settings.

- [x] Разделить UI theme и editor color scheme.

- [x] Добавить legacy `settings.json` migration для прежних полей и сохранение в `ui.ini`.

- [x] Расширить GUI migration acceptance: valid, oversized, negative и malformed configs; 4/4 PASS на изолированном build dir.

- [x] Добавить Apply без закрытия, OK/Cancel и Restore Defaults.

- [ ] Добавить живой preview темы/цветовой схемы и automated acceptance Apply/Cancel/Reset.

- [ ] Добавить выбор семейства UI/editor fonts и полноценную схему project-level settings.



### Phase D — Editor experience



- [x] Вернуть создание обычного текстового документа отдельно от RBL.

- [ ] Проверить parity редакторских команд и shortcuts.

- [x] Command Palette: фильтрация команд по мере ввода, выбор стрелками и подтверждение Enter.

- [x] Quick Open: фоновый ограниченный индекс файлов проекта, фильтрация по имени/пути, ранжирование совпадений, keyboard selection и быстрый переход; добавлен static acceptance.

- [x] Заменить модальные Find/Replace на постоянную dockable-панель: Next/Previous, Replace/Replace All, Match Case, wrap-around и единый undo для Replace All; добавлен static acceptance.

- [x] Проверить новую Find/Replace-панель визуально на 1440×900; вынести её в отдельный AUI row над Run/Output, чтобы она не делила нижнюю область по горизонтали.

- [ ] Проверить Find/Replace, Find in Files, navigation и diagnostics в едином UX.

- [ ] Добавить недостающие редакторские удобства после проверки Scintilla API и RBL grammar.



### Phase E — Workspace workflow



- [x] Добавить View menu для показа/скрытия Project и Run/Output и восстановления default layout.

- [x] Сохранить базовый compact mode.

- [x] Разнести Find/Replace и Run/Output по отдельным AUI rows; адаптировать последнюю колонку Problems/Search под ширину viewport.

- [x] Обновлять enabled state toolbar при смене документа, polling worker и завершении процесса.

- [ ] Улучшить tool-window layout, visibility controls и reset layout.

- [ ] Проверить сохранение/восстановление размеров и состояния панелей.

- [ ] Улучшить status bar и контекстные меню.

- [ ] Проверить multi-monitor, разные DPI и компактные экраны.



### Phase F — Acceptance



- [x] Сделать чистые GUI captures базового окна, Find/Replace pane и Settings dialog на 1440×900.

- [ ] Скриншоты и ручной осмотр Dark/Light/Nord/Monokai/High Contrast.

- [ ] Скриншоты обычной и compact density.

- [ ] GUI acceptance для Settings Apply/Cancel/Reset и миграции.

- [ ] Keyboard-only smoke и проверка focus states.

- [ ] Regression suite, CTest, Rust tests, Linux/Windows builds.

- [ ] Обновить руководство пользователя и release notes.



## 9. Definition of Done



Результат не считается готовым, пока:



- все встроенные темы визуально согласованы во всех основных панелях;

- UI theme и editor scheme независимы;

- настройки сохраняются и применяются сразу, отмена действительно откатывает изменения;

- прежние настройки мигрируют без потери поддерживаемых значений;

- основные действия доступны через меню, toolbar или command palette и имеют рабочие shortcuts;

- пользователь может восстановить стандартный layout;

- интерфейс корректно работает на Windows/Linux, при DPI scaling и в compact mode;

- GUI screenshots и automated tests подтверждают поведение;

- документация соответствует реально реализованным возможностям.



## 10. Текущий статус / ближайшая работа



На 2026-11-10 реализована следующая UX-итерация: Quick Open получил bounded background indexing с фильтрацией по имени/пути, ранжированием, keyboard navigation, текстовым allowlist и безопасным no-follow обходом директорий (Windows directory-chain locks, POSIX openat/fstatat); Find/Replace переведён с модальных диалогов на постоянную dockable-панель с Next/Previous, Replace/Replace All, Match Case, wrap-around, F3/Shift+F3 и единым undo для Replace All. Дополнительно: Settings, Quick Open и Command Palette наследуют активную palette; compact metrics пересчитываются при DPI changes; Problems используют semantic error/warning colors; консоль следует compact typography; колонки Problems/Search адаптируются к ширине; toolbar отражает доступность действий по документу/backend/task; иконки Quick Open/Benchmarks разведены; Toolchain page на Linux отключена и помечена Windows-only; Run menu перемещён рядом с Edit. Изолированная Windows сборка `build/wx-ui-ux-check` проходит; CTest — 10/10 PASS; GUI settings migration/persistence — 4/4 PASS. Изолированная Linux сборка `build/wx-ui-ux-check-linux` проходит; CTest — 9/9 PASS. Восстановлен отсутствовавший `tests/posix_file_metadata.cpp`, на который уже ссылался CMake; тест покрывает no-follow чтение, descriptor-relative операции, identity и atomic name exchange. GUI screenshots базового окна, Find pane, Settings и themed Quick Open получены на 1440×900. Полная visual acceptance всех пяти тем, DPI scales 125–200% и keyboard-only сценариев ещё не завершена. Отдельный 7200-секундный resource soak на релизном `build/wx-ui-win` завершился ранее: 7220.9 секунд, 6990 samples, 119 task cycles, peak process-tree WS 281.5 MiB, private 197.1 MiB, max UI ping 206.5 ms при пороге 500 ms. Основной релизный бинарник во время текущей UX-итерации не заменялся.



### Resource soak — Windows baseline build



2026-11-10 завершён естественно двухчасовой resource acceptance для `build/wx-ui-win` (это baseline build до текущей theme/settings итерации):



- CSV: `build/wx-ui-win/resource-acceptance/wx-resource-soak-20261110-105212.csv`.

- 6,990 samples; 7,220.9 s sampled duration при запросе 7,200 s.

- 119 task cycles; в журнале присутствуют Check, Build, Run, Tests и 20 post-close samples.

- Peak process-tree working set: 281.5 MiB; peak private bytes: 197.1 MiB (лимит harness: 512 MiB).

- UI ping p95/p99/max: 0.111 / 0.262 / 206.466 ms (лимит max: 500 ms).

- Post-close footprint: 61.7 MiB working set / 18.6 MiB private bytes.

- Harness дошёл до post-close sampling; это означает, что обязательные task-coverage и Close All Tabs checks не выставили failure.



Итог: baseline soak acceptance PASS по duration, resource и responsiveness thresholds. Это не заменяет повторный soak после интеграции текущих UI changes: новая сборка пока проверена отдельным GUI migration acceptance 4/4 и CTest 7/7, но не двухчасовым soak.



Следующие приоритеты:



1. Перенести Project Tree enumeration из UI thread в bounded background worker с теми же no-follow guarantees, что у Quick Open/Find in Files.

2. Добавить живой preview в Settings и automated Apply/Cancel/Reset acceptance.

3. Добавить tool-window focus/hide shortcuts и контекстные меню.

4. Проверить все темы на светлых/тёмных поверхностях, контраст, DPI 125–200% и компактные экраны на реальном дисплее.

5. Завершить keyboard-only acceptance для Quick Open, Find/Replace и Command Palette.



Не смешивать этот roadmap с release soak: длительные тесты остаются вспомогательной проверкой стабильности, но основной приоритет разработки теперь — дизайн и developer experience.

