#pragma once
// ==================== БЛОКИ ====================
// Мир кубический: всё, что игрок видит и ломает, — блок 1x1x1 метр. Отсюда следует
// вся остальная механика: добыча — это разрушение блока, строительство — установка,
// коллизия — проверка занятости куба, а рендер — сборка меша из ВИДИМЫХ граней.
//
// Таблица ниже — единственное место, где описан блок: цвет, прочность, что с него
// падает, твёрдый ли он и прозрачный ли. Добавить новый блок = дописать строку.
#include <cstdint>

enum class Block : uint8_t {
    Air = 0,
    Grass,      // трава (верх земли на равнине и в лесу)
    Dirt,       // земля под травой
    Stone,      // камень
    Sand,       // песок (пустыня, пляж)
    Snow,       // снег (горы, холодный север)
    Wood,       // ствол дерева
    Leaves,     // листва
    LeavesSnow, // листва под снегом (зимняя зона)
    OreMetal,   // железная жила
    OreSulfur,  // серная жила
    Water,      // вода (прозрачная, не твёрдая)
    Planks,     // доски (крафт из дерева) — строительный блок
    StoneBrick, // каменный блок (крафт) — прочнее досок
    Mud,        // болотная жижа
    Road,       // дорожное покрытие (полоса через карту)
    Barrel,     // бочка у дороги: ломается, даёт скрап
    COUNT
};

struct BlockInfo {
    const char* id;
    const char* nameRu;
    // Цвет граней: верх ярче, бок средний, низ темнее — классическая кубическая
    // подача, при которой форма читается даже без текстур.
    float topR, topG, topB;
    float sideR, sideG, sideB;
    bool  solid;        // держит игрока и пули
    bool  transparent;  // сквозь него видно соседние грани (воздух, вода, листва)
    float hardness;     // секунды добычи голыми руками (0 — мгновенно)
    Block drop;         // что падает при разрушении (Air — ничего)
    int   dropCount;
};

const BlockInfo& blockInfo(Block b);
// Что вообще можно добывать. Рельеф (трава, земля, песок, снег, дорога) не ломается:
// у нас Rust, а не Minecraft — игрок собирает ресурсы, а не копает землю.
bool isHarvestable(Block b);
inline bool blockIsSolid(Block b){ return blockInfo(b).solid; }
inline bool blockIsTransparent(Block b){ return blockInfo(b).transparent; }
inline bool blockIsAir(Block b){ return b == Block::Air; }
const char* blockName(Block b);
