# Polden OBS

Форк OBS Studio 32.2.2 для Windows: [VslyDream/Polden-OBS](https://github.com/VslyDream/Polden-OBS). Исходный тег: `obsproject/obs-studio`, `32.2.2`; основная ветка: `polden-obs`. Базовый код и лицензия GPL-2.0-or-later сохранены.

Изменены заголовок главного окна, метаданные Windows и значок. Док-панель **Polden** хранит проекты игр и четыре уровня автоматизации: запись без действий, конвертация в MP4, доставка на LucidLink, импорт в Premiere. Для Premiere подготовлена отдельная UXP панель. Windows-сборка прошла; установку панели и действия с рабочей Production ещё нужно проверить в Premiere.

Карта для следующего разработчика:

- [Сборка Windows](docs/polden/build-windows.md) — зависимости, команды, проверка и ярлык в поиске Windows.
- [Перенос локального OBS](docs/polden/local-obs-import.md) — профиль записи, коллекция сцен и InfoWriter.
- [Проекты и пайплайн Polden](docs/polden/project-pipeline.md) — сущность игры, уровни автоматизации, шаблоны и открытые решения.
- [MP4 после записи](docs/polden/recording-mp4.md) — поведение OBS, вариант FFmpeg и точки интеграции.
- [Premiere / Productions](docs/polden/premiere.md) — подключение UXP панели, API 25.6 и ограничения первой версии.

Интерфейс и пайплайн: `frontend/widgets/PoldenPanel.cpp`, `PoldenPipeline.cpp`, подключение в `OBSBasic.cpp` и `OBSBasic_Recording.cpp`. Панель Premiere: `premiere/polden-bridge`. Исходный рисунок — `Polden OBS.png`; Windows ICO и Qt PNG получены из него.
