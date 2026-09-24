# Polden OBS

Форк OBS Studio 32.2.2 для Windows: [VslyDream/Polden-OBS](https://github.com/VslyDream/Polden-OBS). Исходный тег: `obsproject/obs-studio`, `32.2.2`; основная ветка: `polden-obs`. Базовый код и лицензия GPL-2.0-or-later сохранены.

Сейчас изменены заголовок главного окна, метаданные Windows, значок и добавлена док-панель **Polden** с двумя неактивными кнопками. Дальнейшая цель — связать окончание записи, преобразование контейнера и импорт материала в Adobe Premiere.

Карта для следующего разработчика:

- [Сборка Windows](docs/polden/build-windows.md) — зависимости, команды, проверка и ярлык в поиске Windows.
- [MP4 после записи](docs/polden/recording-mp4.md) — поведение OBS, вариант FFmpeg и точки интеграции.
- [Premiere / Productions](docs/polden/premiere.md) — доступные API, ограничения версий и план интеграции.

Основные изменения интерфейса находятся в `frontend/widgets/OBSBasic.cpp`, `OBSBasic_Docks.cpp` и `OBSBasic.hpp`. Исходный рисунок — `Polden OBS.png`; Windows ICO и Qt PNG получены из него.
