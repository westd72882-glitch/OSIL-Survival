#pragma once
// ==================== ВОКСЕЛЬНЫЙ МИР ====================
// Поверх процедурной карты (World: высота, влажность, температура, биомы) построен
// кубический слой: мир состоит из блоков 1x1x1 м, как в Minecraft. Земля не хранится
// в памяти поблочно — 4000x4000x220 это 3.5 миллиарда блоков, ни один телефон такого
// не выдержит. Вместо этого:
//
//   * рельеф вычисляется на лету из карты высот (какой блок лежит на глубине d);
//   * деревья и жилы руды достраиваются по чанкам «декором», детерминированно от сида,
//     и выбрасываются вместе с чанком — их можно пересоздать в любой момент;
//   * ПРАВКИ ИГРОКА (сломал/поставил) хранятся отдельно и навсегда: их мало, они
//     занимают память по факту и позже уедут в базу данных сервера.
//
// Такой подход даёт бесконечно «плотный» мир при памяти в единицы мегабайт.
#include "Blocks.h"
#include "Resources.h"
#include "World.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

// Сторона чанка в блоках. 16 — как в Minecraft: достаточно мелко, чтобы перестройка
// одного чанка после удара киркой была незаметной, и достаточно крупно, чтобы вызовов
// отрисовки было немного.
const int CHUNK_SIZE = 16;

struct RayHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;          // блок, в который попали
    int prevX = 0, prevY = 0, prevZ = 0; // соседняя клетка перед ним (куда ставить блок)
    Block block = Block::Air;
    float distance = 0.0f;
};

class VoxelWorld {
public:
    VoxelWorld(const World& world, const ResourceMap& resources);

    // ---- Чтение
    Block blockAt(int x, int y, int z) const;
    bool  isSolidAt(int x, int y, int z) const;
    // Высота верхнего твёрдого блока рельефа в колонке (без деревьев).
    int   surfaceY(int x, int z) const;
    int   maxHeightBlocks() const { return maxY_; }
    int   waterLevelBlocks() const { return waterY_; }

    // ---- Правки игрока
    void  setBlock(int x, int y, int z, Block b);
    size_t editCount() const { return edits_.size(); }
    // Диапазон высот, затронутых правками игрока в этом чанке. Нужен сборщику меша:
    // без него выкопанная яма глубже пары блоков остаётся без стенок — их грани просто
    // не попадают в геометрию, и сквозь них видно пустоту.
    void editYRange(int cx, int cz, int& outMinY, int& outMaxY) const;

    // ---- Луч из глаз игрока: что он сейчас видит перед собой (добыча/установка).
    RayHit raycast(Vec3 origin, Vec3 dir, float maxDistance) const;

    // ---- Декор чанка (деревья, руда). Мешер зовёт это перед сборкой геометрии;
    // повторный вызов бесплатен.
    void ensureChunkDecor(int cx, int cz) const;
    // Освободить декор чанков дальше radius от точки (память возвращается миру).
    void pruneDecor(float centerX, float centerZ, float radius) const;

    const World& world() const { return world_; }

private:
    // Блок рельефа на глубине: трава/песок/снег сверху, земля, ниже камень и жилы.
    Block terrainBlock(int x, int y, int z, int surface) const;
    static uint64_t packKey(int x, int y, int z);
    void generateDecor(int cx, int cz) const;

    const World& world_;
    const ResourceMap& resources_;
    int maxY_ = 220;
    int waterY_ = 18;

    // Правки игрока: ключ — упакованные координаты блока.
    std::unordered_map<uint64_t, Block> edits_;
    // По чанку: самая нижняя и самая верхняя правка. Обновляется при каждой правке.
    struct EditRange { int minY = 1 << 30; int maxY = -(1 << 30); };
    std::unordered_map<uint64_t, EditRange> editRange_;
    // Декор по чанкам: ключ чанка -> (ключ блока -> блок). mutable, потому что
    // достраивается лениво при чтении — снаружи мир остаётся логически неизменным.
    mutable std::unordered_map<uint64_t, std::unordered_map<uint64_t, Block>> decor_;
};
