# last verified: v0.8.1 | 2026.11.17 | UBUNTU 24.04 NATIVE AND FLATPAK SCOPE

## Варианты Linux runtime

**Текущий статус (2026-11-17):** native Linux GUI на wxWidgets и Rust worker
версии 0.8.1 собраны, установка через `installer/install_linux.sh` проверена
на Ubuntu 24.04 с системным wxWidgets 3.2.4 (`RBL_USE_SYSTEM_WX=1`). RBL Studio 0.8.1 опубликован как [`v0.8.1`](https://github.com/Reiwa-111/RBL/releases/tag/v0.8.1) 2026-11-17. Локальный Flatpak payload собран
и установлен; sandbox CLI `version`, `doctor`, `check` и `run` прошли.
Интерактивный Flatpak GUI/display/portal acceptance и Flathub publication
остаются непроверенными.

Для GUI RBL Studio предусмотрены два варианта поставки:

| Поставка | GTK на хосте | wxWidgets | Assembler/linker | Python |
|---|---|---|---|---|
| Native shell installer | GTK 3 runtime и X11/Wayland display stack | Системный wxWidgets 3.2.4 для проверенной сборки Ubuntu 24.04 (`RBL_USE_SYSTEM_WX=1`); app-local режим также существует | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |
| Flatpak | Предоставляет `org.gnome.Platform//51` внутри sandbox | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |

Проверенная native-сборка использует системный wxWidgets 3.2.4 и требует GTK 3
runtime на хосте, а также рабочий X11- или Wayland-display. Native installer
также размещает shared libraries wxWidgets в каталоге приложения; GTK и
display stack остаются runtime-зависимостями хоста. Независимый Flatpak-путь
не завершён и не считается принятым вариантом поставки. CLI можно использовать
отдельно от GUI.

Linux toolchain — ClangBuiltLinux LLVM 23.1.5. `clang` запускается в режиме
integrated assembler (`-x assembler`), а `ld.lld` выполняет ELF linking. Bundle
закреплён по SHA-256, содержит полный текст лицензии Apache-2.0 с LLVM
Exceptions и указывает upstream source commit. Для бинарного toolchain нужен
GLIBC 2.27 или новее и host `libstdc++` с `GLIBCXX_3.4.22`. Но полный native
application имеет более высокий измеренный ABI floor: GLIBC 2.39 и GLIBCXX
3.4.32 из-за wxWidgets/IDE, собранных на Ubuntu 24.04. Для более старых native
дистрибутивов потребуется пересборка с более старым sysroot. Аудит сохранён в
`LINUX_ABI_AUDIT.txt`, а проверки CPython native modules/import — в
`PYTHON_RUNTIME_ELF_AUDIT.txt`. В двух upstream Tcl/Tk библиотеках
python-build-standalone остаются RUNPATH `/tools/deps/lib`; на тестовой системе
все зависимости разрешаются, `tkinter` импортируется без внешнего library
path. Detached GPG signature kernel.org проверяется над распакованным tar; key, signature и checksum evidence хранятся в `toolchain/linux-x86_64/provenance/`, а staged files покрыты `SHA256SUMS`. Packaged launchers устанавливают
`RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1`: отсутствие встроенного toolchain
считается ошибкой, а не поводом незаметно перейти на host GNU Binutils.

## Native shell installer

Native installer размещает shared libraries wxWidgets и LLVM toolchain рядом
с приложением. Для обычного использования не нужны development-файлы
wxWidgets, GNU Binutils, системный Python или C compiler. На хосте должны быть
GTK 3 runtime libraries, GLIBC >=2.39, libstdc++ с GLIBCXX_3.4.32 и рабочая
сессия X11 либо Wayland. Проверенная сборка использовала системный wxWidgets
3.2.4 из дистрибутива (`RBL_USE_SYSTEM_WX=1`); альтернативный app-local режим
сборки wxWidgets является отдельным вариантом.

Сборка и установка:

```sh
RBL_USE_SYSTEM_WX=1 bash tools/build_wx_ide.sh build/linux-ubuntu2404
bash installer/install_linux.sh --build-dir build/linux-ubuntu2404
```

Точные аргументы installer показывает команда
`bash installer/install_linux.sh --help`.

## Flatpak (локальный bundle и CLI acceptance пройдены; GUI acceptance ожидает)

Flatpak остаётся независимым от проверенного native Linux build/install path.
Payload 0.8.1 подготовлен, локальный bundle собран и установлен в user
installation Ubuntu 24.04 WSL; sandbox CLI `version`, `doctor`, `check` и `run`
прошли. wx GUI внутри sandbox не запускался, поэтому интерактивный display,
file-dialog, portal и multi-monitor acceptance остаются непроверенными. Это
локальное свидетельство разработки, а не Flathub или public-release acceptance.

Flatpak manifest объявляет `org.gnome.Platform//51`. При установке приложения
Flatpak автоматически устанавливает этот runtime; GTK берётся из runtime, а
не из host-дистрибутива. Сам RBL bundle включает shared libraries wxWidgets,
CPython runtime и LLVM tools.

Подготовка и сборка локального bundle:

```sh
RBL_USE_SYSTEM_WX=1 bash tools/build_wx_ide.sh build/output/wx-ui-linux-0.8.1
python3 tools/prepare_flatpak_payload.py \
  --build-dir build/output/wx-ui-linux-0.8.1 \
  --output "$HOME/rblstudio-flatpak-payload"
ln -s "$HOME/rblstudio-flatpak-payload" build/flatpak-payload
flatpak install flathub org.gnome.Platform//51 org.gnome.Sdk//51
flatpak-builder --state-dir="$HOME/.cache/rblstudio-flatpak-builder" \
  --force-clean --repo="$HOME/rblstudio-flatpak-repo" \
  "$HOME/rblstudio-flatpak-build" packaging/flatpak/com.reiwa.rblstudio.yml
flatpak build-bundle "$HOME/rblstudio-flatpak-repo" "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak" \
  com.reiwa.rblstudio
flatpak install --user "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak"
flatpak run com.reiwa.rblstudio
```

CLI-команды внутри sandbox:

```sh
flatpak run --command=rbl com.reiwa.rblstudio version
flatpak run --command=rbl com.reiwa.rblstudio run /path/inside/home/example.rbl
```

Sandbox получает доступ к домашнему каталогу пользователя, чтобы открывать и
сохранять проекты. Разрешены Wayland и X11 fallback sockets, а также DRI для
графического ускорения. Не запрашивается неограниченный доступ к файловой
системе хоста или сети.

Manifest репозитория является локальным прототипом с prebuilt payload, а не
Flathub submission. Публичный manifest должен собирать приложение из исходных
модулей внутри SDK.
App ID `com.reiwa.rblstudio` предварительный; перед любой публичной публикацией
его нужно сверить с namespace издателя.

Payload, Flatpak builder state/repository/build directories нужно размещать на
case-sensitive Linux filesystem. Symlink `build/flatpak-payload` позволяет
manifest репозитория обратиться к payload, не размещая файлы с различающимся
регистром имён непосредственно на Windows-mounted checkout filesystem.
В bundled terminfo CPython есть имена файлов, различающиеся только регистром,
поэтому preparer отклоняет WSL-пути `/mnt/c`, `/mnt/q` и другие Windows-mounted
диски. Используйте каталог под `$HOME` или другую native Linux filesystem.
Payload содержит `SHA256SUMS`; Flatpak build проверяет его перед копированием
файлов в `/app`.

## Runtime-лицензии сторонних компонентов и бинарные build inputs

Native Linux build использует закреплённые бинарные runtime/toolchain inputs;
их upstream URL, SHA-256 и provenance хранятся в metadata соответствующего
runtime/toolchain. Это внутренние build inputs, а не corresponding-source
archives и не release deliverables. GitHub Releases публикуют дерево исходников
репозитория; отдельные corresponding-source archives не staged, не требуются и
не публикуются в процессе выпуска RBL Studio.

| Компонент | Runtime-лицензия/notice | Бинарный input / upstream provenance |
|---|---|---|
| CPython 3.14.10 / python-build-standalone | Тексты CPython PSF и python-build-standalone MPL-2.0; notices bundled pip/vendor | `runtime/python/python-runtime.tar.gz` — внутренний бинарный build input, проверяемый через `runtime/python/python-runtime.sha256`; upstream URL, hash архива и license/provenance указаны в `runtime/python/UPSTREAM.md` и `rbl-python-runtime.json`. Архив распаковывается в app-local runtime. |
| wxWidgets 3.2.4 | wxWindows Library Licence 3.1; copyright notice системного пакета в system-wx режиме | Проверенная сборка Ubuntu 24.04 использует системный wxWidgets 3.2.4 (`RBL_USE_SYSTEM_WX=1`); app-local режим — отдельная опция сборки. |
| Scintilla / wxStyledTextCtrl | Scintilla license notice в `licenses/Scintilla/License.txt` | Используется через wxStyledTextCtrl; отдельный corresponding-source archive не является release requirement. |
| Clang/LLD 23.1.5 | Apache-2.0 with LLVM Exceptions в `toolchain/linux-x86_64/LICENSE.TXT` | Закреплённый бинарный toolchain; upstream source commit, URL/signature evidence и binary SHA-256 записаны в `UPSTREAM.md`, `manifest.json` и `provenance/`. |
| Rust IDE worker | Лицензии crates MIT/Apache и другие в `licenses/Rust/` | Собирается из исходников репозитория и `Cargo.lock`; отдельный corresponding-source archive не является release requirement. |
| GTK 3 / host display stack | Runtime GTK и display integration предоставляет дистрибутив хоста | Runtime-зависимости native GUI; native installer их не включает. |

Packaging pipeline проверяет закреплённые размеры и SHA-256 бинарных архивов и
не перезаписывает повреждённый cache. CPython runtime tarball является
внутренним бинарным build input, а не corresponding-source archive; его
upstream URL/hash provenance сохраняется, хотя сам архив не является release
deliverable. Включённые license texts и provenance records — технические
свидетельства, а не юридическое заключение или формальное согласование.
