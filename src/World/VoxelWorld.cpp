#include "VoxelWorld.h"
#include "../Core/Log.h"
#include "../Core/Random.h"

#include <cmath>
#include <array>

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

// Дорога идёт через всю карту с запада на восток и слегка виляет — прямая линейка
// посреди острова выглядела бы чертежом, а не дорогой. Ширина 7 блоков: по ней видно,
// что это дорога, и она не режет карту пополам.
float VoxelWorld::roadCenterZ(int x) const {
    float size = world_.config().size;
    float t = (float)x / (size > 1.0f ? size : 1.0f);
    return size * 0.5f
         + sinf(t * 6.2831853f * 1.5f) * size * 0.075f
         + sinf(t * 6.2831853f * 4.0f + 1.7f) * size * 0.022f;
}

bool VoxelWorld::onRoad(int x, int z) const {
    if(x < 0 || z < 0) return false;
    float size = world_.config().size;
    if((float)x >= size || (float)z >= size) return false;
    // По воде дорога не идёт: мостов у нас пока нет.
    if(world_.isWater((float)x + 0.5f, (float)z + 0.5f)) return false;
    return fabsf((float)z + 0.5f - roadCenterZ(x)) <= 3.5f;
}

// Заправка стоит у дороги в правой трети карты: до неё есть смысл идти, и её видно
// с дороги. Координаты выводятся из размера мира, а не зашиты числом.
void VoxelWorld::gasStationCentre(float& x, float& z) const {
    float size = world_.config().size;
    x = size * 0.70f;
    z = roadCenterZ((int)(size * 0.70f)) + 14.0f;   // рядом с полотном, но не на нём
}

bool VoxelWorld::inGasStation(int x, int z) const {
    float cx, cz;
    gasStationCentre(cx, cz);
    return fabsf((float)x - cx) <= 11.0f && fabsf((float)z - cz) <= 9.0f;
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
    if(depth == 0 && (onRoad(x, z) || inGasStation(x, z))) return Block::Road;
    if(depth == 0){
        switch(b){
            case Biome::Desert: case Biome::Beach: return Block::Sand;
            case Biome::Snow:                      return Block::Snow;
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
                // В зиме листва заснеженная — свой блок с белой текстурой.
                Block leaf = (world_.biomeAt((float)bx, (float)bz) == Biome::Snow)
                             ? Block::LeavesSnow : Block::Leaves;
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
                                put(bx + dx, y, bz + dz, leaf);
                            }
                    }
                    put(bx, topY + 1, bz, leaf);
                } else {
                    // Лиственное: шар листвы вокруг верхушки ствола.
                    for(int dx = -2; dx <= 2; ++dx)
                        for(int dz = -2; dz <= 2; ++dz)
                            for(int dy = -1; dy <= 2; ++dy){
                                if(abs(dx) + abs(dz) + abs(dy) > 4) continue;
                                if(dx == 0 && dz == 0 && dy <= 0) continue;
                                put(bx + dx, topY + dy, bz + dz, leaf);
                            }
                }
                break;
            }
            case ResourceKind::StoneNode:
            case ResourceKind::MetalOre:
            case ResourceKind::SulfurOre: {
                // Жила — площадка 4x4 блока в два слоя: её видно издалека, и с неё
                // есть что добыть, в отличие от одиночного куба.
                Block ore = (n->kind == ResourceKind::MetalOre)  ? Block::OreMetal
                          : (n->kind == ResourceKind::SulfurOre) ? Block::OreSulfur
                                                                 : Block::Stone;
                for(int dx = 0; dx < 4; ++dx)
                    for(int dz = 0; dz < 4; ++dz){
                        int wx = bx + dx - 1, wz = bz + dz - 1;
                        int sy = surfaceY(wx, wz) + 1;
                        // Углы ниже середины: иначе жила выглядит кирпичом.
                        bool corner = (dx == 0 || dx == 3) && (dz == 0 || dz == 3);
                        int h = corner ? 1 : 2;
                        for(int i = 0; i < h; ++i) put(wx, sy + i, wz, ore);
                    }
                break;
            }
            default:
                break;
        }
    }

    // ---- Заправка: навес на столбах, стена сзади и ящики с лутом под ним.
    {
        float gcx, gcz;
        gasStationCentre(gcx, gcz);
        int cx0 = (int)gcx, cz0 = (int)gcz;
        for(int dx = -10; dx <= 10; ++dx){
            for(int dz = -8; dz <= 8; ++dz){
                int wx = cx0 + dx, wz = cz0 + dz;
                if(wx < cx * CHUNK_SIZE || wx >= (cx + 1) * CHUNK_SIZE) continue;
                if(wz < cz * CHUNK_SIZE || wz >= (cz + 1) * CHUNK_SIZE) continue;
                int sy = surfaceY(wx, wz) + 1;

                bool edgeX = (dx == -10 || dx == 10);
                bool backWall = (dz == -8);
                // Столбы по углам и по краям навеса.
                if(edgeX && (dz == -8 || dz == 0 || dz == 8)){
                    for(int i = 0; i < 4; ++i) cells[packKey(wx, sy + i, wz)] = Block::StoneBrick;
                }
                // Задняя стена в два блока.
                if(backWall && !edgeX){
                    for(int i = 0; i < 2; ++i) cells[packKey(wx, sy + i, wz)] = Block::StoneBrick;
                }
                // Крыша навеса.
                if(dz >= -8 && dz <= 8 && dx >= -10 && dx <= 10)
                    cells[packKey(wx, sy + 4, wz)] = Block::Planks;

                // Ящики с лутом: несколько штук вдоль задней стены и по углам.
                bool crateSpot = (dz == -6 && (dx == -7 || dx == -2 || dx == 3 || dx == 8)) ||
                                 (dz == 5  && (dx == -8 || dx == 7));
                if(crateSpot) cells[packKey(wx, sy, wz)] = Block::Crate;
            }
        }
    }

    // ---- Бочки вдоль дороги. Ставятся не из ResourceMap, а прямо здесь: это чисто
    // декоративная привязка к дороге, и городить ради неё отдельный вид ресурса незачем.
    for(int x = cx * CHUNK_SIZE; x < (cx + 1) * CHUNK_SIZE; ++x){
        for(int z = cz * CHUNK_SIZE; z < (cz + 1) * CHUNK_SIZE; ++z){
            // Обочина: рядом с полотном, но не на нём.
            float d = fabsf((float)z + 0.5f - roadCenterZ(x));
            if(d < 4.0f || d > 6.5f) continue;
            if(world_.isWater((float)x + 0.5f, (float)z + 0.5f)) continue;
            uint64_t h = hashCoords(x, z, world_.config().seed, SALT_TREE + 909ULL);
            if((h & 0xFF) > 5) continue;          // примерно одна бочка на 40 метров обочины
            int sy = surfaceY(x, z) + 1;
            cells[packKey(x, sy, z)] = Block::Barrel;
            if(((h >> 8) & 1) != 0) cells[packKey(x, sy + 1, z)] = Block::Barrel;
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

bool VoxelWorld::isDecorBlock(int x, int y, int z) const {
    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    auto ch = decor_.find(packChunk(cx, cz));
    if(ch == decor_.end()) return false;
    return ch->second.find(packKey(x, y, z)) != ch->second.end();
}

// Сколько ждёт жила до восстановления. Полчаса: ресурс должен быть ценным, иначе
// добывать его незачем — вырубил рощу и стой жди.
static const float RESPAWN_SECONDS = 1800.0f;

int VoxelWorld::updateRespawn(float dtSeconds){
    if(respawn_.empty()) return 0;
    int restored = 0;
    for(auto it = respawn_.begin(); it != respawn_.end(); ){
        it->second.left -= dtSeconds;
        if(it->second.left > 0.0f){ ++it; continue; }
        uint64_t key = it->first;
        RespawnCell cell = it->second;
        it = respawn_.erase(it);
        // Возврат — это снятие правки: под ней снова окажется блок декора. Если игрок
        // успел что-то на этом месте поставить, правку не трогаем.
        auto e = edits_.find(key);
        if(e == edits_.end() || e->second != Block::Air) continue;
        edits_.erase(e);
        if(onBlockChanged) onBlockChanged(cell.x, cell.y, cell.z);
        ++restored;
    }
    return restored;
}

int VoxelWorld::fellCluster(int startX, int startY, int startZ){
    Block hit = blockAt(startX, startY, startZ);
    if(!isHarvestable(hit)) return 0;

    // Что считаем частью одного объекта. У дерева это ствол и крона, у жилы — только
    // её собственная порода, иначе «жилой» окажется вся гора.
    bool tree = (hit == Block::Wood);
    auto sameObject = [&](Block b){
        if(tree) return b == Block::Wood || b == Block::Leaves || b == Block::LeavesSnow;
        return b == hit;
    };

    // Обход в ширину по 26 соседям: у дерева крона касается ствола и по диагонали.
    const int LIMIT = tree ? 400 : 64;
    std::vector<FallingCell> found;
    std::unordered_map<uint64_t, bool> seen;
    std::deque<std::array<int,3>> queue;
    queue.push_back({ startX, startY, startZ });
    seen[packKey(startX, startY, startZ)] = true;
    while(!queue.empty() && (int)found.size() < LIMIT){
        std::array<int,3> c = queue.front(); queue.pop_front();
        found.push_back(FallingCell{ 0.0f, c[0], c[1], c[2] });
        for(int dx = -1; dx <= 1; ++dx)
            for(int dy = -1; dy <= 1; ++dy)
                for(int dz = -1; dz <= 1; ++dz){
                    if(!dx && !dy && !dz) continue;
                    int nx = c[0] + dx, ny = c[1] + dy, nz = c[2] + dz;
                    if(ny < 0 || ny > maxY_) continue;
                    uint64_t key = packKey(nx, ny, nz);
                    if(seen.count(key)) continue;
                    if(!sameObject(blockAt(nx, ny, nz))) continue;
                    seen[key] = true;
                    queue.push_back({ nx, ny, nz });
                }
    }
    if(found.empty()) return 0;

    // Сверху вниз: чем выше блок, тем раньше он уходит — крона валится первой, комель
    // последним, и это читается как падение дерева.
    int maxYFound = found[0].y;
    for(const FallingCell& c : found) if(c.y > maxYFound) maxYFound = c.y;
    for(FallingCell& c : found) c.left = (float)(maxYFound - c.y) * 0.05f;
    falling_.insert(falling_.end(), found.begin(), found.end());
    return (int)found.size();
}

void VoxelWorld::updateFalling(float dtSeconds){
    if(falling_.empty()) return;
    for(size_t i = 0; i < falling_.size(); ){
        falling_[i].left -= dtSeconds;
        if(falling_[i].left > 0.0f){ ++i; continue; }
        FallingCell c = falling_[i];
        falling_[i] = falling_.back();
        falling_.pop_back();
        setBlock(c.x, c.y, c.z, Block::Air);
    }
}

void VoxelWorld::setBlock(int x, int y, int z, Block b){
    if(y < 0 || y > maxY_) return;
    // Выбили блок, который поставил мир (жила, дерево) — ставим его в очередь на
    // восстановление. Построенное игроком в очередь не попадает.
    if(b == Block::Air && isDecorBlock(x, y, z))
        respawn_[packKey(x, y, z)] = RespawnCell{ RESPAWN_SECONDS, x, y, z };
    edits_[packKey(x, y, z)] = b;
    if(onBlockChanged) onBlockChanged(x, y, z);
    queueWaterAround(x, y, z);

    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    EditRange& r = editRange_[packChunk(cx, cz)];
    if(y < r.minY) r.minY = y;
    if(y > r.maxY) r.maxY = y;
}

void VoxelWorld::queueWaterAround(int x, int y, int z){
    // В очередь попадает сама клетка и шесть соседей: вода могла как прийти в неё,
    // так и уйти из соседней (сломали дно запруды).
    static const int dx[7] = { 0, 1,-1, 0, 0, 0, 0 };
    static const int dy[7] = { 0, 0, 0, 1,-1, 0, 0 };
    static const int dz[7] = { 0, 0, 0, 0, 0, 1,-1 };
    for(int i = 0; i < 7; ++i){
        int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if(ny < 0 || ny > maxY_) continue;
        // Выше уровня моря вода не поднимается: источник у нас один — океан.
        if(ny > waterY_) continue;
        waterQueue_.push_back(packKey(nx, ny, nz));
        // Очередь ограничена: при массовом копании она иначе растёт быстрее, чем
        // обсчитывается, и съедает память.
        if(waterQueue_.size() > 20000) waterQueue_.pop_front();
    }
}

int VoxelWorld::updateWater(int maxCells){
    int changed = 0;
    for(int i = 0; i < maxCells && !waterQueue_.empty(); ++i){
        uint64_t key = waterQueue_.front();
        waterQueue_.pop_front();

        // Распаковка ключа обратно в координаты (см. packKey).
        int y = (int)(key & 0xFFF);
        int z = (int)((key >> 12) & 0x1FFFFF);
        int x = (int)((key >> 33) & 0x1FFFFF);
        // Знак: координаты укладывались по модулю, восстанавливаем отрицательные.
        if(x >= (1 << 20)) x -= (1 << 21);
        if(z >= (1 << 20)) z -= (1 << 21);

        if(y > waterY_ || y < 0) continue;
        if(blockAt(x, y, z) != Block::Air) continue;

        // Вода приходит сверху или с боков — как в море: свободная клетка ниже уровня
        // моря, у которой есть водяной сосед, заполняется.
        bool fed = blockAt(x, y + 1, z) == Block::Water ||
                   blockAt(x + 1, y, z) == Block::Water ||
                   blockAt(x - 1, y, z) == Block::Water ||
                   blockAt(x, y, z + 1) == Block::Water ||
                   blockAt(x, y, z - 1) == Block::Water;
        if(!fed) continue;

        setBlock(x, y, z, Block::Water);
        ++changed;
    }
    return changed;
}

void VoxelWorld::editYRange(int cx, int cz, int& outMinY, int& outMaxY) const {
    outMinY = 1 << 30;
    outMaxY = -(1 << 30);
    // Смотрим не только свой чанк, но и соседние: стенка ямы, выкопанной у самой
    // границы, принадлежит соседу и тоже должна попасть в его геометрию.
    for(int dz = -1; dz <= 1; ++dz){
        for(int dx = -1; dx <= 1; ++dx){
            auto it = editRange_.find(packChunk(cx + dx, cz + dz));
            if(it == editRange_.end()) continue;
            if(it->second.minY < outMinY) outMinY = it->second.minY;
            if(it->second.maxY > outMaxY) outMaxY = it->second.maxY;
        }
    }
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
