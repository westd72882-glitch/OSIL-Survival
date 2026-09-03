// ==================== ТЕСТЫ ИГРОКА ====================
// Здесь проверяется то, что игрок чувствует руками: удар — одно нажатие, один замах и
// ровно одна порция ресурса, а смерть — это смерть, а не мигание нуля здоровья.
// Обе ошибки уже были в игре: зажатая кнопка «копала» без остановки, а игрок с нулём
// HP оставался жив, потому что регенерация возвращала ему долю единицы в том же кадре.
#include "TestHarness.h"
#include "../src/Client/Survivor.h"
#include "../src/World/Environment.h"

namespace {
WorldConfig survivorTestConfig(){
    WorldConfig cfg;
    cfg.seed = 909ULL;
    cfg.size = 1000.0f;
    cfg.heightGridStep = 8.0f;
    cfg.monumentCount = 2;
    cfg.sanitize();
    return cfg;
}

// Колонка на суше, подальше от воды: тонуть в этих тестах не входит в план.
bool findDryColumn(const World& w, const VoxelWorld& v, int& outX, int& outZ){
    for(int i = 60; i < 900; i += 7){
        for(int j = 60; j < 900; j += 11){
            if(w.isWater((float)i, (float)j)) continue;
            if(v.surfaceY(i, j) <= v.waterLevelBlocks() + 3) continue;
            outX = i; outZ = j;
            return true;
        }
    }
    return false;
}
} // namespace

TEST(удар_по_дереву_даёт_ресурс_один_раз_за_нажатие){
    WorldConfig cfg = survivorTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);
    Environment env(cfg);
    Inventory inv;
    Survivor player(v, env, inv);

    int x = 0, z = 0;
    CHECK_MSG(findDryColumn(w, v, x, z), "не нашли сухую колонку для теста");
    player.spawn(Vec3{ (float)x + 0.5f, 0.0f, (float)z + 0.5f });

    // Добывать можно только топором — берём его в руки, как в игре.
    inv.slot(0) = ItemStack{ ItemType::Axe, 1 };
    inv.select(0);

    // Ствол дерева ставим прямо перед лицом: при yaw = 0 взгляд направлен в -Z.
    Vec3 eye = player.eyePosition();
    int bx = (int)floorf(eye.x), by = (int)floorf(eye.y), bz = (int)floorf(eye.z) - 1;
    v.setBlock(bx, by, bz, Block::Wood);

    SurvivorInput in;
    in.yaw = 0.0f; in.pitch = 0.0f;
    in.attack = true;                 // одно нажатие
    player.update(in, 1.0f / 60.0f);
    in.attack = false;                // палец убран, кнопка больше не нажата
    for(int i = 0; i < 120; ++i) player.update(in, 1.0f / 60.0f);

    CHECK_MSG(inv.countOf(ItemType::Wood) == 5,
              "одно нажатие должно давать ровно 5 дерева за удар");

    // Ещё две секунды без нажатия — ресурс не должен капать сам по себе.
    for(int i = 0; i < 120; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(inv.countOf(ItemType::Wood) == 5, "ресурс продолжает капать без нажатия");

    // Второе нажатие — второй удар.
    in.attack = true;
    player.update(in, 1.0f / 60.0f);
    in.attack = false;
    for(int i = 0; i < 120; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(inv.countOf(ItemType::Wood) == 10, "второе нажатие не засчиталось ударом");

    // Факелом ресурс не добывается: бить им можно, но дерево от этого не рубится.
    inv.slot(1) = ItemStack{ ItemType::Torch, 1 };
    inv.select(1);
    in.attack = true;
    player.update(in, 1.0f / 60.0f);
    in.attack = false;
    for(int i = 0; i < 120; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(inv.countOf(ItemType::Wood) == 10, "факелом добыли ресурс, а должен только топор");
}

TEST(замах_виден_даже_когда_перед_игроком_пусто){
    WorldConfig cfg = survivorTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);
    Environment env(cfg);
    Inventory inv;
    Survivor player(v, env, inv);

    int x = 0, z = 0;
    CHECK(findDryColumn(w, v, x, z));
    player.spawn(Vec3{ (float)x + 0.5f, 0.0f, (float)z + 0.5f });

    SurvivorInput in;
    in.pitch = -0.9f;   // смотрим в небо: добывать точно нечего
    in.attack = true;
    player.update(in, 1.0f / 60.0f);
    CHECK_MSG(player.swinging(), "удар в пустоту не запустил замах");
    // Фаза считается от начала замаха, поэтому в кадре нажатия она ещё ноль.
    in.attack = false;
    player.update(in, 1.0f / 60.0f);
    CHECK_MSG(player.swingPhase() > 0.0f && player.swingPhase() < 1.0f,
              "фаза замаха вне диапазона");
    // Замах конечен: через секунду рука опущена.
    for(int i = 0; i < 60; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(!player.swinging(), "замах не кончился");
}

TEST(игрок_умирает_при_нуле_здоровья_и_не_воскресает_сам){
    WorldConfig cfg = survivorTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);
    Environment env(cfg);
    Inventory inv;
    Survivor player(v, env, inv);

    int x = 0, z = 0;
    CHECK(findDryColumn(w, v, x, z));
    player.spawn(Vec3{ (float)x + 0.5f, 0.0f, (float)z + 0.5f });

    // Ждём голодной смерти: жажда кончается за 55 минут, дальше урон 1.6 HP/с.
    SurvivorInput in;
    bool died = false;
    for(int i = 0; i < 6000 && !died; ++i){
        player.update(in, 1.0f);
        if(player.isDead()) died = true;
    }
    CHECK_MSG(died, "игрок не умер, хотя здоровье должно было дойти до нуля");
    CHECK_MSG(player.health() <= 0.0f, "мёртвый игрок с положительным здоровьем");

    // Регенерация не должна поднимать труп: без возрождения он остаётся мёртвым.
    for(int i = 0; i < 60; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(player.isDead(), "игрок ожил сам, без возрождения");
    CHECK_MSG(player.respawnLeft() > 0.0f && player.respawnLeft() <= 5.0f,
              "отсчёт до возрождения не идёт");

    // Возрождение возвращает игрока в игру с полным здоровьем.
    player.spawn(Vec3{ (float)x + 0.5f, 0.0f, (float)z + 0.5f });
    CHECK_MSG(!player.isDead(), "после возрождения игрок остался мёртвым");
    CHECK_MSG(player.health() > 99.0f, "после возрождения здоровье не восстановилось");
}

TEST(падение_с_высоты_убивает_и_регенерация_не_поднимает){
    WorldConfig cfg = survivorTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);
    Environment env(cfg);
    Inventory inv;
    Survivor player(v, env, inv);

    int x = 0, z = 0;
    CHECK(findDryColumn(w, v, x, z));
    player.spawn(Vec3{ (float)x + 0.5f, 0.0f, (float)z + 0.5f });
    int top = (int)player.position().y;

    // Выбиваем колодец под ногами: игрок падает с полусотни блоков — это заведомо
    // смертельно. Ровно на это жаловались: здоровье уходило в ноль, а игрок жил.
    for(int y = top - 1; y > top - 50 && y > 1; --y) v.setBlock(x, y, z, Block::Air);

    SurvivorInput in;
    for(int i = 0; i < 600 && !player.isDead(); ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(player.isDead(), "падение с полусотни блоков не убило игрока");

    // И через несколько секунд он всё ещё мёртв: непрерывной регенерации больше нет.
    for(int i = 0; i < 180; ++i) player.update(in, 1.0f / 60.0f);
    CHECK_MSG(player.isDead(), "игрок ожил сам после смертельного падения");
    CHECK_MSG(player.health() <= 0.0f, "у мёртвого игрока отросло здоровье");
}
