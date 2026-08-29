# Схема базы данных (проект, реализуется на этапе 2)

СУБД — **SQLite** в режиме WAL. Почему не PostgreSQL: сервер игры пишет часто и мелко,
читает почти только при старте, и всё это — с одной машины. Встроенная база убирает
сетевой круг, отдельный процесс и целый класс проблем с развёртыванием. PostgreSQL
понадобится, если появится общий для нескольких серверов профиль игрока (кланы между
серверами, единый банлист) — схема ниже переносится один в один.

**Что НЕ хранится:** карта высот, биомы, деревья, камни, монументы. Всё это
восстанавливается из сида (`world.seed`) за полсекунды. В базе — только отличия мира от
процедурного состояния и то, что игроки создали сами.

## Таблицы

```sql
-- ---------- Мир ----------
CREATE TABLE world_state (
    id              INTEGER PRIMARY KEY CHECK (id = 1),  -- строка ровно одна
    seed            INTEGER NOT NULL,
    size            REAL    NOT NULL,
    world_seconds   REAL    NOT NULL,   -- время мира: сутки и погода восстанавливаются из него
    wipe_time       INTEGER NOT NULL,   -- unix-время последнего вайпа
    save_version    INTEGER NOT NULL    -- версия схемы для миграций
);

-- ---------- Игроки ----------
CREATE TABLE players (
    steam_id        INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL,
    pos_x           REAL, pos_y REAL, pos_z REAL,
    yaw             REAL,
    health          REAL NOT NULL DEFAULT 100,
    hunger          REAL NOT NULL DEFAULT 100,
    thirst          REAL NOT NULL DEFAULT 100,
    radiation       REAL NOT NULL DEFAULT 0,
    temperature     REAL NOT NULL DEFAULT 20,
    is_dead         INTEGER NOT NULL DEFAULT 0,
    sleeping_bag_id INTEGER,            -- точка возрождения
    clan_id         INTEGER REFERENCES clans(id) ON DELETE SET NULL,
    first_join      INTEGER NOT NULL,
    last_seen       INTEGER NOT NULL,
    play_seconds    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_players_clan ON players(clan_id);

-- ---------- Предметы ----------
-- Одна таблица на все контейнеры: инвентарь игрока, ящик, печь, труп, мешок с лутом.
-- container_type + container_id образуют «адрес» контейнера — так добавление нового вида
-- хранилища не требует ни новой таблицы, ни миграции.
CREATE TABLE items (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    container_type  INTEGER NOT NULL,   -- 0 игрок, 1 одежда, 2 быстрые слоты, 3 ящик, 4 печь, 5 труп, 6 мешок
    container_id    INTEGER NOT NULL,
    slot            INTEGER NOT NULL,
    item_id         TEXT    NOT NULL,   -- "wood", "rifle_ak", "ammo_rifle"
    amount          INTEGER NOT NULL,
    condition       REAL    NOT NULL DEFAULT 1.0,  -- износ 0..1
    ammo            INTEGER NOT NULL DEFAULT 0,    -- патронов в магазине
    attachments     TEXT,                          -- JSON: прицел, глушитель, фонарь
    UNIQUE (container_type, container_id, slot)
);
CREATE INDEX idx_items_container ON items(container_type, container_id);

-- ---------- Постройки ----------
CREATE TABLE structures (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    block_type      INTEGER NOT NULL,   -- фундамент, стена, дверь, крыша, лестница...
    grade           INTEGER NOT NULL,   -- 0 твиг, 1 дерево, 2 камень, 3 металл, 4 армированный
    pos_x           REAL NOT NULL, pos_y REAL NOT NULL, pos_z REAL NOT NULL,
    rotation        REAL NOT NULL,
    health          REAL NOT NULL,
    owner_id        INTEGER REFERENCES players(steam_id) ON DELETE SET NULL,
    privilege_id    INTEGER REFERENCES building_privileges(id) ON DELETE SET NULL,
    created_at      INTEGER NOT NULL,
    decay_at        INTEGER             -- когда начнёт разрушаться без обслуживания
);
-- Пространственный индекс «на бедного»: постройки грузятся квадратами 100 м.
CREATE INDEX idx_structures_cell ON structures(CAST(pos_x/100 AS INT), CAST(pos_z/100 AS INT));

CREATE TABLE building_privileges (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    pos_x           REAL NOT NULL, pos_y REAL NOT NULL, pos_z REAL NOT NULL,
    radius          REAL NOT NULL DEFAULT 15,
    owner_id        INTEGER NOT NULL REFERENCES players(steam_id) ON DELETE CASCADE
);
CREATE TABLE privilege_authorized (
    privilege_id    INTEGER NOT NULL REFERENCES building_privileges(id) ON DELETE CASCADE,
    steam_id        INTEGER NOT NULL,
    PRIMARY KEY (privilege_id, steam_id)
);

CREATE TABLE locks (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    structure_id    INTEGER NOT NULL REFERENCES structures(id) ON DELETE CASCADE,
    lock_type       INTEGER NOT NULL,   -- 0 замок с ключом, 1 кодовый
    code            TEXT,               -- хранится как хеш, а не открытым текстом
    guest_code      TEXT
);

-- ---------- Мир: изменённые объекты ----------
-- Процедурные узлы ресурсов в базе не лежат; сюда попадают только выработанные —
-- чтобы после перезапуска срубленное дерево не «воскресло» раньше времени.
CREATE TABLE depleted_nodes (
    node_id         INTEGER PRIMARY KEY,  -- идентификатор из ResourceMap
    respawn_at      INTEGER NOT NULL
);

CREATE TABLE containers (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    kind            INTEGER NOT NULL,   -- ящик, печь, верстак, спальник, турель
    pos_x           REAL NOT NULL, pos_y REAL NOT NULL, pos_z REAL NOT NULL,
    rotation        REAL NOT NULL,
    owner_id        INTEGER,
    health          REAL NOT NULL,
    state           TEXT                -- JSON: горит ли печь, топливо, прогресс
);

-- ---------- Социальное и администрирование ----------
CREATE TABLE clans (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT NOT NULL UNIQUE,
    leader_id       INTEGER NOT NULL,
    created_at      INTEGER NOT NULL
);
CREATE TABLE clan_members (
    clan_id         INTEGER NOT NULL REFERENCES clans(id) ON DELETE CASCADE,
    steam_id        INTEGER NOT NULL,
    role            INTEGER NOT NULL DEFAULT 0,  -- 0 участник, 1 офицер, 2 лидер
    PRIMARY KEY (clan_id, steam_id)
);

CREATE TABLE bans (
    steam_id        INTEGER PRIMARY KEY,
    name            TEXT,
    reason          TEXT,
    banned_by       TEXT,
    banned_at       INTEGER NOT NULL,
    expires_at      INTEGER            -- NULL — навсегда
);

CREATE TABLE research_unlocks (   -- изученные рецепты (стол исследований)
    steam_id        INTEGER NOT NULL,
    item_id         TEXT NOT NULL,
    PRIMARY KEY (steam_id, item_id)
);
```

## Правила работы

- **Транзакции.** Один тик сохранения — одна транзакция. Половинчатая запись инвентаря
  (предмет исчез из ящика, но не появился у игрока) недопустима.
- **Атомарность файла.** Снимок пишется во временный файл и переименовывается: сервер,
  убитый посреди сохранения, не оставит битую базу.
- **Фоновая запись.** Сохранение идёт в отдельном потоке по снимку состояния, чтобы тик не
  проседал на диске.
- **Миграции.** `save_version` растёт при каждом изменении схемы; при несовпадении сервер
  выполняет миграцию и делает резервную копию.
- **Вайп.** Смена `world.seed` = новый мир: таблицы построек, предметов, контейнеров
  очищаются, банлист и кланы (по настройке) сохраняются.
