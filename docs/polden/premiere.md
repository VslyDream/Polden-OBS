# Связь с Premiere Pro 25.6.2

Первая реализация состоит из локального сервера в Polden OBS (`PoldenPipeline.cpp`, `127.0.0.1:37941`) и UXP панели `premiere/polden-bridge`. OBS отправляет только готовый MP4 после копирования в LucidLink. Панель Premiere проверяет активный проект, открывает `Footage.prproj`, создаёт bins `1_VIDEO/{дата}`, импортирует файл и сохраняет проект. Кнопка таймлайна берёт активную секвенцию в проекте выбранной Production, вставляет медиа в её конец на V1/A1 и сохраняет этот проект. `.prproj` на диске напрямую не редактируется.

## Подключение

1. Собрать или распаковать Polden OBS и запустить его. Создать проект игры во вкладке **Polden**, указать корень `L:\<игра>`, проверить автоматически найденные папку видео и `Footage.prproj`, выбрать источник и окно «Захват игры».
2. В настройках Polden выбрать `ffmpeg.exe`. Скопировать ключ связи кнопкой **Copy**.
3. Установить `premiere/Polden-OBS-Bridge-premierepro.ccx` из архива сборки через Adobe Creative Cloud (двойной щелчок по файлу) и открыть **Window → UXP Plugins → Polden OBS Bridge**. Для разработки можно включить **Developer Mode** в настройках Plugins, перезапустить Premiere и загрузить папку `premiere/polden-bridge` через **UXP Developer Tool 2.2+**.
4. Вставить ключ, нажать **Подключить** и оставить панель открытой. В Premiere открыть Production выбранной игры и активировать любой её проект. Для вставки на таймлайн также активировать нужную секвенцию.

При отсутствии связи с панелью Premiere уровень 3 либо оставляет импорт в очереди, либо показывает ошибку с кнопкой повтора — это выбирается в настройках Polden. Панель опрашивает локальный сервер раз в 2,5 секунды. Ключ хранится локально в настройках OBS и панели Premiere; соединение принимает только localhost.

## Поведение и ограничения первой версии

- Путь активного проекта Premiere должен находиться внутри папки Production, содержащей указанный `00_Footage/…Footage.prproj`. Так предотвращается импорт при активной Production другой игры. Для Premiere 25.6 нет официального UXP метода получения самой Production; проверка основана на путях проектов.
- Если `Footage.prproj` заблокирован другим участником Production или открыт только для чтения, импорт/сохранение может завершиться ошибкой. Задание сохраняется, в OBS появляется ошибка и кнопка повтора.
- Импорт распознаёт ранее добавленный файл по полному пути медиа в целевом bin. Подтверждённые команды также сохраняются по ID задания в локальном хранилище панели. После потери подтверждения во время вставки на таймлайн возможно повторное добавление; до испытания на рабочей Production используйте кнопку на копии секвенции.
- Для автоматической работы панель UXP должна быть загружена, открыта и связана ключом. На машине пока не проверены установка `.ccx`, импорт в реальную Production и вставка в реальную секвенцию. Компиляция OBS не подтверждает эти действия.

## Основание API

Premiere 25.6 предоставляет [Project.open, getActiveProject, importFiles, save и getActiveSequence](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/project), [FolderItem.createBinAction и getItems](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/folderitem), [Sequence.getEndTime](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/sequence) и [SequenceEditor.createInsertProjectItemAction](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/sequenceeditor). Adobe описывает [установку `.ccx`](https://developer.adobe.com/premiere-pro/uxp/plugins/distribution/install/) и [UXP Developer Tool](https://developer.adobe.com/premiere-pro/uxp/plugins/).
