# Сборка и запуск

## Требования

- CMake 3.16+
- Компилятор C++17 (GCC 9+, Clang 10+, MSVC 2019+)
- Больше ничего: сервер, утилиты и тесты не зависят от внешних библиотек.
  SDL2 и OpenGL понадобятся только клиенту (этап 5).

## Сборка

```bash
git clone https://github.com/westd72882-glitch/OSIL-Survival.git
cd OSIL-Survival
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Результат — в `build/bin/`: `osil_server`, `osil_mapgen`, `osil_tests`.

## Тесты

```bash
./build/bin/osil_tests            # все тесты
./build/bin/osil_tests погода     # только тесты, в имени которых есть «погода»
ctest --test-dir build            # то же через CTest
```

## Запуск сервера

```bash
./build/bin/osil_server                                   # config/server.cfg
./build/bin/osil_server --config /etc/osil/server.cfg      # другой конфиг
./build/bin/osil_server +world.seed 12345 --server.maxplayers=50
```

Приоритет настроек: значения по умолчанию → файл конфигурации → аргументы командной строки.

### Команды консоли

| Команда | Что делает |
|---|---|
| `help` | список команд |
| `status` | состояние сервера: время, погода, тики, игроки |
| `world` | параметры мира и доли биомов |
| `time [часы]` | показать или задать время суток (`time 12`) |
| `weather [clear\|cloudy\|rain\|fog\|snow\|storm] [0..1]` | показать или задать погоду |
| `monuments` | список монументов с координатами и радиацией |
| `probe <x> <z>` | что в точке: высота, уклон, биом, климат, радиация, можно ли строить |
| `spawn` | подобрать безопасную точку возрождения |
| `config [ключ]` | показать конфигурацию |
| `players`, `kick`, `ban`, `unban`, `save` | зарегистрированы; заработают с сетевым слоем (этап 2) |
| `quit` / `stop` | остановить сервер (то же делает Ctrl+C) |

Под systemd stdin недоступен — управление на 2-м этапе идёт через RCON, который использует
тот же реестр команд.

### Пример unit-файла systemd

```ini
[Unit]
Description=OSIL Survival Dedicated Server
After=network.target

[Service]
Type=simple
User=osil
WorkingDirectory=/opt/osil
ExecStart=/opt/osil/bin/osil_server --config /opt/osil/config/server.cfg
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## Предпросмотр карты

```bash
./build/bin/osil_mapgen +world.seed 12345 --out build/map --meters-per-pixel 4
```

- `build/map_biomes.png` — биомы с рельефной подсветкой; окружности — монументы
  (красные — радиационные), крест — центр;
- `build/map_height.png` — карта высот (чёрное — уровень моря, белое — максимум);
- `build/map_report.txt` — доли биомов, количество ресурсов по видам, монументы, примеры
  точек возрождения.

Подбор сида для вайпа: прогнать несколько сидов с `--meters-per-pixel 8` (быстро),
посмотреть отчёты и картинки, выбранный сид записать в `world.seed`.

## Настройка сервера

Все параметры — в `config/server.cfg` с комментариями. Ключевые:

| Ключ | Смысл |
|---|---|
| `server.maxplayers` | до 500; 100 — целевая нагрузка |
| `server.tickrate` | 30 — рабочее значение; ниже стрельба «ватная», выше растёт нагрузка |
| `server.loglevel` | `trace`/`debug`/`info`/`warn`/`error` |
| `server.logfile` | дублировать лог в файл |
| `world.*` | генерация мира, см. `docs/WORLDGEN.md` |
| `raid.*` | рейд-часы (окно, когда работает взрывчатка) — этап 4 |

## Диагностика

| Симптом | Причина и что делать |
|---|---|
| `конфиг не найден` | запуск не из корня репозитория — укажите `--config` |
| `сервер не успевает: отброшено шагов` | машина не тянет тикрейт: снизьте `server.tickrate` или число игроков |
| Карта почти целиком океан | слишком высокий `world.waterlevel` относительно `world.maxheight` |
| Мир генерируется дольше секунды | уменьшите `world.gridstep`… наоборот, увеличьте его (шаг 4 → 8 ускоряет вчетверо) |
| Мало ресурсов | `world.resourcedensity` (1.0 — базовая плотность, 2.0 — вдвое больше) |
