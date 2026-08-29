// ==================== ТЕСТЫ МИРА ====================
// Проверяем не «красоту» карты (её оценивает глаз по osil_mapgen), а свойства, на
// которые опирается остальная игра: детерминизм, границы значений, наличие суши,
// пригодность точек возрождения, корректность рассева ресурсов и монументов.
#include "TestHarness.h"
#include "../src/World/Environment.h"
#include "../src/World/Monuments.h"
#include "../src/World/Resources.h"
#include "../src/World/World.h"

#include <set>

namespace {
// Маленькая карта: тесты обязаны идти секунды, а не минуты. Все проверяемые свойства
// от размера не зависят — генератор один и тот же.
WorldConfig testConfig(uint64_t seed = 20240601ULL, float size = 1200.0f){
    WorldConfig cfg;
    cfg.seed = seed;
    cfg.size = size;
    cfg.heightGridStep = 8.0f;
    cfg.monumentCount = 6;
    cfg.monumentMinSpacing = 120.0f;
    cfg.sanitize();
    return cfg;
}
} // namespace

TEST(мир_детерминирован_по_сиду){
    WorldConfig cfg = testConfig();
    World a(cfg), b(cfg);
    a.generate();
    b.generate();
    CHECK(a.heightGrid().size() == b.heightGrid().size());
    for(size_t i = 0; i < a.heightGrid().size(); ++i){
        if(a.heightGrid()[i] != b.heightGrid()[i]){
            testFail(__FILE__, __LINE__, "высоты разошлись при одинаковом сиде");
        }
    }
}

TEST(разные_сиды_дают_разные_карты){
    World a(testConfig(1)), b(testConfig(2));
    a.generate(); b.generate();
    int diff = 0;
    for(size_t i = 0; i < a.heightGrid().size(); ++i)
        if(std::fabs(a.heightGrid()[i] - b.heightGrid()[i]) > 0.5f) ++diff;
    CHECK_MSG(diff > (int)(a.heightGrid().size() / 2), "карты слишком похожи");
}

TEST(высоты_в_допустимых_пределах){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    for(float v : w.heightGrid()) CHECK_RANGE(v, -6.5, (double)cfg.maxHeight + 0.01);
}

TEST(край_карты_всегда_океан){
    // Иначе база у границы упирается в невидимую стену, а не в берег.
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    for(float t = 0.0f; t <= 1.0f; t += 0.02f){
        float p = t * cfg.size;
        CHECK(w.isWater(0.0f, p));
        CHECK(w.isWater(cfg.size, p));
        CHECK(w.isWater(p, 0.0f));
        CHECK(w.isWater(p, cfg.size));
    }
}

TEST(суши_достаточно_для_игры){
    World w(testConfig());
    w.generate();
    const float* f = w.biomeFractions();
    float land = 1.0f - f[(int)Biome::Ocean];
    // Карта-остров: суши должно быть много, но и океан обязан остаться.
    CHECK_RANGE(land, 0.35, 0.92);
    float sum = 0.0f;
    for(int i = 0; i < (int)Biome::COUNT; ++i) sum += f[i];
    CHECK_NEAR(sum, 1.0, 1e-3);
}

TEST(на_карте_есть_разные_биомы){
    World w(testConfig());
    w.generate();
    const float* f = w.biomeFractions();
    int present = 0;
    for(int i = 0; i < (int)Biome::COUNT; ++i) if(f[i] > 0.001f) ++present;
    CHECK_MSG(present >= 4, "мир вышел однообразным: меньше четырёх биомов");
}

TEST(запросы_вне_карты_безопасны){
    World w(testConfig());
    w.generate();
    // Пуля, улетевшая за карту, не должна ронять сервер и возвращать мусор.
    CHECK(std::isfinite(w.heightAt(-5000.0f, -5000.0f)));
    CHECK(std::isfinite(w.heightAt(99999.0f, 99999.0f)));
    CHECK(!w.inBounds(-1.0f, 10.0f));
    CHECK(w.inBounds(10.0f, 10.0f));
    CHECK(std::isfinite(w.slopeAt(0.0f, 0.0f)));
}

TEST(нормаль_и_уклон_согласованы){
    World w(testConfig());
    w.generate();
    for(int i = 0; i < 200; ++i){
        float x = 50.0f + (float)i * 5.0f;
        float z = 300.0f;
        Vec3 n = w.normalAt(x, z);
        CHECK_NEAR(v3len(n), 1.0, 1e-3);
        CHECK(n.y > 0.0f);            // поверхность всегда «смотрит вверх»
        CHECK_RANGE(w.slopeAt(x, z), 0.0, 90.0);
    }
}

TEST(точка_возрождения_пригодна_для_жизни){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    Rng rng(1234);
    for(int i = 0; i < 50; ++i){
        Vec3 p = w.findSpawnPoint(rng);
        CHECK(w.inBounds(p.x, p.z));
        CHECK_MSG(!w.isWater(p.x, p.z), "игрок появился в воде");
        CHECK_NEAR(p.y, w.heightAt(p.x, p.z), 0.01);
        CHECK_MSG(w.slopeAt(p.x, p.z) <= 45.0f, "игрок появился на отвесном склоне");
    }
}

TEST(ресурсы_детерминированы_и_не_под_водой){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    ResourceMap res(w);

    // Одна и та же ячейка, посчитанная дважды, обязана дать те же объекты —
    // на этом держится достройка мира на клиенте.
    for(int cx = 3; cx < 8; ++cx){
        std::vector<ResourceNode> a = res.nodesInCell(cx, 5);
        std::vector<ResourceNode> b = res.nodesInCell(cx, 5);
        CHECK(a.size() == b.size());
        for(size_t i = 0; i < a.size(); ++i){
            CHECK(a[i].kind == b[i].kind);
            CHECK(a[i].pos.x == b[i].pos.x);
            CHECK(a[i].pos.z == b[i].pos.z);
        }
    }

    res.generate();
    CHECK_MSG(res.nodes().size() > 100, "мир пустой: ресурсы не расставились");
    for(const ResourceNode& n : res.nodes()){
        CHECK_MSG(!w.isWater(n.pos.x, n.pos.z), "объект добычи оказался под водой");
        CHECK(w.inBounds(n.pos.x, n.pos.z));
        CHECK_NEAR(n.pos.y, w.heightAt(n.pos.x, n.pos.z), 0.01);
        CHECK(n.health > 0.0f);
    }
}

TEST(ресурсы_есть_всех_основных_видов){
    WorldConfig cfg = testConfig(777, 2000.0f);
    World w(cfg);
    w.generate();
    ResourceMap res(w);
    res.generate();
    size_t trees = res.countOf(ResourceKind::TreePine) + res.countOf(ResourceKind::TreeOak) +
                   res.countOf(ResourceKind::TreeBirch) + res.countOf(ResourceKind::TreeDead);
    size_t rocks = res.countOf(ResourceKind::Boulder) + res.countOf(ResourceKind::RockCluster);
    CHECK_MSG(trees > 50, "деревьев почти нет");
    CHECK_MSG(rocks > 20, "камня почти нет");
    CHECK_MSG(res.countOf(ResourceKind::MetalOre) > 0, "нет металлической руды");
    CHECK_MSG(res.countOf(ResourceKind::SulfurOre) > 0, "нет серы — нечем будет рейдить");
}

TEST(поиск_ресурсов_по_радиусу_совпадает_с_перебором){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    ResourceMap res(w);
    res.generate();

    float x = cfg.size * 0.5f, z = cfg.size * 0.5f, r = 60.0f;
    std::vector<const ResourceNode*> fast = res.query(x, z, r);
    size_t slow = 0;
    for(const ResourceNode& n : res.nodes()){
        float dx = n.pos.x - x, dz = n.pos.z - z;
        if(dx*dx + dz*dz <= r*r) ++slow;
    }
    CHECK(fast.size() == slow);
}

TEST(монументы_не_налезают_друг_на_друга){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    MonumentMap mons(w);
    mons.generate();
    CHECK_MSG(!mons.monuments().empty(), "не поставлено ни одного монумента");
    for(size_t i = 0; i < mons.monuments().size(); ++i){
        const Monument& a = mons.monuments()[i];
        CHECK(w.inBounds(a.pos.x, a.pos.z));
        CHECK_MSG(!w.isWater(a.pos.x, a.pos.z), "монумент утонул");
        for(size_t j = i + 1; j < mons.monuments().size(); ++j){
            const Monument& b = mons.monuments()[j];
            float d = horizontalDist(a.pos, b.pos);
            CHECK_MSG(d > a.radius + b.radius, "зоны монументов пересекаются");
        }
    }
}

TEST(радиация_спадает_к_краю_зоны){
    WorldConfig cfg = testConfig();
    World w(cfg);
    w.generate();
    MonumentMap mons(w);
    mons.generate();

    const Monument* hot = nullptr;
    for(const Monument& m : mons.monuments()) if(m.radiation > 0.0f){ hot = &m; break; }
    if(!hot) return; // на маленькой карте могло не достаться радиационного монумента

    float center = mons.radiationAt(hot->pos.x, hot->pos.z);
    float mid    = mons.radiationAt(hot->pos.x + hot->radius * 0.5f, hot->pos.z);
    float edge   = mons.radiationAt(hot->pos.x + hot->radius * 1.2f, hot->pos.z);
    CHECK(center > mid);
    CHECK(mid > edge);
    CHECK_NEAR(edge, 0.0, 1e-5);
    CHECK(!mons.isSafeSpawn(hot->pos.x, hot->pos.z));
}

TEST(сутки_проходят_за_заданное_время){
    WorldConfig cfg = testConfig();
    cfg.dayLengthMinutes = 60.0f;
    cfg.startTimeOfDay = 8.0f;
    Environment env(cfg);
    CHECK_NEAR(env.timeOfDay(), 8.0, 0.01);
    CHECK(env.dayNumber() == 1);

    // Полчаса реального времени = половина суток: 8:00 -> 20:00.
    for(int i = 0; i < 30 * 60; ++i) env.tick(1.0f);
    CHECK_NEAR(env.timeOfDay(), 20.0, 0.05);
    CHECK(env.isNight());

    // Ещё полчаса — новые сутки, снова 8 утра.
    for(int i = 0; i < 30 * 60; ++i) env.tick(1.0f);
    CHECK_NEAR(env.timeOfDay(), 8.0, 0.05);
    CHECK(env.dayNumber() == 2);
    CHECK(!env.isNight());
}

TEST(освещённость_и_температура_меняются_за_сутки){
    WorldConfig cfg = testConfig();
    Environment env(cfg);
    env.forceWeather(Weather::Clear, 0.0f);

    env.setTimeOfDay(12.0f);
    float dayLight = env.lightLevel();
    float dayTemp = env.temperatureModifier();
    env.setTimeOfDay(0.0f);
    float nightLight = env.lightLevel();
    float nightTemp = env.temperatureModifier();

    CHECK(dayLight > nightLight);
    CHECK_RANGE(nightLight, 0.0, 0.35);
    CHECK_MSG(nightTemp < dayTemp - 5.0f, "ночь должна быть заметно холоднее дня");
}

TEST(погода_детерминирована_по_сиду_и_времени){
    WorldConfig cfg = testConfig();
    Environment a(cfg), b(cfg);
    for(int i = 0; i < 4000; ++i){ a.tick(1.0f); b.tick(1.0f); }
    CHECK(a.weather() == b.weather());
    CHECK_NEAR(a.weatherIntensity(), b.weatherIntensity(), 1e-6);
    CHECK_NEAR(a.timeOfDay(), b.timeOfDay(), 1e-4);
}

TEST(погода_за_сутки_меняется){
    WorldConfig cfg = testConfig();
    Environment env(cfg);
    std::set<int> seen;
    for(int i = 0; i < 3600 * 3; ++i){
        env.tick(1.0f);
        seen.insert((int)env.weather());
    }
    CHECK_MSG(seen.size() >= 2, "погода застряла в одном состоянии");
    CHECK_RANGE(env.visibilityFactor(), 0.0, 1.0);
    CHECK(env.windSpeed() >= 0.0f);
}

TEST(все_биомы_встречаются_хотя_бы_на_одном_сиде){
    // На отдельном сиде мир может выйти холодным (без пустыни) или тёплым (без снега) —
    // это нормальная изменчивость. Ненормально, если биом не появляется НИКОГДА: значит
    // условие в классификаторе недостижимо. Проверяем объединение по нескольким сидам.
    bool seen[(int)Biome::COUNT] = {false};
    for(uint64_t seed : {11ULL, 22ULL, 33ULL, 44ULL, 55ULL}){
        WorldConfig cfg = testConfig(seed, 1600.0f);
        World w(cfg);
        w.generate();
        const float* f = w.biomeFractions();
        for(int i = 0; i < (int)Biome::COUNT; ++i) if(f[i] > 0.0005f) seen[i] = true;
    }
    for(int i = 0; i < (int)Biome::COUNT; ++i)
        CHECK_MSG(seen[i], std::string("биом не встретился ни разу: ") + biomeInfo((Biome)i).nameRu);
}
