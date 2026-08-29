#include "Terrain.h"
#include <algorithm>
#include "GL.h"

#include <cmath>
#include <map>
#include <vector>

namespace {

struct ChunkKey {
    int x, z;
    bool operator<(const ChunkKey& o) const { return x != o.x ? x < o.x : z < o.z; }
};

struct Chunk {
    Mesh mesh;
    float centerX = 0, centerZ = 0;
};

TerrainStreamConfig g_cfg;
TerrainHeightFn g_height = nullptr;
std::map<ChunkKey, Chunk> g_chunks;
std::vector<ChunkKey> g_pending;

// Аналитическая нормаль: центральные разности функции высоты. Освещение получается
// непрерывным независимо от плотности сетки — без этого рельеф выглядит «оригами»,
// а он занимает почти весь экран.
Vec3 normalAt(float x, float z){
    const float e = 1.5f;
    float hL = g_height(x - e, z), hR = g_height(x + e, z);
    float hD = g_height(x, z - e), hU = g_height(x, z + e);
    return v3norm(Vec3{ hL - hR, 2.0f * e, hD - hU });
}

void buildChunk(const ChunkKey& key){
    const float size = g_cfg.chunkSize;
    const int   res  = g_cfg.chunkRes;
    const float step = size / (float)res;
    const float baseX = (float)key.x * size;
    const float baseZ = (float)key.z * size;

    std::vector<Vertex> v;
    v.reserve((size_t)res * res * 6);

    // Вершины считаем построчно и переиспользуем предыдущую строку: высота — не самый
    // дешёвый запрос, а без переиспользования каждая точка сетки считалась бы четырежды.
    std::vector<Vec3> rowA((size_t)res + 1), rowB((size_t)res + 1);
    std::vector<Vec3> nrmA((size_t)res + 1), nrmB((size_t)res + 1);
    auto fillRow = [&](int iz, std::vector<Vec3>& pos, std::vector<Vec3>& nrm){
        float wz = baseZ + (float)iz * step;
        for(int ix = 0; ix <= res; ++ix){
            float wx = baseX + (float)ix * step;
            pos[(size_t)ix] = Vec3{ wx, g_height(wx, wz), wz };
            nrm[(size_t)ix] = normalAt(wx, wz);
        }
    };

    fillRow(0, rowA, nrmA);
    for(int iz = 0; iz < res; ++iz){
        fillRow(iz + 1, rowB, nrmB);
        for(int ix = 0; ix < res; ++ix){
            const Vec3& p00 = rowA[(size_t)ix];
            const Vec3& p10 = rowA[(size_t)ix + 1];
            const Vec3& p01 = rowB[(size_t)ix];
            const Vec3& p11 = rowB[(size_t)ix + 1];
            const Vec3& n00 = nrmA[(size_t)ix];
            const Vec3& n10 = nrmA[(size_t)ix + 1];
            const Vec3& n01 = nrmB[(size_t)ix];
            const Vec3& n11 = nrmB[(size_t)ix + 1];

            // UV привязаны к МИРОВЫМ координатам, а не к номеру чанка: иначе текстура
            // земли рвалась бы на границе каждого чанка.
            float u0 = p00.x * g_cfg.uvTilePerMeter, u1 = p10.x * g_cfg.uvTilePerMeter;
            float w0 = p00.z * g_cfg.uvTilePerMeter, w1 = p01.z * g_cfg.uvTilePerMeter;

            v.push_back({p00.x,p00.y,p00.z, u0,w0, n00.x,n00.y,n00.z});
            v.push_back({p11.x,p11.y,p11.z, u1,w1, n11.x,n11.y,n11.z});
            v.push_back({p10.x,p10.y,p10.z, u1,w0, n10.x,n10.y,n10.z});
            v.push_back({p00.x,p00.y,p00.z, u0,w0, n00.x,n00.y,n00.z});
            v.push_back({p01.x,p01.y,p01.z, u0,w1, n01.x,n01.y,n01.z});
            v.push_back({p11.x,p11.y,p11.z, u1,w1, n11.x,n11.y,n11.z});
        }
        rowA.swap(rowB);
        nrmA.swap(nrmB);
    }

    Chunk ch;
    ch.mesh = uploadMesh(v, 0);
    ch.centerX = baseX + size * 0.5f;
    ch.centerZ = baseZ + size * 0.5f;
    g_chunks[key] = ch;
}

void freeChunk(Chunk& ch){
    if(ch.mesh.vao){
        glDeleteVertexArrays(1, &ch.mesh.vao);
        glDeleteBuffers(1, &ch.mesh.vbo);
        ch.mesh.vao = 0;
    }
}

} // namespace

void terrainInit(const TerrainStreamConfig& cfg, TerrainHeightFn heightFn){
    terrainShutdown();
    g_cfg = cfg;
    g_height = heightFn;
}

void terrainShutdown(){
    for(auto& kv : g_chunks) freeChunk(kv.second);
    g_chunks.clear();
    g_pending.clear();
}

void terrainUpdate(Vec3 camPos, float radius){
    if(!g_height) return;
    const float size = g_cfg.chunkSize;
    int minX = (int)floorf((camPos.x - radius) / size);
    int maxX = (int)floorf((camPos.x + radius) / size);
    int minZ = (int)floorf((camPos.z - radius) / size);
    int maxZ = (int)floorf((camPos.z + radius) / size);

    // Чанки за границами карты не строим: там всё равно только океан, и вода рисуется
    // отдельной плоскостью.
    int limit = (int)(g_cfg.worldSize / size);
    if(minX < 0) minX = 0;
    if(minZ < 0) minZ = 0;
    if(maxX > limit) maxX = limit;
    if(maxZ > limit) maxZ = limit;

    // Собираем недостающие и сортируем по расстоянию: ближние появляются первыми,
    // поэтому дыра в рельефе не возникает прямо под ногами.
    g_pending.clear();
    for(int cz = minZ; cz <= maxZ; ++cz){
        for(int cx = minX; cx <= maxX; ++cx){
            ChunkKey key{cx, cz};
            if(g_chunks.count(key)) continue;
            float dx = ((float)cx + 0.5f) * size - camPos.x;
            float dz = ((float)cz + 0.5f) * size - camPos.z;
            if(dx*dx + dz*dz > (radius + size) * (radius + size)) continue;
            g_pending.push_back(key);
        }
    }
    if(!g_pending.empty()){
        float cx = camPos.x, cz = camPos.z;
        std::sort(g_pending.begin(), g_pending.end(), [&](const ChunkKey& a, const ChunkKey& b){
            float ax = ((float)a.x + 0.5f) * size - cx, az = ((float)a.z + 0.5f) * size - cz;
            float bx = ((float)b.x + 0.5f) * size - cx, bz = ((float)b.z + 0.5f) * size - cz;
            return ax*ax + az*az < bx*bx + bz*bz;
        });
        int builds = g_cfg.buildsPerFrame;
        for(size_t i = 0; i < g_pending.size() && builds > 0; ++i, --builds)
            buildChunk(g_pending[i]);
    }

    // Выгрузка: держим запас в полтора радиуса, чтобы шаг назад не заставлял
    // перестраивать только что выброшенный чанк.
    const float keep = (radius * 1.5f + size);
    const float keep2 = keep * keep;
    for(auto it = g_chunks.begin(); it != g_chunks.end(); ){
        float dx = it->second.centerX - camPos.x, dz = it->second.centerZ - camPos.z;
        if(dx*dx + dz*dz > keep2){
            freeChunk(it->second);
            it = g_chunks.erase(it);
        } else {
            ++it;
        }
    }
}

int terrainRender(Vec3 camPos, float radius){
    const float chunkHalf = g_cfg.chunkSize * 0.7072f; // половина диагонали чанка
    const float r = radius + chunkHalf;
    const float r2 = r * r;
    int drawn = 0;
    for(const auto& kv : g_chunks){
        float dx = kv.second.centerX - camPos.x, dz = kv.second.centerZ - camPos.z;
        if(dx*dx + dz*dz > r2) continue;
        glBindVertexArray(kv.second.mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, kv.second.mesh.vertexCount);
        ++drawn;
    }
    return drawn;
}

int terrainLoadedChunks(){ return (int)g_chunks.size(); }
int terrainPendingChunks(){ return (int)g_pending.size(); }
