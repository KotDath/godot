# Godot 4.7.2 для Aurora OS

Экспериментальная ветка `4.7.2_auroraos`: отдельная платформа
`platform=auroraos`, ARM32/ARM64, редактор и игровые шаблоны, GDExtension,
экспорт RPM из настольного Linux-редактора через Aurora SDK.

- [Экспорт из редактора на ПК](misc/aurora/EXPORT.md)
- [Сборка и bootstrap Python/SCons](misc/aurora/README.md)
- [Что действительно проверено и что осталось](misc/aurora/STATUS.md)
- [Архитектурный план](misc/aurora/PLAN.md)
- [Адаптация godot-cpp и пример цепочки SO](https://github.com/KotDath/godot-cpp/tree/4.7.2_auroraos/examples/aurora_dependency_chain)

На Квадре ARM64 проверены установка RPM, графический запуск игры через
GLES/Mali, открытие проекта в редакторе и загрузка GDExtension с транзитивными
зависимостями. ARM32 проверяется сборкой и RPM-валидатором; аппаратного теста
пока нет. Виртуальная клавиатура/IME и полный мобильный жизненный цикл ещё
не завершены. Это не готовый production-релиз.
