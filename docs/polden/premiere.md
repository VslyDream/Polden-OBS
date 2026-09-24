# Автоимпорт в Premiere Productions

**Вывод: реализуемо при установленном расширении внутри Premiere.** OBS сам по себе не имеет поддерживаемого API для редактирования открытого проекта Premiere. Надёжная схема: OBS завершает запись/ремультиплексирование и отправляет путь локальному расширению Premiere; расширение проверяет активный проект и секвенцию, импортирует файл и вставляет клип на выбранную дорожку.

## Версии

- Premiere **25.6+**: официальный UXP API предоставляет `Project.getActiveProject()`, `Project.importFiles()`, `Project.getActiveSequence()` и `SequenceEditor.createInsertProjectItemAction()`. Операцию вставки следует выполнить через `Project.executeTransaction()`.
- Premiere **25.0–25.5**: использовать CEP/ExtendScript; официальный пример Adobe PProPanel показывает `app.project.importFiles()` и вставку в активную секвенцию.
- Прямой объект `PRProduction.getActiveProduction()` в UXP отмечен Adobe как доступный **с 26.2**. В 25.x можно работать с активным проектом, входящим в Production, но принадлежность Production и статус блокировки нужно проверять отдельно. Нельзя менять `.prproj` на диске за спиной Premiere.

## Условия и открытые решения

1. Если Premiere не запущен, нет активной Production, проекта или секвенции — оставить файл в очереди и показать состояние в OBS.
2. Проект Production должен быть открыт на запись. Проект в режиме read-only не изменять.
3. Задать целевые bin, секвенцию, видео/аудио дорожки и позицию: конец секвенции или playhead. Это продуктовые настройки; текущие кнопки Polden остаются заглушками.
4. После `importFiles()` найти созданный ProjectItem по нормализованному пути, дождаться окончания импорта и вставить один раз; использовать идентификатор задания для защиты от дублей.
5. Проверять на установленной версии Premiere 25.x. API 25.6 нельзя обещать для более ранних 25.x, а API Production 26.2 нельзя использовать в 25.x.

Источники: [Adobe Project API](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/project), [Adobe SequenceEditor API](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/sequenceeditor), [Adobe PRProduction API](https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/prproduction), [Adobe CEP PProPanel](https://github.com/Adobe-CEP/Samples/tree/master/PProPanel), [Adobe Production locking](https://helpx.adobe.com/premiere/desktop/collaborate-with-others/collaborate-using-productions/change-project-lock-status-in-production.html).
