#include "World.h"
#include "../Core/Log.h"
#include "../Core/Time.h"

#include <cmath>

namespace {
// Соли: каждый слой шума получает свой сид, иначе высота, влажность и температура
// оказались бы одной и той же картинкой, и биомы легли бы кольцами вокруг гор.
const uint64_t SALT_CONTINENT   = 0x1001;
const uint64_t SALT_HILLS       = 0x1002;
const uint64_t SALT_MOUNTAIN    = 0x1003;
const uint64_t SALT_WARP        = 0x1004;
const uint64_t SALT_MOISTURE    = 0x1005;
const uint64_t SALT_TEMPERATURE = 0x1006;
const uint64_t SALT_RIVER       = 0x1007;
} // namespace

World::World(const WorldConfig& cfg) : cfg_(cfg) {
    cfg_.sanitize();
    gridSize_ = cfg_.gridSize();
    continentNoise_.reseed(splitMix64(cfg_.seed ^ SALT_CONTINENT));
    hillNoise_.reseed(splitMix64(cfg_.seed ^ SALT_HILLS));
    mountainNoise_.reseed(splitMix64(cfg_.seed ^ SALT_MOUNTAIN));
    warpNoise_.reseed(splitMix64(cfg_.seed ^ SALT_WARP));
    moistureNoise_.reseed(splitMix64(cfg_.seed ^ SALT_MOISTURE));
    temperatureNoise_.reseed(splitMix64(cfg_.seed ^ SALT_TEMPERATURE));
    riverNoise_.reseed(splitMix64(cfg_.seed ^ SALT_RIVER));
}

float World::edgeDistance(float x, float z) const {
    // Метрика намеренно смешанная. Чистый круг (евклидова) оставляет четыре огромных
    // угла бесполезного океана; чистый квадрат (максимум по осям) даёт видимую
    // прямоугольную рамку берега и залом рельефа по диагоналям — на карте предпросмотра
    // это читается как нарисованный квадрат. Смесь 55/45 даёт скруглённый квадрат: суша
    // занимает почти всю карту, а берег выглядит естественно.
    float half = cfg_.size * 0.5f;
    float nx = fabsf(x - half) / half; // 0 в центре, 1 у края
    float nz = fabsf(z - half) / half;
    float chebyshev = nx > nz ? nx : nz;
    float euclidean = sqrtf(nx*nx + nz*nz) * 0.70710678f; // нормировано: 1 в углу карты
    return chebyshev * 0.55f + euclidean * 0.45f;
}

float World::islandMask(float x, float z) const {
    // Спад к краям карты гарантирует океан по периметру: иначе база у самой границы
    // упиралась бы в невидимую стену, а не в берег.
    //
    // smoothstep, а не степенная функция от расстояния: у него нулевая производная на
    // обоих концах, поэтому в месте начала спада нет излома рельефа (он был бы виден как
    // складка, опоясывающая остров).
    float t = smoothstepf(cfg_.islandFalloffStart, 1.0f, edgeDistance(x, z));
    return powf(1.0f - t, cfg_.islandFalloffPower);
}

float World::riverCarve(float x, float z) const {
    if(!cfg_.riversEnabled) return 0.0f;
    // Русло — линия нулевого уровня шума: там, где |noise| близок к нулю, вырезаем канал.
    // Такой приём даёт извилистые непересекающиеся русла без моделирования стока воды.
    float n = riverNoise_.noise2(x * cfg_.riverFrequency, z * cfg_.riverFrequency);
    float d = fabsf(n);
    if(d > cfg_.riverWidth) return 0.0f;
    float t = 1.0f - (d / cfg_.riverWidth);     // 1 по центру русла
    return cfg_.riverDepth * t * t;             // квадрат даёт U-образный профиль берега
}

float World::generateHeightRaw(float x, float z) const {
    // Искажение координат: без него материк получается «клетчатым» — глаз считывает
    // сетку Перлина по прямым границам холмов.
    float wx = x, wz = z;
    warpNoise_.warp(wx, wz, cfg_.warpFrequency, cfg_.warpStrength);

    NoiseParams contP; contP.frequency = cfg_.continentFrequency; contP.octaves = 4;
    NoiseParams hillP; hillP.frequency = cfg_.hillFrequency;      hillP.octaves = 4; hillP.gain = 0.45f;
    NoiseParams mtnP;  mtnP.frequency  = cfg_.mountainFrequency;  mtnP.octaves  = 5; mtnP.gain = 0.5f;

    float continent = noiseTo01(continentNoise_.fbm(wx, wz, contP)); // 0..1 общая форма
    // Маску считаем по ИСКАЖЁННЫМ координатам: тогда береговая линия повторяет изгибы
    // рельефа и не выглядит геометрической фигурой. Но искажение может «вытолкнуть» сушу
    // к самой границе карты (на маленьких картах смещение сопоставимо с их размером),
    // поэтому поверх накладывается жёсткая гарантия по НЕискажённым координатам:
    // внешние проценты карты — всегда вода, при любом сиде и любом размере.
    float mask = islandMask(wx, wz);
    mask *= 1.0f - smoothstepf(0.90f, 0.99f, edgeDistance(x, z));
    // Смещение вниз до маски: край карты гарантированно уходит под воду.
    float base = continent * mask;

    // Горы растут только там, где материк и так высокий: иначе одинокие пики торчали бы
    // прямо из моря. smoothstep даёт мягкое подножие вместо ступеньки. Порог держим
    // заметно ниже максимума base (материк редко бывает «сплошь высоким»), иначе маска
    // почти всегда близка к нулю и хребты вырождаются в пологие бугры.
    float mountainMask = smoothstepf(0.45f, 0.75f, base);
    float mountains = mountainNoise_.ridged(wx, wz, mtnP) * mountainMask;

    float hills = noiseTo01(hillNoise_.fbm(wx, wz, hillP)) * 0.5f;

    // Итоговая нормированная высота. Веса подобраны так, чтобы:
    //   - равнины лежали чуть выше уровня воды (играбельная суша, а не болото);
    //   - горы доходили до 1.0 только в редких гребнях.
    float h01 = base * 0.66f + hills * 0.20f * mask + mountains * 0.62f;
    h01 = clampf(h01, 0.0f, 1.0f);

    float height = h01 * cfg_.maxHeight;
    // Русла режем после подъёма рельефа. Врезаем только в сушу: под водой канал всё равно
    // не виден, а на карте высот он читался бы как царапина через весь океан.
    float carve = riverCarve(x, z) * smoothstepf(cfg_.waterLevel - 2.0f, cfg_.waterLevel + 12.0f, height);
    height -= carve;
    return clampf(height, -6.0f, cfg_.maxHeight);
}

float World::generateMoistureRaw(float x, float z, float height) const {
    NoiseParams p; p.frequency = cfg_.moistureFrequency; p.octaves = 3;
    float raw = noiseTo01(moistureNoise_.fbm(x, z, p));
    // Растяжение контраста. Сумма октав фрактального шума тяготеет к середине: без
    // растяжки почти вся суша оказывается «средней влажности», и сухие биомы (пустыня)
    // не появляются вовсе — это ровно то, что показала первая версия генератора.
    float m = 0.5f + (raw - 0.5f) * 1.45f;

    float relief = clampf((height - cfg_.waterLevel) / (cfg_.maxHeight - cfg_.waterLevel), 0.0f, 1.0f);
    m -= relief * 0.20f;                    // в горах суше

    // Близость к воде добавляет влажности: так вдоль берегов и рек ложатся леса и болота.
    float aboveWater = height - cfg_.waterLevel;
    if(aboveWater < 30.0f) m += 0.22f * (1.0f - clampf(aboveWater / 30.0f, 0.0f, 1.0f));
    return clampf(m, 0.0f, 1.0f);
}

float World::generateTemperatureRaw(float x, float z, float height) const {
    NoiseParams p; p.frequency = cfg_.temperatureFrequency; p.octaves = 3;
    float raw = noiseTo01(temperatureNoise_.fbm(x, z, p));
    // Тот же приём, что и с влажностью: без растяжки контраста температура держится
    // около 0.5, и на карте не бывает ни настоящей жары, ни настоящего мороза.
    float t = 0.5f + (raw - 0.5f) * 1.55f;

    // Широтный градиент: «север» карты (z→0) холоднее «юга» (z→size). Без него
    // пустыни и снега перемешивались бы пятнами, и мир читался бы как случайный шум.
    float latitude = z / cfg_.size;
    t += (latitude - 0.5f) * 0.5f;

    // Высота: считаем от уровня воды, иначе низины у моря штрафовались бы наравне с горами.
    float relief = clampf((height - cfg_.waterLevel) / (cfg_.maxHeight - cfg_.waterLevel), 0.0f, 1.0f);
    t -= relief * 0.50f;
    return clampf(t, 0.0f, 1.0f);
}

void World::generate(){
    int64_t started = nowMillis();
    LOG_INFO("генерация мира: %s", cfg_.describe().c_str());

    const int n = gridSize_;
    heights_.assign((size_t)n * n, 0.0f);
    moisture_.assign((size_t)n * n, 0.0f);
    temperature_.assign((size_t)n * n, 0.0f);

    // Проход 1: высоты. Отдельным проходом, потому что влажность зависит от готовой
    // высоты (в том числе от того, ниже ли точка уровня воды).
    for(int j = 0; j < n; ++j){
        float z = (float)j * cfg_.heightGridStep;
        for(int i = 0; i < n; ++i){
            float x = (float)i * cfg_.heightGridStep;
            heights_[(size_t)j * n + i] = generateHeightRaw(x, z);
        }
        if((j % 128) == 0) LOG_DEBUG("высоты: %d%%", (int)(100.0f * j / (float)n));
    }

    // Проход 2: климат. Здесь же считаем статистику по биомам.
    int biomeCount[(int)Biome::COUNT] = {0};
    for(int j = 0; j < n; ++j){
        float z = (float)j * cfg_.heightGridStep;
        for(int i = 0; i < n; ++i){
            float x = (float)i * cfg_.heightGridStep;
            size_t idx = (size_t)j * n + i;
            float h = heights_[idx];
            float h01 = clampf(h / cfg_.maxHeight, 0.0f, 1.0f);

            moisture_[idx] = generateMoistureRaw(x, z, h);
            temperature_[idx] = generateTemperatureRaw(x, z, h);

            bool underwater = h < cfg_.waterLevel;
            bool beach = !underwater && h < cfg_.waterLevel + cfg_.beachBand;
            Biome b = classifyBiome(h01, moisture_[idx], temperature_[idx], underwater, beach);
            biomeCount[(int)b]++;
        }
    }

    float total = (float)((size_t)n * n);
    for(int i = 0; i < (int)Biome::COUNT; ++i) biomeFraction_[i] = (float)biomeCount[i] / total;

    generated_ = true;
    LOG_INFO("мир готов за %lld мс; суша %.1f%%, лес %.1f%%, равнина %.1f%%, пустыня %.1f%%, "
             "болото %.1f%%, снег %.1f%%, берег %.1f%%",
             (long long)(nowMillis() - started),
             (double)(100.0f * (1.0f - biomeFraction_[(int)Biome::Ocean])),
             (double)(100.0f * biomeFraction_[(int)Biome::Forest]),
             (double)(100.0f * biomeFraction_[(int)Biome::Grassland]),
             (double)(100.0f * biomeFraction_[(int)Biome::Desert]),
             (double)(100.0f * biomeFraction_[(int)Biome::Swamp]),
             (double)(100.0f * biomeFraction_[(int)Biome::Snow]),
             (double)(100.0f * biomeFraction_[(int)Biome::Beach]));
}

float World::sampleGrid(const std::vector<float>& grid, float x, float z) const {
    if(grid.empty()) return 0.0f;
    // Зажим координат к карте: запрос за границей — это упавшая в океан пуля или
    // вертолёт на подлёте, отвечать на него нужно краевым значением, а не мусором.
    float gx = clampf(x / cfg_.heightGridStep, 0.0f, (float)(gridSize_ - 1));
    float gz = clampf(z / cfg_.heightGridStep, 0.0f, (float)(gridSize_ - 1));

    int i0 = (int)gx, j0 = (int)gz;
    int i1 = i0 + 1 < gridSize_ ? i0 + 1 : i0;
    int j1 = j0 + 1 < gridSize_ ? j0 + 1 : j0;
    float tx = gx - (float)i0, tz = gz - (float)j0;

    float v00 = grid[(size_t)j0 * gridSize_ + i0];
    float v10 = grid[(size_t)j0 * gridSize_ + i1];
    float v01 = grid[(size_t)j1 * gridSize_ + i0];
    float v11 = grid[(size_t)j1 * gridSize_ + i1];
    return lerpf(lerpf(v00, v10, tx), lerpf(v01, v11, tx), tz);
}

float World::heightAt(float x, float z) const {
    // До генерации отвечаем прямым расчётом по шуму: так утилиты и тесты могут
    // спрашивать высоту, не строя всю сетку.
    if(!generated_) return generateHeightRaw(x, z);
    return sampleGrid(heights_, x, z);
}

Vec3 World::normalAt(float x, float z) const {
    // Центральные разности с шагом сетки: нормаль согласована с той же поверхностью,
    // по которой ходит игрок (важно, чтобы скольжение по склону совпадало с картинкой).
    float s = cfg_.heightGridStep;
    float hl = heightAt(x - s, z), hr = heightAt(x + s, z);
    float hd = heightAt(x, z - s), hu = heightAt(x, z + s);
    Vec3 n{ hl - hr, 2.0f * s, hd - hu };
    return v3norm(n);
}

float World::slopeAt(float x, float z) const {
    Vec3 n = normalAt(x, z);
    float cosAngle = clampf(n.y, -1.0f, 1.0f);
    return acosf(cosAngle) * 57.2957795f;
}

float World::moistureAt(float x, float z) const {
    if(!generated_) return generateMoistureRaw(x, z, heightAt(x, z));
    return sampleGrid(moisture_, x, z);
}

float World::temperatureAt(float x, float z) const {
    if(!generated_) return generateTemperatureRaw(x, z, heightAt(x, z));
    return sampleGrid(temperature_, x, z);
}

Biome World::biomeAt(float x, float z) const {
    float h = heightAt(x, z);
    float h01 = clampf(h / cfg_.maxHeight, 0.0f, 1.0f);
    bool underwater = h < cfg_.waterLevel;
    bool beach = !underwater && h < cfg_.waterLevel + cfg_.beachBand;
    return classifyBiome(h01, moistureAt(x, z), temperatureAt(x, z), underwater, beach);
}

WorldSample World::sampleAt(float x, float z) const {
    WorldSample s;
    s.height = heightAt(x, z);
    s.normal = normalAt(x, z);
    s.slopeDegrees = acosf(clampf(s.normal.y, -1.0f, 1.0f)) * 57.2957795f;
    s.moisture01 = moistureAt(x, z);
    s.temperature01 = temperatureAt(x, z);
    s.underwater = s.height < cfg_.waterLevel;
    bool beach = !s.underwater && s.height < cfg_.waterLevel + cfg_.beachBand;
    float h01 = clampf(s.height / cfg_.maxHeight, 0.0f, 1.0f);
    s.biome = classifyBiome(h01, s.moisture01, s.temperature01, s.underwater, beach);
    // Температура для метаболизма: базовая по биому минус поправка на высоту.
    float aboveWater = s.height - cfg_.waterLevel;
    s.ambientTempC = biomeInfo(s.biome).ambientTemp - clampf(aboveWater, 0.0f, 400.0f) * 0.0065f * 10.0f;
    return s;
}

bool World::inBounds(float x, float z) const {
    return x >= 0.0f && z >= 0.0f && x <= cfg_.size && z <= cfg_.size;
}

bool World::isWater(float x, float z) const {
    return heightAt(x, z) < cfg_.waterLevel;
}

bool World::isBuildable(float x, float z, float maxSlopeDegrees) const {
    if(!inBounds(x, z)) return false;
    if(isWater(x, z)) return false;
    return slopeAt(x, z) <= maxSlopeDegrees;
}

Vec3 World::findSpawnPoint(Rng& rng, int attempts) const {
    // Отбор с постепенным послаблением требований: сначала ищем действительно удобную
    // точку (пологий берег/равнина), а если карта неудачная — снижаем планку, но
    // никогда не отдаём точку в воде.
    float margin = cfg_.size * 0.06f; // не спавним вплотную к краю карты
    for(int pass = 0; pass < 3; ++pass){
        float maxSlope = 18.0f + (float)pass * 12.0f;
        float minAboveWater = 3.0f - (float)pass;
        for(int i = 0; i < attempts; ++i){
            float x = rng.nextRange(margin, cfg_.size - margin);
            float z = rng.nextRange(margin, cfg_.size - margin);
            float h = heightAt(x, z);
            if(h < cfg_.waterLevel + minAboveWater) continue;
            if(slopeAt(x, z) > maxSlope) continue;
            Biome b = biomeAt(x, z);
            // На снежных вершинах голый новичок замёрзнет за минуту — туда не спавним.
            if(pass == 0 && b == Biome::Snow) continue;
            return Vec3{ x, h, z };
        }
    }
    // Крайний случай (например, карта почти целиком океан) — центр карты.
    float c = cfg_.size * 0.5f;
    return Vec3{ c, heightAt(c, c), c };
}
