# Polden OBS

Форк OBS Studio 32.2.2 для Windows: [VslyDream/Polden-OBS](https://github.com/VslyDream/Polden-OBS). Исходный тег: `obsproject/obs-studio`, `32.2.2`; основная ветка: `polden-obs`. Базовый код и лицензия GPL-2.0-or-later сохранены.

Текущая версия Polden OBS — **0.1.0**, предыдущая обозначена как **0.0.1**. Версия базы OBS (`32.2.2`) хранится отдельно. Изменены заголовок главного окна, метаданные Windows и значок. Док-панель **Polden** хранит проекты игр и три уровня автоматизации: запись без действий, конвертация в MP4, доставка на LucidLink. Кнопка **Open files folder** открывает папку готовых файлов последней записи выбранной игры. Маленький значок жука в нижнем правом углу панели открывает [ЛС для баг-репортов](https://t.me/Vsly_Dream). Интеграция с Premiere удалена.

Точка запуска актуальной Windows-сборки: `D:\3_Proga\Polden-OBS\latest\bin\64bit\obs64.exe`. `latest` — ссылка на последний успешно собранный Release-каталог; сборочный скрипт обновляет её и ярлык «Polden OBS» в меню «Пуск». Поэтому поиск Windows и прямой запуск файла ведут к одной версии. Папку `latest` не удалять и не копировать отдельно от репозитория.

Карта для следующего разработчика:

- [Сборка Windows](docs/polden/build-windows.md) — зависимости, команды, проверка и ярлык в поиске Windows.
- [Перенос локального OBS](docs/polden/local-obs-import.md) — профиль записи, коллекция сцен и InfoWriter.
- [Проекты и пайплайн Polden](docs/polden/project-pipeline.md) — сущность игры, уровни автоматизации, шаблоны и открытые решения.
- [MP4 после записи](docs/polden/recording-mp4.md) — поведение OBS, вариант FFmpeg и точки интеграции.
- [История версий](docs/polden/releases.md) — версии 0.0.1 и 0.1.0.
- [Релизы и обновление](docs/polden/updating.md) — публикация ZIP, сохранение настроек и резервные копии.

Интерфейс и пайплайн: `frontend/widgets/PoldenPanel.cpp`, `PoldenPipeline.cpp`, подключение в `OBSBasic.cpp` и `OBSBasic_Recording.cpp`. Номер выпуска — `POLDEN_VERSION`; из него берутся версия интерфейса, Windows EXE и имя ZIP. Исходный рисунок — `Polden OBS.png`; Windows ICO и Qt PNG получены из него.
