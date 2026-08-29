// ==================== osil_mapgen — ПРЕДПРОСМОТР МИРА ====================
// Утилита генерирует мир по тем же правилам, что и сервер, и выкладывает результат в
// картинки и текстовый отчёт. Зачем это нужно отдельным инструментом:
//   - подобрать сид для вайпа, не поднимая сервер и не заходя в игру;
//   - глазами проверить, что генератор не «сломался» после правки констант (нет карты
//     из сплошного океана, нет гор из угла в угол, монументы не слиплись);
//   - получить эталон для тестов: карта из одного и того же сида обязана совпадать.
//
// Пример:
//   ./osil_mapgen +world.seed osil +world.size 4000 --out build/map --meters-per-pixel 4
#include "../../Core/PngWriter.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Core/Text.h"
#include "../../World/Environment.h"
#include "../../World/Monuments.h"
#include "../../World/Resources.h"
#include "../../World/World.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Затенение рельефа: скалярное произведение нормали и направления на «солнце».
// Без него карта биомов читается как плоская заливка, и горы неотличимы от равнины.
float hillshade(const World& world, float x, float z){
    Vec3 n = world.normalAt(x, z);
    Vec3 sun = v3norm(Vec3{ -0.55f, 0.72f, -0.42f });
    float d = n.x*sun.x + n.y*sun.y + n.z*sun.z;
    return clampf(0.45f + d * 0.75f, 0.25f, 1.35f);
}

void putPixel(std::vector<uint8_t>& img, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b){
    if(x < 0 || y < 0 || x >= w || y >= h) return;
    size_t i = ((size_t)y * w + x) * 3;
    img[i] = r; img[i+1] = g; img[i+2] = b;
}

void drawCircle(std::vector<uint8_t>& img, int w, int h, int cx, int cy, int radius,
                uint8_t r, uint8_t g, uint8_t b){
    // Окружность по «средней точке» — рисуем восемь симметричных точек за шаг.
    int x = radius, y = 0, err = 0;
    while(x >= y){
        putPixel(img, w, h, cx + x, cy + y, r, g, b);
        putPixel(img, w, h, cx + y, cy + x, r, g, b);
        putPixel(img, w, h, cx - y, cy + x, r, g, b);
        putPixel(img, w, h, cx - x, cy + y, r, g, b);
        putPixel(img, w, h, cx - x, cy - y, r, g, b);
        putPixel(img, w, h, cx - y, cy - x, r, g, b);
        putPixel(img, w, h, cx + y, cy - x, r, g, b);
        putPixel(img, w, h, cx + x, cy - y, r, g, b);
        y += 1;
        if(err <= 0) err += 2*y + 1;
        if(err > 0){ x -= 1; err -= 2*x + 1; }
    }
}

void drawCross(std::vector<uint8_t>& img, int w, int h, int cx, int cy, int size,
               uint8_t r, uint8_t g, uint8_t b){
    for(int i = -size; i <= size; ++i){
        putPixel(img, w, h, cx + i, cy, r, g, b);
        putPixel(img, w, h, cx, cy + i, r, g, b);
    }
}

} // namespace

int main(int argc, char** argv){
    Config cfg;
    // Утилита читает тот же конфиг, что и сервер: сид и размер карты обязаны совпадать
    // с боевыми, иначе предпросмотр показывает не тот мир, который увидят игроки.
    cfg.loadFile("config/server.cfg");
    cfg.applyArgs(argc, argv);
    logSetLevel(logLevelFromString(cfg.getString("server.loglevel", "info")));

    std::string outPrefix = cfg.getString("out", "map");
    float metersPerPixel = cfg.getFloat("meters-per-pixel", 4.0f);
    if(metersPerPixel < 1.0f) metersPerPixel = 1.0f;

    WorldConfig wcfg = WorldConfig::fromConfig(cfg);
    World world(wcfg);
    world.generate();

    ResourceMap resources(world);
    resources.generate();
    MonumentMap monuments(world);
    monuments.generate();

    const int w = (int)(wcfg.size / metersPerPixel);
    const int h = w;
    LOG_INFO("рисую карту %dx%d пикселей (%.1f м/пиксель)", w, h, (double)metersPerPixel);

    std::vector<uint8_t> biomeImg((size_t)w * h * 3, 0);
    std::vector<uint8_t> heightImg((size_t)w * h * 3, 0);

    for(int py = 0; py < h; ++py){
        float z = ((float)py + 0.5f) * metersPerPixel;
        for(int px = 0; px < w; ++px){
            float x = ((float)px + 0.5f) * metersPerPixel;
            WorldSample s = world.sampleAt(x, z);

            // --- Карта биомов
            const BiomeInfo& bi = biomeInfo(s.biome);
            float shade = s.underwater ? 1.0f : hillshade(world, x, z);
            float rr = bi.r * shade, gg = bi.g * shade, bb = bi.b * shade;
            if(s.underwater){
                // Глубину показываем затемнением: мелководье светлее, ямы темнее.
                float depth = clampf((wcfg.waterLevel - s.height) / 40.0f, 0.0f, 1.0f);
                rr = lerpf(90.0f, 18.0f, depth);
                gg = lerpf(140.0f, 40.0f, depth);
                bb = lerpf(180.0f, 82.0f, depth);
            }
            putPixel(biomeImg, w, h, px, py,
                     (uint8_t)clampf(rr, 0, 255), (uint8_t)clampf(gg, 0, 255), (uint8_t)clampf(bb, 0, 255));

            // --- Карта высот (чёрное — уровень моря, белое — максимум)
            float t = clampf(s.height / wcfg.maxHeight, 0.0f, 1.0f);
            uint8_t v = (uint8_t)(t * 255.0f);
            putPixel(heightImg, w, h, px, py, v, v, v);
        }
    }

    // Монументы поверх карты биомов: круг по радиусу зоны + крест в центре.
    for(const Monument& m : monuments.monuments()){
        int cx = (int)(m.pos.x / metersPerPixel);
        int cy = (int)(m.pos.z / metersPerPixel);
        int rad = (int)(m.radius / metersPerPixel);
        bool hot = m.radiation > 0.0f;
        drawCircle(biomeImg, w, h, cx, cy, rad, hot ? 255 : 40, hot ? 60 : 40, hot ? 60 : 40);
        drawCross(biomeImg, w, h, cx, cy, 4, 255, 255, 255);
    }

    std::string biomePath  = outPrefix + "_biomes.png";
    std::string heightPath = outPrefix + "_height.png";
    if(!writePng(biomePath, w, h, biomeImg)){ LOG_ERROR("не удалось записать %s", biomePath.c_str()); return 1; }
    if(!writePng(heightPath, w, h, heightImg)){ LOG_ERROR("не удалось записать %s", heightPath.c_str()); return 1; }

    // ---- Текстовый отчёт: то, что нужно администратору при выборе сида.
    std::string reportPath = outPrefix + "_report.txt";
    std::ofstream rep(reportPath);
    const float* bf = world.biomeFractions();
    rep << "OSIL Survival — отчёт по карте\n";
    rep << "================================\n";
    rep << wcfg.describe() << "\n\n";
    rep << "Биомы (доля клеток сетки высот):\n";
    for(int i = 0; i < (int)Biome::COUNT; ++i){
        char line[128];
        snprintf(line, sizeof(line), "  %s %6.2f%%\n",
                 padRightUtf8(biomeInfo((Biome)i).nameRu, 16).c_str(), (double)(bf[i] * 100.0f));
        rep << line;
    }

    rep << "\nРесурсы (всего объектов: " << resources.nodes().size() << "):\n";
    for(int i = 0; i < (int)ResourceKind::COUNT; ++i){
        char line[128];
        snprintf(line, sizeof(line), "  %s %8zu\n",
                 padRightUtf8(resourceInfo((ResourceKind)i).nameRu, 16).c_str(),
                 resources.countOf((ResourceKind)i));
        rep << line;
    }

    rep << "\nМонументы (" << monuments.monuments().size() << "):\n";
    for(const Monument& m : monuments.monuments()){
        char line[192];
        snprintf(line, sizeof(line), "  %s (%5.0f, %5.0f)  радиус %3.0f м  радиация %.1f  лут %d\n",
                 padRightUtf8(m.name, 20).c_str(), (double)m.pos.x, (double)m.pos.z,
                 (double)m.radius, (double)m.radiation, m.lootTier);
        rep << line;
    }

    // Несколько точек возрождения — проверка, что новичку есть где появиться:
    // если карта почти целиком горы или океан, это видно сразу по этому списку.
    rep << "\nПримеры точек возрождения:\n";
    Rng rng(splitMix64(wcfg.seed ^ 0x5350415745524ULL /* 'SPAWNER' */));
    for(int i = 0; i < 8; ++i){
        Vec3 p{};
        bool safe = false;
        for(int attempt = 0; attempt < 32 && !safe; ++attempt){
            p = world.findSpawnPoint(rng);
            safe = monuments.isSafeSpawn(p.x, p.z);
        }
        char line[160];
        snprintf(line, sizeof(line), "  (%5.0f, %5.0f) высота %5.1f м, биом %s %s\n",
                 (double)p.x, (double)p.z, (double)p.y,
                 padRightUtf8(biomeName(world.biomeAt(p.x, p.z)), 16).c_str(),
                 safe ? "" : "(рядом радиация!)");
        rep << line;
    }
    rep.close();

    LOG_INFO("готово: %s, %s, %s", biomePath.c_str(), heightPath.c_str(), reportPath.c_str());
    return 0;
}
