# MP4 после записи

**Вывод: возможно.** Но OBS 32.2.2 уже умеет Hybrid MP4: после штатного завершения записи файл оформлен как обычный MP4, хотя во время записи используется фрагментация. Поэтому FFmpeg нужен только если downstream требует физического перепаковывания или исходная запись осталась фрагментированной после сбоя. OBS также умеет автоматический remux MKV в MP4 (`Video/AutoRemux`).

Для принудительного нефрагментированного MP4 без повторного кодирования:

```powershell
ffmpeg -i input.mp4 -map 0 -c copy -movflags +faststart output.mp4
```

MP4 muxer FFmpeg без `frag_*` создаёт обычный индекс; `+faststart` переносит индекс к началу файла. Потоки копируются, качество не меняется. Нужно проверить совместимость кодеков с MP4; если исходник MKV содержит неподдерживаемый поток, операция может завершиться ошибкой.

В OBS точка окончания записи — `frontend/widgets/OBSBasic_Recording.cpp::RecordingStop`, событие `OBS_FRONTEND_EVENT_RECORDING_STOPPED`; путь лежит в `outputHandler->lastRecordingPath`. Есть также `RecordingFileChanged` для разделённых файлов. В будущей реализации запускать FFmpeg в отдельном процессе после закрытия файла, выводить прогресс/ошибку в Polden, писать сначала во временный файл, проверять результат и только затем переименовывать. Не заменять исходник до успешной проверки. Учесть одновременные записи и повторные события.

Источники: [OBS Hybrid MP4](https://obsproject.com/kb/hybrid-mp4), [OBS Recording Guide](https://obsproject.com/kb/standard-recording-output-guide), [FFmpeg MOV/MP4 muxer](https://ffmpeg.org/ffmpeg-formats.html#mov_002c-mp4_002c-ismv).
