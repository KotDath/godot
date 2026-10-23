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
   Экспортёр передаёт SHA256 PCK, вычисленный на ПК; несовпадение внутри SDK
   прекращает упаковку (защита от усечённых чтений shared mount).
4. Каждая private SO получает собственный `DT_RUNPATH=$ORIGIN`. Файлы размещаются
   в `/usr/share/<app-id>/lib`; главный ELF — в `/usr/bin/<app-id>` с требуемым
   валидатором `DT_RPATH`. Системные RPM-зависимости сохраняются.
5. Выполняется `rpm-validator -p regular`. Ошибка прекращает экспорт. Замечания
   показываются в журнале, включая ожидаемое предупреждение non-Silica.

По умолчанию результат — **неподписанный RPM**. Опция `signing/enabled` включает
подпись нового пакета Regular-сертификатом. Пути `export/auroraos/signing_key` и
`export/auroraos/signing_certificate` задаются в **Editor Settings**, содержимое
ключей в пресет не попадает. Подписывается временный пакет без `--force`;
существующий выходной RPM заменяется только после успешного завершения.
Зашифрованные ключи пока требуют отдельной интерактивной подписи: синхронный
экспортёр отклоняет их, не запрашивает и не сохраняет пароль. Проверка доверия
корневому CA автоматически не выполняется.
Ручной запуск debug-бинарника с `--remote-debug` уже проверен на Квадре:
[инструкция и границы проверки](debugger_smoke/README.md).

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

## Запуск через Aurora SDK

Включите `remote_deploy/enabled` и `signing/enabled` в runnable-пресете.
В **Editor Settings → Export → AuroraOS → Device** укажите точное имя устройства
из `sfdk device list`, предварительно зарегистрированного в Aurora SDK/Qt Creator.
Устройство и ключи — локальная настройка компьютера, не часть проекта.

Меню **Remote Deploy → Run on Aurora SDK device** выполняет:

1. Проверку подключения и архитектуры через `sfdk device exec ... uname -m`.
2. Экспорт debug RPM во временном подкаталоге `.godot/aurora-deploy/`, Regular-валидацию и подпись.
3. Остановку предыдущего процесса, запущенного этой сессией редактора.
4. `sfdk -c device=... deploy --sdk --keepUserData` для установки/обновления.
5. Запуск установленного ELF через SDK с результатом `gen_export_flags()`.

При Remote Deploy release получает суффикс `.godot.<timestamp>`: Aurora отвергает
переустановку одинаковой версии/release. Пресет и обычный экспорт не меняются.
Временный пакет удаляется после окончания операции. SDK получает полное RPM-имя
`<имя>-<версия>-<релиз>.<архитектура>.rpm`; произвольное `game.rpm` он не принимает.

Отладчику нужен доступный с устройства адрес ПК в `network/debug/remote_host`.
Параметры отладчика передаются при запуске, не записываются в RPM. Remote filesystem
и localhost forwarding пока отклоняются. Обычный экспорт с ненулевыми launch flags
также отклоняется: для запуска используется отдельный путь Remote Deploy.

Пункт **Stop Aurora project** отправляет SIGTERM только сохранённому PID после
проверки времени старта ядром. Он не удаляет пакет и пользовательские данные.
Лог приложения: `~/.cache/godot-aurora/<app-id>.log` на устройстве. Состояние процесса
хранится в текущей сессии редактора; после перезапуска редактора оно не восстанавливается.

Это запуск для разработки через SDK, а не проверка контекста штатного лаунчера
и sandbox. SDK может показать диалог установки на устройстве. Операции SDK пока
синхронные; отмена доступна между стадиями, а не внутри выполняемой команды.
Ни audb, ни собственная SSH-конфигурация экспортёру не требуются.
