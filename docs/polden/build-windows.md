# Сборка Polden OBS для Windows

База: официальный тег OBS Studio `32.2.2` от 14.08.2026. `upstream` указывает на `https://github.com/obsproject/obs-studio.git`; локальная ветка — `polden-obs`.

## Среда

- Visual Studio Community 2022 17.14 с C++ toolchain и Windows SDK 10.0.26100.0.
- CMake 3.31.6; генератор `Visual Studio 17 2022` (штатный preset тега требует VS 2026, поэтому здесь выбран генератор установленной VS 2022).
- Зависимости, Qt и CEF CMake получает из официальных архивов по хешам из `CMakePresets.json` в `.deps/`.
- Подмодули: `git submodule update --init --recursive`.

Конфигурация:

```powershell
& 'T:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build_x64 -G 'Visual Studio 17 2022' -A 'x64,version=10.0.26100.0' -DVIRTUALCAM_GUID='A3FCE0F5-3493-419F-958A-ABA1250EC20B' -DENABLE_BROWSER=ON
```

Сборка:

```powershell
python scripts/build-windows.py --config Release --jobs 4
```

В среде Codex встречаются одновременно `Path` и `PATH`, что ломает MSBuild до компиляции (`MSB6001`). Скрипт сборки запускает MSBuild с одной переменной `Path`.

Результат ожидается в `build_x64/rundir/Release/`, исполняемый файл — `bin/64bit/obs64.exe` (внутреннее имя оставлено для совместимости OBS). Для поиска по имени **Polden OBS** создаётся ярлык меню «Пуск» скриптом `scripts/Install-PoldenShortcut.ps1` после сборки. Иконка exe и ярлыка — `frontend/cmake/windows/polden-obs.ico`.

Сборочные каталоги и `.deps/` не входят в Git. При обновлении OBS повторно проверить CMake, изменения док-панелей, метаданные и иконку.
