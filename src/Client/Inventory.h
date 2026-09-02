#pragma once
// ==================== ИНВЕНТАРЬ ====================
// Устройство взято из ТЗ (Rust): 30 слотов сеткой 6x5, стаки до 1000 для сырья, плюс
// пояс быстрого доступа. Из A.N.O.D.E перенесён подход к предметам: одна таблица, где
// про предмет собрано ВСЁ — имя, стак, ставится ли блоком, съедобен ли. Раньше в том
// проекте цена лежала в диалоге, вес — в подсчёте нагрузки, а имя в локализации, и
// добавление предмета означало правку пяти мест.
//
// Отличие от A.N.O.D.E: там сетка была сталкерская, с размерами предметов в клетках и
// слотами экипировки. Здесь мир кубический, и почти каждый предмет — это блок, который
// можно поставить обратно, поэтому сетка простая и одинаковая.
#include "../World/Blocks.h"

#include <cstdint>
#include <string>

enum class ItemType : uint8_t {
    None = 0,
    // Блоки (ставятся в мир)
    Grass, Dirt, Stone, Sand, Snow, Wood, Leaves, OreMetal, OreSulfur, Planks, StoneBrick, Mud,
    // Не-блоки
    Cloth,      // ткань с кустов
    Berry,      // ягоды (еда)
    MetalFrag,  // металлические фрагменты (из руды, печь — этап 3)
    Sulfur,     // сера (порох, этап 4)
    Scrap,      // скрап из бочек у дороги
    Axe,        // каменный топорик — им добывают
    Torch,      // факел
    Furnace,    // печь: ставится на землю и плавит руду
    COUNT
};

struct ItemDef {
    const char* id;
    const char* nameRu;
    Block placeable;   // Block::Air — предмет не ставится в мир
    int   maxStack;
    int   food;        // сколько сытости даёт (0 — не еда)
    int   water;       // сколько жажды утоляет
    float r, g, b;     // цвет иконки в слоте (текстур у нас нет)
};

const ItemDef& itemDef(ItemType t);
// Что падает с блока при разрушении (Block::Air -> ItemType::None).
ItemType itemFromBlock(Block b);

struct ItemStack {
    ItemType type = ItemType::None;
    int count = 0;
    bool empty() const { return type == ItemType::None || count <= 0; }
    void clear(){ type = ItemType::None; count = 0; }
};

class Inventory {
public:
    static const int COLS = 6;
    static const int ROWS = 5;
    static const int SIZE = COLS * ROWS;   // 30 слотов по ТЗ
    static const int HOTBAR = 6;           // первый ряд — пояс быстрого доступа

    // Кладёт предметы, докладывая в существующие стаки. Возвращает, сколько НЕ влезло.
    int add(ItemType type, int count);
    // Снимает count предметов; false — столько не набралось (ничего не списано).
    bool remove(ItemType type, int count);
    int countOf(ItemType type) const;

    ItemStack& slot(int index);
    const ItemStack& slot(int index) const;
    // Перенос/объединение между слотами — то, что в интерфейсе делается перетаскиванием.
    void moveOrSwap(int from, int to);
    // Выбросить весь стак из ячейки (предмет пропадает — сумок на земле пока нет).
    void dropSlot(int index);
    // Разделить стак пополам, положив половину в ближайшую свободную ячейку.
    // false — делить нечего или некуда.
    bool splitSlot(int index);

    int selected() const { return selected_; }
    void select(int hotbarIndex);
    ItemStack& selectedStack(){ return slots_[selected_]; }
    const ItemStack& selectedStack() const { return slots_[selected_]; }
    // Списывает одну единицу выбранного предмета (поставили блок / съели).
    bool consumeSelected();

    int usedSlots() const;

private:
    ItemStack slots_[SIZE];
    int selected_ = 0;
};
