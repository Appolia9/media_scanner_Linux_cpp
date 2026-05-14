# Media Scanner

Утилита для Linux, которая рекурсивно обходит указанный каталог, находит мультимедийные файлы (аудио, видео, изображения) и формирует результат в формате JSON.

## Функциональность

- Рекурсивное сканирование каталога
- Поддержка аудио (mp3, wav, flac, aac, ogg, wma, m4a, opus)
- Поддержка видео (mp4, mkv, avi, mov, wmv, flv, webm, mpg, mpeg, m4v)
- Поддержка изображений (jpg, jpeg, png, gif, bmp, tiff, webp, svg, ico)
- Два режима работы: запись в файл или HTTP-сервер
- Настраиваемый интервал сканирования
- Настраиваемый путь к каталогу
- Корректная обработка сигналов SIGINT и SIGTERM

## Требования

- Операционная система Linux (или WSL2 для Windows)
- Компилятор C++17 (GCC 8+ или Clang 7+)
- CMake 3.16 или новее
- Make

## Установка зависимостей

### Ubuntu / Debian
```
sudo apt update
sudo apt install -y build-essential cmake
```

## Сборка
```git clone https://github.com/Appolia9/media_scanner_Linux_cpp.git
cd media_scanner_Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

После сборки исполняемый файл находится в `build/media_scanner_Linux`.

## Использование

### Синтаксис
`./build/media_scanner_Linux [Опции]`

### Опции

| Опция | Описание | Значение по умолчанию |
|-------|----------|----------------------|
| -d <путь> | Каталог для сканирования | $HOME |
| -i <секунды> | Интервал между сканированиями | 60 |
| -o <файл> | Выходной JSON-файл | $HOME/.media_files |
| --http | Включить HTTP-режим вместо записи в файл | false |
| -p <порт> | Порт HTTP-сервера (требует --http) | 1234 |
| -h | Показать справку | - |

### Примеры запуска

**Режим записи в файл:**
```cd build
./media_scanner_Linux -d /home/user/Music -i 30
./media_scanner_Linux -d /home/user/Videos -o /home/user/result.json
```

**HTTP-режим:**
```cd build
./media_scanner_Linux --http
./media_scanner_Linux --http -d /home/user/Media -i 15 -p 8080
```

**Получение данных в HTTP-режиме:**
```
curl http://localhost:1234/media_files
```

### Остановка программы

Нажмите Ctrl+C для корректного завершения. Программа перехватывает сигналы SIGINT и SIGTERM.

## Пример вывода

```json
{
  "audio": ["song.mp3", "podcast.wav"],
  "video": ["movie.mkv"],
  "images": ["photo.jpg", "screenshot.png"]
}
```
## Примечания

- Программа разработана для Linux. Для использования в Windows рекомендуется WSL2.
- При сканировании пропускаются каталоги, к которым нет прав доступа.
- HTTP-сервер поддерживает CORS заголовок Access-Control-Allow-Origin: *.
