# Color Picker

## Изображение

![image](https://github.com/user-attachments/assets/58ebea11-a9e7-4e7b-86f5-197a07ae882d) <br> Рисунок 1. Интерфейс программы

## Описание

**Color Picker** — это приложение для Windows, которое позволяет захватывать и отображать цвет пикселей на экране с помощью графического интерфейса. Программа поддерживает запуск и остановку сканирования цвета, а также интеграцию с системным треем для удобного доступа.

## Функции

- Захват цвета пикселей на экране и отображение его информации.
- Запуск и остановка сканирования с помощью горячих клавиш.
- Интеграция с системным треем для удобного управления приложением.

## Сборка

Для сборки проекта вам понадобится установленный [GCC](https://gcc.gnu.org/) и [MinGW](https://www.mingw-w64.org/downloads/), если вы работаете на Windows.

1. **Установите необходимые инструменты** (GCC и MinGW).

2. **Создайте исполняемый файл**:

   Откройте командную строку и перейдите в каталог, содержащий ваш исходный код и `Makefile`. Выполните команду:

   ```bash
   make
   ```

   Это соберет проект и создаст исполняемый файл `color_picker.exe`.

## Использование

1. **Запустите приложение** `color_picker.exe`.

2. **Нажмите клавишу `P`**, чтобы начать или остановить захват цвета.
   - Клавишами **WASD** и **стрелками** можно двигать курсор на позицию 1 пикселя за одно нажатие, чтобы можно было точнее новодиться курсором по нужному месту.

3. **Используйте иконку в системном трее** для:
   - Восстановления окна приложения (правый клик на иконке и выберите "Show").
   - Закрытия приложения (правый клик на иконке и выберите "Exit").

4. **Чтобы фокусироваться на окне приложения**, кликните на него перед использованием горячих клавиш для корректной работы.

## Очистка

Для очистки собранных файлов выполните команду:

```bash
make clean
```

Это удалит объектные файлы и исполняемый файл.

## Примечания

- Убедитесь, что приложение имеет фокус для правильной работы горячих клавиш.
- При возникновении проблем с компиляцией или запуском, проверьте наличие всех необходимых зависимостей.

## Изменения версий

- **v1.0**: Первая версия программы с базовыми функциями захвата цвета. Написана на `C#`.
- **v2.0.0**: Вторая версия программы работающая быстрее и переписанная на `Си` и `WinApi`.
- **v2.1.0**: Перекрашен интерфейс программы на темный цвет и улучшен дизайн элементов.
- **v2.2.0**: Добавлен прицел для лупы, чтобы было понятно из какого пикселя берутся цвета. Так же сделана обработка клавиш **WASD** и **стрелок** чтобы можно было двигать курсор на позицию 1 пикселя за одно нажатие, чтобы можно было точнее новодиться курсором по нужному месту

## Лицензия

Этот проект является открытым и свободно распространяемым по лицензии **MIT**. Вы можете использовать и модифицировать его в соответствии с вашими потребностями.

---

Разработано [Tailogs](https://github.com/tailogs).