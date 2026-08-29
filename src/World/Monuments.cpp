#include "Monuments.h"
#include "../Core/Log.h"

#include <cmath>
#include <cstdio>

namespace {
const uint64_t SALT_MONUMENTS = 0x3001;

const MonumentInfo kMonuments[(int)MonumentType::COUNT] = {
    // id            имя                радиус радиация тир уклон берег горы
    { "military",    "Военный лагерь",   90.f,  9.0f,   3,  9.0f,  false, false },
    { "airfield",    "Аэродром",        150.f,  5.0f,   3,  6.0f,  false, false },
    { "radtown",     "Радтаун",          80.f,  6.0f,   2, 12.0f,  false, false },
    { "gas_station", "Заправка",         45.f,  1.5f,   1, 10.0f,  false, false },
    { "warehouse",   "Склад",            55.f,  0.0f,   1, 10.0f,  false, false },
    { "lighthouse",  "Маяк",             40.f,  0.0f,   1, 16.0f,  true,  false },
    { "quarry",      "Карьер",           70.f,  0.0f,   2, 20.0f,  false, true  },
    { "cave",        "Пещера",           35.f,  3.0f,   2, 45.0f,  false, true  },
};

// Порядок расстановки: сначала крупные и требовательные к площадке, потом мелочь.
// Иначе заправки занимают все ровные места, и аэродром некуда ставить.
const MonumentType kOrder[] = {
    MonumentType::Airfield, MonumentType::MilitaryBase, MonumentType::RadTown,
    MonumentType::Quarry,   MonumentType::Cave,         MonumentType::Warehouse,
    MonumentType::GasStation, MonumentType::Lighthouse,
};
const int kOrderCount = (int)(sizeof(kOrder)/sizeof(kOrder[0]));
} // namespace

const MonumentInfo& monumentInfo(MonumentType type){
    int i = (int)type;
    if(i < 0 || i >= (int)MonumentType::COUNT) i = 0;
    return kMonuments[i];
}

bool MonumentMap::placeOne(MonumentType type, int index, Rng& rng){
    const MonumentInfo& info = monumentInfo(type);
    const WorldConfig& cfg = world_.config();
    float margin = info.radius + cfg.size * 0.03f;

    const int kCandidates = 600;
    float bestScore = -1e9f;
    Vec3 bestPos{};
    bool found = false;

    for(int i = 0; i < kCandidates; ++i){
        float x = rng.nextRange(margin, cfg.size - margin);
        float z = rng.nextRange(margin, cfg.size - margin);

        WorldSample s = world_.sampleAt(x, z);
        if(s.underwater) continue;
        if(s.height < cfg.waterLevel + 1.5f) continue;

        // Соседи: расстояние считаем между границами зон, а не между центрами —
        // иначе большой аэродром «наезжает» на маленькую заправку.
        bool tooClose = false;
        for(const Monument& m : monuments_){
            float d = horizontalDist(m.pos, Vec3{x, s.height, z});
            if(d < cfg.monumentMinSpacing + m.radius + info.radius){ tooClose = true; break; }
        }
        if(tooClose) continue;

        // Площадка: проверяем уклон не только в центре, но и по кольцу — центр может
        // оказаться ровной полкой посреди обрыва.
        float worstSlope = s.slopeDegrees;
        bool ringOk = true;
        for(int a = 0; a < 8; ++a){
            float ang = (float)a * 0.7853982f;
            float px = x + cosf(ang) * info.radius * 0.7f;
            float pz = z + sinf(ang) * info.radius * 0.7f;
            if(world_.isWater(px, pz) && !info.wantsCoast){ ringOk = false; break; }
            float sl = world_.slopeAt(px, pz);
            if(sl > worstSlope) worstSlope = sl;
        }
        if(!ringOk) continue;
        if(worstSlope > info.maxSlope) continue;

        // Оценка кандидата: чем ровнее, тем лучше; плюс бонусы за «характер» монумента.
        float score = -worstSlope;
        if(info.wantsCoast){
            // Маяк тем лучше, чем ближе к воде: ищем воду в радиусе 120 м.
            float bestWater = 1e9f;
            for(int a = 0; a < 12; ++a){
                float ang = (float)a * 0.5235988f;
                for(float r = 20.0f; r <= 120.0f; r += 20.0f){
                    if(world_.isWater(x + cosf(ang)*r, z + sinf(ang)*r)){
                        if(r < bestWater) bestWater = r;
                        break;
                    }
                }
            }
            if(bestWater > 500.0f) continue;      // воды рядом нет — не берег
            score += (200.0f - bestWater) * 0.5f;
        }
        if(info.wantsMountain){
            // Карьеры и пещеры тянутся вверх: премия за высоту над уровнем моря.
            score += (s.height - cfg.waterLevel) * 0.8f;
        } else {
            // Остальным нужен «средний» рельеф — слишком высоко неудобно бегать.
            float h01 = clampf((s.height - cfg.waterLevel) / (cfg.maxHeight - cfg.waterLevel), 0.f, 1.f);
            score -= fabsf(h01 - 0.28f) * 60.0f;
        }

        if(score > bestScore){
            bestScore = score;
            bestPos = Vec3{ x, s.height, z };
            found = true;
        }
    }

    if(!found) return false;

    Monument m;
    m.type = type;
    m.pos = bestPos;
    m.radius = info.radius;
    m.radiation = info.radiation;
    m.lootTier = info.lootTier;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s №%d", info.nameRu, index + 1);
    m.name = buf;
    monuments_.push_back(m);
    return true;
}

void MonumentMap::generate(){
    monuments_.clear();
    const WorldConfig& cfg = world_.config();
    Rng rng(splitMix64(cfg.seed ^ SALT_MONUMENTS));

    // Раскладываем запрошенное количество по кругу типов: получится примерно поровну,
    // но крупные монументы (в начале списка) точно попадут в мир.
    int perType[(int)MonumentType::COUNT] = {0};
    int placed = 0, failures = 0;
    for(int i = 0; i < cfg.monumentCount; ++i){
        MonumentType type = kOrder[i % kOrderCount];
        if(placeOne(type, perType[(int)type], rng)){
            perType[(int)type]++;
            ++placed;
        } else {
            ++failures;
        }
    }

    LOG_INFO("монументы: расставлено %d из %d (%d мест не нашлось — карта тесная или гористая)",
             placed, cfg.monumentCount, failures);
    for(const Monument& m : monuments_){
        LOG_DEBUG("  %s: (%.0f, %.0f), радиус %.0f м, радиация %.1f", m.name.c_str(),
                  (double)m.pos.x, (double)m.pos.z, (double)m.radius, (double)m.radiation);
    }
}

float MonumentMap::radiationAt(float x, float z) const {
    float total = 0.0f;
    for(const Monument& m : monuments_){
        if(m.radiation <= 0.0f) continue;
        float dx = m.pos.x - x, dz = m.pos.z - z;
        float d2 = dx*dx + dz*dz;
        float r2 = m.radius * m.radius;
        if(d2 >= r2) continue;
        float t = 1.0f - sqrtf(d2) / m.radius; // 0 на границе, 1 в центре
        total += m.radiation * t * t;          // квадрат: «горячее» ядро, мягкий край
    }
    return total;
}

const Monument* MonumentMap::nearest(float x, float z, float* outDistance) const {
    const Monument* best = nullptr;
    float bestD = 1e9f;
    for(const Monument& m : monuments_){
        float d = horizontalDist(m.pos, Vec3{x, m.pos.y, z});
        if(d < bestD){ bestD = d; best = &m; }
    }
    if(outDistance) *outDistance = best ? bestD : 0.0f;
    return best;
}

bool MonumentMap::isSafeSpawn(float x, float z, float safetyMargin) const {
    for(const Monument& m : monuments_){
        if(m.radiation <= 0.0f) continue;
        if(horizontalDist(m.pos, Vec3{x, m.pos.y, z}) < m.radius + safetyMargin) return false;
    }
    return true;
}
