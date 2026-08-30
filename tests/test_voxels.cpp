// ==================== ТЕСТЫ КУБИЧЕСКОГО МИРА ====================
// Проверяем то, на чём держится вся игра в блоках: колонка сложена в правильном
// порядке, луч из глаз попадает в тот блок, который видно, сломанный блок остаётся
// сломанным, а поставленный — стоит. Ошибка в любом из этих мест выглядит как
// «мир просвечивает» или «блоки не ломаются», и на глаз ловится долго.
#include "TestHarness.h"
#include "../src/World/VoxelWorld.h"

namespace {
WorldConfig voxelTestConfig(uint64_t seed = 4242ULL){
    WorldConfig cfg;
    cfg.seed = seed;
    cfg.size = 1000.0f;
    cfg.heightGridStep = 8.0f;
    cfg.monumentCount = 2;
    cfg.sanitize();
    return cfg;
}

// Находит колонку на суше, подальше от воды: часть проверок к воде неприменима.
bool findLandColumn(const World& w, const VoxelWorld& v, int& outX, int& outZ){
    for(int i = 40; i < 900; i += 7){
        for(int j = 40; j < 900; j += 11){
            float x = (float)i, z = (float)j;
            if(w.isWater(x, z)) continue;
            if(v.surfaceY(i, j) <= v.waterLevelBlocks() + 3) continue;
            outX = i; outZ = j;
            return true;
        }
    }
    return false;
}
} // namespace

TEST(колонка_блоков_сложена_сверху_вниз){
    WorldConfig cfg = voxelTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int x = 0, z = 0;
    CHECK(findLandColumn(w, v, x, z));
    int s = v.surfaceY(x, z);

    // Над поверхностью — воздух (или крона дерева, но не рельеф).
    CHECK(v.blockAt(x, s + 6, z) == Block::Air || v.blockAt(x, s + 6, z) == Block::Leaves ||
          v.blockAt(x, s + 6, z) == Block::Wood);
    // Верхний блок твёрдый и это не воздух.
    CHECK(blockIsSolid(v.blockAt(x, s, z)));
    // Глубже четырёх блоков — камень или жила.
    Block deep = v.blockAt(x, s - 6, z);
    CHECK_MSG(deep == Block::Stone || deep == Block::OreMetal || deep == Block::OreSulfur,
              std::string("на глубине оказался ") + blockName(deep));
}

TEST(правки_игрока_сохраняются){
    WorldConfig cfg = voxelTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int x = 0, z = 0;
    CHECK(findLandColumn(w, v, x, z));
    int s = v.surfaceY(x, z);

    CHECK(blockIsSolid(v.blockAt(x, s, z)));
    v.setBlock(x, s, z, Block::Air);
    CHECK_MSG(v.blockAt(x, s, z) == Block::Air, "сломанный блок вернулся на место");
    CHECK(v.editCount() == 1);

    v.setBlock(x, s + 1, z, Block::Planks);
    CHECK(v.blockAt(x, s + 1, z) == Block::Planks);
    CHECK(v.isSolidAt(x, s + 1, z));
}

TEST(луч_попадает_в_ближайший_блок){
    WorldConfig cfg = voxelTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int x = 0, z = 0;
    CHECK(findLandColumn(w, v, x, z));
    int s = v.surfaceY(x, z);

    // Смотрим строго вниз с высоты двух блоков над поверхностью.
    Vec3 eye{ (float)x + 0.5f, (float)s + 3.0f, (float)z + 0.5f };
    RayHit hit = v.raycast(eye, Vec3{0, -1, 0}, 8.0f);
    CHECK_MSG(hit.hit, "луч вниз не нашёл земли");
    CHECK(hit.x == x && hit.z == z);
    CHECK_MSG(hit.y == s, "луч попал не в верхний блок колонки");
    // Соседняя клетка перед блоком — ровно над ним: туда и ставится новый блок.
    CHECK(hit.prevY == s + 1);

    // Луч в небо не должен ни во что попасть.
    RayHit up = v.raycast(eye, Vec3{0, 1, 0}, 8.0f);
    CHECK(!up.hit || up.block == Block::Leaves || up.block == Block::Wood);
}

TEST(сломанный_блок_пропускает_луч){
    WorldConfig cfg = voxelTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int x = 0, z = 0;
    CHECK(findLandColumn(w, v, x, z));
    int s = v.surfaceY(x, z);
    Vec3 eye{ (float)x + 0.5f, (float)s + 3.0f, (float)z + 0.5f };

    v.setBlock(x, s, z, Block::Air);
    RayHit hit = v.raycast(eye, Vec3{0, -1, 0}, 8.0f);
    CHECK(hit.hit);
    CHECK_MSG(hit.y == s - 1, "луч не провалился в выкопанную яму");
}

TEST(вода_не_твёрдая_и_не_ломается_лучом){
    WorldConfig cfg = voxelTestConfig();
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    // Ищем колонку под водой: там верхние блоки должны быть водой.
    for(int i = 20; i < 980; i += 3){
        for(int j = 20; j < 980; j += 3){
            if(!w.isWater((float)i, (float)j)) continue;
            int s = v.surfaceY(i, j);
            if(s >= v.waterLevelBlocks()) continue;
            CHECK(v.blockAt(i, s + 1, j) == Block::Water);
            CHECK(!v.isSolidAt(i, s + 1, j));
            // Луч сверху сквозь воду достаёт до дна, а не останавливается на глади.
            Vec3 eye{ (float)i + 0.5f, (float)v.waterLevelBlocks() + 4.0f, (float)j + 0.5f };
            RayHit hit = v.raycast(eye, Vec3{0,-1,0}, 30.0f);
            if(hit.hit) CHECK(hit.block != Block::Water);
            return;
        }
    }
}

TEST(декор_детерминирован){
    // Один и тот же чанк, посчитанный дважды, обязан дать те же деревья: иначе
    // после выгрузки и повторной загрузки лес «перерастает» на глазах у игрока.
    WorldConfig cfg = voxelTestConfig(777);
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();

    VoxelWorld a(w, res), b(w, res);
    int diff = 0;
    for(int x = 100; x < 132; ++x)
        for(int z = 100; z < 132; ++z){
            int s = a.surfaceY(x, z);
            for(int y = s; y < s + 12; ++y)
                if(a.blockAt(x, y, z) != b.blockAt(x, y, z)) ++diff;
        }
    CHECK_MSG(diff == 0, "декор чанка разошёлся между двумя мирами с одним сидом");
}

TEST(в_мире_есть_деревья_из_блоков){
    WorldConfig cfg = voxelTestConfig(5);
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int wood = 0, leaves = 0;
    for(int x = 200; x < 280; ++x)
        for(int z = 200; z < 280; ++z){
            int s = v.surfaceY(x, z);
            for(int y = s + 1; y < s + 12; ++y){
                Block b = v.blockAt(x, y, z);
                if(b == Block::Wood) ++wood;
                if(b == Block::Leaves) ++leaves;
            }
        }
    CHECK_MSG(wood > 0, "в лесу не нашлось ни одного блока ствола");
    CHECK_MSG(leaves > wood, "листвы должно быть больше, чем стволов");
}

TEST(глубокая_яма_расширяет_диапазон_чанка){
    // Регрессия: копая глубже пары блоков, игрок видел «прозрачные» стены — сборщик
    // меша не знал, что колонка стала глубже естественного дна, и не строил их грани.
    // Диапазон правок обязан это учитывать, в том числе для соседнего чанка.
    WorldConfig cfg = voxelTestConfig(31337);
    World w(cfg); w.generate();
    ResourceMap res(w); res.generate();
    VoxelWorld v(w, res);

    int x = 0, z = 0;
    CHECK(findLandColumn(w, v, x, z));
    int s = v.surfaceY(x, z);

    int cx = x / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    int minY = 0, maxY = 0;
    v.editYRange(cx, cz, minY, maxY);
    CHECK_MSG(minY > maxY, "до правок диапазон должен быть пустым");

    for(int i = 0; i < 6; ++i) v.setBlock(x, s - i, z, Block::Air);
    v.editYRange(cx, cz, minY, maxY);
    CHECK(minY <= s - 5);
    CHECK(maxY >= s);

    // Башня вверх тоже расширяет диапазон — иначе у построенной колонны пропадали бы
    // верхние грани.
    v.setBlock(x, s + 9, z, Block::Planks);
    v.editYRange(cx, cz, minY, maxY);
    CHECK(maxY >= s + 9);

    // Соседний чанк должен видеть эти правки: стенка ямы у границы принадлежит ему.
    int nMin = 0, nMax = 0;
    v.editYRange(cx + 1, cz, nMin, nMax);
    CHECK_MSG(nMin <= s - 5, "соседний чанк не узнал о яме у своей границы");
}
