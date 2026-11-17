# Урок 10: native Linux build и область Flatpak

Native Linux build wxWidgets GUI + Rust worker и shell install path проверены на Ubuntu 24.04 с системным wxWidgets 3.2.4. Этот вариант использует host GTK 3 и host display stack.

Собери native frontend из корня репозитория:

```bash
./tools/build_wx_ide.sh
./RBLStudio.sh
```

Flatpak — отдельный путь упаковки. Его локальный payload/bundle и sandbox CLI-команды (`version`, `doctor`, `check`, `run`) проверены. Эти CLI-проверки не покрывают интерактивный запуск Flatpak GUI, Wayland/X11 display, file dialogs/portals, desktop integration и публикацию во Flathub.

Используй [руководство по Linux packaging](../how-to/linux-packaging.md), чтобы увидеть prerequisites и различия между host dependencies нативного приложения и Flatpak runtime.

**Упражнение:** собери native Linux GUI на Ubuntu 24.04, запусти `rbl doctor`, затем изучи Flatpak guide и перечисли, какие проверки относятся только к CLI, а какие требуют интерактивного GUI.