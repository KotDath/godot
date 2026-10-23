# Экспорт в Aurora OS из редактора на ПК

В меню **Project → Export → Add…** доступна отдельная платформа **Aurora OS**.
Первый проверенный хост — Linux с установленным Aurora SDK. Экспортёр встроен
в редактор; исходники движка и отдельный `package.py` рядом с редактором не нужны.
Сам Python/SCons bootstrap нужен для компиляции движка, а для упаковки достаточно
штатного Python 3.8 из SDK.

## Настройка пресета

- `binary_format/architecture`: `arm64` или `arm32`.
- `custom_template/debug`, `custom_template/release`: собранные Aurora ELF
  для соответствующего режима и архитектуры. Если поля пусты, используются
  установленные шаблоны `auroraos_debug.arm64`, `auroraos_release.arm64`,
  `auroraos_debug.arm32`, `auroraos_release.arm32`.
- `package/unique_name`: идентификатор вида `org.example.game`.
- `package/name`, `version`, `release`, `license`: метаданные RPM.
- `package/icon`: PNG/SVG; по умолчанию берётся иконка проекта.
- `package/permissions`: разрешения Regular-профиля через точку с запятой.
- `sdk/sfdk`: путь к `sfdk` или имя команды в PATH.
- `sdk/target`: установленный SDK target с суффиксом `aarch64` или `armv7hl`,
  совпадающим с архитектурой. Версия SDK не зашита в экспортёр.
- `sdk/python`: `python3` внутри SDK.
- `sdk/patchelf`: доступный внутри Build Engine host executable версии ≥0.18.
  Встроенный в проверенный SDK patchelf 0.11 портит build-id; ссылка и SHA256
  проверенной версии указаны в [README.md](README.md).

Шаблоны, проект, библиотеки расширений и каталог результата должны быть доступны
в shared workspace SDK. Обычно это домашний каталог разработчика. Экспортёр
создаёт временный каталог рядом с результатом и удаляет его после завершения.

## Что делает экспорт

1. Штатный Godot `save_pack` экспортирует ресурсы, сцены, скрипты и собирает
   `SharedObject` из GDExtension и export plugins.
2. Через `sfdk engine exec` выбирается SB2 target. Упаковщик проверяет ELF-класс,
   архитектуру, ARM32 hard-float ABI, SONAME и транзитивные DT_NEEDED.
3. PCK встраивается после stripping в ELF; стандартный запрет подмены пути
   проекта у export templates сохраняется. RPM stripping не удаляет PCK.
4. Каждая private SO получает собственный `DT_RUNPATH=$ORIGIN`. Файлы размещаются
   в `/usr/share/<app-id>/lib`; главный ELF — в `/usr/bin/<app-id>` с требуемым
   валидатором `DT_RPATH`. Системные RPM-зависимости сохраняются.
5. Выполняется `rpm-validator -p regular`. Ошибка прекращает экспорт. Замечания
   показываются в журнале, включая ожидаемое предупреждение non-Silica.

Результат — **неподписанный RPM**. Ключи не хранятся в пресете. Подпишите пакет
подходящим Regular-сертификатом отдельным шагом. Запуск/установка одной кнопкой
и remote-debug launch flags пока не реализованы.

Headless-экспорт использует тот же код:

```sh
godot --headless --path /path/to/project --export-debug 'Aurora arm64' /path/to/game.rpm
godot --headless --path /path/to/project --export-release 'Aurora arm64' /path/to/game.rpm
```

## GDExtension

В `.gdextension` используйте теги `auroraos.arm32` / `auroraos.arm64` и перечислите
private dependencies в соответствующих секциях. Пример есть в ветке godot-cpp:
`examples/aurora_dependency_chain/project/probe.gdextension`.

Для работы расширения в самом ПК-редакторе также нужна его Linux x86_64 сборка
с тегом `linux.x86_64`. Экспортёр выберет ARM-файлы по пресету, не упакует
хостовую библиотеку. Проверенный пример содержит обе версии.

Private SO сейчас размещаются в одном каталоге. Одинаковые basenames,
несовпадающий SONAME, RUNPATH за пределами `$ORIGIN` и отсутствующие зависимости
приводят к ошибке. Библиотеки с деревом plugin-подкаталогов требуют отдельной
адаптации; автоматически копировать произвольные SDK-библиотеки экспортёр
не пытается. Исключение — libxkbcommon с лицензией: она нужна Wayland-бэкенду,
но не входит в Regular public API.
