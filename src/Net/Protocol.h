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
#include "../Core/Json.h"

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

// Событие мира: то, что происходит РАЗОВО и не сводится к правке блока. Через него
// разъезжаются выброшенные предметы, падение деревьев, взрывы и удары по игрокам.
// Формат нарочно плоский: пять чисел на всё, зато разбор дешёвый и на телефоне.
enum class EventType : int {
    Drop = 0,     // предмет брошен на землю: id — метка дропа, a — предмет, b — сколько
    Pickup,       // дроп подобрали: id — метка дропа
    TreeFell,     // дерево срублено: xyz — его комель, остальное считает каждый сам
    Explosion,    // взрыв: xyz — центр, b — максимальный урон
    Hit,          // удар по игроку: id — кого ударили, b — сколько сняли
};

struct Event {
    int   type = 0;
    int   id = 0;             // дроп или игрок — смотря по типу
    int   a = 0, b = 0;       // предмет/количество или урон
    float x = 0, y = 0, z = 0;
    long long seq = 0;        // номер в журнале сервера
};

// ---- Сборка и разбор. Реализация в Protocol.cpp; SDL и GL здесь не нужны, поэтому
// один и тот же код собирается и в клиент, и в выделенный сервер.
std::string encodePlayer(const PlayerState& p);
// Список событий отдельной строкой: сервер отдаёт им же состояние мира вошедшему.
std::string encodeEvents(const std::vector<Event>& events);
void decodeEvents(const JsonValue& arr, std::vector<Event>& out);
std::string encodeSyncRequest(const PlayerState& me, const std::vector<Edit>& edits,
                              const std::vector<Event>& events,
                              long long sinceSeq, long long sinceEventSeq);
std::string encodeSyncResponse(const std::vector<PlayerState>& players,
                               const std::vector<Edit>& edits,
                               const std::vector<Event>& events,
                               long long headSeq, long long eventHeadSeq,
                               float timeOfDay);

// Разбор. Возвращают false, если JSON битый (тогда выходные значения не трогаются).
bool decodeSyncRequest(const std::string& json, PlayerState& out,
                       std::vector<Edit>& edits, std::vector<Event>& events,
                       long long& sinceSeq, long long& sinceEventSeq);
bool decodeSyncResponse(const std::string& json, std::vector<PlayerState>& players,
                        std::vector<Edit>& edits, std::vector<Event>& events,
                        long long& headSeq, long long& eventHeadSeq, float& timeOfDay);

// Экранирование строки для JSON (имена игроков приходят от людей).
std::string jsonEscape(const std::string& text);

} // namespace net
