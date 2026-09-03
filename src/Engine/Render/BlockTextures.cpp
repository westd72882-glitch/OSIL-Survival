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
    LAYER_SNOW,
    LAYER_STONE,
    LAYER_WOOD,
    LAYER_PLANKS,
    LAYER_SULFUR,
    LAYER_METAL,
    LAYER_LEAVES,
    LAYER_LEAVES_SNOW,
    LAYER_ROAD,
    LAYER_BARREL,
    LAYER_FURNACE,
    LAYER_CRATE,
    LAYER_BUILD,
    LAYER_WATER,
    LAYER_COUNT
};

const char* kLayerFiles[LAYER_COUNT] = {
    "block_ground.png",
    nullptr,            // снег — тот же грунт, обесцвеченный в белый (см. makeSnowLayer)
    "block_stone.png",
    "block_wood.png",
    "block_planks.png",
    "block_sulfur.png",
    "block_metal.png",
    "block_leaves.png",
    nullptr,            // заснеженная листва — та же картинка, обесцвеченная
    "block_road.png",
    nullptr,            // бочка рисуется на месте: ржавый бок с обручами
    "block_furnace.png",
    "block_crate.png",
    "block_build.png",
    "block_water.png",
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

// Снег из грунта. Умножением цвета белым его не сделать: множитель поднимает яркость,
// но оставляет тёплый песочный оттенок, и зима выглядела бледной пустыней. Поэтому
// цвет сводится к своей яркости (обесцвечивание) и затем осветляется — это и есть
// «тот же песок под белым фильтром», только фильтр честный.
void makeSnowLayer(const std::vector<uint8_t>& ground, std::vector<uint8_t>& out, float lift){
    out.assign((size_t)LAYER_SIZE * LAYER_SIZE * 4, 255);
    for(size_t i = 0; i + 3 < ground.size(); i += 4){
        float r = ground[i] / 255.0f, g = ground[i+1] / 255.0f, b = ground[i+2] / 255.0f;
        float lum = 0.299f * r + 0.587f * g + 0.114f * b;
        // Немного исходного цвета оставляем: совсем плоский белый теряет зернистость.
        const float KEEP = 0.12f;
        float nr = lum + (r - lum) * KEEP;
        float ng = lum + (g - lum) * KEEP;
        float nb = lum + (b - lum) * KEEP;
        // Осветление к белому. Насколько — зависит от исходника: песок и так светлый,
        // а листва тёмная, и без сильного подъёма она становится не снегом, а сажей.
        const float LIFT = lift;
        nr += (1.0f - nr) * LIFT;
        ng += (1.0f - ng) * LIFT;
        nb += (1.0f - nb) * (LIFT + 0.06f);   // чуть холоднее, синева читается как снег
        out[i]   = (uint8_t)(nr * 255.0f + 0.5f);
        out[i+1] = (uint8_t)(ng * 255.0f + 0.5f);
        out[i+2] = (uint8_t)(nb > 1.0f ? 255.0f : nb * 255.0f + 0.5f);
        out[i+3] = ground[i+3];
    }
}

// Бочка: ржавый бок с двумя обручами. Картинки бочки в наборе нет, а рисунок ей нужен
// узнаваемый — иначе у дороги стоят просто рыжие кубы.
void makeBarrelLayer(std::vector<uint8_t>& out){
    out.assign((size_t)LAYER_SIZE * LAYER_SIZE * 4, 255);
    for(int y = 0; y < LAYER_SIZE; ++y){
        // Два тёмных обруча по высоте и лёгкая вертикальная штриховка «жести».
        float fy = (float)y / LAYER_SIZE;
        bool band = (fy > 0.18f && fy < 0.28f) || (fy > 0.72f && fy < 0.82f);
        for(int x = 0; x < LAYER_SIZE; ++x){
            uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            float grain = 0.88f + (float)(h & 0x3F) / 63.0f * 0.22f;
            float r = 0.62f, g = 0.30f, b = 0.17f;
            if(band){ r = 0.34f; g = 0.20f; b = 0.14f; }
            uint8_t* p = &out[((size_t)y * LAYER_SIZE + x) * 4];
            p[0] = (uint8_t)(SDL_min(r * grain, 1.0f) * 255.0f);
            p[1] = (uint8_t)(SDL_min(g * grain, 1.0f) * 255.0f);
            p[2] = (uint8_t)(SDL_min(b * grain, 1.0f) * 255.0f);
            p[3] = 255;
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
            // Две волны разной частоты и слабый контраст: одна синусоида давала
            // крупную «чешую», которая на большой глади бросалась в глаза сильнее
            // самой воды. Пока своей картинки воды нет, рябь должна быть еле заметной.
            float wave = SDL_sinf(fx * 25.13f + SDL_sinf(fy * 12.57f) * 0.6f) * 0.5f
                       + SDL_sinf(fy * 37.70f + SDL_sinf(fx * 18.85f) * 0.4f) * 0.5f;
            float v = 0.94f + wave * 0.06f;
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
    std::vector<uint8_t> groundPixels, leavesPixels;
    for(int i = 0; i < LAYER_COUNT; ++i){
        if(false){
        } else if(i == LAYER_SNOW){
            makeSnowLayer(groundPixels, buffer, 0.34f);
        } else if(i == LAYER_BARREL){
            makeBarrelLayer(buffer);
        } else if(i == LAYER_LEAVES_SNOW){
            makeSnowLayer(leavesPixels, buffer, 0.80f);
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
            if(i == LAYER_GROUND) groundPixels = buffer;   // из него делается снег
            if(i == LAYER_LEAVES) leavesPixels = buffer;   // а из листвы — заснеженная
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
    SDL_Log("Текстуры блоков: загружено %d слоёв из %d", loaded, LAYER_COUNT - 3);
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
        case Block::LeavesSnow: return LAYER_LEAVES_SNOW;
        case Block::Water:      return LAYER_WATER;
        case Block::Snow:       return LAYER_SNOW;
        case Block::Road:       return LAYER_ROAD;
        case Block::Barrel:     return LAYER_BARREL;
        case Block::Furnace:    return LAYER_FURNACE;
        case Block::Crate:      return LAYER_CRATE;
        case Block::Foundation:
        case Block::BuildWall:
        case Block::BuildWallZ:
        case Block::BuildDoorZ:
        case Block::BuildFloor:
        case Block::BuildDoor:  return LAYER_BUILD;
        default:                return LAYER_GROUND;   // песок, снег, трава, земля, жижа
    }
}

void blockTextureTint(Block b, float& r, float& g, float& bl){
    // Фильтр поверх текстуры. Грунт один на четыре блока: пустыня — как есть, зима —
    // осветлённый в белый, равнина — светло-салатовый, земля — коричневатый.
    switch(b){
        case Block::Sand:       r = 1.00f; g = 0.98f; bl = 0.92f; break;
        // Снегу свой слой уже обесцвечен, поэтому здесь фильтр почти нейтральный.
        case Block::Snow:       r = 1.00f; g = 1.01f; bl = 1.03f; break;
        case Block::Grass:      r = 0.62f; g = 1.05f; bl = 0.52f; break;
        case Block::Dirt:       r = 0.78f; g = 0.62f; bl = 0.46f; break;
        case Block::Mud:        r = 0.55f; g = 0.52f; bl = 0.40f; break;
        case Block::Leaves:     r = 0.85f; g = 1.05f; bl = 0.75f; break;
        // Заснеженной листве свой слой уже обесцвечен — фильтр почти нейтральный.
        case Block::LeavesSnow: r = 1.00f; g = 1.01f; bl = 1.04f; break;
        case Block::Water:      r = 1.00f; g = 1.00f; bl = 1.00f; break;
        case Block::StoneBrick: r = 1.05f; g = 1.05f; bl = 1.02f; break;
        // У печи, ящика и построек свои картинки — фильтр нейтральный. Дверь чуть
        // темнее стены, чтобы проём читался.
        case Block::BuildDoor:
        case Block::BuildDoorZ: r = 0.78f; g = 0.74f; bl = 0.70f; break;
        default:                r = 1.00f; g = 1.00f; bl = 1.00f; break;
    }
}
