#pragma once
// ==================== РЕСУРСЫ МИРА (объекты добычи) ====================
// Деревья, камни, рудные и серные жилы, кусты и грибы. По ТЗ добыча идёт ударами
// инструмента, а выпадение зависит от качества инструмента — сама добыча появится на
// 3-м этапе; здесь решается более базовый вопрос: ЧТО и ГДЕ стоит в мире.
//
// Ключевое решение: рассев детерминирован и локален. Мир делится на квадратные ячейки
// (world.resourceCellSize, по умолчанию 12 м); содержимое ячейки считается генератором,
// засеянным от (координаты ячейки, сид, соль слоя). Отсюда два важных свойства:
//   - сервер может собрать всю карту сразу, а клиент — только ближние ячейки, и деревья
//     у них совпадут до сантиметра;
//   - добавление нового слоя (скажем, кактусов) не сдвигает уже существующие деревья,
//     потому что у каждого слоя своя соль.
#include "World.h"

#include <cstdint>
#include <vector>

enum class ResourceKind : uint8_t {
    TreePine = 0,   // сосна — много дерева
    TreeOak,        // дуб
    TreeBirch,      // берёза
    TreeDead,       // сухостой (пустыня/болото)
    Boulder,        // валун — камень
    RockCluster,    // скальный выход
    MetalOre,       // металлическая жила
    SulfurOre,      // серная жила
    StoneNode,      // мелкие камни, собираются руками
    Bush,           // куст (ткань)
    BerryBush,      // ягоды
    Pumpkin,        // тыква
    Mushroom,       // гриб
    Hemp,           // конопля — ткань
    COUNT
};

struct ResourceInfo {
    const char* id;         // идентификатор для протокола/БД
    const char* nameRu;
    float health;           // «прочность» узла: сколько урона добычи он держит
    int   yieldAmount;      // сколько единиц ресурса даёт при полной выработке
    const char* yieldItem;  // что именно даёт (item id, каталог предметов — 3-й этап)
    float radius;           // радиус коллизии/занимаемого места, метры
    bool  requiresTool;     // руками не собрать (нужен топор/кирка)
};

const ResourceInfo& resourceInfo(ResourceKind kind);

struct ResourceNode {
    uint32_t id = 0;        // сетевой идентификатор (детерминирован от координат)
    ResourceKind kind = ResourceKind::TreePine;
    Vec3 pos{};             // точка основания на поверхности
    float rotationY = 0.0f; // радианы
    float scale = 1.0f;     // разброс размеров, 0.8..1.25
    float health = 0.0f;    // текущая «прочность» (полная при спавне)
};

class ResourceMap {
public:
    explicit ResourceMap(const World& world) : world_(world) {}

    // Полный проход по карте — на сервере вызывается один раз при старте.
    void generate();

    // Содержимое одной ячейки без обращения к общему списку: чистая функция от
    // координат ячейки и сида. Именно этим клиент достраивает мир вокруг игрока.
    std::vector<ResourceNode> nodesInCell(int cx, int cz) const;

    const std::vector<ResourceNode>& nodes() const { return nodes_; }
    size_t countOf(ResourceKind kind) const;

    // Узлы в радиусе (для добычи, для проверки места под постройку, для рассылки клиенту).
    std::vector<const ResourceNode*> query(float x, float z, float radius) const;

    int cellsPerSide() const { return cellsPerSide_; }

private:
    const World& world_;
    std::vector<ResourceNode> nodes_;
    // Пространственный индекс: ведро на ячейку рассева, в нём индексы в nodes_.
    std::vector<std::vector<uint32_t>> buckets_;
    int cellsPerSide_ = 0;
};
