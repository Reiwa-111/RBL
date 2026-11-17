# Урок 7: версия Runtime ABI и совместимость

Runtime ABI версионируется независимо от версии языка и компилятора RBL. Текущая ABI — 1.2.0. Descriptor содержит сведения о layout и возможностях; сгенерированные программы и связанный Runtime проверяют совместимость до входа в RBL `main`.

Запусти диагностику окружения:

```bash
python3 rbl.py doctor
python3 rbl.py version
```

`doctor` показывает выбранный backend, доступность assembler/linker, используемый bundled toolchain и наличие Runtime ABI. Это проверка окружения, а не замена ABI regression suite. ABI-тесты проверяют совместимые minor/patch версии, отклонение несовместимых layout/features и отсутствие versioned symbols.

ABI descriptor — внутренний контракт интеграции compiler/Runtime. Обычные программы RBL не читают и не изменяют его напрямую.

**Упражнение:** запусти `doctor` в Windows и Linux/WSL и сравни backend/toolchain. Не ожидай совпадения платформенных путей и источников системного времени.