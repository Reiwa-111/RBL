# Урок 8: мастер первой настройки RBL Studio

В RBL Studio 0.8.1 появился нативный setup wizard. Он проверяет application root, Rust worker, Python runtime и compiler toolchain до начала обычной работы. Он сохраняет per-user настройки и предлагает отдельный development/source-checkout path.

Если доступен полный installer payload для платформы, мастер может асинхронно запустить user-scope Install или Repair и показать перехваченный вывод и exit status. Мастер можно повторно открыть из Settings. Source checkout не равен зарегистрированной установке; при работе прямо из репозитория выбирай development path.

**Упражнение:** запусти IDE из корня репозитория, проверь обнаруженные пути, заверши development setup и повторно открой мастер через Settings. Не запускай installer для каталога, который сам не выбрал.