#include "VoxelChunks.h"
#include "BlockTextures.h"
#include "Shaders.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
inline uint64_t chunkKey(int cx, int cz){
    return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}

// Шесть направлений и их грани. Порядок вершин — против часовой стрелки со стороны
// СНАРУЖИ блока: при включённом отсечении задних граней это ровно то, что нужно, а
// ошибка здесь выглядит как «мир просвечивает насквозь».
struct FaceDef {
    int dx, dy, dz;
    float verts[4][3];  // четыре угла грани в локальных координатах блока
    float nx, ny, nz;
};

// Оси, поперечные нормали грани: нужны для затенения по углам (см. vertexAO).
struct TangentAxes { int u[3]; int v[3]; };

const FaceDef kFaces[6] = {
    // +Y (верх)
    { 0, 1, 0, {{0,1,0},{0,1,1},{1,1,1},{1,1,0}}, 0, 1, 0 },
    // -Y (низ)
    { 0,-1, 0, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}}, 0,-1, 0 },
    // +X
    { 1, 0, 0, {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, 1, 0, 0 },
    // -X
    {-1, 0, 0, {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, -1, 0, 0 },
    // +Z
    { 0, 0, 1, {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}, 0, 0, 1 },
    // -Z
    { 0, 0,-1, {{0,0,0},{0,1,0},{1,1,0},{1,0,0}}, 0, 0,-1 },
};

// Затенение по углам (ambient occlusion). Именно оно делает кубическую картинку
// читаемой: без него соседние блоки одного типа сливаются в одну плоскую заливку, и
// глаз не различает ни ступенек рельефа, ни углов построек. Правило классическое: чем
// больше соседей у вершины, тем она темнее, а если оба боковых соседа заняты — угол
// затемняется полностью, независимо от диагонального.
float vertexAO(const VoxelWorld& w, int bx, int by, int bz, const FaceDef& f,
               float vx, float vy, float vz){
    // Тангенциальные оси грани — те, вдоль которых вершина «гуляет» по её плоскости.
    int u[3] = {0,0,0}, v[3] = {0,0,0};
    if(f.dx != 0){ u[1] = 1; v[2] = 1; }
    else if(f.dy != 0){ u[0] = 1; v[2] = 1; }
    else { u[0] = 1; v[1] = 1; }

    // Знак: вершина в 0 или в 1 по этой оси -> сосед слева или справа.
    float local[3] = { vx, vy, vz };
    int su = (local[u[0] ? 0 : (u[1] ? 1 : 2)] > 0.5f) ? 1 : -1;
    int sv = (local[v[0] ? 0 : (v[1] ? 1 : 2)] > 0.5f) ? 1 : -1;

    int nx = bx + f.dx, ny = by + f.dy, nz = bz + f.dz;
    auto solid = [&](int ax, int ay, int az){
        return blockIsSolid(w.blockAt(ax, ay, az)) ? 1 : 0;
    };
    int side1 = solid(nx + u[0]*su, ny + u[1]*su, nz + u[2]*su);
    int side2 = solid(nx + v[0]*sv, ny + v[1]*sv, nz + v[2]*sv);
    int corner = solid(nx + u[0]*su + v[0]*sv, ny + u[1]*su + v[1]*sv, nz + u[2]*su + v[2]*sv);

    int level = (side1 && side2) ? 0 : (3 - (side1 + side2 + corner));
    static const float kAO[4] = { 0.55f, 0.70f, 0.85f, 1.00f };
    return kAO[level];
}

// Разброса яркости по блокам больше нет. Он был нужен, пока блоки заливались одним
// цветом: без него поле выглядело одной краской. С текстурами рисунок и так у каждого
// блока свой, а разнобой яркости поверх него читался как грязь.
float blockShade(int, int, int){ return 1.0f; }

GLuint uploadVoxelMesh(const std::vector<VoxelVertex>& verts, GLuint& vboOut){
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)),
                 verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)(9*sizeof(float)));
    glBindVertexArray(0);
    vboOut = vbo;
    return vao;
}
} // namespace

void frustumFromViewProj(const Mat4& m, float planes[6][4]){
    // Плоскости извлекаются как суммы и разности строк матрицы (метод Грибба–Хартманна).
    const float* v = m.m;
    // Матрица хранится по столбцам: v[col*4 + row].
    auto row = [&](int r, int c){ return v[c*4 + r]; };
    for(int i = 0; i < 6; ++i){
        int r = i / 2;
        float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        for(int c = 0; c < 4; ++c)
            planes[i][c] = row(3, c) + sign * row(r, c);
        float len = sqrtf(planes[i][0]*planes[i][0] + planes[i][1]*planes[i][1] + planes[i][2]*planes[i][2]);
        if(len > 1e-6f){ planes[i][0]/=len; planes[i][1]/=len; planes[i][2]/=len; planes[i][3]/=len; }
    }
}

bool aabbInFrustum(const float planes[6][4], float minX, float minY, float minZ,
                   float maxX, float maxY, float maxZ){
    for(int i = 0; i < 6; ++i){
        // Берём «дальнюю» точку коробки по нормали плоскости: если даже она снаружи,
        // вся коробка снаружи.
        float x = planes[i][0] >= 0 ? maxX : minX;
        float y = planes[i][1] >= 0 ? maxY : minY;
        float z = planes[i][2] >= 0 ? maxZ : minZ;
        if(planes[i][0]*x + planes[i][1]*y + planes[i][2]*z + planes[i][3] < 0.0f) return false;
    }
    return true;
}

void VoxelRenderer::init(const VoxelWorld* world){
    shutdown();
    world_ = world;
}

void VoxelRenderer::freeChunk(ChunkMesh& m){
    if(m.vao){ glDeleteVertexArrays(1, &m.vao); glDeleteBuffers(1, &m.vbo); m.vao = 0; }
    if(m.waterVao){ glDeleteVertexArrays(1, &m.waterVao); glDeleteBuffers(1, &m.waterVbo); m.waterVao = 0; }
}

void VoxelRenderer::shutdown(){
    for(auto& kv : chunks_) freeChunk(kv.second);
    chunks_.clear();
    world_ = nullptr;
}

void VoxelRenderer::buildChunk(int cx, int cz){
    if(!world_) return;
    world_->ensureChunkDecor(cx, cz);

    std::vector<VoxelVertex> solid, water;
    solid.reserve(4096);

    const int x0 = cx * CHUNK_SIZE, z0 = cz * CHUNK_SIZE;

    // Высоты колонок нужны и для своего чанка, и на один блок вокруг — иначе на швах
    // между чанками появятся лишние (или пропущенные) боковые грани.
    int surf[CHUNK_SIZE + 2][CHUNK_SIZE + 2];
    for(int i = 0; i < CHUNK_SIZE + 2; ++i)
        for(int j = 0; j < CHUNK_SIZE + 2; ++j)
            surf[i][j] = world_->surfaceY(x0 + i - 1, z0 + j - 1);

    float minYUsed = 1e9f, maxYUsed = -1e9f;

    // Правки игрока расширяют диапазон высот чанка: выкопанная яма уходит ниже
    // «естественного» дна колонки, а построенная башня — выше кроны деревьев.
    int editMinY = 0, editMaxY = 0;
    world_->editYRange(cx, cz, editMinY, editMaxY);
    bool hasEdits = (editMinY <= editMaxY);

    for(int lx = 0; lx < CHUNK_SIZE; ++lx){
        for(int lz = 0; lz < CHUNK_SIZE; ++lz){
            int wx = x0 + lx, wz = z0 + lz;
            int s = surf[lx+1][lz+1];

            // Снизу опускаемся до самого низкого соседа: только там могут быть видимые
            // боковые грани. Глубже — сплошная земля, у которой не видно ни одной грани.
            int lowest = s;
            lowest = std::min(lowest, surf[lx][lz+1]);
            lowest = std::min(lowest, surf[lx+2][lz+1]);
            lowest = std::min(lowest, surf[lx+1][lz]);
            lowest = std::min(lowest, surf[lx+1][lz+2]);
            int yStart = std::max(0, lowest - 1);
            // Сверху — запас на деревья (до 12 блоков) и на уровень воды.
            int yEnd = std::max(s + 14, world_->waterLevelBlocks() + 1);
            if(hasEdits){
                yStart = std::max(0, std::min(yStart, editMinY - 1));
                yEnd = std::max(yEnd, editMaxY + 1);
            }

            for(int y = yStart; y <= yEnd; ++y){
                Block b = world_->blockAt(wx, y, wz);
                if(b == Block::Air) continue;

                const BlockInfo& info = blockInfo(b);
                bool isWater = (b == Block::Water);

                for(int f = 0; f < 6; ++f){
                    const FaceDef& face = kFaces[f];
                    Block neighbour = world_->blockAt(wx + face.dx, y + face.dy, wz + face.dz);
                    if(isWater){
                        // У воды рисуем только ВЕРХНИЕ грани, граничащие с воздухом.
                        // Боковые давали по всей глади тёмные крапинки: у каждой
                        // ступеньки дна торчал бок водяного блока, опущенного на 0.12 м,
                        // и эти полоски читались как мусор на воде.
                        if(face.dy <= 0) continue;
                        if(neighbour != Block::Air) continue;
                    } else {
                        if(!blockIsTransparent(neighbour)) continue;
                    }

                    // Цвет грани: без текстур — краска из таблицы блоков, с текстурами —
                    // цветовой фильтр поверх картинки. Верх чуть светлее бока в обоих
                    // случаях: так куб читается как объём.
                    float r, g, bl;
                    if(blockTexturesReady()){
                        blockTextureTint(b, r, g, bl);
                        if(face.dy <= 0){ r *= 0.94f; g *= 0.94f; bl *= 0.94f; }
                    } else if(face.dy > 0){ r = info.topR; g = info.topG; bl = info.topB; }
                    else                  { r = info.sideR; g = info.sideG; bl = info.sideB; }
                    float layer = (float)blockTextureLayer(b);

                    float shade = blockShade(wx, y, wz);
                    std::vector<VoxelVertex>& out = isWater ? water : solid;
                    VoxelVertex quad[4];
                    float aoLevels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    for(int k = 0; k < 4; ++k){
                        // У воды затенения по углам нет: она прозрачная, и тёмные углы
                        // на ней читаются как грязь, а не как объём.
                        float ao = isWater ? 1.0f
                                           : vertexAO(*world_, wx, y, wz, face,
                                                      face.verts[k][0], face.verts[k][1], face.verts[k][2]);
                        aoLevels[k] = ao;
                        float k2 = shade * ao;
                        // Координаты текстуры берём из двух осей грани, поперечных её
                        // нормали: тогда рисунок не «съезжает» при повороте куба и
                        // одинаково лежит на всех шести сторонах.
                        float u, v;
                        if(face.dx != 0){ u = face.verts[k][2]; v = face.verts[k][1]; }
                        else if(face.dy != 0){ u = face.verts[k][0]; v = face.verts[k][2]; }
                        else { u = face.verts[k][0]; v = face.verts[k][1]; }
                        // Дорога идёт с запада на восток, а её картинка нарисована
                        // «полотном вверх». Без этого разворота рисунок ложился поперёк
                        // дороги — колея шла не туда, куда едут.
                        if(b == Block::Road){ float t = u; u = v; v = t; }

                        quad[k] = VoxelVertex{
                            (float)wx + face.verts[k][0],
                            (float)y  + face.verts[k][1] - (isWater ? 0.12f : 0.0f), // вода чуть ниже края блока
                            (float)wz + face.verts[k][2],
                            face.nx, face.ny, face.nz,
                            r * k2, g * k2, bl * k2,
                            u, v, layer
                        };
                    }
                    // Разворот разбиения квада по затенению. Квад режется на два
                    // треугольника, и если затемнённые углы попали на разные половины,
                    // на грани появляется чёткая диагональная полоса — тот самый «рубец».
                    // Разворачиваем разрез так, чтобы он шёл между похожими по яркости
                    // углами, и полоса исчезает.
                    if(aoLevels[0] + aoLevels[2] < aoLevels[1] + aoLevels[3]){
                        out.push_back(quad[1]); out.push_back(quad[2]); out.push_back(quad[3]);
                        out.push_back(quad[1]); out.push_back(quad[3]); out.push_back(quad[0]);
                    } else {
                        out.push_back(quad[0]); out.push_back(quad[1]); out.push_back(quad[2]);
                        out.push_back(quad[0]); out.push_back(quad[2]); out.push_back(quad[3]);
                    }

                    minYUsed = std::min(minYUsed, (float)y);
                    maxYUsed = std::max(maxYUsed, (float)y + 1.0f);
                }
            }
        }
    }

    if(hasEdits) LOG_DEBUG("чанк %d,%d: правки %d..%d, вершин %zu", cx, cz, editMinY, editMaxY, solid.size());

    ChunkMesh mesh;
    mesh.centerX = (float)x0 + CHUNK_SIZE * 0.5f;
    mesh.centerZ = (float)z0 + CHUNK_SIZE * 0.5f;
    mesh.minY = (minYUsed > maxYUsed) ? 0.0f : minYUsed;
    mesh.maxY = (minYUsed > maxYUsed) ? 1.0f : maxYUsed;
    mesh.vertexCount = (int)solid.size();
    mesh.waterVertexCount = (int)water.size();
    if(!solid.empty()) mesh.vao = uploadVoxelMesh(solid, mesh.vbo);
    if(!water.empty()) mesh.waterVao = uploadVoxelMesh(water, mesh.waterVbo);

    auto it = chunks_.find(chunkKey(cx, cz));
    if(it != chunks_.end()){
        freeChunk(it->second);
        it->second = mesh;
    } else {
        chunks_[chunkKey(cx, cz)] = mesh;
    }
    ++stats_.chunksBuilt;
}

void VoxelRenderer::update(Vec3 camPos, float viewDistance, int buildBudget){
    if(!world_) return;
    stats_.chunksBuilt = 0;

    int camCx = (int)floorf(camPos.x / CHUNK_SIZE);
    int camCz = (int)floorf(camPos.z / CHUNK_SIZE);
    int radius = (int)ceilf(viewDistance / CHUNK_SIZE);

    // Сначала перестраиваем «грязные» — игрок только что сломал блок и ждёт результата
    // немедленно; отложить это на общий бюджет значит показать ему дыру через полсекунды.
    for(auto& kv : chunks_){
        if(!kv.second.dirty) continue;
        if(buildBudget <= 0) break;
        int cx = (int)(int32_t)(kv.first >> 32);
        int cz = (int)(int32_t)(kv.first & 0xFFFFFFFF);
        kv.second.dirty = false;
        buildChunk(cx, cz);
        --buildBudget;
        break; // карта могла перестроиться внутри buildChunk — продолжим на следующем кадре
    }

    // Затем достраиваем недостающие, начиная с ближних: дыра под ногами недопустима,
    // дыра на горизонте незаметна.
    struct Pending { int cx, cz; float dist2; };
    std::vector<Pending> pending;
    for(int cz = camCz - radius; cz <= camCz + radius; ++cz){
        for(int cx = camCx - radius; cx <= camCx + radius; ++cx){
            if(chunks_.count(chunkKey(cx, cz))) continue;
            float dx = ((float)cx + 0.5f) * CHUNK_SIZE - camPos.x;
            float dz = ((float)cz + 0.5f) * CHUNK_SIZE - camPos.z;
            float d2 = dx*dx + dz*dz;
            if(d2 > (viewDistance + CHUNK_SIZE) * (viewDistance + CHUNK_SIZE)) continue;
            pending.push_back({cx, cz, d2});
        }
    }
    std::sort(pending.begin(), pending.end(),
              [](const Pending& a, const Pending& b){ return a.dist2 < b.dist2; });
    for(size_t i = 0; i < pending.size() && buildBudget > 0; ++i, --buildBudget)
        buildChunk(pending[i].cx, pending[i].cz);

    // Выгрузка: с запасом в полтора радиуса, чтобы шаг назад не заставлял пересобирать
    // только что выброшенный чанк.
    float keep = viewDistance * 1.5f + CHUNK_SIZE;
    float keep2 = keep * keep;
    for(auto it = chunks_.begin(); it != chunks_.end(); ){
        float dx = it->second.centerX - camPos.x, dz = it->second.centerZ - camPos.z;
        if(dx*dx + dz*dz > keep2){
            freeChunk(it->second);
            it = chunks_.erase(it);
        } else ++it;
    }
    world_->pruneDecor(camPos.x, camPos.z, keep * 1.2f);
    stats_.chunksLoaded = (int)chunks_.size();
}

void VoxelRenderer::markDirty(int blockX, int blockY, int blockZ){
    (void)blockY;
    auto mark = [&](int cx, int cz){
        auto it = chunks_.find(chunkKey(cx, cz));
        if(it != chunks_.end()) it->second.dirty = true;
    };
    int cx = (int)floorf((float)blockX / CHUNK_SIZE);
    int cz = (int)floorf((float)blockZ / CHUNK_SIZE);
    mark(cx, cz);
    // Блок у границы виден и соседнему чанку: без этого на шве остаётся «призрак» грани.
    int lx = blockX - cx * CHUNK_SIZE, lz = blockZ - cz * CHUNK_SIZE;
    if(lx == 0) mark(cx - 1, cz);
    if(lx == CHUNK_SIZE - 1) mark(cx + 1, cz);
    if(lz == 0) mark(cx, cz - 1);
    if(lz == CHUNK_SIZE - 1) mark(cx, cz + 1);
}

void VoxelRenderer::renderOpaque(const Mat4& view, const Mat4& proj, Vec3 camPos, float viewDistance){
    Mat4 viewProj = mat4Multiply(proj, view);
    float planes[6][4];
    frustumFromViewProj(viewProj, planes);

    stats_.chunksDrawn = 0;
    stats_.trianglesDrawn = 0;
    float far2 = (viewDistance + CHUNK_SIZE) * (viewDistance + CHUNK_SIZE);

    for(const auto& kv : chunks_){
        const ChunkMesh& m = kv.second;
        if(!m.vao || m.vertexCount == 0) continue;
        float dx = m.centerX - camPos.x, dz = m.centerZ - camPos.z;
        if(dx*dx + dz*dz > far2) continue;
        float half = CHUNK_SIZE * 0.5f;
        if(!aabbInFrustum(planes, m.centerX - half, m.minY, m.centerZ - half,
                                   m.centerX + half, m.maxY, m.centerZ + half)) continue;
        glBindVertexArray(m.vao);
        glDrawArrays(GL_TRIANGLES, 0, m.vertexCount);
        ++stats_.chunksDrawn;
        stats_.trianglesDrawn += m.vertexCount / 3;
    }
    glBindVertexArray(0);
}

void VoxelRenderer::renderWater(const Mat4& view, const Mat4& proj, Vec3 camPos, float viewDistance){
    Mat4 viewProj = mat4Multiply(proj, view);
    float planes[6][4];
    frustumFromViewProj(viewProj, planes);
    float far2 = (viewDistance + CHUNK_SIZE) * (viewDistance + CHUNK_SIZE);

    for(const auto& kv : chunks_){
        const ChunkMesh& m = kv.second;
        if(!m.waterVao || m.waterVertexCount == 0) continue;
        float dx = m.centerX - camPos.x, dz = m.centerZ - camPos.z;
        if(dx*dx + dz*dz > far2) continue;
        float half = CHUNK_SIZE * 0.5f;
        if(!aabbInFrustum(planes, m.centerX - half, m.minY, m.centerZ - half,
                                   m.centerX + half, m.maxY, m.centerZ + half)) continue;
        glBindVertexArray(m.waterVao);
        glDrawArrays(GL_TRIANGLES, 0, m.waterVertexCount);
        stats_.trianglesDrawn += m.waterVertexCount / 3;
    }
    glBindVertexArray(0);
}
