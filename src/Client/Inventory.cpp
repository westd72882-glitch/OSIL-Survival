#include "Inventory.h"

namespace {
const ItemDef kItems[(int)ItemType::COUNT] = {
    // id            имя              ставится как        стак  еда вода  цвет
    { "none",        "—",             Block::Air,           0,   0,  0,  0.0f,0.0f,0.0f },
    { "grass",       "Дёрн",          Block::Grass,      1000,   0,  0,  0.42f,0.68f,0.30f },
    { "dirt",        "Земля",         Block::Dirt,       1000,   0,  0,  0.48f,0.36f,0.24f },
    { "stone",       "Камень",        Block::Stone,      1000,   0,  0,  0.58f,0.58f,0.58f },
    { "sand",        "Песок",         Block::Sand,       1000,   0,  0,  0.86f,0.78f,0.52f },
    { "snow",        "Снег",          Block::Snow,       1000,   0,  0,  0.94f,0.96f,0.98f },
    { "wood",        "Дерево",        Block::Wood,       1000,   0,  0,  0.55f,0.40f,0.24f },
    { "leaves",      "Листва",        Block::Leaves,     1000,   0,  0,  0.24f,0.52f,0.22f },
    { "ore_metal",   "Железная руда", Block::OreMetal,    100,   0,  0,  0.62f,0.52f,0.34f },
    { "ore_sulfur",  "Серная руда",   Block::OreSulfur,   100,   0,  0,  0.78f,0.74f,0.28f },
    { "planks",      "Доски",         Block::Planks,     1000,   0,  0,  0.72f,0.56f,0.34f },
    { "stone_brick", "Каменный блок", Block::StoneBrick, 1000,   0,  0,  0.62f,0.62f,0.60f },
    { "mud",         "Жижа",          Block::Mud,        1000,   0,  0,  0.34f,0.32f,0.22f },
    { "cloth",       "Ткань",         Block::Air,        1000,   0,  0,  0.80f,0.76f,0.66f },
    { "berry",       "Ягоды",         Block::Air,         100,  12,  6,  0.72f,0.20f,0.32f },
    { "metal_frag",  "Металл",        Block::Air,         100,   0,  0,  0.70f,0.70f,0.74f },
    { "sulfur",      "Сера",          Block::Air,        1000,   0,  0,  0.85f,0.82f,0.30f },
};
} // namespace

const ItemDef& itemDef(ItemType t){
    int i = (int)t;
    if(i < 0 || i >= (int)ItemType::COUNT) i = 0;
    return kItems[i];
}

ItemType itemFromBlock(Block b){
    switch(b){
        case Block::Grass:      return ItemType::Grass;
        case Block::Dirt:       return ItemType::Dirt;
        case Block::Stone:      return ItemType::Stone;
        case Block::Sand:       return ItemType::Sand;
        case Block::Snow:       return ItemType::Snow;
        case Block::Wood:       return ItemType::Wood;
        case Block::Leaves:     return ItemType::Cloth;   // с листвы падает ткань, как с кустов
        case Block::OreMetal:   return ItemType::OreMetal;
        case Block::OreSulfur:  return ItemType::OreSulfur;
        case Block::LeavesSnow: return ItemType::None;
        case Block::Planks:     return ItemType::Planks;
        case Block::StoneBrick: return ItemType::StoneBrick;
        case Block::Mud:        return ItemType::Mud;
        default:                return ItemType::None;
    }
}

int Inventory::add(ItemType type, int count){
    if(type == ItemType::None || count <= 0) return count;
    const ItemDef& def = itemDef(type);

    // Сначала докладываем в уже начатые стаки — иначе инвентарь забивается огрызками
    // по одной единице, и через пять минут игры класть новое становится некуда.
    for(int i = 0; i < SIZE && count > 0; ++i){
        if(slots_[i].type != type) continue;
        int space = def.maxStack - slots_[i].count;
        if(space <= 0) continue;
        int put = count < space ? count : space;
        slots_[i].count += put;
        count -= put;
    }
    for(int i = 0; i < SIZE && count > 0; ++i){
        if(!slots_[i].empty()) continue;
        int put = count < def.maxStack ? count : def.maxStack;
        slots_[i].type = type;
        slots_[i].count = put;
        count -= put;
    }
    return count; // не влезло
}

bool Inventory::remove(ItemType type, int count){
    if(countOf(type) < count) return false;
    for(int i = 0; i < SIZE && count > 0; ++i){
        if(slots_[i].type != type) continue;
        int take = slots_[i].count < count ? slots_[i].count : count;
        slots_[i].count -= take;
        count -= take;
        if(slots_[i].count <= 0) slots_[i].clear();
    }
    return true;
}

int Inventory::countOf(ItemType type) const {
    int n = 0;
    for(int i = 0; i < SIZE; ++i) if(slots_[i].type == type) n += slots_[i].count;
    return n;
}

ItemStack& Inventory::slot(int index){
    static ItemStack dummy;
    if(index < 0 || index >= SIZE) return dummy;
    return slots_[index];
}

const ItemStack& Inventory::slot(int index) const {
    static const ItemStack dummy;
    if(index < 0 || index >= SIZE) return dummy;
    return slots_[index];
}

void Inventory::moveOrSwap(int from, int to){
    if(from == to) return;
    if(from < 0 || from >= SIZE || to < 0 || to >= SIZE) return;
    ItemStack& a = slots_[from];
    ItemStack& b = slots_[to];
    if(a.empty()) return;

    // Одинаковые предметы — сливаем в один стак, остаток оставляем на месте.
    if(b.type == a.type){
        int space = itemDef(a.type).maxStack - b.count;
        if(space > 0){
            int move = a.count < space ? a.count : space;
            b.count += move;
            a.count -= move;
            if(a.count <= 0) a.clear();
            return;
        }
    }
    ItemStack tmp = b;
    b = a;
    a = tmp;
}

void Inventory::select(int hotbarIndex){
    if(hotbarIndex < 0) hotbarIndex = 0;
    if(hotbarIndex >= HOTBAR) hotbarIndex = HOTBAR - 1;
    selected_ = hotbarIndex;
}

bool Inventory::consumeSelected(){
    ItemStack& s = slots_[selected_];
    if(s.empty()) return false;
    s.count -= 1;
    if(s.count <= 0) s.clear();
    return true;
}

int Inventory::usedSlots() const {
    int n = 0;
    for(int i = 0; i < SIZE; ++i) if(!slots_[i].empty()) ++n;
    return n;
}
