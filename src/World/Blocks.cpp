#include "Blocks.h"

namespace {
// Порядок строк строго совпадает с enum Block — доступ по индексу без поиска.
const BlockInfo kBlocks[(int)Block::COUNT] = {
    // id           имя            верх (RGB)            бок (RGB)             тв.  прозр. прочн. дроп             кол-во
    { "air",        "Воздух",      0.00f,0.00f,0.00f,    0.00f,0.00f,0.00f,    false, true,  0.0f, Block::Air,       0 },
    { "grass",      "Трава",       0.42f,0.68f,0.30f,    0.36f,0.50f,0.24f,    true,  false, 0.6f, Block::Dirt,      1 },
    { "dirt",       "Земля",       0.48f,0.36f,0.24f,    0.45f,0.33f,0.22f,    true,  false, 0.6f, Block::Dirt,      1 },
    { "stone",      "Камень",      0.58f,0.58f,0.58f,    0.52f,0.52f,0.52f,    true,  false, 2.2f, Block::Stone,     1 },
    { "sand",       "Песок",       0.86f,0.78f,0.52f,    0.80f,0.72f,0.48f,    true,  false, 0.5f, Block::Sand,      1 },
    { "snow",       "Снег",        0.94f,0.96f,0.98f,    0.86f,0.89f,0.93f,    true,  false, 0.4f, Block::Snow,      1 },
    { "wood",       "Дерево",      0.55f,0.40f,0.24f,    0.42f,0.30f,0.18f,    true,  false, 1.4f, Block::Wood,      1 },
    { "leaves",     "Листва",      0.24f,0.52f,0.22f,    0.20f,0.45f,0.19f,    true,  false, 0.3f, Block::Leaves,    1 },
    { "leaves_snow","Заснеженная листва",0.92f,0.95f,0.97f,0.84f,0.88f,0.92f,  true,  false, 0.3f, Block::LeavesSnow,1 },
    { "ore_metal",  "Железная руда",0.62f,0.52f,0.34f,   0.56f,0.47f,0.31f,    true,  false, 3.0f, Block::OreMetal,  1 },
    { "ore_sulfur", "Сера",        0.78f,0.74f,0.28f,    0.70f,0.66f,0.25f,    true,  false, 3.0f, Block::OreSulfur, 1 },
    { "water",      "Вода",        0.18f,0.38f,0.62f,    0.15f,0.33f,0.56f,    false, true,  0.0f, Block::Air,       0 },
    { "planks",     "Доски",       0.72f,0.56f,0.34f,    0.66f,0.50f,0.30f,    true,  false, 1.2f, Block::Planks,    1 },
    { "stone_brick","Каменный блок",0.62f,0.62f,0.60f,   0.55f,0.55f,0.53f,    true,  false, 3.2f, Block::StoneBrick,1 },
    { "mud",        "Жижа",        0.34f,0.32f,0.22f,    0.30f,0.28f,0.19f,    true,  false, 0.5f, Block::Mud,       1 },
    { "road",       "Дорога",      0.46f,0.44f,0.41f,    0.40f,0.38f,0.36f,    true,  false, 0.0f, Block::Air,       0 },
    { "barrel",     "Бочка",       0.62f,0.34f,0.20f,    0.56f,0.30f,0.17f,    true,  false, 0.8f, Block::Air,       0 },
    { "furnace",    "Печь",        0.52f,0.50f,0.48f,    0.46f,0.44f,0.42f,    true,  false, 2.0f, Block::Air,       0 },
    { "crate",      "Ящик",        0.66f,0.52f,0.30f,    0.58f,0.45f,0.26f,    true,  false, 0.9f, Block::Air,       0 },
    { "foundation", "Фундамент",   0.62f,0.48f,0.30f,    0.55f,0.42f,0.26f,    true,  false, 5.0f, Block::Air,       0 },
    { "build_wall", "Стена",       0.62f,0.48f,0.30f,    0.55f,0.42f,0.26f,    true,  false, 5.0f, Block::Air,       0 },
    { "build_wall_z","Стена",      0.62f,0.48f,0.30f,    0.55f,0.42f,0.26f,    true,  false, 5.0f, Block::Air,       0 },
    { "build_floor","Потолок",     0.62f,0.48f,0.30f,    0.55f,0.42f,0.26f,    true,  false, 5.0f, Block::Air,       0 },
    { "build_door", "Дверь",       0.58f,0.44f,0.27f,    0.52f,0.39f,0.24f,    true,  false, 4.0f, Block::Air,       0 },
    { "build_door_z","Дверь",      0.58f,0.44f,0.27f,    0.52f,0.39f,0.24f,    true,  false, 4.0f, Block::Air,       0 },
};
} // namespace

const BlockInfo& blockInfo(Block b){
    int i = (int)b;
    if(i < 0 || i >= (int)Block::COUNT) i = 0;
    return kBlocks[i];
}

const char* blockName(Block b){ return blockInfo(b).nameRu; }

bool isHarvestable(Block b){
    switch(b){
        case Block::Wood:
        case Block::Stone:
        case Block::OreMetal:
        case Block::OreSulfur:
        case Block::Barrel:
            return true;
        default:
            return false;
    }
}

int thinAxisOf(Block b){
    switch(b){
        case Block::BuildWall:
        case Block::BuildDoor:  return 1;   // пластина поперёк X
        case Block::BuildWallZ:
        case Block::BuildDoorZ: return 3;   // пластина поперёк Z
        default:                return 0;
    }
}
