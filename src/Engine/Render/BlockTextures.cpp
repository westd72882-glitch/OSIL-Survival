#include "BlockTextures.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "../Core/Paths.h"
#include <vector>

namespace {

// Размер слоя. 256 — предел, за которым на телефоне разницы уже не видно, а память
// растёт вчетверо с каждым удвоением.
const int LAYER_SIZE = 256;

// Порядок слоёв. Один и тот же грунт используется песком, снегом, травой и землёй —
// различаются только фильтром (см. blockTextureTint).
enum Layer {
    LAYER_GROUND = 0,
    LAYER_STONE,
    LAYER_WOOD,
    LAYER_PLANKS,
    LAYER_SULFUR,
    LAYER_METAL,
    LAYER_LEAVES,
    LAYER_WATER,
    LAYER_COUNT
};

const char* kLayerFiles[LAYER_COUNT] = {
    "block_ground.png",
    "block_stone.png",
    "block_wood.png",
    "block_planks.png",
    "block_sulfur.png",
    "block_metal.png",
    "block_leaves.png",
    nullptr,            // вода рисуется процедурно, файла для неё нет
};

GLuint g_array = 0;
bool g_ready = false;

// Растягивает поверхность до LAYER_SIZE x LAYER_SIZE усреднением. Своя функция вместо
// SDL_BlitScaled: та требует совпадения форматов и на некоторых сборках SDL отключена.
void scaleInto(SDL_Surface* src, std::vector<uint8_t>& out){
    out.assign((size_t)LAYER_SIZE * LAYER_SIZE * 4, 255);
    const uint8_t* pixels = (const uint8_t*)src->pixels;
    for(int y = 0; y < LAYER_SIZE; ++y){
        int sy = y * src->h / LAYER_SIZE;
        for(int x = 0; x < LAYER_SIZE; ++x){
            int sx = x * src->w / LAYER_SIZE;
            const uint8_t* s = pixels + (size_t)sy * src->pitch + (size_t)sx * 4;
            uint8_t* d = &out[((size_t)y * LAYER_SIZE + x) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
}

// Процедурная вода: спокойная рябь. Отдельной картинки для неё нет, а плоская заливка
// на большой глади выглядит как пластик.
void makeWaterLayer(std::vector<uint8_t>& out){
    out.assign((size_t)LAYER_SIZE * LAYER_SIZE * 4, 255);
    for(int y = 0; y < LAYER_SIZE; ++y){
        for(int x = 0; x < LAYER_SIZE; ++x){
            float fx = (float)x / LAYER_SIZE, fy = (float)y / LAYER_SIZE;
            float wave = 0.5f + 0.5f * SDL_sinf(fx * 12.566f) * SDL_sinf(fy * 9.42f);
            float v = 0.72f + wave * 0.28f;
            uint8_t* d = &out[((size_t)y * LAYER_SIZE + x) * 4];
            d[0] = (uint8_t)(120 * v);
            d[1] = (uint8_t)(190 * v);
            d[2] = (uint8_t)(230 * v);
            d[3] = 255;
        }
    }
}

} // namespace

bool blockTexturesInit(){
    blockTexturesShutdown();

    glGenTextures(1, &g_array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_array);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, LAYER_SIZE, LAYER_SIZE, LAYER_COUNT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    int loaded = 0;
    std::vector<uint8_t> buffer;
    for(int i = 0; i < LAYER_COUNT; ++i){
        if(!kLayerFiles[i]){
            makeWaterLayer(buffer);
        } else {
            SDL_Surface* raw = IMG_Load(assetPath(kLayerFiles[i]).c_str());
            if(!raw){
                SDL_Log("Текстура блока не найдена: %s (%s)", kLayerFiles[i], IMG_GetError());
                continue;
            }
            SDL_Surface* conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
            SDL_FreeSurface(raw);
            if(!conv) continue;
            scaleInto(conv, buffer);
            SDL_FreeSurface(conv);
            ++loaded;
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, LAYER_SIZE, LAYER_SIZE, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    g_ready = (loaded > 0);
    SDL_Log("Текстуры блоков: загружено %d слоёв из %d", loaded, LAYER_COUNT - 1);
    return g_ready;
}

void blockTexturesShutdown(){
    if(g_array){ glDeleteTextures(1, &g_array); g_array = 0; }
    g_ready = false;
}

bool blockTexturesReady(){ return g_ready; }
GLuint blockTextureArray(){ return g_array; }

int blockTextureLayer(Block b){
    switch(b){
        case Block::Stone:
        case Block::StoneBrick: return LAYER_STONE;
        case Block::Wood:       return LAYER_WOOD;
        case Block::Planks:     return LAYER_PLANKS;
        case Block::OreSulfur:  return LAYER_SULFUR;
        case Block::OreMetal:   return LAYER_METAL;
        case Block::Leaves:     return LAYER_LEAVES;
        case Block::Water:      return LAYER_WATER;
        default:                return LAYER_GROUND;   // песок, снег, трава, земля, жижа
    }
}

void blockTextureTint(Block b, float& r, float& g, float& bl){
    // Фильтр поверх текстуры. Грунт один на четыре блока: пустыня — как есть, зима —
    // осветлённый в белый, равнина — светло-салатовый, земля — коричневатый.
    switch(b){
        case Block::Sand:       r = 1.00f; g = 0.98f; bl = 0.92f; break;
        case Block::Snow:       r = 1.35f; g = 1.42f; bl = 1.50f; break;
        case Block::Grass:      r = 0.62f; g = 1.05f; bl = 0.52f; break;
        case Block::Dirt:       r = 0.78f; g = 0.62f; bl = 0.46f; break;
        case Block::Mud:        r = 0.55f; g = 0.52f; bl = 0.40f; break;
        case Block::Leaves:     r = 0.85f; g = 1.05f; bl = 0.75f; break;
        case Block::Water:      r = 0.85f; g = 0.95f; bl = 1.05f; break;
        case Block::StoneBrick: r = 1.05f; g = 1.05f; bl = 1.02f; break;
        default:                r = 1.00f; g = 1.00f; bl = 1.00f; break;
    }
}
