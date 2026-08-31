#include "Resources.h"
#include "../Core/Log.h"

#include <cmath>

namespace {
const uint64_t SALT_RESOURCES = 0x2001;

const ResourceInfo kResources[(int)ResourceKind::COUNT] = {
    // id            имя            HP    выход  предмет     радиус  нужен инструмент
    { "tree_pine",   "Сосна",       420.f, 340,  "wood",      0.6f,  true  },
    { "tree_oak",    "Дуб",         520.f, 480,  "wood",      0.8f,  true  },
    { "tree_birch",  "Берёза",      360.f, 280,  "wood",      0.5f,  true  },
    { "tree_dead",   "Сухостой",    240.f, 190,  "wood",      0.5f,  true  },
    { "boulder",     "Валун",       500.f, 620,  "stone",     1.2f,  true  },
    { "rock_cluster","Скальный выход", 700.f, 900, "stone",   2.0f,  true  },
    { "ore_metal",   "Металлическая жила", 500.f, 260, "metal_ore", 1.1f, true },
    { "ore_sulfur",  "Серная жила", 500.f, 220,  "sulfur_ore",1.1f,  true  },
    { "stone_node",  "Камни",        1.f,   50,  "stone",     0.3f,  false },
    { "bush",        "Куст",         1.f,   10,  "cloth",     0.4f,  false },
    { "berry_bush",  "Ягодный куст", 1.f,    5,  "berry",     0.4f,  false },
    { "pumpkin",     "Тыква",        1.f,    1,  "pumpkin",   0.4f,  false },
    { "mushroom",    "Гриб",         1.f,    1,  "mushroom",  0.2f,  false },
    { "hemp",        "Конопля",      1.f,   30,  "cloth",     0.4f,  false },
};

// Веса деревьев по биомам: сосна тянет к холодному и лесному, дуб — к тёплому лесу,
// берёза — к равнине, сухостой — к пустыне и болоту.
void treeWeights(Biome b, float w[4]){
    switch(b){
        // Равнина — смешанный лес, зима — сосны, пустыня — сухие стволы.
        case Biome::Grassland: w[0]=0.30f; w[1]=0.28f; w[2]=0.38f; w[3]=0.04f; break;
        case Biome::Snow:      w[0]=0.88f; w[1]=0.02f; w[2]=0.04f; w[3]=0.06f; break;
        case Biome::Desert:    w[0]=0.05f; w[1]=0.05f; w[2]=0.05f; w[3]=0.85f; break;
        default:               w[0]=0.30f; w[1]=0.25f; w[2]=0.35f; w[3]=0.10f; break;
    }
}
} // namespace

const ResourceInfo& resourceInfo(ResourceKind kind){
    int i = (int)kind;
    if(i < 0 || i >= (int)ResourceKind::COUNT) i = 0;
    return kResources[i];
}

std::vector<ResourceNode> ResourceMap::nodesInCell(int cx, int cz) const {
    std::vector<ResourceNode> out;
    const WorldConfig& cfg = world_.config();
    float cell = cfg.resourceCellSize;
    if(cx < 0 || cz < 0) return out;
    float baseX = (float)cx * cell;
    float baseZ = (float)cz * cell;
    if(baseX > cfg.size || baseZ > cfg.size) return out;

    Rng rng = rngForCell(cx, cz, cfg.seed, SALT_RESOURCES);

    // Биом берём в центре ячейки: 12 м — заметно меньше характерного размера биома,
    // поэтому смешивать типы внутри одной ячейки смысла нет.
    float centerX = baseX + cell * 0.5f;
    float centerZ = baseZ + cell * 0.5f;
    WorldSample s = world_.sampleAt(centerX, centerZ);
    if(s.underwater) return out; // под водой ничего не растёт и не добывается
    const BiomeInfo& bi = biomeInfo(s.biome);

    // Крутые склоны: деревья на них не держатся, а камни, наоборот, встречаются чаще.
    float steep = smoothstepf(28.0f, 55.0f, s.slopeDegrees);

    // Сколько попыток спавна делаем в ячейке. Дробную часть разыгрываем: 1.4 попытки —
    // это одна гарантированная и вторая с вероятностью 40%, иначе плотность «квантуется».
    auto attemptsFor = [&](float expected) -> int {
        int whole = (int)expected;
        float frac = expected - (float)whole;
        if(rng.chance(frac)) whole += 1;
        return whole;
    };

    float density = cfg.resourceDensity;

    // ---- Деревья
    int treeTries = attemptsFor(2.6f * bi.treeDensity * density * (1.0f - steep));
    for(int i = 0; i < treeTries; ++i){
        float x = baseX + rng.nextFloat() * cell;
        float z = baseZ + rng.nextFloat() * cell;
        if(world_.isWater(x, z)) continue;
        if(world_.slopeAt(x, z) > 38.0f) continue;

        float w[4]; treeWeights(s.biome, w);
        float pick = rng.nextFloat();
        ResourceKind kind = ResourceKind::TreePine;
        float acc = 0.0f;
        for(int k = 0; k < 4; ++k){
            acc += w[k];
            if(pick <= acc){
                kind = (ResourceKind)((int)ResourceKind::TreePine + k);
                break;
            }
        }

        ResourceNode n;
        n.kind = kind;
        n.pos = Vec3{ x, world_.heightAt(x, z), z };
        n.rotationY = rng.nextRange(0.0f, 6.28318f);
        n.scale = rng.nextRange(0.82f, 1.25f);
        n.health = resourceInfo(kind).health * n.scale;
        n.id = (uint32_t)(hashCoords(cx, cz, cfg.seed, SALT_RESOURCES + (uint64_t)i * 31ULL) & 0xffffffffu);
        out.push_back(n);
    }

    // ---- Жилы: сера, железо и камень. Спавнятся нечасто — это точки интереса, а не
    // фон. Валунов и «скальных выходов» больше нет: они были просто кубами камня.
    int rockTries = attemptsFor(0.42f * bi.rockDensity * density * (0.7f + steep * 0.6f));
    for(int i = 0; i < rockTries; ++i){
        float x = baseX + rng.nextFloat() * cell;
        float z = baseZ + rng.nextFloat() * cell;
        if(world_.isWater(x, z)) continue;

        float roll = rng.nextFloat();
        ResourceKind kind;
        if(roll < bi.oreChance)                        kind = ResourceKind::MetalOre;
        else if(roll < bi.oreChance + bi.sulfurChance) kind = ResourceKind::SulfurOre;
        else                                           kind = ResourceKind::StoneNode;

        ResourceNode n;
        n.kind = kind;
        n.pos = Vec3{ x, world_.heightAt(x, z), z };
        n.rotationY = rng.nextRange(0.0f, 6.28318f);
        n.scale = rng.nextRange(0.8f, 1.3f);
        n.health = resourceInfo(kind).health * n.scale;
        n.id = (uint32_t)(hashCoords(cx, cz, cfg.seed, SALT_RESOURCES + 977ULL + (uint64_t)i * 31ULL) & 0xffffffffu);
        out.push_back(n);
    }

    // Мелочи (кусты, ягоды, тыквы, грибы, конопля, мелкие камни под руками) больше
    // нет: она превращала поле в ковёр из кубов, а давала пустяки. Остались деревья
    // и рудные/каменные жилы — то, ради чего вообще ходят по карте.

    return out;
}

void ResourceMap::generate(){
    const WorldConfig& cfg = world_.config();
    cellsPerSide_ = (int)(cfg.size / cfg.resourceCellSize) + 1;
    nodes_.clear();
    buckets_.assign((size_t)cellsPerSide_ * cellsPerSide_, {});

    for(int cz = 0; cz < cellsPerSide_; ++cz){
        for(int cx = 0; cx < cellsPerSide_; ++cx){
            std::vector<ResourceNode> cellNodes = nodesInCell(cx, cz);
            std::vector<uint32_t>& bucket = buckets_[(size_t)cz * cellsPerSide_ + cx];
            for(const ResourceNode& n : cellNodes){
                bucket.push_back((uint32_t)nodes_.size());
                nodes_.push_back(n);
            }
        }
    }

    LOG_INFO("ресурсы расставлены: всего %zu объектов (деревьев %zu, жил камня и руды %zu, прочего %zu)",
             nodes_.size(),
             countOf(ResourceKind::TreePine) + countOf(ResourceKind::TreeOak) +
                 countOf(ResourceKind::TreeBirch) + countOf(ResourceKind::TreeDead),
             countOf(ResourceKind::Boulder) + countOf(ResourceKind::RockCluster) +
                 countOf(ResourceKind::MetalOre) + countOf(ResourceKind::SulfurOre),
             countOf(ResourceKind::StoneNode) + countOf(ResourceKind::Bush) +
                 countOf(ResourceKind::BerryBush) + countOf(ResourceKind::Pumpkin) +
                 countOf(ResourceKind::Mushroom) + countOf(ResourceKind::Hemp));
}

size_t ResourceMap::countOf(ResourceKind kind) const {
    size_t n = 0;
    for(const ResourceNode& node : nodes_) if(node.kind == kind) ++n;
    return n;
}

std::vector<const ResourceNode*> ResourceMap::query(float x, float z, float radius) const {
    std::vector<const ResourceNode*> out;
    if(buckets_.empty()) return out;

    const WorldConfig& cfg = world_.config();
    float cell = cfg.resourceCellSize;
    int minX = clampi((int)((x - radius) / cell), 0, cellsPerSide_ - 1);
    int maxX = clampi((int)((x + radius) / cell), 0, cellsPerSide_ - 1);
    int minZ = clampi((int)((z - radius) / cell), 0, cellsPerSide_ - 1);
    int maxZ = clampi((int)((z + radius) / cell), 0, cellsPerSide_ - 1);
    float r2 = radius * radius;

    for(int cz = minZ; cz <= maxZ; ++cz){
        for(int cx = minX; cx <= maxX; ++cx){
            for(uint32_t idx : buckets_[(size_t)cz * cellsPerSide_ + cx]){
                const ResourceNode& n = nodes_[idx];
                float dx = n.pos.x - x, dz = n.pos.z - z;
                if(dx*dx + dz*dz <= r2) out.push_back(&n);
            }
        }
    }
    return out;
}
