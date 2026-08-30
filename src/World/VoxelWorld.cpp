#include "VoxelWorld.h"
#include "../Core/Log.h"
#include "../Core/Random.h"

#include <cmath>

namespace {
const uint64_t SALT_ORE  = 0x5001;
const uint64_t SALT_TREE = 0x5002;

inline uint64_t packChunk(int cx, int cz){
    return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}
} // namespace

VoxelWorld::VoxelWorld(const World& world, const ResourceMap& resources)
    : world_(world), resources_(resources) {
    maxY_ = (int)world.config().maxHeight + 8;
    waterY_ = (int)world.config().waterLevel;
}

uint64_t VoxelWorld::packKey(int x, int y, int z){
    // 21 бит на X и Z (до 2 млн блоков), 12 бит на Y (до 4096) — с запасом для карты
    // 4000 м и высоты 220 м, и всё это влезает в один 64-битный ключ хеш-таблицы.
    return ((uint64_t)(uint32_t)(x & 0x1FFFFF) << 33) |
           ((uint64_t)(uint32_t)(z & 0x1FFFFF) << 12) |
           (uint64_t)(uint32_t)(y & 0xFFF);
}

int VoxelWorld::surfaceY(int x, int z) const {
    // Высота берётся в центре блока: иначе на границе двух блоков колонка «дрожит»
    // между двумя значениями и в рельефе появляются щели.
    float h = world_.heightAt((float)x + 0.5f, (float)z + 0.5f);
    int y = (int)floorf(h);
    if(y < 0) y = 0;
    if(y > maxY_) y = maxY_;
    return y;
}

Block VoxelWorld::terrainBlock(int x, int y, int z, int surface) const {
    if(y > surface) return (y <= waterY_) ? Block::Water : Block::Air;

    int depth = surface - y;
    // Жилы руды: детерминированный хеш по координатам блока. Считаются на лету —
    // хранить их незачем, а повторяемость нужна: клиент и сервер обязаны видеть одну руду.
    if(depth >= 2){
        uint64_t h = hashCoords((int64_t)x * 91 + y, (int64_t)z * 71 + y * 13,
                                world_.config().seed, SALT_ORE);
        uint32_t roll = (uint32_t)(h & 0xFFFF);
        if(roll < 380) return Block::OreMetal;    // ~0.6%
        if(roll < 560) return Block::OreSulfur;   // ~0.3%
    }

    if(depth >= 4) return Block::Stone;

    // Верхний слой определяется биомом: в пустыне песок, в горах снег, в болоте жижа.
    Biome b = world_.biomeAt((float)x + 0.5f, (float)z + 0.5f);
    if(depth == 0){
        switch(b){
            case Biome::Desert: case Biome::Beach: return Block::Sand;
            case Biome::Snow:                      return Block::Snow;
            case Biome::Swamp:                     return Block::Mud;
            case Biome::Ocean:                     return Block::Sand;
            default: break;
        }
        // Крутой склон обнажает камень — иначе трава «стекает» по отвесным стенам.
        if(world_.slopeAt((float)x + 0.5f, (float)z + 0.5f) > 46.0f) return Block::Stone;
        return Block::Grass;
    }
    if(b == Biome::Desert || b == Biome::Beach) return Block::Sand;
    return Block::Dirt;
}

void VoxelWorld::generateDecor(int cx, int cz) const {
    std::unordered_map<uint64_t, Block>& cells = decor_[packChunk(cx, cz)];

    float minX = (float)(cx * CHUNK_SIZE), minZ = (float)(cz * CHUNK_SIZE);
    float centerX = minX + CHUNK_SIZE * 0.5f, centerZ = minZ + CHUNK_SIZE * 0.5f;
    // Радиус с запасом: дерево, стоящее у самой границы соседнего чанка, кроной
    // достаёт до нашего — без запаса крона обрывалась бы ровно по шву чанков.
    float radius = CHUNK_SIZE * 0.5f + 4.0f;

    std::vector<const ResourceNode*> nodes = resources_.query(centerX, centerZ, radius);
    for(const ResourceNode* n : nodes){
        int bx = (int)floorf(n->pos.x);
        int bz = (int)floorf(n->pos.z);
        int base = surfaceY(bx, bz) + 1;

        auto put = [&](int x, int y, int z, Block b){
            // Пишем только блоки нашего чанка: соседний сам достроит свою часть.
            if(x < cx * CHUNK_SIZE || x >= (cx + 1) * CHUNK_SIZE) return;
            if(z < cz * CHUNK_SIZE || z >= (cz + 1) * CHUNK_SIZE) return;
            if(y < 0 || y > maxY_) return;
            cells[packKey(x, y, z)] = b;
        };

        switch(n->kind){
            case ResourceKind::TreePine:
            case ResourceKind::TreeOak:
            case ResourceKind::TreeBirch:
            case ResourceKind::TreeDead: {
                Rng rng = rngForCell(bx, bz, world_.config().seed, SALT_TREE);
                bool pine = (n->kind == ResourceKind::TreePine);
                bool dead = (n->kind == ResourceKind::TreeDead);
                int trunk = dead ? rng.nextInt(3, 4) : (pine ? rng.nextInt(6, 9) : rng.nextInt(4, 6));
                for(int i = 0; i < trunk; ++i) put(bx, base + i, bz, Block::Wood);
                if(dead) break;

                int topY = base + trunk;
                if(pine){
                    // Сосна: сужающаяся к верхушке ёлка — три яруса разного радиуса.
                    for(int level = 0; level < 3; ++level){
                        int r = 2 - level;
                        int y = topY - 2 + level;
                        for(int dx = -r; dx <= r; ++dx)
                            for(int dz = -r; dz <= r; ++dz){
                                if(abs(dx) + abs(dz) > r + 1) continue;
                                if(dx == 0 && dz == 0 && level < 2) continue;
                                put(bx + dx, y, bz + dz, Block::Leaves);
                            }
                    }
                    put(bx, topY + 1, bz, Block::Leaves);
                } else {
                    // Лиственное: шар листвы вокруг верхушки ствола.
                    for(int dx = -2; dx <= 2; ++dx)
                        for(int dz = -2; dz <= 2; ++dz)
                            for(int dy = -1; dy <= 2; ++dy){
                                if(abs(dx) + abs(dz) + abs(dy) > 4) continue;
                                if(dx == 0 && dz == 0 && dy <= 0) continue;
                                put(bx + dx, topY + dy, bz + dz, Block::Leaves);
                            }
                }
                break;
            }
            case ResourceKind::Boulder:
            case ResourceKind::RockCluster: {
                // Валун: кучка каменных блоков на поверхности.
                int size = (n->kind == ResourceKind::RockCluster) ? 2 : 1;
                for(int dx = -size; dx <= size; ++dx)
                    for(int dz = -size; dz <= size; ++dz){
                        if(abs(dx) + abs(dz) > size) continue;
                        int h = (abs(dx) + abs(dz) == 0) ? size : 1;
                        int sy = surfaceY(bx + dx, bz + dz) + 1;
                        for(int i = 0; i < h; ++i) put(bx + dx, sy + i, bz + dz, Block::Stone);
                    }
                break;
            }
            case ResourceKind::MetalOre:
                put(bx, base, bz, Block::OreMetal);
                put(bx, base + 1, bz, Block::OreMetal);
                break;
            case ResourceKind::SulfurOre:
                put(bx, base, bz, Block::OreSulfur);
                put(bx, base + 1, bz, Block::OreSulfur);
                break;
            case ResourceKind::Bush:
            case ResourceKind::BerryBush:
            case ResourceKind::Hemp: {
                // Куст — один блок листвы: с него падает ткань. Ставим не каждый: мелочи
                // в мире около 80 тысяч, и сплошной ковёр кубов под ногами и мешал бы
                // ходить, и съедал бы кадры на ровном месте.
                Rng rng = rngForCell(bx, bz, world_.config().seed, SALT_TREE + 17);
                if(rng.chance(0.35f)) put(bx, base, bz, Block::Leaves);
                break;
            }
            default:
                break;
        }
    }
}

void VoxelWorld::ensureChunkDecor(int cx, int cz) const {
    if(decor_.find(packChunk(cx, cz)) != decor_.end()) return;
    generateDecor(cx, cz);
}

void VoxelWorld::pruneDecor(float centerX, float centerZ, float radius) const {
    float r2 = radius * radius;
    for(auto it = decor_.begin(); it != decor_.end(); ){
        int cx = (int)(int32_t)(it->first >> 32);
        int cz = (int)(int32_t)(it->first & 0xFFFFFFFF);
        float dx = (float)(cx * CHUNK_SIZE + CHUNK_SIZE/2) - centerX;
        float dz = (float)(cz * CHUNK_SIZE + CHUNK_SIZE/2) - centerZ;
        if(dx*dx + dz*dz > r2) it = decor_.erase(it);
        else ++it;
    }
}

Block VoxelWorld::blockAt(int x, int y, int z) const {
    if(y < 0 || y > maxY_) return Block::Air;

    // 1. Правки игрока сильнее всего: сломанный блок не должен «зарастать».
    auto e = edits_.find(packKey(x, y, z));
    if(e != edits_.end()) return e->second;

    // 2. Декор чанка (деревья, валуны, жилы на поверхности).
    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    auto chunk = decor_.find(packChunk(cx, cz));
    if(chunk == decor_.end()){
        ensureChunkDecor(cx, cz);
        chunk = decor_.find(packChunk(cx, cz));
    }
    if(chunk != decor_.end()){
        auto c = chunk->second.find(packKey(x, y, z));
        if(c != chunk->second.end()) return c->second;
    }

    // 3. Рельеф.
    return terrainBlock(x, y, z, surfaceY(x, z));
}

bool VoxelWorld::isSolidAt(int x, int y, int z) const {
    return blockIsSolid(blockAt(x, y, z));
}

void VoxelWorld::setBlock(int x, int y, int z, Block b){
    if(y < 0 || y > maxY_) return;
    edits_[packKey(x, y, z)] = b;
}

RayHit VoxelWorld::raycast(Vec3 origin, Vec3 dir, float maxDistance) const {
    RayHit hit;
    // Алгоритм Амануатидиса–Вуо: шагаем ровно по границам блоков, без «пропусков»
    // и без выборки с мелким шагом (та даёт промахи по тонким блокам и стоит дороже).
    float len = v3len(dir);
    if(len < 1e-5f) return hit;
    Vec3 d = v3scale(dir, 1.0f / len);

    int x = (int)floorf(origin.x), y = (int)floorf(origin.y), z = (int)floorf(origin.z);
    int stepX = d.x > 0 ? 1 : -1, stepY = d.y > 0 ? 1 : -1, stepZ = d.z > 0 ? 1 : -1;

    auto boundary = [](float pos, int coord, int step) -> float {
        return (step > 0) ? ((float)(coord + 1) - pos) : (pos - (float)coord);
    };
    float tMaxX = fabsf(d.x) < 1e-6f ? 1e9f : boundary(origin.x, x, stepX) / fabsf(d.x);
    float tMaxY = fabsf(d.y) < 1e-6f ? 1e9f : boundary(origin.y, y, stepY) / fabsf(d.y);
    float tMaxZ = fabsf(d.z) < 1e-6f ? 1e9f : boundary(origin.z, z, stepZ) / fabsf(d.z);
    float tDeltaX = fabsf(d.x) < 1e-6f ? 1e9f : 1.0f / fabsf(d.x);
    float tDeltaY = fabsf(d.y) < 1e-6f ? 1e9f : 1.0f / fabsf(d.y);
    float tDeltaZ = fabsf(d.z) < 1e-6f ? 1e9f : 1.0f / fabsf(d.z);

    int px = x, py = y, pz = z;
    float travelled = 0.0f;
    while(travelled <= maxDistance){
        Block b = blockAt(x, y, z);
        if(b != Block::Air && b != Block::Water){
            hit.hit = true;
            hit.x = x; hit.y = y; hit.z = z;
            hit.prevX = px; hit.prevY = py; hit.prevZ = pz;
            hit.block = b;
            hit.distance = travelled;
            return hit;
        }
        px = x; py = y; pz = z;
        if(tMaxX < tMaxY && tMaxX < tMaxZ){ x += stepX; travelled = tMaxX; tMaxX += tDeltaX; }
        else if(tMaxY < tMaxZ){            y += stepY; travelled = tMaxY; tMaxY += tDeltaY; }
        else {                             z += stepZ; travelled = tMaxZ; tMaxZ += tDeltaZ; }
        if(y < 0 || y > maxY_) break;
    }
    return hit;
}
