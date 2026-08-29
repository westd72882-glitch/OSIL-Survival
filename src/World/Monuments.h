#pragma once
// ==================== МОНУМЕНТЫ И РАДИАЦИЯ ====================
// По ТЗ на карте есть точки интереса с лутом и радиацией: военные базы, аэродром,
// «часовня» (радтаун), заправки, пещеры с оксидными зонами. Здесь решается их
// РАССТАНОВКА и радиационное поле; внутреннее наполнение (ящики, бочки, NPC) — это
// 3-4 этапы, а сюда закладывается ровно то, от чего зависит остальной мир:
//   - место (ровная площадка нужного размера, не в воде, не впритык к соседу);
//   - радиус и уровень радиации (влияет на спавн игроков и на требования к костюму);
//   - уровень лута (tier) — какие ящики там будут разрешены.
//
// Алгоритм расстановки — «лучший из N кандидатов»: для каждого монумента бросаем
// несколько сотен случайных точек, отбрасываем негодные (вода, уклон, близкий сосед),
// оставшиеся оцениваем и берём лучшую. Это стабильнее честного пуассоновского диска:
// расстановка гарантированно завершается на любой карте, даже почти целиком гористой.
#include "World.h"

#include <string>
#include <vector>

enum class MonumentType : uint8_t {
    MilitaryBase = 0, // военный лагерь — сильная радиация, лут 3 уровня
    Airfield,         // аэродром
    RadTown,          // заброшенный посёлок/часовня
    GasStation,       // заправка
    Warehouse,        // склад
    Lighthouse,       // маяк на побережье
    Quarry,           // карьер: руда и сера
    Cave,             // пещера с оксидной зоной
    COUNT
};

struct MonumentInfo {
    const char* id;
    const char* nameRu;
    float radius;         // радиус зоны, метры
    float radiation;      // рад/сек в эпицентре (0 — чистый монумент)
    int   lootTier;       // 1..3 — какие ящики разрешены внутри
    float maxSlope;       // требование к площадке
    bool  wantsCoast;     // ставится у воды (маяк)
    bool  wantsMountain;  // ставится в горах (пещера, карьер)
};

const MonumentInfo& monumentInfo(MonumentType type);

struct Monument {
    MonumentType type = MonumentType::RadTown;
    std::string name;    // человекочитаемое имя с номером ("Военный лагерь №2")
    Vec3 pos{};
    float radius = 60.0f;
    float radiation = 0.0f;
    int lootTier = 1;
};

class MonumentMap {
public:
    explicit MonumentMap(const World& world) : world_(world) {}

    void generate();

    const std::vector<Monument>& monuments() const { return monuments_; }

    // Суммарная радиация в точке (рад/сек). Спад — квадратичный от границы к центру:
    // на краю зоны 0, в эпицентре — полное значение. Пороговые значения и защита
    // костюмом считаются в системе персонажа (3-й этап).
    float radiationAt(float x, float z) const;

    // Ближайший монумент и расстояние до него (nullptr, если монументов нет).
    const Monument* nearest(float x, float z, float* outDistance = nullptr) const;

    // Безопасна ли точка для спавна игрока: вне радиационных зон с запасом.
    bool isSafeSpawn(float x, float z, float safetyMargin = 40.0f) const;

private:
    bool placeOne(MonumentType type, int index, Rng& rng);

    const World& world_;
    std::vector<Monument> monuments_;
};
