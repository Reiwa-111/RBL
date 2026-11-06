# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: RBL Studio IDE

RBL Studio — необязательный графический редактор на Python/Tkinter, использующий тот же project/toolchain driver, что и CLI. Для компиляции языка IDE не требуется.

## Запуск

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

## Основные горячие клавиши

| Клавиши | Действие |
|---|---|
| `Ctrl+N` | Новый файл |
| `Ctrl+O` | Открыть файл |
| `Ctrl+S` | Сохранить |
| `F5` | Собрать и запустить выбранный файл |
| `Ctrl+B` | Сборка / просмотр assembly |
| `Ctrl+Shift+B` | Проверка исходника (`check`) |
| `Ctrl+T` | Запуск тестов |
| `Ctrl+Shift+T` | Запуск benchmark |

Набор панелей и элементов интерфейса может меняться между сборками Studio. `F5` использует настроенный toolchain backend; `Ctrl+Shift+B` не означает линковку исполняемого файла.

## Диагностика запуска

```bash
python3 tools/ide_doctor.py
python3 tools/ide_smoke.py
```

На Linux/WSL для отладочного запуска:

```bash
bash RBLStudio.sh --debug
```

Для изоляции проблем дисплея предусмотрены `tools/gui_probe.py` и запуск reduced safe mode:

```bash
bash RBLStudio.sh --safe
```

Изучите `ide_startup.log` в корне checkout: он содержит последнюю записанную стадию старта и traceback, если он возник.
