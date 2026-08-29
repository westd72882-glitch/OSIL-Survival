#include "Biome.h"

namespace {
// Таблица держится строго в порядке enum Biome — доступ по индексу без поиска.
const BiomeInfo kBiomes[(int)Biome::COUNT] = {
    // id          имя           R    G    B   деревья камни кусты  руда  сера  t°C  жажда
    { "ocean",     "Океан",      40,  78, 120, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  14.0f, 1.0f },
    { "beach",     "Побережье", 214, 199, 143, 0.05f, 0.20f, 0.15f, 0.02f, 0.02f,  22.0f, 1.15f },
    { "grass",     "Равнина",   116, 158,  86, 0.35f, 0.55f, 1.00f, 0.10f, 0.05f,  18.0f, 1.0f },
    { "forest",    "Лес",        62, 108,  60, 1.00f, 0.45f, 0.70f, 0.12f, 0.05f,  15.0f, 0.95f },
    { "desert",    "Пустыня",   206, 180, 110, 0.06f, 0.70f, 0.10f, 0.15f, 0.30f,  34.0f, 1.6f },
    { "swamp",     "Болото",     78,  92,  62, 0.75f, 0.25f, 0.90f, 0.08f, 0.10f,  16.0f, 0.85f },
    { "snow",      "Снежные горы", 228, 232, 238, 0.20f, 1.00f, 0.05f, 0.25f, 0.12f, -8.0f, 0.8f },
};
} // namespace

const BiomeInfo& biomeInfo(Biome b){
    int i = (int)b;
    if(i < 0 || i >= (int)Biome::COUNT) i = 0;
    return kBiomes[i];
}

const char* biomeName(Biome b){ return biomeInfo(b).nameRu; }

Biome classifyBiome(float height01, float moisture01, float temperature01,
                    bool underwater, bool beach){
    // 1. Вода и берег решаются высотой — никакая влажность их не перебивает.
    if(underwater) return Biome::Ocean;
    if(beach)      return Biome::Beach;

    // 2. Высокогорье: выше снеговой линии всегда снег. Линия немного «плавает» от
    //    температуры — на жарком юге снег начинается выше, чем на холодном севере.
    float snowLine = 0.54f + temperature01 * 0.28f;
    if(height01 > snowLine) return Biome::Snow;
    // Холодный север карты остаётся снежным и на равнине: без этого правила снег был бы
    // исключительно «высотным» и не образовывал бы целого края карты, как того требует ТЗ.
    if(temperature01 < 0.13f) return Biome::Snow;

    // 3. Низины с высокой влажностью — болото. Ограничение по высоте обязательно:
    //    иначе «болота» появлялись бы на склонах гор, где вода не стоит.
    if(height01 < 0.20f && moisture01 > 0.62f) return Biome::Swamp;

    // 4. Жарко и сухо — пустыня.
    if(temperature01 > 0.58f && moisture01 < 0.45f) return Biome::Desert;

    // 5. Влажно — лес, иначе равнина. Порог слегка зависит от температуры:
    //    в прохладном климате лес держится и при меньшей влажности.
    float forestThreshold = 0.44f + (temperature01 - 0.5f) * 0.14f;
    if(moisture01 > forestThreshold) return Biome::Forest;
    return Biome::Grassland;
}
