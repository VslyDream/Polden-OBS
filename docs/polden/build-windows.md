# Сборка Polden OBS для Windows

База: официальный тег OBS Studio `32.2.2` от 14.08.2026. `upstream` указывает на `https://github.com/obsproject/obs-studio.git`; локальная ветка — `polden-obs`.

## Среда

- Visual Studio Community 2022 17.14 с C++ toolchain и Windows SDK 10.0.26100.0.
- CMake 3.31.6; генератор `Visual Studio 17 2022` (штатный preset тега требует VS 2026, поэтому здесь выбран генератор установленной VS 2022).
- Зависимости, Qt и CEF CMake получает из официальных архивов по хешам из `CMakePresets.json` в `.deps/`.
- Подмодули: `git submodule update --init --recursive`.

Конфигурация:

```powershell
& 'T:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build_polden_next -G 'Visual Studio 17 2022' -A 'x64,version=10.0.26100.0' -DVIRTUALCAM_GUID='A3FCE0F5-3493-419F-958A-ABA1250EC20B' -DENABLE_BROWSER=ON
```

Сборка:

```powershell
python scripts/build-windows.py --config Release --jobs 1
```

В среде Codex встречаются одновременно `Path` и `PATH`, что ломает MSBuild до компиляции (`MSB6001`). Скрипт сборки запускает MSBuild с одной переменной `Path`.

Постоянный путь для запуска: **`D:\3_Proga\Polden-OBS\latest\bin\64bit\obs64.exe`**. Внутреннее имя `obs64.exe` сохранено для совместимости OBS. После успешной Release-сборки `scripts/build-windows.py` вызывает `scripts/Publish-PoldenBuild.ps1`: тот обновляет ссылку `latest` на готовый каталог `build_polden_next/rundir/Release/` и ярлык **Polden OBS** в меню «Пуск». При сборке в другой каталог через `--build-dir` ссылка переключается на него. Если компиляция завершится ошибкой, ссылка останется на предыдущей рабочей версии. Запускать файл можно напрямую или через поиск Windows. Иконка exe и ярлыка — `frontend/cmake/windows/polden-obs.ico`.

Сборка Release завершена успешно на этой машине. Версия Polden задаётся в `POLDEN_VERSION` независимо от базы OBS. Проверить её можно через `latest/bin/64bit/obs64.exe --version` или свойства EXE; ярлык поиска Windows указывает на тот же exe. Проверка 05.10.2026: Release EXE собран; `--version` выводит `Polden OBS - 0.1.0 (OBS 32.2.2-6-ge2d9f82-modified)`, FileVersion и ProductVersion равны `0.1.0`. В работающем интерфейсе проверены панель, значок баг-репорта и открытие папки с готовыми MP4. В настройках отсутствуют элементы Premiere; порт 37941 не прослушивается. Оба архива 0.1.0 проверены: EXE совпадает с текущей сборкой, файлов Premiere и отладочных символов нет, общий архив не содержит личной конфигурации.

Для переносимого архива запустите `python scripts/package-windows.py`: он пакует текущий `build_polden_next/rundir/Release/` без отладочных `.pdb`, локального профиля `config/` и персонального плагина InfoWriter. Архив включает все остальные рабочие библиотеки, ресурсы, плагины и `portable_mode.txt`. Вариант `--include-local-settings` создаёт отдельный персональный ZIP только с выбранным профилем записи, коллекцией сцен, `user.ini` и InfoWriter из `latest`, исключая журналы, cookies и другие файлы конфигурации. Такой архив нельзя публиковать без проверки личных данных.

Прежний `build_x64` содержит старую версию и больше не является точкой запуска. Архив новой версии называется `dist/Polden-OBS-0.1.0-Windows-x64.zip`; персональный вариант имеет суффикс `-Personal`. Старые архивы сохранены с номером `0.0.1`. В сборке 0.1.0 нет расширения Premiere, его автоустановки или локального сервера связи.

Сборочные каталоги и `.deps/` не входят в Git. При обновлении OBS повторно проверить CMake, изменения док-панелей, метаданные и иконку.

Для публичного релиза при открытом Polden используется отдельный каталог `build_polden_release` с параметрами `--configure --no-publish`; это не заменяет файлы работающего приложения. Упаковка `python scripts/package-windows.py --build-dir build_polden_release` создаёт общий ZIP, манифест файлов и `.zip.sha256`. Проверка помощника: `python scripts/test-windows-updater.py`. [Порядок публикации и обновления](updating.md).
