#pragma once
// ==================== РЕНДЕР КУБИЧЕСКОГО МИРА ====================
// Мир состоит из блоков, но рисовать блок кубом из 36 вершин нельзя: в поле зрения их
// десятки тысяч. Поэтому чанк 16x16 собирается в ОДИН меш, и в него попадают только
// ВИДИМЫЕ грани — те, у которых сосед прозрачный. В плотной земле не остаётся ни одной
// грани, и подземелье не стоит ничего.
//
// Что ещё сделано ради телефона (это и есть «оптимизация», которой требует такой мир):
//   * чанки строятся и выбрасываются на ходу, вокруг игрока, не больше N за кадр;
//   * перед отрисовкой каждый чанк проверяется на попадание в пирамиду видимости —
//     за спиной игрока ничего не рисуется, а это половина загруженных чанков;
//   * вода собирается отдельным мешем и рисуется вторым проходом с выключенной записью
//     глубины: иначе прозрачная поверхность закрывает то, что должно быть под ней;
//   * изменённый блок помечает свой чанк (и соседний, если блок на границе) грязным,
//     и перестраивается только он, а не весь мир.
#include "GL.h"
#include "../../Core/Math.h"
#include "../../World/VoxelWorld.h"

#include <cstdint>

// Вершина кубического мира: позиция, нормаль грани и цвет. Текстур нет — цвет блока
// и ступенчатое затенение по нормали дают нужную картинку и экономят память.
struct VoxelVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
};

struct VoxelRenderStats {
    int chunksLoaded = 0;
    int chunksDrawn = 0;
    int chunksBuilt = 0;      // за последний кадр
    int trianglesDrawn = 0;
};

class VoxelRenderer {
public:
    void init(const VoxelWorld* world);
    void shutdown();

    // Достраивает и выбрасывает чанки вокруг камеры. buildBudget — сколько чанков
    // разрешено собрать за этот кадр (защита от рывков при быстром беге).
    void update(Vec3 camPos, float viewDistance, int buildBudget);

    // Рисует непрозрачную геометрию. view/proj нужны для отсечения по пирамиде видимости.
    void renderOpaque(const Mat4& view, const Mat4& proj, Vec3 camPos, float viewDistance);
    // Второй проход: вода.
    void renderWater(const Mat4& view, const Mat4& proj, Vec3 camPos, float viewDistance);

    // Пометить чанк с этим блоком (и соседний, если блок у границы) на перестройку.
    void markDirty(int blockX, int blockY, int blockZ);

    const VoxelRenderStats& stats() const { return stats_; }

private:
    struct ChunkMesh {
        GLuint vao = 0, vbo = 0;
        int vertexCount = 0;
        GLuint waterVao = 0, waterVbo = 0;
        int waterVertexCount = 0;
        float centerX = 0, centerZ = 0;
        float minY = 0, maxY = 0;
        bool dirty = false;
    };

    void buildChunk(int cx, int cz);
    void freeChunk(ChunkMesh& m);

    const VoxelWorld* world_ = nullptr;
    std::unordered_map<uint64_t, ChunkMesh> chunks_;
    VoxelRenderStats stats_;
};

// Пирамида видимости из матрицы вида-проекции: шесть плоскостей в мировых координатах.
void frustumFromViewProj(const Mat4& viewProj, float planes[6][4]);
bool aabbInFrustum(const float planes[6][4], float minX, float minY, float minZ,
                   float maxX, float maxY, float maxZ);
