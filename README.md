# UdpGame — UDP-клиент и сервер игровых команд (C++)

Практическая работа №1: разработка UDP-протокола и проектирование архитектуры.
Двусторонний обмен: клиент отправляет `MOVEMENT` / `SHOOT` (+ `LOGIN`),
сервер проверяет CRC32, авторизует, обрабатывает и возвращает состояние.
Системные сокеты: Berkeley Sockets (Linux) / Winsock2 (Windows).

## Состав

* `src/common/protocol.h/.cpp` — заголовок 9 байт
  (`type u8 | seq u16 BE | size u16 BE | crc32 u32 BE`), CRC32 IEEE,
  сериализация `MOVEMENT(float x,y,z)`, `SHOOT(weaponId)`, `LOGIN`,
  ответов `STATE_UPDATE` / `SHOT_RESULT` / `AUTH_*` / `PACKET_ERROR`.
* `src/server/server.cpp` — `socket → bind → recvfrom`-цикл, лог всех команд
  (stdout + `server.log`), авторизация по списку, ответы `sendto`.
* `src/client/client.cpp` — `LOGIN`, затем команды раз в секунду
  (настраивается), ожидание ответа `select()`, печать результата.
* `docs/Protocol_Specification.md` — формат пакетов, поля, примеры.
* `docs/Architecture_Design.md` — топология Dedicated Server, обязанности, схема.

## Требования

* Windows: MinGW-w64 (UCRT) + CMake 3.16+ (проверено: GCC 16.1, CMake 4.4).
  Статическая линковка — `.exe` работают без DLL в PATH.
* Linux: `g++` + `cmake` (сокеты BSD, `ws2_32` не нужен).

## Сборка (Windows)

```bat
build.bat
rem или вручную:
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Результат: `build/udp_server.exe`, `build/udp_client.exe`.

## Сборка (Linux)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Запуск

```sh
# Терминал 1 — сервер (порт по умолчанию 54000):
./build/udp_server        # или: ./build/udp_server 54000

# Терминал 2 — клиент:
./build/udp_client 127.0.0.1 54000 player1 qwerty 10 1000
#               ip         port  login   pass   count interval_ms
```

Логины для доп. задания: `player1/qwerty`, `admin/admin123`, `guest/guest`.

## Пример сессии

Клиент:

```text
UDP client -> 127.0.0.1:54000 login=player1 count=6 interval=300ms
[0] -> LOGIN
  <- AUTH_OK seq=0 size=19 ack=0 code=0 msg='welcome player1'
[1] -> MOVEMENT x=1 y=0.5 z=0
  <- STATE_UPDATE seq=1 size=14 ack=1 new_pos=(1,0.5,0)
[2] -> SHOOT weaponId=0
  <- SHOT_RESULT seq=2 size=4 ack=2 weapon=0 hit=1
```

Сервер (`server.log`):

```text
[2026-09-27 22:02:26] UDP server listening on 0.0.0.0:54000
[2026-09-27 22:02:26] 127.0.0.1:53608 <- LOGIN seq=0 size=15 crc=OK login='player1'
[2026-09-27 22:02:26] 127.0.0.1:53608 AUTH OK (player1)
[2026-09-27 22:02:26] 127.0.0.1:53608 <- MOVEMENT seq=1 size=12 crc=OK x=1.000000 y=0.500000 z=0.000000
[2026-09-27 22:02:26] 127.0.0.1:53608 -> STATE_UPDATE seq=1 pos=(1.000000,0.500000,0.000000)
```

## Как это закрывает требования

* Системные сокеты — да (`socket/bind/recvfrom/sendto`, `htons/htonl`-эквиваленты вручную).
* Заголовок (тип, seq, размер) + payload — да, плюс `Checksum` CRC32 (доп. задание).
* Два типа команд `MOVEMENT`, `SHOOT` — да (+ `LOGIN`/авторизация, доп. задание).
* Лог сервера — да (консоль + `server.log`).
* Периодическая отправка + вывод ответа — да (`interval_ms`, по умолчанию 1000).
* Сериализация (порядок байт, выравнивание) — ручная BE-упаковка, без `struct`-кастов.
* Документация — `docs/`, архитектура — Dedicated Server, схема классов.
