# MP4 после записи

**Вывод: возможно.** Но OBS 32.2.2 уже умеет Hybrid MP4: после штатного завершения записи файл оформлен как обычный MP4, хотя во время записи используется фрагментация. Поэтому FFmpeg нужен только если downstream требует физического перепаковывания или исходная запись осталась фрагментированной после сбоя. OBS также умеет автоматический remux MKV в MP4 (`Video/AutoRemux`).

Для принудительного нефрагментированного MP4 без повторного кодирования:

```powershell
ffmpeg -i input.mp4 -map 0 -c copy -movflags +faststart output.mp4
```

MP4 muxer FFmpeg без `frag_*` создаёт обычный индекс; `+faststart` переносит индекс к началу файла. Потоки копируются, качество не меняется. Нужно проверить совместимость кодеков с MP4; если исходник MKV содержит неподдерживаемый поток, операция может завершиться ошибкой.

В Polden OBS этап реализован в `frontend/widgets/PoldenPipeline.cpp`. По завершении записи путь берётся из `outputHandler->lastRecordingPath`; `RecordingFileChanged` учитывает сегменты. FFmpeg запускается отдельным процессом с `-map 0 -c copy -movflags +faststart -f mp4`, пишет временный файл в локальную папку `Polden Converted/<ID игры>/`, затем готовый файл переименовывается. Исходник остаётся на месте. Ошибка видна в панели и допускает повтор. Короткая проба с синтетическим фрагментированным MP4 прошла: в результате нет атома `moof`. Полный цикл записи в интерфейсе ещё не проверен.

Источники: [OBS Hybrid MP4](https://obsproject.com/kb/hybrid-mp4), [OBS Recording Guide](https://obsproject.com/kb/standard-recording-output-guide), [FFmpeg MOV/MP4 muxer](https://ffmpeg.org/ffmpeg-formats.html#mov_002c-mp4_002c-ismv).
