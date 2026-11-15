# last verified: v0.8.0 | 2026.11.12 | LINUX PORTABLE TOOLCHAIN AND FLATPAK

## Варианты Linux runtime

**Статус интеграции (2026-11-12):** app-local wxWidgets/IDE собран, CTest
(11/11), native Linux installer acceptance, LLVM provenance и установленный
CLI/worker acceptance прошли на Ubuntu 24.04 WSL. Подготовка Flatpak payload,
проверка 5 246 checksum и разбор manifest прошли; GNOME runtime bundle и
sandbox acceptance ещё не завершены. GUI не запускался; готовность к публичному
выпуску не заявляется.

Для GUI RBL Studio предусмотрены два варианта поставки:

| Поставка | GTK на хосте | wxWidgets | Assembler/linker | Python |
|---|---|---|---|---|
| Native shell installer | GTK 3 runtime и X11/Wayland display stack | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |
| Flatpak | Предоставляет `org.gnome.Platform//51` внутри sandbox | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |

Ни одному варианту не нужны host GNU Binutils (`as`/`ld`) или системный
wxWidgets. Flatpak также снимает зависимость от host GTK runtime. Оба варианта
по-прежнему используют ядро хоста, интеграцию графического драйвера и display
server. CLI можно использовать отдельно от GUI.

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
сессия X11 либо Wayland. Сборка wxWidgets из
исходников дополнительно требует GTK 3 development headers, C/C++ compiler,
CMake, Make и Cargo.

Сборка и установка:

```sh
bash tools/build_wx_ide.sh build/portable-linux
bash installer/install_linux.sh --build-dir build/portable-linux
```

Точные аргументы installer показывает команда
`bash installer/install_linux.sh --help`.

## Flatpak

Flatpak manifest объявляет `org.gnome.Platform//51`. При установке приложения
Flatpak автоматически устанавливает этот runtime; GTK берётся из runtime, а
не из host-дистрибутива. Сам RBL bundle включает shared libraries wxWidgets,
CPython runtime и LLVM tools.

Подготовка и сборка локального bundle:

```sh
bash tools/build_wx_ide.sh build/portable-linux
python3 tools/prepare_flatpak_payload.py \
  --build-dir build/portable-linux \
  --output build/flatpak-payload
flatpak install flathub org.gnome.Platform//51 org.gnome.Sdk//51
flatpak-builder --force-clean --repo=build/flatpak-repo \
  build/flatpak-build packaging/flatpak/com.reiwa.rblstudio.yml
flatpak build-bundle build/flatpak-repo build/RBLStudio-0.8.0-linux-x86_64.flatpak \
  com.reiwa.rblstudio
flatpak install --user build/RBLStudio-0.8.0-linux-x86_64.flatpak
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

Текущий manifest собирает локально подготовленный binary payload. Он подходит
для локального/side-load Flatpak-тестирования, но пока не готов к публикации в
Flathub: публичный manifest должен собирать приложение из исходных модулей
внутри SDK. App ID `com.reiwa.rblstudio` предварительный; перед публичной
публикацией его нужно сверить с namespace издателя.

Подготовленный payload необходимо размещать на case-sensitive Linux filesystem.
В bundled terminfo CPython есть имена файлов, различающиеся только регистром,
поэтому preparer отклоняет WSL-пути `/mnt/c`, `/mnt/q` и другие Windows-mounted
диски. Используйте каталог под `$HOME` или другую native Linux filesystem.
Payload содержит `SHA256SUMS`; Flatpak build проверяет его перед копированием
файлов в `/app`.

## Инвентаризация сторонних компонентов и исходников

Таблица описывает Linux packaging pipeline: при сборке Linux payload upstream-
архивы исходников складываются в `source/`. Текущий Windows checkout эти архивы
не содержит; upstream URL, хэши и source-ссылки записаны в
`runtime/python/rbl-python-runtime.json` и
`toolchain/windows-x86_64/SOURCE_CODE.md`.

| Компонент | Runtime-лицензия/notice | Соответствующие исходники или provenance |
|---|---|---|
| CPython 3.14.10 | PSF license и notices pip/vendor | `source/Python-3.14.10.tar.xz`; hash runtime и upstream metadata в `runtime/python/` |
| python-build-standalone | MPL-2.0 | `source/python-build-standalone-20261110.tar.gz`; commit tag и SHA-256 в runtime metadata |
| wxWidgets 3.2.4 | wxWindows Library Licence 3.1 | `source/wxWidgets-3.2.4.tar.bz2`; лицензия `licenses/wxWidgets/License.txt` |
| Scintilla / wxStyledTextCtrl | Scintilla license notice | Входит в wxWidgets source tree; notice `licenses/Scintilla/License.txt` |
| Clang/LLD 23.1.5 | Apache-2.0 with LLVM Exceptions | Полная лицензия `toolchain/linux-x86_64/LICENSE.TXT`; source commit и binary SHA-256 в `UPSTREAM.md` и `manifest.json` |
| Rust IDE worker | MIT/Apache и другие лицензии, заявленные crates | `source/Rust-worker-source.tar.zst`; notices crates в `licenses/Rust/` |
| GTK и GNOME runtime | Лицензии GNOME Platform runtime | Устанавливается отдельно через Flatpak; не включён в RBL application bundle |

У Windows MinGW-w64/GCC/Binutils dependency closure отдельные notices и
corresponding-source archive; см.
[`toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md`](../../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md).

Скрипты упаковки проверяют закреплённые размеры и SHA-256 архивов и не
перезаписывают повреждённый cache. Это технические свидетельства соблюдения
условий распространения, а не юридическое заключение или формальное
согласование. Публичная дистрибуция по-прежнему требует проверки всей
dependency closure и выбранной процедуры распространения владельцем/юристом.
