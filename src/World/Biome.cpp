#include "Biome.h"

namespace {
// Таблица держится строго в порядке enum Biome — доступ по индексу без поиска.
const BiomeInfo kBiomes[(int)Biome::COUNT] = {
    // id          имя          R    G    B   деревья камни кусты  руда  сера  t°C  жажда
    { "ocean",     "Океан",      40,  78, 120, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  14.0f, 1.0f },
    { "beach",     "Побережье", 214, 199, 143, 0.05f, 0.20f, 0.15f, 0.02f, 0.02f,  22.0f, 1.15f },
    { "grass",     "Равнина",   116, 158,  86, 0.45f, 0.50f, 0.90f, 0.10f, 0.05f,  18.0f, 1.0f },
    { "desert",    "Пустыня",   206, 180, 110, 0.06f, 0.70f, 0.10f, 0.15f, 0.30f,  34.0f, 1.6f },
    { "snow",      "Зима",      228, 232, 238, 0.30f, 0.90f, 0.05f, 0.25f, 0.12f,  -8.0f, 0.8f },
};
} // namespace

const BiomeInfo& biomeInfo(Biome b){
    int i = (int)b;
    if(i < 0 || i >= (int)Biome::COUNT) i = 0;
    return kBiomes[i];
}

const char* biomeName(Biome b){ return biomeInfo(b).nameRu; }

Biome classifyBiome(float height01, float zone01, bool underwater, bool beach){
    // 1. Вода и берег решаются высотой: никакая зона их не перебивает.
    if(underwater) return Biome::Ocean;
    if(beach)      return Biome::Beach;

    // 2. Высокогорье везде снежное — снеговая линия одна на всю карту.
    if(height01 > 0.62f) return Biome::Snow;

    // 3. Зоны. Границы узкие и жёсткие: игрок должен видеть, что перешёл в пустыню,
    //    а не гадать, стоит ли он ещё на равнине.
    if(zone01 < 0.34f) return Biome::Desert;
    if(zone01 > 0.66f) return Biome::Snow;
    return Biome::Grassland;
}
