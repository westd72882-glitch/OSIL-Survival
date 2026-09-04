#pragma once
// ==================== ПРОТОКОЛ МУЛЬТИПЛЕЕРА ====================
// Что клиент и сервер говорят друг другу. Тела запросов — JSON, потому что его видно
// глазами в браузере и в логах, а разбор уже есть в ядре (Core/Json.h).
//
// Обмен один-единственный: раз в 100 мс клиент шлёт своё состояние и накопившиеся
// правки мира, а в ответ получает всех остальных игроков и правки, которых у него ещё
// нет. Сервер авторитарен только для списка игроков и журнала правок: движение и
// добычу считает клиент. Для выживания вдвоём-впятером этого достаточно, а честный
// авторитарный сервер — отдельная большая работа (см. docs/network.md).
//
//   GET  /            — состояние сервера. Он же health-check для хостинга.
//   POST /join        — вход: имя игрока -> выдаётся id и сид мира.
//   POST /sync        — обмен состоянием (главный запрос игры).
//   POST /leave       — выход.
#include <string>
#include <vector>

namespace net {

// Что игрок делает прямо сейчас — по этому остальные рисуют его анимацию.
enum class Pose : int {
    Idle = 0,
    Walk,
    Run,
    Crouch,
    Swing,      // машет инструментом
    Build,      // ставит деталь дома
    Dead,
};

struct PlayerState {
    int   id = 0;
    std::string name;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    float speed = 0;          // м/с — по ней шагают ноги
    float swing = 0;          // фаза замаха 0..1 (0 — рука опущена)
    int   pose = (int)Pose::Idle;
    int   held = 0;           // ItemType числом: что в руках (топор, факел, ...)
    int   health = 100;
};

// Правка мира: поставленный или убранный блок. Блок передаётся числом Block.
struct Edit {
    int x = 0, y = 0, z = 0, block = 0;
    long long seq = 0;        // номер в журнале сервера
};

// ---- Сборка и разбор. Реализация в Protocol.cpp; SDL и GL здесь не нужны, поэтому
// один и тот же код собирается и в клиент, и в выделенный сервер.
std::string encodePlayer(const PlayerState& p);
std::string encodeSyncRequest(const PlayerState& me, const std::vector<Edit>& edits,
                              long long sinceSeq);
std::string encodeSyncResponse(const std::vector<PlayerState>& players,
                               const std::vector<Edit>& edits, long long headSeq,
                               float timeOfDay);

// Разбор. Возвращают false, если JSON битый (тогда выходные значения не трогаются).
bool decodeSyncRequest(const std::string& json, PlayerState& out,
                       std::vector<Edit>& edits, long long& sinceSeq);
bool decodeSyncResponse(const std::string& json, std::vector<PlayerState>& players,
                        std::vector<Edit>& edits, long long& headSeq, float& timeOfDay);

// Экранирование строки для JSON (имена игроков приходят от людей).
std::string jsonEscape(const std::string& text);

} // namespace net
