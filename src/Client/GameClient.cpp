#include "GameClient.h"

#include "../Core/Log.h"
#include "../Core/PngWriter.h"
#include "../Core/Random.h"
#include "../Core/Time.h"
#include "../Engine/Core/Audio.h"
#include "../Engine/Core/Console.h"
#include "../Engine/Core/Paths.h"
#include "../Engine/Core/Settings.h"
#include "../Engine/Core/Window.h"
#include "../Engine/Render/AssetLoader.h"
#include "../Engine/Render/BlockTextures.h"
#include "../Engine/Render/Framebuffer.h"
#include "../Engine/Render/Primitives.h"
#include "../Engine/Render/Shaders.h"
#include "../Engine/Render/UIStyle.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
// Сид мира: одиночная игра идёт на той же карте, что и сервер с этим сидом.
const char* WORLD_SEED_TEXT = "osil";

// Дальность прорисовки задаётся в настройках; уровень качества дополнительно её
// поджимает. В кубическом мире это главный расход: каждый метр — ещё кольцо чанков,
// которое надо собрать и нарисовать.

// Рецепты: минимальный набор, чтобы добытое сырьё уже приносило пользу.
// В списке только то, что действительно работает прямо сейчас. Доски и каменные блоки
// убраны — их некуда ставить, строительства нет; переплавка руды и порох ждут печи и
// верстака. Пустых строчек «на будущее» в крафте быть не должно.
const Recipe kRecipes[] = {
    { ItemType::Torch, 1, ItemType::Wood,  2, ItemType::None,  0,
      "Светит в темноте. Возьмите в руки, выбрав в поясе." },
    { ItemType::Axe,   1, ItemType::Wood, 10, ItemType::Stone, 6,
      "Каменный топор. С ним добыча идёт вдвое быстрее, чем голыми руками." },
    { ItemType::Furnace, 1, ItemType::Stone, 50, ItemType::None, 0,
      "Печь. Поставьте её на землю и плавьте в ней руду, топя дровами." },
    { ItemType::BuildPlan, 1, ItemType::Wood, 5, ItemType::None, 0,
      "План постройки. Возьмите в руки — появятся кнопки стройки." },
    { ItemType::Gunpowder, 5, ItemType::Sulfur, 3, ItemType::Wood, 2,
      "Порох из серы и угля. Пойдёт на патроны и взрывчатку." },
    { ItemType::Box, 1, ItemType::Wood, 60, ItemType::None, 0,
      "Ящик. Поставьте в доме и складывайте в него ресурсы: в рюкзаке места мало." },
    { ItemType::Grenade, 1, ItemType::Gunpowder, 20, ItemType::MetalFrag, 20,
      "Граната. Возьмите в руки и нажмите удар — полетит. Взрывается через три секунды: "
      "сносит 50 прочности постройке и до 150 здоровья тому, кто рядом." },
    { ItemType::Cupboard, 1, ItemType::Wood, 100, ItemType::None, 0,
      "Шкаф дома. В него кладут дерево на аренду: 10 дерева в сутки за каждую деталь "
      "постройки. Не заплатил — дом начинает гнить." },
};
const int kRecipeCount = (int)(sizeof(kRecipes)/sizeof(kRecipes[0]));

TTF_Font* openAnyFont(int size){
    // Своего шрифта в репозитории нет (лицензии), берём системный: на Android это
    // Roboto, на настольной Linux — DejaVu. Нет ни одного — игра идёт без надписей.
    const char* candidates[] = {
        "font.ttf", "font.otf", "assets/font.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for(const char* path : candidates){
        TTF_Font* f = TTF_OpenFont(path, size);
        if(f){ SDL_Log("Шрифт: %s", path); return f; }
    }
    SDL_Log("Шрифт не найден — интерфейс будет без текста");
    return nullptr;
}
} // namespace

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

bool GameClient::initPlatform(){
    // Отключаем превращение касаний в события мыши ДО SDL_Init: иначе каждое касание
    // приходит дважды — как палец и как мышь, — и интерфейс срабатывает по два раза,
    // а камера и добыча включаются от касания где угодно.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    consoleInit();
    initWritablePaths();
    loadSettings();
    // Список серверов и имя игрока лежат рядом с настройками: меню — это браузер
    // серверов, и без списка ему нечего показывать.
    loadServerList();
    // https-транспорт поднимаем в главном потоке: на Android ссылку на Java-класс
    // можно получить только отсюда, из сетевого потока её уже не найти.
    net::initSecureTransport();

    if(TTF_Init() != 0) SDL_Log("TTF_Init: %s", TTF_GetError());
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    if(!audioInit()) SDL_Log("Звук не инициализирован — играем молча");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

#ifdef __ANDROID__
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#endif
    // По умолчанию окно 1280x720. Ключ --size WxH нужен, чтобы на сборочной машине
    // проверить раскладку под вытянутый экран телефона (20:9) — иначе подписи карты и
    // кнопки проверяются только в одном соотношении сторон.
    SCR_W = forcedW_ > 0 ? forcedW_ : 1280;
    SCR_H = forcedH_ > 0 ? forcedH_ : 720;
    win = SDL_CreateWindow("OSIL Survival", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           SCR_W, SCR_H, flags);
    if(!win){ SDL_Log("SDL_CreateWindow: %s", SDL_GetError()); return false; }

    glCtx = SDL_GL_CreateContext(win);
    if(!glCtx){ SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError()); return false; }
    SDL_GL_GetDrawableSize(win, &SCR_W, &SCR_H);

    // Вертикальная синхронизация ВЫКЛЮЧЕНА: она привязывает кадр к развёртке экрана и
    // прячет настоящую производительность — по счётчику в углу видно ровно 60, даже
    // если запас втрое больше или, наоборот, кадр не успевает. Темп задаёт свой
    // ограничитель (settings.fpsLimit), он же экономит батарею.
    SDL_GL_SetSwapInterval(0);

    uiFont = openAnyFont(28);
    return true;
}

bool GameClient::initGraphics(){
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    voxelProg = linkProgram(voxelVS, voxelFS);
    voxelViewLoc        = glGetUniformLocation(voxelProg, "uView");
    voxelProjLoc        = glGetUniformLocation(voxelProg, "uProj");
    voxelLightDirLoc    = glGetUniformLocation(voxelProg, "uLightDir");
    voxelLightAmountLoc = glGetUniformLocation(voxelProg, "uLightAmount");
    voxelFogColorLoc    = glGetUniformLocation(voxelProg, "uFogColor");
    voxelFogDensityLoc  = glGetUniformLocation(voxelProg, "uFogDensity");
    voxelCamPosLoc      = glGetUniformLocation(voxelProg, "uCamPos");
    voxelAlphaLoc       = glGetUniformLocation(voxelProg, "uAlpha");
    voxelBlocksLoc      = glGetUniformLocation(voxelProg, "uBlocks");
    voxelTexturedLoc    = glGetUniformLocation(voxelProg, "uTextured");

    skyProg = linkProgram(skyVS, skyFS);
    skyTimeLoc        = glGetUniformLocation(skyProg, "uTime");
    skySunDirLoc      = glGetUniformLocation(skyProg, "uSunDir");
    skyLightAmountLoc = glGetUniformLocation(skyProg, "uLightAmount");
    skyCamRightLoc    = glGetUniformLocation(skyProg, "uCamRight");
    skyCamUpLoc       = glGetUniformLocation(skyProg, "uCamUp");
    skyCamForwardLoc  = glGetUniformLocation(skyProg, "uCamForward");
    skyTanHalfFovLoc  = glGetUniformLocation(skyProg, "uTanHalfFov");
    skyAspectLoc      = glGetUniformLocation(skyProg, "uAspect");
    skyFogColorLoc    = glGetUniformLocation(skyProg, "uFogColor");
    // Небо рисуется треугольником без атрибутов, но пустой VAO в GLES 3.0 обязателен.
    glGenVertexArrays(1, &skyVao_);

    uiProg = linkProgram(uiVS, uiFS);
    uiProjLoc       = glGetUniformLocation(uiProg, "uProj");
    uiTexLoc        = glGetUniformLocation(uiProg, "uTex");
    uiColorLoc      = glGetUniformLocation(uiProg, "uColor");
    uiUseTextureLoc = glGetUniformLocation(uiProg, "uUseTexture");

    postProg = linkProgram(postVS, postFS);
    postProjLoc = glGetUniformLocation(postProg, "uProj");
    postTexLoc  = glGetUniformLocation(postProg, "uTex");
    postTimeLoc = glGetUniformLocation(postProg, "uTime");
    postResLoc  = glGetUniformLocation(postProg, "uResolution");
    GLint linked = GL_FALSE;
    glGetProgramiv(postProg, GL_LINK_STATUS, &linked);
    postProgOk_ = (linked == GL_TRUE);

    initUIQuad();
    initUICircle();
    blockTexturesInit();
    loadInterfaceTextures();

    // Рамка выделенного блока: 12 рёбер куба, обновляются каждый кадр.
    glGenVertexArrays(1, &highlightVao_);
    glGenBuffers(1, &highlightVbo_);
    glBindVertexArray(highlightVao_);
    glBindBuffer(GL_ARRAY_BUFFER, highlightVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VoxelVertex) * 24, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void*)(6*sizeof(float)));
    glBindVertexArray(0);

    // Динамические буферы: частицы от разбитой жилы и топорик в руке. И то и другое —
    // обычные воксельные вершины, поэтому шейдер тот же, что у мира.
    auto makeDynamicVoxelBuffer = [](GLuint& vao, GLuint& vbo, int verts){
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(VoxelVertex) * verts, nullptr, GL_DYNAMIC_DRAW);
        for(int i = 0; i < 3; ++i){
            glEnableVertexAttribArray((GLuint)i);
            glVertexAttribPointer((GLuint)i, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex),
                                  (void*)(intptr_t)(i * 3 * sizeof(float)));
        }
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex),
                              (void*)(intptr_t)(9 * sizeof(float)));
        glBindVertexArray(0);
    };
    makeDynamicVoxelBuffer(partVao_, partVbo_, 64 * 36);
    makeDynamicVoxelBuffer(heldVao_, heldVbo_, 8 * 36);
    // Буфер под срубленные деревья: ствол с кроной — это сотни кубов, и они рисуются
    // одним вызовом.
    makeDynamicVoxelBuffer(fallVao_, fallVbo_, 1024 * 36);
    // Буфер под чужих игроков: у каждого дюжина брусков.
    makeDynamicVoxelBuffer(remoteVao_, remoteVbo_, 24 * 12 * 36);

    return true;
}

// ==================== ЧАСТИЦЫ И ПРЕДМЕТ В РУКЕ ====================
// Осколки от выработанной жилы и топорик в руке — это маленькие кубы, поэтому рисуются
// тем же воксельным шейдером, что и мир: отдельной системы частиц заводить незачем.

namespace {
// Один кубик в набор вершин. Центр, полуразмер, цвет и слой текстуры.
void pushCube(std::vector<VoxelVertex>& out, Vec3 c, float half,
              float r, float g, float b, float layer){
    static const float N[6][3] = { {0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1} };
    for(int f = 0; f < 6; ++f){
        const float* n = N[f];
        int axis = (n[0] != 0) ? 0 : (n[1] != 0) ? 1 : 2;
        int a = (axis + 1) % 3, b2 = (axis + 2) % 3;
        float centre[3] = { c.x, c.y, c.z };
        float corner[4][3];
        for(int k = 0; k < 4; ++k){
            float sa = (k == 0 || k == 3) ? -1.0f : 1.0f;
            float sb = (k < 2) ? -1.0f : 1.0f;
            corner[k][axis] = centre[axis] + half * (n[axis] > 0 ? 1.0f : -1.0f);
            corner[k][a] = centre[a] + half * sa;
            corner[k][b2] = centre[b2] + half * sb;
        }
        // Плоское затенение по нормали — как у граней мира.
        float face = (n[1] > 0.5f) ? 1.0f : (n[1] < -0.5f ? 0.5f : (fabsf(n[0]) > 0.5f ? 0.75f : 0.88f));
        VoxelVertex q[4];
        for(int k = 0; k < 4; ++k){
            float uu = (k == 1 || k == 2) ? 1.0f : 0.0f;
            float vv = (k >= 2) ? 1.0f : 0.0f;
            q[k] = VoxelVertex{ corner[k][0], corner[k][1], corner[k][2],
                                n[0], n[1], n[2],
                                r * face, g * face, b * face, uu, vv, layer };
        }
        out.push_back(q[0]); out.push_back(q[1]); out.push_back(q[2]);
        out.push_back(q[0]); out.push_back(q[2]); out.push_back(q[3]);
    }
}
} // namespace

void GameClient::spawnBreakParticles(Block block, int x, int y, int z){
    float tr, tg, tb;
    blockTextureTint(block, tr, tg, tb);
    float layer = (float)blockTextureLayer(block);
    Rng rng((uint64_t)(x * 73856093 ^ y * 19349663 ^ z * 83492791) ^ 0xB00Bu);
    // Двух десятков осколков хватает: больше на телефоне только шумит.
    for(int i = 0; i < 22; ++i){
        Particle p;
        p.pos = Vec3{ (float)x + rng.nextRange(0.1f, 0.9f),
                      (float)y + rng.nextRange(0.1f, 0.9f),
                      (float)z + rng.nextRange(0.1f, 0.9f) };
        p.vel = Vec3{ rng.nextRange(-2.4f, 2.4f), rng.nextRange(1.5f, 4.5f),
                      rng.nextRange(-2.4f, 2.4f) };
        p.life = rng.nextRange(0.7f, 1.3f);
        p.size = rng.nextRange(0.045f, 0.10f);
        p.r = tr; p.g = tg; p.b = tb; p.layer = layer;
        if(particles_.size() < 64) particles_.push_back(p);
    }
}

void GameClient::updateParticles(float dt){
    for(size_t i = 0; i < particles_.size(); ){
        Particle& p = particles_[i];
        p.life -= dt;
        if(p.life <= 0.0f){
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.vel.y -= 12.0f * dt;                 // осколки падают
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;
        ++i;
    }
}

// ==================== ПРОЧНОСТЬ, ШКАФ И ЯЩИК ====================
// Прочность показывается только у побитой детали: у целого дома цифры над каждой
// стеной — это мусор на экране, а вот «осталось 60 из 100» игрок должен видеть.
void GameClient::renderBuildTargetInfo(){
    if(overlay_ != Overlay::None) return;
    const RayHit& t = player_->target();
    if(!t.hit || !isBuildBlock(t.block)) return;
    int idx = pieceIndexAt(t.x, t.y, t.z);
    if(idx < 0) return;
    const BuildPiece& p = pieces_[(size_t)idx];
    if(p.health >= 95) return;      // целая деталь молчит

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float cx = (float)SCR_W * 0.5f, cy = (float)SCR_H * 0.5f;
    float w = 190.0f * s, h = 14.0f * s;
    float y = cy + 54.0f * s;
    drawUIRect(cx - w * 0.5f, y, w, h, 0, 0.06f, 0.06f, 0.07f, 0.60f, false);
    float k = clampf((float)p.health / (float)PIECE_MAX_HEALTH, 0.0f, 1.0f);
    drawUIRect(cx - w * 0.5f, y, w * k, h, 0,
               k > 0.5f ? 0.55f : 0.80f, k > 0.5f ? 0.72f : 0.42f, 0.32f, 0.85f, false);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  %d/%d", blockName(p.block), p.health, PIECE_MAX_HEALTH);
    drawTextCentered(cx, y + h + 3.0f * s, 16.0f * s, buf, 1, 1, 1, 0.92f);
}

// ---- Аренда дома. Раз в игровые сутки за каждую деталь дома со шкафа списывается
// 10 дерева. Не заплатил — дом начинает гнить: прочность деталей падает, и в конце
// концов постройка разваливается сама.
void GameClient::updateUpkeep(){
    int day = env_->dayNumber();
    if(day == lastUpkeepDay_) return;
    lastUpkeepDay_ = day;
    if(pieces_.empty()) return;

    int need = upkeepPerDay();
    int paid = 0;
    for(WorldCupboard& c : cupboards_){
        if(paid >= need) break;
        int take = need - paid;
        if(take > c.wood) take = c.wood;
        c.wood -= take;
        paid += take;
    }
    char buf[128];
    if(paid >= need){
        snprintf(buf, sizeof(buf), "Аренда дома оплачена: -%d дерева", need);
        SDL_Log("%s", buf);
        return;
    }
    // Недоплата — гниение: чем больше долг, тем быстрее сыпется постройка.
    int shortfall = need - paid;
    int rot = 5 + shortfall / 10;
    if(rot > 25) rot = 25;
    for(size_t i = 0; i < pieces_.size(); ){
        pieces_[i].health -= rot;
        if(pieces_[i].health <= 0){
            fillPieceCells(pieces_[i], false);
            pieces_.erase(pieces_.begin() + (long)i);
        } else ++i;
    }
    snprintf(buf, sizeof(buf), "Дом гниёт: не хватило %d дерева в шкафу", shortfall);
    SDL_Log("%s", buf);
}

// ---- Окно ящика: сетка хранилища сверху, рюкзак снизу. Касание по ячейке перекладывает
// стак туда-обратно — на телефоне это понятнее, чем перетаскивание между двумя сетками.
// Ящик и рюкзак стоят рядом двумя сетками: так на телефоне видно и то, и другое, и
// ничто не залезает на пояс внизу экрана. Ящик — 3 столбца, рюкзак — свои 6.
namespace {
const int BOX_COLS = 3;
} // namespace

void GameClient::boxSlotPos(int i, float& x, float& y, float& slot) const {
    float sc = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    slot = 72.0f * sc;
    float gap = 8.0f * sc;
    int boxRows = (BOX_SLOTS + BOX_COLS - 1) / BOX_COLS;
    (void)boxRows;
    float boxW = slot * BOX_COLS + gap * (BOX_COLS - 1);
    float bagW = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
    float totalW = boxW + 46.0f * sc + bagW;
    float gx = ((float)SCR_W - totalW) * 0.5f;
    float gy = (float)SCR_H * 0.22f;
    if(i < 0){                       // служебный вызов: левый верхний угол рюкзака
        x = gx + boxW + 46.0f * sc;
        y = gy;
        return;
    }
    x = gx + (i % BOX_COLS) * (slot + gap);
    y = gy + (i / BOX_COLS) * (slot + gap);
}

void GameClient::renderBox(){
    if(openBox_ < 0 || openBox_ >= (int)boxes_.size()){ overlay_ = Overlay::None; return; }
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    const WorldBox& box = boxes_[(size_t)openBox_];
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.05f, 0.06f, 0.32f, false);

    float x0, y0, slot;
    boxSlotPos(0, x0, y0, slot);
    float bagX, bagY, unused;
    boxSlotPos(-1, bagX, bagY, unused);
    float gap = 8.0f * s;

    drawText(x0, y0 - 34.0f * s, 24.0f * s, "ЯЩИК", 1, 1, 1, 0.96f);
    drawText(bagX, bagY - 34.0f * s, 24.0f * s, "РЮКЗАК", 1, 1, 1, 0.96f);
    for(int i = 0; i < BOX_SLOTS; ++i){
        float x, y, sl;
        boxSlotPos(i, x, y, sl);
        drawSlot(x, y, sl, box.slots[i], false);
    }
    for(int i = Inventory::HOTBAR; i < Inventory::SIZE; ++i){
        int k = i - Inventory::HOTBAR;
        float x = bagX + (k % Inventory::COLS) * (slot + gap);
        float y = bagY + (k / Inventory::COLS) * (slot + gap);
        drawSlot(x, y, slot, inventory_.slot(i), false);
    }
    drawTextCentered((float)SCR_W * 0.5f, y0 + slot * 4.0f + gap * 4.0f + 10.0f * s, 17.0f * s,
                     "Нажмите на предмет, чтобы переложить", 1, 1, 1, 0.6f);

    float cxb = bagX + slot * Inventory::COLS + gap * (Inventory::COLS - 1) + 14.0f * s;
    float cyb = y0 - 40.0f * s;
    float csz = 50.0f * s;
    if(cxb + csz > (float)SCR_W - 8.0f * s) cxb = (float)SCR_W - csz - 8.0f * s;
    if(texClose_) drawUIRect(cxb, cyb, csz, csz, texClose_, 1, 1, 1, 0.9f, true);
    else {
        drawUIRect(cxb, cyb, csz, csz, 0, 0.20f, 0.10f, 0.10f, 0.9f, false);
        drawTextCentered(cxb + csz * 0.5f, cyb + csz * 0.25f, 24.0f * s, "X", 1, 1, 1, 0.95f);
    }
}

bool GameClient::handleBoxTouch(float x, float y){
    if(openBox_ < 0 || openBox_ >= (int)boxes_.size()){ overlay_ = Overlay::None; return true; }
    WorldBox& box = boxes_[(size_t)openBox_];
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float x0, y0, slot;
    boxSlotPos(0, x0, y0, slot);
    float bagX, bagY, unused;
    boxSlotPos(-1, bagX, bagY, unused);
    float gap = 8.0f * s;

    // Крестик — та же геометрия, что в отрисовке, плюс запас под палец.
    float cxb = bagX + slot * Inventory::COLS + gap * (Inventory::COLS - 1) + 14.0f * s;
    float cyb = y0 - 40.0f * s;
    float csz = 50.0f * s;
    if(cxb + csz > (float)SCR_W - 8.0f * s) cxb = (float)SCR_W - csz - 8.0f * s;
    float pad = 12.0f * s;
    if(x >= cxb - pad && x <= cxb + csz + pad && y >= cyb - pad && y <= cyb + csz + pad){
        overlay_ = Overlay::None; openBox_ = -1;
        return true;
    }

    // Из ящика в рюкзак.
    for(int i = 0; i < BOX_SLOTS; ++i){
        float bx, by, sl;
        boxSlotPos(i, bx, by, sl);
        if(x < bx || x > bx + sl || y < by || y > by + sl) continue;
        if(box.slots[i].empty()) return true;
        int left = inventory_.add(box.slots[i].type, box.slots[i].count);
        box.slots[i].count = left;
        if(left <= 0) box.slots[i].clear();
        return true;
    }

    // Из рюкзака в ящик.
    for(int i = Inventory::HOTBAR; i < Inventory::SIZE; ++i){
        int k = i - Inventory::HOTBAR;
        float sx = bagX + (k % Inventory::COLS) * (slot + gap);
        float sy = bagY + (k / Inventory::COLS) * (slot + gap);
        if(x < sx || x > sx + slot || y < sy || y > sy + slot) continue;
        ItemStack st = inventory_.slot(i);
        if(st.empty()) return true;
        // Сначала докладываем в такой же стак, потом занимаем пустую ячейку.
        for(int b = 0; b < BOX_SLOTS && st.count > 0; ++b){
            if(box.slots[b].empty() || box.slots[b].type != st.type) continue;
            int room = itemDef(st.type).maxStack - box.slots[b].count;
            int move = (st.count < room) ? st.count : room;
            if(move <= 0) continue;
            box.slots[b].count += move;
            st.count -= move;
        }
        for(int b = 0; b < BOX_SLOTS && st.count > 0; ++b){
            if(!box.slots[b].empty()) continue;
            box.slots[b] = st;
            st.count = 0;
        }
        inventory_.slot(i) = (st.count > 0) ? st : ItemStack{};
        return true;
    }
    return true;
}

// ---- Окно шкафа: сколько в нём дерева, сколько стоит дом в сутки и кнопки оплаты.
void GameClient::cupboardButtonRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pw = clampf((float)SCR_W * 0.42f, 380.0f, 620.0f);
    float px = ((float)SCR_W - pw) * 0.5f;
    float py = (float)SCR_H * 0.26f;
    w = pw - 40.0f * s;
    h = 50.0f * s;
    x = px + 20.0f * s;
    y = py + 150.0f * s + (float)i * (h + 10.0f * s);
}

void GameClient::renderCupboard(){
    if(openCupboard_ < 0 || openCupboard_ >= (int)cupboards_.size()){ overlay_ = Overlay::None; return; }
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    const WorldCupboard& c = cupboards_[(size_t)openCupboard_];
    float pw = clampf((float)SCR_W * 0.42f, 380.0f, 620.0f);
    float ph = 380.0f * s;
    float px = ((float)SCR_W - pw) * 0.5f, py = (float)SCR_H * 0.26f;

    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.05f, 0.06f, 0.30f, false);
    drawUIRect(px, py, pw, ph, 0, 0.22f, 0.23f, 0.25f, 0.72f, false);
    uiThinFrame(px, py, pw, ph, UI_LINE, 0.5f);
    drawTextCentered(px + pw * 0.5f, py + 16.0f * s, 27.0f * s, "ШКАФ", 1, 1, 1, 0.96f);

    char buf[160];
    snprintf(buf, sizeof(buf), "В шкафу дерева: %d", c.wood);
    drawText(px + 20.0f * s, py + 62.0f * s, 20.0f * s, buf, 1, 1, 1, 0.92f);
    snprintf(buf, sizeof(buf), "Деталей дома: %d", (int)pieces_.size());
    drawText(px + 20.0f * s, py + 90.0f * s, 20.0f * s, buf, 1, 1, 1, 0.92f);
    snprintf(buf, sizeof(buf), "Аренда: %d дерева в сутки", upkeepPerDay());
    bool enough = c.wood >= upkeepPerDay();
    drawText(px + 20.0f * s, py + 118.0f * s, 20.0f * s, buf,
             enough ? 0.60f : 0.90f, enough ? 0.85f : 0.40f, 0.40f, 0.95f);

    const char* labels[3] = { "ПОЛОЖИТЬ 10 ДЕРЕВА", "ПОЛОЖИТЬ 100 ДЕРЕВА", "ЗАКРЫТЬ" };
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        cupboardButtonRect(i, bx, by, bw, bh);
        bool can = (i == 2) || inventory_.countOf(ItemType::Wood) >= (i == 0 ? 10 : 100);
        drawUIRect(bx, by, bw, bh, 0, 0.88f, 0.87f, 0.83f, can ? 0.22f : 0.10f, false);
        uiThinFrame(bx, by, bw, bh, can ? UI_ACCENT : UI_LINE, can ? 0.8f : 0.35f);
        drawTextCentered(bx + bw * 0.5f, by + bh * 0.28f, 20.0f * s, labels[i],
                         1, 1, 1, can ? 0.95f : 0.45f);
    }
}

bool GameClient::handleCupboardTouch(float x, float y){
    if(openCupboard_ < 0 || openCupboard_ >= (int)cupboards_.size()){ overlay_ = Overlay::None; return true; }
    WorldCupboard& c = cupboards_[(size_t)openCupboard_];
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        cupboardButtonRect(i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if(i == 2){ overlay_ = Overlay::None; openCupboard_ = -1; return true; }
        int want = (i == 0) ? 10 : 100;
        int have = inventory_.countOf(ItemType::Wood);
        if(have < want) want = have;
        if(want > 0){
            inventory_.remove(ItemType::Wood, want);
            c.wood += want;
        }
        return true;
    }
    return true;
}

// ==================== СЕТЕВЫЕ СОБЫТИЯ, ГРАНАТЫ И БОЙ ====================
// Правки блоков разъезжаются сами (см. onBlockChanged), но всё разовое — выброшенный
// предмет, упавшее дерево, взрыв, удар по игроку — блоками не описывается. Для этого
// в протоколе есть события, и вся их обработка собрана здесь.

int GameClient::makeDropId(){
    // Номер игрока в старших разрядах: два клиента не выдадут одинаковую метку, а
    // сговариваться с сервером ради этого незачем.
    int player = net_.connected() ? net_.playerId() : 1;
    return player * 100000 + (nextDropId_++);
}

void GameClient::netSendEvent(net::EventType type, int id, int a, int b, Vec3 pos){
    if(!net_.connected()) return;
    net::Event e;
    e.type = (int)type;
    e.id = id;
    e.a = a;
    e.b = b;
    e.x = pos.x; e.y = pos.y; e.z = pos.z;
    net_.pushEvent(e);
}

void GameClient::spawnRemoteDrop(int netId, ItemType type, int count, Vec3 pos){
    for(const DroppedItem& d : drops_) if(d.netId == netId) return;   // уже есть
    DroppedItem d;
    d.pos = pos;
    d.type = type;
    d.count = count;
    d.netId = netId;
    drops_.push_back(d);
}

void GameClient::netApplyEvents(){
    if(!net_.connected()) return;
    std::vector<net::Event> events = net_.takeEvents();
    for(const net::Event& e : events){
        switch((net::EventType)e.type){
            case net::EventType::Drop:
                if(e.a > 0 && e.a < (int)ItemType::COUNT && e.b > 0)
                    spawnRemoteDrop(e.id, (ItemType)e.a, e.b, Vec3{ e.x, e.y, e.z });
                break;
            case net::EventType::Pickup:
                for(size_t i = 0; i < drops_.size(); ++i){
                    if(drops_[i].netId != e.id) continue;
                    drops_.erase(drops_.begin() + (long)i);
                    break;
                }
                break;
            case net::EventType::TreeFell: {
                // Дерево валит каждый у себя: мир один и тот же, значит и куски те же.
                // Блоки при этом наружу не уходят — иначе на сеть уезжали бы сотни правок.
                // a == 1 — дерево упало до нашего входа: убираем его молча, без падения.
                int bx = (int)floorf(e.x), by = (int)floorf(e.y), bz = (int)floorf(e.z);
                if(isHarvestable(voxels_->blockAt(bx, by, bz))){
                    netFelling_ = true;
                    netSilentFell_ = (e.a == 1);
                    voxels_->fellCluster(bx, by, bz);
                    netSilentFell_ = false;
                    netFelling_ = false;
                }
                break;
            }
            case net::EventType::Explosion:
                explode(Vec3{ e.x, e.y, e.z }, e.b, true);
                break;
            case net::EventType::Hit:
                // Урон применяет тот, по кому попали: своё здоровье считает только он.
                if(net_.playerId() != 0 && e.id == net_.playerId() && !player_->isDead())
                    player_->hurt((float)e.b, "Вас убили");
                break;
        }
    }
}

// Кто оказался перед лицом на расстоянии удара. Проверка простая — расстояние от
// игрока до отрезка взгляда; для рукопашной этого достаточно.
int GameClient::remotePlayerInFront(float reach) const {
    if(remote_.empty()) return 0;
    Vec3 eye = player_->eyePosition();
    Vec3 dir = player_->lookDirection();
    int best = 0;
    float bestT = reach;
    for(const RemoteView& v : remote_){
        if(v.pose == (int)net::Pose::Dead) continue;
        // Центр фигуры — примерно на метре над её ногами.
        Vec3 c{ v.pos.x, v.pos.y + 1.0f, v.pos.z };
        Vec3 rel{ c.x - eye.x, c.y - eye.y, c.z - eye.z };
        float t = rel.x * dir.x + rel.y * dir.y + rel.z * dir.z;
        if(t < 0.0f || t > reach) continue;
        Vec3 close{ eye.x + dir.x * t - c.x, eye.y + dir.y * t - c.y, eye.z + dir.z * t - c.z };
        float miss = sqrtf(close.x * close.x + close.y * close.y + close.z * close.z);
        if(miss > 0.9f) continue;              // мимо: полметра шире фигуры
        if(t < bestT){ bestT = t; best = v.id; }
    }
    return best;
}

void GameClient::onSwingImpact(){
    if(!net_.connected()) return;
    int target = remotePlayerInFront(3.2f);
    if(target == 0) return;
    // Урон по игроку: топором больнее, факелом слабее, кулаком совсем чуть-чуть.
    const ItemStack& sel = inventory_.selectedStack();
    int damage = 5;
    if(!sel.empty() && sel.type == ItemType::Axe)   damage = 20;
    if(!sel.empty() && sel.type == ItemType::Torch) damage = 10;
    netSendEvent(net::EventType::Hit, target, net_.playerId(), damage, player_->position());
    hitMarkAge_ = 0.0f;
}

// ---- Гранаты
void GameClient::throwGrenade(){
    const ItemStack& sel = inventory_.selectedStack();
    if(sel.empty() || sel.type != ItemType::Grenade) return;
    Grenade g;
    Vec3 eye = player_->eyePosition();
    Vec3 dir = player_->lookDirection();
    g.pos = Vec3{ eye.x + dir.x * 0.5f, eye.y + dir.y * 0.5f, eye.z + dir.z * 0.5f };
    // Бросок с руки: скорость по взгляду плюс подброс, чтобы граната летела дугой.
    g.vel = Vec3{ dir.x * 15.0f, dir.y * 15.0f + 3.0f, dir.z * 15.0f };
    g.fuse = 3.0f;
    grenades_.push_back(g);
    inventory_.consumeSelected();
}

void GameClient::updateGrenades(float dt){
    for(size_t i = 0; i < grenades_.size(); ){
        Grenade& g = grenades_[i];
        g.fuse -= dt;
        g.spin += dt * 6.0f;
        g.vel.y -= 20.0f * dt;
        Vec3 next{ g.pos.x + g.vel.x * dt, g.pos.y + g.vel.y * dt, g.pos.z + g.vel.z * dt };
        // Отскок от твёрдого: граната не проваливается сквозь стены и пол.
        if(voxels_->isSolidAt((int)floorf(next.x), (int)floorf(next.y), (int)floorf(next.z))){
            g.vel = Vec3{ g.vel.x * -0.35f, g.vel.y * -0.35f, g.vel.z * -0.35f };
            next = g.pos;
        }
        g.pos = next;
        if(g.fuse <= 0.0f || g.pos.y < -8.0f){
            Vec3 at = g.pos;
            grenades_.erase(grenades_.begin() + (long)i);
            explode(at, 150, false);
            continue;
        }
        ++i;
    }
}

void GameClient::explode(Vec3 at, int maxDamage, bool remote){
    // Осколки и вспышка: взрыв должен быть виден и слышен всем, кто рядом.
    for(int i = 0; i < 40; ++i){
        Particle p;
        Rng rng(splitMix64((uint64_t)(animTime_ * 1000.0f) + (uint64_t)i * 977ULL));
        p.pos = at;
        p.vel = Vec3{ (rng.nextFloat() - 0.5f) * 12.0f, rng.nextFloat() * 9.0f,
                      (rng.nextFloat() - 0.5f) * 12.0f };
        p.life = 0.5f + rng.nextFloat() * 0.7f;
        p.size = 0.10f + rng.nextFloat() * 0.12f;
        p.r = 1.6f; p.g = 0.9f; p.b = 0.35f;
        p.layer = (float)blockTextureLayer(Block::Sand);
        particles_.push_back(p);
    }
    hitMarkAge_ = 0.0f;

    // Постройки: минус 50 прочности всему, что в радиусе. Считает тот, кто бросил, —
    // у остальных это приедет обычными правками блоков.
    if(!remote){
        const float BUILD_RADIUS = 4.5f;
        for(size_t i = 0; i < pieces_.size(); ){
            const BuildPiece& p = pieces_[i];
            float cx = (float)p.x + p.sx * 0.5f, cy = (float)p.y + p.sy * 0.5f,
                  cz = (float)p.z + p.sz * 0.5f;
            float dx = cx - at.x, dy = cy - at.y, dz = cz - at.z;
            if(sqrtf(dx * dx + dy * dy + dz * dz) > BUILD_RADIUS){ ++i; continue; }
            pieces_[i].health -= 50;
            if(pieces_[i].health <= 0){
                fillPieceCells(pieces_[i], false);
                pieces_.erase(pieces_.begin() + (long)i);
            } else ++i;
        }
        netSendEvent(net::EventType::Explosion, 0, 0, maxDamage, at);
    }

    // Живым — по расстоянию: в эпицентре весь урон, к восьми метрам ноль. Свой урон
    // каждый считает сам, поэтому здесь только про себя.
    Vec3 me = player_->position();
    float dx = me.x - at.x, dy = me.y + 0.9f - at.y, dz = me.z - at.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    const float BLAST = 8.0f;
    if(dist < BLAST && !player_->isDead()){
        float k = 1.0f - dist / BLAST;
        player_->hurt((float)maxDamage * k * k, "Вас разорвало взрывом");
    }
}

void GameClient::renderGrenades(const Mat4& view, const Mat4& proj){
    if(grenades_.empty()) return;
    std::vector<VoxelVertex> verts;
    verts.reserve(grenades_.size() * 36);
    for(const Grenade& g : grenades_){
        // Мигает всё чаще к концу запала — по этому и понимают, что пора убегать.
        float blink = (g.fuse < 1.0f) ? (sinf(g.fuse * 40.0f) > 0.0f ? 1.8f : 0.6f) : 1.0f;
        pushCube(verts, g.pos, 0.10f, 0.45f * blink, 0.55f * blink, 0.38f * blink,
                 (float)blockTextureLayer(Block::Stone));
    }
    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    glBindVertexArray(partVao_);
    glBindBuffer(GL_ARRAY_BUFFER, partVbo_);
    size_t maxVerts = 64 * 36;
    if(verts.size() > maxVerts) verts.resize(maxVerts);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
}

// ==================== ДРУГИЕ ИГРОКИ ====================
// Сервер присылает голые числа: где игрок, куда смотрит, с какой скоростью идёт и в
// какой фазе замах. Модель, походка и инструмент в руке — забота клиента. Фигурка
// собрана из брусков в духе кубических игр: голова, корпус, две руки, две ноги.
namespace {
// Брусок в произвольной ориентации: три оси и полуразмеры по ним. Тем же способом
// собран инструмент в руке, но здесь оси задаёт скелет, а не камера.
void pushBox(std::vector<VoxelVertex>& out, Vec3 c, Vec3 ax, Vec3 ay, Vec3 az,
             float hx, float hy, float hz, float r, float g, float b, float layer){
    static const int SX[6] = { 0, 0, 1, -1, 0, 0 };
    static const int SY[6] = { 1, -1, 0, 0, 0, 0 };
    static const int SZ[6] = { 0, 0, 0, 0, 1, -1 };
    for(int f = 0; f < 6; ++f){
        Vec3 n{ ax.x * SX[f] + ay.x * SY[f] + az.x * SZ[f],
                ax.y * SX[f] + ay.y * SY[f] + az.y * SZ[f],
                ax.z * SX[f] + ay.z * SY[f] + az.z * SZ[f] };
        Vec3 t1, t2; float h1, h2;
        if(SY[f] != 0){ t1 = ax; h1 = hx; t2 = az; h2 = hz; }
        else if(SX[f] != 0){ t1 = ay; h1 = hy; t2 = az; h2 = hz; }
        else { t1 = ax; h1 = hx; t2 = ay; h2 = hy; }
        float off = SY[f] ? hy : (SX[f] ? hx : hz);
        Vec3 base{ c.x + n.x * off, c.y + n.y * off, c.z + n.z * off };
        float face = (SY[f] > 0) ? 1.0f : (SY[f] < 0 ? 0.6f : (SX[f] ? 0.84f : 0.92f));
        VoxelVertex q[4];
        for(int k = 0; k < 4; ++k){
            float s1 = (k == 0 || k == 3) ? -1.0f : 1.0f;
            float s2 = (k < 2) ? -1.0f : 1.0f;
            q[k] = VoxelVertex{
                base.x + t1.x * h1 * s1 + t2.x * h2 * s2,
                base.y + t1.y * h1 * s1 + t2.y * h2 * s2,
                base.z + t1.z * h1 * s1 + t2.z * h2 * s2,
                n.x, n.y, n.z,
                r * face, g * face, b * face,
                (k == 1 || k == 2) ? 1.0f : 0.0f, (k >= 2) ? 1.0f : 0.0f, layer };
        }
        out.push_back(q[0]); out.push_back(q[1]); out.push_back(q[2]);
        out.push_back(q[0]); out.push_back(q[2]); out.push_back(q[3]);
    }
}
} // namespace

void GameClient::updateRemotePlayers(float dt){
    if(!net_.connected()){
        if(!remote_.empty()) remote_.clear();
        return;
    }
    std::vector<net::PlayerState> snapshot = net_.players();

    // Обновляем то, что уже рисуем, и заводим новых.
    for(const net::PlayerState& p : snapshot){
        RemoteView* view = nullptr;
        for(RemoteView& v : remote_) if(v.id == p.id){ view = &v; break; }
        if(!view){
            RemoteView v;
            v.id = p.id;
            v.pos = Vec3{ p.x, p.y, p.z };
            remote_.push_back(v);
            view = &remote_.back();
        }
        view->name = p.name;
        view->target = Vec3{ p.x, p.y, p.z };
        view->yaw = p.yaw;
        view->pitch = p.pitch;
        view->speed = p.speed;
        view->swing = p.swing;
        view->held = p.held;
        view->pose = p.pose;
        view->health = p.health;
    }
    // Тех, кого сервер больше не присылает, убираем.
    for(size_t i = 0; i < remote_.size(); ){
        bool alive = false;
        for(const net::PlayerState& p : snapshot) if(p.id == remote_[i].id){ alive = true; break; }
        if(alive) ++i;
        else remote_.erase(remote_.begin() + (long)i);
    }

    for(RemoteView& v : remote_){
        // Позиция догоняет присланную: обмен идёт десять раз в секунду, и без
        // сглаживания чужой игрок дёргался бы рывками.
        float k = clampf(dt * 12.0f, 0.0f, 1.0f);
        v.pos.x += (v.target.x - v.pos.x) * k;
        v.pos.y += (v.target.y - v.pos.y) * k;
        v.pos.z += (v.target.z - v.pos.z) * k;
        // Фаза шага крутится от скорости — по ней ходят ноги и руки.
        v.phase += dt * (1.6f + v.speed * 1.5f);
        if(v.phase > 6.28318f) v.phase -= 6.28318f;
    }
}

void GameClient::renderRemotePlayers(const Mat4& view, const Mat4& proj){
    for(RemoteView& v : remote_) v.onScreen = false;
    if(remote_.empty()) return;

    std::vector<VoxelVertex> verts;
    verts.reserve(remote_.size() * 12 * 36);
    // Слой берём почти белый: поверх него краска фигурки читается как краска, а не как
    // подкрашенные доски. На тёмной текстуре и кожа, и одежда сливались в бурое пятно.
    float layer = (float)blockTextureLayer(Block::Snow);

    for(RemoteView& v : remote_){
        Vec3 fwd{ -sinf(v.yaw), 0.0f, -cosf(v.yaw) };
        Vec3 right{ -fwd.z, 0.0f, fwd.x };
        Vec3 up{ 0.0f, 1.0f, 0.0f };

        bool dead = (v.pose == (int)net::Pose::Dead);
        bool crouch = (v.pose == (int)net::Pose::Crouch);
        float scale = crouch ? 0.82f : 1.0f;
        // Ход: чем быстрее идёт, тем шире шаг. Стоящий игрок не машет конечностями.
        float gait = clampf(v.speed / 4.5f, 0.0f, 1.4f);
        float legA = sinf(v.phase) * 0.7f * gait;
        float armA = -legA;
        // Замах перебивает походку у правой руки: удар важнее шага.
        float swingA = 0.0f;
        if(v.swing > 0.001f) swingA = -sinf(v.swing * 3.14159265f) * 1.9f;

        Vec3 base = v.pos;
        auto bodyPoint = [&](float upM, float rightM, float fwdM){
            return Vec3{ base.x + up.x * upM * scale + right.x * rightM + fwd.x * fwdM,
                         base.y + up.y * upM * scale + right.y * rightM + fwd.y * fwdM,
                         base.z + up.z * upM * scale + right.z * rightM + fwd.z * fwdM };
        };
        // Конечность: висит из точки крепления и поворачивается вокруг поперечной оси.
        auto limb = [&](Vec3 pivot, float angle, float len, float halfW, float halfD,
                        float r, float g, float b){
            float ca = cosf(angle), sa = sinf(angle);
            Vec3 dir{ fwd.x * sa - up.x * ca, fwd.y * sa - up.y * ca, fwd.z * sa - up.z * ca };
            Vec3 c{ pivot.x + dir.x * len * 0.5f, pivot.y + dir.y * len * 0.5f,
                    pivot.z + dir.z * len * 0.5f };
            Vec3 ay{ -dir.x, -dir.y, -dir.z };
            Vec3 az{ right.y * ay.z - right.z * ay.y,
                     right.z * ay.x - right.x * ay.z,
                     right.x * ay.y - right.y * ay.x };
            pushBox(verts, c, right, ay, az, halfW, len * 0.5f, halfD, r, g, b, layer);
            return Vec3{ pivot.x + dir.x * len, pivot.y + dir.y * len, pivot.z + dir.z * len };
        };

        if(dead){
            // Труп лежит: один плоский брусок вместо фигуры.
            Vec3 c = bodyPoint(0.25f, 0.0f, 0.0f);
            pushBox(verts, c, right, up, fwd, 0.30f, 0.22f, 0.85f,
                    0.42f, 0.30f, 0.28f, layer);
            continue;
        }

        // Ноги, корпус, голова.
        // Пропорции сложены так, чтобы фигурка укладывалась в те же 1.8 м, что и сам
        // игрок: ноги 0.85, корпус до 1.55, голова до 1.9.
        Vec3 hipL = bodyPoint(0.86f, -0.12f, 0.0f);
        Vec3 hipR = bodyPoint(0.86f,  0.12f, 0.0f);
        limb(hipL,  legA, 0.86f * scale, 0.105f, 0.105f, 0.22f, 0.24f, 0.32f);
        limb(hipR, -legA, 0.86f * scale, 0.105f, 0.105f, 0.22f, 0.24f, 0.32f);

        Vec3 torso = bodyPoint(1.21f, 0.0f, 0.0f);
        pushBox(verts, torso, right, up, fwd, 0.23f, 0.33f * scale, 0.13f,
                0.52f, 0.44f, 0.34f, layer);

        Vec3 head = bodyPoint(1.72f, 0.0f, 0.0f);
        // Голова доворачивается по наклону взгляда: так видно, куда смотрит игрок.
        float hp = clampf(v.pitch, -0.9f, 0.9f);
        Vec3 headUp{ up.x * cosf(hp) + fwd.x * sinf(hp),
                     up.y * cosf(hp) + fwd.y * sinf(hp),
                     up.z * cosf(hp) + fwd.z * sinf(hp) };
        Vec3 headFwd{ fwd.x * cosf(hp) - up.x * sinf(hp),
                      fwd.y * cosf(hp) - up.y * sinf(hp),
                      fwd.z * cosf(hp) - up.z * sinf(hp) };
        pushBox(verts, head, right, headUp, headFwd, 0.19f, 0.19f, 0.19f,
                0.88f, 0.70f, 0.55f, layer);

        Vec3 shoulderL = bodyPoint(1.50f, -0.30f, 0.0f);
        Vec3 shoulderR = bodyPoint(1.50f,  0.30f, 0.0f);
        limb(shoulderL, armA, 0.66f * scale, 0.075f, 0.075f, 0.88f, 0.70f, 0.55f);
        Vec3 hand = limb(shoulderR, (v.swing > 0.001f ? swingA : -armA), 0.66f * scale,
                         0.075f, 0.075f, 0.88f, 0.70f, 0.55f);

        // Инструмент в руке: его видно всем, как и просили. Топор — рукоять и голова,
        // факел — палка с огоньком.
        ItemType heldType = (v.held > 0 && v.held < (int)ItemType::COUNT)
                            ? (ItemType)v.held : ItemType::None;
        if(heldType == ItemType::Axe || heldType == ItemType::Torch){
            float angle = (v.swing > 0.001f ? swingA : -armA);
            float ca = cosf(angle), sa = sinf(angle);
            Vec3 along{ fwd.x * sa - up.x * ca, fwd.y * sa - up.y * ca, fwd.z * sa - up.z * ca };
            Vec3 shaftUp{ -along.x, -along.y, -along.z };
            Vec3 az{ right.y * shaftUp.z - right.z * shaftUp.y,
                     right.z * shaftUp.x - right.x * shaftUp.z,
                     right.x * shaftUp.y - right.y * shaftUp.x };
            Vec3 mid{ hand.x + along.x * 0.10f, hand.y + along.y * 0.10f, hand.z + along.z * 0.10f };
            if(heldType == ItemType::Axe){
                pushBox(verts, mid, right, shaftUp, az, 0.028f, 0.15f, 0.028f,
                        0.42f, 0.30f, 0.18f, (float)blockTextureLayer(Block::Wood));
                Vec3 headPos{ mid.x + shaftUp.x * 0.15f + fwd.x * 0.03f,
                              mid.y + shaftUp.y * 0.15f + fwd.y * 0.03f,
                              mid.z + shaftUp.z * 0.15f + fwd.z * 0.03f };
                pushBox(verts, headPos, right, shaftUp, az, 0.035f, 0.06f, 0.07f,
                        0.72f, 0.72f, 0.74f, (float)blockTextureLayer(Block::Stone));
            } else {
                pushBox(verts, mid, right, shaftUp, az, 0.024f, 0.15f, 0.024f,
                        0.42f, 0.30f, 0.18f, (float)blockTextureLayer(Block::Wood));
                Vec3 flame{ mid.x + shaftUp.x * 0.19f, mid.y + shaftUp.y * 0.19f,
                            mid.z + shaftUp.z * 0.19f };
                float flick = 0.85f + 0.15f * sinf(animTime_ * 11.0f + (float)v.id);
                pushBox(verts, flame, right, shaftUp, az, 0.04f, 0.05f, 0.04f,
                        1.9f * flick, 1.2f * flick, 0.35f, (float)blockTextureLayer(Block::Sand));
            }
        }

        // Куда попала голова на экране — по этому HUD подпишет имя.
        float wp[4] = { head.x, head.y + 0.42f, head.z, 1.0f };
        float vp[4], cp[4];
        for(int i = 0; i < 4; ++i)
            vp[i] = view.m[i] * wp[0] + view.m[4 + i] * wp[1] + view.m[8 + i] * wp[2] + view.m[12 + i] * wp[3];
        for(int i = 0; i < 4; ++i)
            cp[i] = proj.m[i] * vp[0] + proj.m[4 + i] * vp[1] + proj.m[8 + i] * vp[2] + proj.m[12 + i] * vp[3];
        if(cp[3] > 0.001f){
            float ndcX = cp[0] / cp[3], ndcY = cp[1] / cp[3];
            v.screenX = (float)SCR_W * (ndcX * 0.5f + 0.5f);
            v.screenY = (float)SCR_H * (0.5f - ndcY * 0.5f);
            v.onScreen = (ndcX > -1.1f && ndcX < 1.1f && ndcY > -1.1f && ndcY < 1.1f);
        }
    }
    if(verts.empty()) return;

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    glDisable(GL_CULL_FACE);
    glBindVertexArray(remoteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, remoteVbo_);
    size_t maxVerts = 24 * 12 * 36;
    if(verts.size() > maxVerts) verts.resize(maxVerts);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

// Имя над головой и полоска здоровья: без них в сетевой игре не отличить своих.
void GameClient::renderRemoteLabels(){
    if(remote_.empty() || overlay_ != Overlay::None) return;
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    Vec3 eye = player_->eyePosition();
    for(const RemoteView& v : remote_){
        if(!v.onScreen) continue;
        float dx = v.pos.x - eye.x, dy = v.pos.y - eye.y, dz = v.pos.z - eye.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if(dist > 60.0f) continue;
        float fh = clampf(20.0f * s * (12.0f / fmaxf(dist, 6.0f)), 11.0f * s, 22.0f * s);
        float tw = textWidth(fh, v.name);
        drawUIRect(v.screenX - tw * 0.5f - 6.0f * s, v.screenY - fh * 0.3f,
                   tw + 12.0f * s, fh * 1.5f, 0, 0.05f, 0.05f, 0.06f, 0.5f, false);
        drawTextCentered(v.screenX, v.screenY, fh, v.name, 1, 1, 1, 0.95f);
        // Полоска здоровья под именем.
        float bw = fmaxf(tw, 40.0f * s), bh = 4.0f * s;
        float by = v.screenY + fh * 1.35f;
        drawUIRect(v.screenX - bw * 0.5f, by, bw, bh, 0, 0.1f, 0.1f, 0.1f, 0.6f, false);
        drawUIRect(v.screenX - bw * 0.5f, by, bw * clampf(v.health / 100.0f, 0.0f, 1.0f), bh,
                   0, 0.75f, 0.25f, 0.22f, 0.9f, false);
    }
}

// Раз в кадр отдаём сети своё состояние и забираем чужие правки мира.
void GameClient::netPumpState(){
    if(!net_.connected()) return;
    net::PlayerState st;
    Vec3 p = player_->position();
    st.x = p.x; st.y = p.y; st.z = p.z;
    st.yaw = yaw_;
    st.pitch = pitch_;
    st.speed = player_->speed();
    st.swing = player_->swingPhase();
    st.held = (int)inventory_.selectedStack().type;
    st.health = (int)player_->health();
    if(player_->isDead())            st.pose = (int)net::Pose::Dead;
    else if(buildMode())             st.pose = (int)net::Pose::Build;
    else if(player_->swinging())     st.pose = (int)net::Pose::Swing;
    else if(player_->isCrouching())  st.pose = (int)net::Pose::Crouch;
    else if(player_->speed() > 5.0f) st.pose = (int)net::Pose::Run;
    else if(player_->speed() > 0.4f) st.pose = (int)net::Pose::Walk;
    else                             st.pose = (int)net::Pose::Idle;
    net_.setLocalState(st);
}

void GameClient::netApplyEdits(){
    if(!net_.connected()) return;
    std::vector<net::Edit> edits = net_.takeEdits();
    if(edits.empty()) return;
    // Пока применяем чужое, свои правки наружу не уходят — иначе они бы гуляли по кругу.
    netApplying_ = true;
    for(const net::Edit& e : edits){
        if(e.block < 0 || e.block >= (int)Block::COUNT) continue;
        voxels_->setBlock(e.x, e.y, e.z, (Block)e.block);
    }
    netApplying_ = false;
}

// ==================== ПАДЕНИЕ СРУБЛЕННОГО ДЕРЕВА ====================
// Мир отдаёт сюда все блоки дерева разом. Дальше дерево живёт как единый предмет:
// наклоняется вокруг комля, за полторы секунды ложится и лежит ещё пятнадцать секунд.
namespace {
const float TREE_FALL_TIME = 1.5f;    // сколько падает
const float TREE_LIFE_TIME = 15.0f;   // сколько лежит до исчезновения
const size_t MAX_FALL_CUBES = 1024;   // потолок буфера отрисовки
} // namespace

void GameClient::spawnFallenTree(const std::vector<VoxelWorld::FelledCell>& cells){
    if(cells.empty()) return;
    // Комель — самая нижняя клетка ствола: вокруг неё дерево и поворачивается.
    int minY = cells[0].y;
    for(const VoxelWorld::FelledCell& c : cells) if(c.y < minY) minY = c.y;
    double sx = 0.0, sz = 0.0; int n = 0;
    for(const VoxelWorld::FelledCell& c : cells){
        if(c.y != minY) continue;
        sx += c.x; sz += c.z; ++n;
    }
    if(n == 0) return;

    FallenTree t;
    t.base = Vec3{ (float)(sx / n) + 0.5f, (float)minY, (float)(sz / n) + 0.5f };
    // Падает от игрока: он только что ударил по стволу с этой стороны.
    Vec3 look = player_->lookDirection();
    float len = sqrtf(look.x * look.x + look.z * look.z);
    if(len < 0.001f){ t.dirX = 1.0f; t.dirZ = 0.0f; }
    else            { t.dirX = look.x / len; t.dirZ = look.z / len; }

    t.cells.reserve(cells.size());
    for(const VoxelWorld::FelledCell& c : cells){
        FallenTree::Cell cell;
        cell.ox = (float)c.x + 0.5f - t.base.x;
        cell.oy = (float)c.y + 0.5f - t.base.y;
        cell.oz = (float)c.z + 0.5f - t.base.z;
        blockTextureTint(c.block, cell.r, cell.g, cell.b);
        cell.layer = (float)blockTextureLayer(c.block);
        t.cells.push_back(cell);
    }
    // Больше восьми лежащих деревьев на экране не держим: это уже свалка, а не лес.
    if(fallenTrees_.size() >= 8) fallenTrees_.erase(fallenTrees_.begin());
    fallenTrees_.push_back(std::move(t));
}

void GameClient::updateFallenTrees(float dt){
    for(FallenTree& t : fallenTrees_) t.t += dt;
    fallenTrees_.erase(std::remove_if(fallenTrees_.begin(), fallenTrees_.end(),
                                      [](const FallenTree& t){ return t.t > TREE_LIFE_TIME; }),
                       fallenTrees_.end());
}

void GameClient::renderFallenTrees(const Mat4& view, const Mat4& proj){
    if(fallenTrees_.empty()) return;
    std::vector<VoxelVertex> verts;
    size_t cubes = 0;
    for(const FallenTree& t : fallenTrees_){
        // Наклон с разгоном: сначала дерево едва качнулось, потом обрушивается.
        float k = clampf(t.t / TREE_FALL_TIME, 0.0f, 1.0f);
        float angle = 1.5707963f * k * k;
        float ca = cosf(angle), sa = sinf(angle);
        // Ось поперёк направления падения.
        float lx = -t.dirZ, lz = t.dirX;
        for(const FallenTree::Cell& c : t.cells){
            if(cubes >= MAX_FALL_CUBES) break;
            // Раскладываем смещение на «вдоль падения», «поперёк» и «вверх», поворачиваем
            // первое с третьим — это и есть падение вокруг комля.
            float along = c.ox * t.dirX + c.oz * t.dirZ;
            float side  = c.ox * lx + c.oz * lz;
            float up    = c.oy;
            float na = along * ca + up * sa;
            float nu = up * ca - along * sa;
            Vec3 p{ t.base.x + t.dirX * na + lx * side,
                    t.base.y + nu,
                    t.base.z + t.dirZ * na + lz * side };
            pushCube(verts, p, 0.5f, c.r, c.g, c.b, c.layer);
            ++cubes;
        }
    }
    if(verts.empty()) return;

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    glBindVertexArray(fallVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fallVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
}

// ==================== ВЫБРОШЕННЫЕ ПРЕДМЕТЫ ====================
// Выброс из инвентаря отправляет ВЕСЬ стак на землю перед игроком: маленький куб
// крутится на месте, над ним висит название и количество, а рядом появляется кнопка
// с рукой — по ней предмет возвращается в инвентарь. Так вещь можно передать,
// переложить или просто освободить слот, не потеряв её насовсем.
namespace {
// Чем показывать предмет в мире: у блочных предметов — их же блок, у остальных —
// ближайший по смыслу материал.
Block dropBlockFor(ItemType t){
    const ItemDef& def = itemDef(t);
    if(def.placeable != Block::Air) return def.placeable;
    switch(t){
        case ItemType::Axe:       return Block::Stone;
        case ItemType::Torch:     return Block::Wood;
        case ItemType::Scrap:
        case ItemType::MetalFrag: return Block::OreMetal;
        case ItemType::Sulfur:
        case ItemType::Gunpowder: return Block::OreSulfur;
        case ItemType::BuildPlan: return Block::Planks;
        case ItemType::Cloth:     return Block::Leaves;
        default:                  return Block::Planks;
    }
}
const float DROP_SIZE = 0.095f;  // полуразмер куба предмета: маленький, но заметный
const float PICKUP_RANGE = 3.4f; // с какого расстояния предмет можно поднять
} // namespace

void GameClient::dropStackToWorld(int slotIndex){
    if(slotIndex < 0 || slotIndex >= Inventory::SIZE) return;
    ItemStack st = inventory_.slot(slotIndex);
    if(st.empty()) return;

    // Куб ложится перед игроком на вытянутую руку, но не внутрь стены: если там
    // занято, кладём под ноги.
    Vec3 eye = player_->eyePosition();
    Vec3 fwd = player_->lookDirection();
    Vec3 p{ eye.x + fwd.x * 1.9f, eye.y + fwd.y * 0.6f, eye.z + fwd.z * 1.9f };
    if(voxels_->isSolidAt((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z))){
        Vec3 base = player_->position();
        p = Vec3{ base.x, base.y + 0.6f, base.z };
    }

    DroppedItem d;
    d.pos = p;
    d.type = st.type;
    d.count = st.count;
    d.spin = 0.0f;
    d.netId = makeDropId();
    drops_.push_back(d);
    inventory_.dropSlot(slotIndex);
    // Выброшенное видят все: событие уезжает на сервер, у остальных куб появляется там же.
    netSendEvent(net::EventType::Drop, d.netId, (int)d.type, d.count, d.pos);

    char buf[96];
    snprintf(buf, sizeof(buf), "Выброшено: %s x%d", itemDef(st.type).nameRu, st.count);
    SDL_Log("%s", buf);
}

void GameClient::updateDrops(float dt){
    for(DroppedItem& d : drops_){
        d.age += dt;
        d.spin += dt * 1.9f;             // крутится вокруг своей оси
        if(d.spin > 6.28318f) d.spin -= 6.28318f;
        // Падение до земли: под кубом всегда должен быть твёрдый блок.
        int bx = (int)floorf(d.pos.x), bz = (int)floorf(d.pos.z);
        // Блок ПОД нижней гранью куба. Раньше клетка бралась с запасом, и лежащий
        // предмет каждый кадр то «висел в воздухе», то приземлялся заново — от этого
        // он и дёргался на месте.
        float bottom = d.pos.y - DROP_SIZE;
        int by = (int)floorf(bottom - 0.02f);
        if(!voxels_->isSolidAt(bx, by, bz)){
            d.vy -= 18.0f * dt;
            d.pos.y += d.vy * dt;
        } else {
            d.vy = 0.0f;
            // Ровно на грани блока: любой зазор снова уронил бы предмет на следующем кадре.
            d.pos.y = (float)(by + 1) + DROP_SIZE;
        }
        if(d.pos.y < -8.0f) d.count = 0;   // провалился сквозь мир — убираем
    }
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const DroppedItem& d){ return d.count <= 0; }),
                 drops_.end());
}

int GameClient::pickupCandidate() const {
    if(drops_.empty()) return -1;
    Vec3 eye = player_->eyePosition();
    int best = -1;
    float bestDist = PICKUP_RANGE;
    for(size_t i = 0; i < drops_.size(); ++i){
        const DroppedItem& d = drops_[i];
        float dx = d.pos.x - eye.x, dy = d.pos.y - eye.y, dz = d.pos.z - eye.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if(dist < bestDist){ bestDist = dist; best = (int)i; }
    }
    return best;
}

// Кнопка «поднять» — рука над поясом по центру: она появляется только когда рядом
// действительно что-то лежит, поэтому постоянного места под неё держать не нужно.
void GameClient::pickupButtonRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    w = h = 84.0f * s;
    // Кнопка стоит НИЖЕ центра экрана и правее самого предмета: над предметом она
    // закрывала бы собой тот самый куб, который просят поднять.
    x = (float)SCR_W * 0.5f + 96.0f * s;
    y = (float)SCR_H * 0.70f;
}

bool GameClient::handlePickupTouch(float x, float y){
    int idx = pickupCandidate();
    if(idx < 0) return false;
    float bx, by, bw, bh;
    pickupButtonRect(bx, by, bw, bh);
    if(x < bx || x > bx + bw || y < by || y > by + bh) return false;

    DroppedItem& d = drops_[(size_t)idx];
    int left = inventory_.add(d.type, d.count);
    if(left >= d.count) return true;      // инвентарь полон — предмет остаётся лежать
    d.count = left;
    if(d.count <= 0){
        int netId = d.netId;
        drops_.erase(drops_.begin() + idx);
        // Подобранное исчезает у всех, а не только у того, кто нагнулся.
        netSendEvent(net::EventType::Pickup, netId, 0, 0, Vec3{});
    }
    return true;
}

void GameClient::renderDrops(const Mat4& view, const Mat4& proj){
    for(DroppedItem& d : drops_) d.onScreen = false;
    if(drops_.empty()) return;

    std::vector<VoxelVertex> verts;
    verts.reserve(drops_.size() * 36);
    for(DroppedItem& d : drops_){
        float tr, tg, tb;
        Block b = dropBlockFor(d.type);
        blockTextureTint(b, tr, tg, tb);
        // Лежащий предмет чуть светлее блока: в траве тёмный кубик просто не видно.
        tr *= 1.22f; tg *= 1.22f; tb *= 1.22f;
        float layer = (float)blockTextureLayer(b);
        float ca = cosf(d.spin), sa = sinf(d.spin);
        // Никакого покачивания: предмет лежит и только вращается. Качание вместе с
        // подгонкой высоты и давало ту самую дрожь.
        Vec3 c{ d.pos.x, d.pos.y, d.pos.z };
        Vec3 ax{ ca, 0.0f, sa }, az{ -sa, 0.0f, ca }, ay{ 0.0f, 1.0f, 0.0f };
        const float hs = DROP_SIZE;
        static const int SX[6] = { 0, 0, 1, -1, 0, 0 };
        static const int SY[6] = { 1, -1, 0, 0, 0, 0 };
        static const int SZ[6] = { 0, 0, 0, 0, 1, -1 };
        for(int f = 0; f < 6; ++f){
            Vec3 n{ ax.x * SX[f] + ay.x * SY[f] + az.x * SZ[f],
                    ax.y * SX[f] + ay.y * SY[f] + az.y * SZ[f],
                    ax.z * SX[f] + ay.z * SY[f] + az.z * SZ[f] };
            Vec3 t1, t2;
            if(SY[f] != 0){ t1 = ax; t2 = az; }
            else if(SX[f] != 0){ t1 = ay; t2 = az; }
            else { t1 = ax; t2 = ay; }
            Vec3 base{ c.x + n.x * hs, c.y + n.y * hs, c.z + n.z * hs };
            float face = (SY[f] > 0) ? 1.0f : (SY[f] < 0 ? 0.6f : (SX[f] ? 0.82f : 0.92f));
            VoxelVertex q[4];
            for(int k = 0; k < 4; ++k){
                float s1 = (k == 0 || k == 3) ? -1.0f : 1.0f;
                float s2 = (k < 2) ? -1.0f : 1.0f;
                q[k] = VoxelVertex{
                    base.x + t1.x * hs * s1 + t2.x * hs * s2,
                    base.y + t1.y * hs * s1 + t2.y * hs * s2,
                    base.z + t1.z * hs * s1 + t2.z * hs * s2,
                    n.x, n.y, n.z,
                    tr * face, tg * face, tb * face,
                    (k == 1 || k == 2) ? 1.0f : 0.0f, (k >= 2) ? 1.0f : 0.0f, layer };
            }
            verts.push_back(q[0]); verts.push_back(q[1]); verts.push_back(q[2]);
            verts.push_back(q[0]); verts.push_back(q[2]); verts.push_back(q[3]);
        }

        // Куда куб попал на экране: подпись и кнопку рисует уже плоский интерфейс,
        // и ему нужна готовая точка, а не матрицы.
        float wp[4] = { c.x, c.y + hs + 0.12f, c.z, 1.0f };
        float vp[4], cp[4];
        for(int i = 0; i < 4; ++i)
            vp[i] = view.m[i] * wp[0] + view.m[4 + i] * wp[1] + view.m[8 + i] * wp[2] + view.m[12 + i] * wp[3];
        for(int i = 0; i < 4; ++i)
            cp[i] = proj.m[i] * vp[0] + proj.m[4 + i] * vp[1] + proj.m[8 + i] * vp[2] + proj.m[12 + i] * vp[3];
        if(cp[3] > 0.001f){
            float ndcX = cp[0] / cp[3], ndcY = cp[1] / cp[3];
            d.screenX = ((float)SCR_W) * (ndcX * 0.5f + 0.5f);
            d.screenY = ((float)SCR_H) * (0.5f - ndcY * 0.5f);
            d.onScreen = (ndcX > -1.2f && ndcX < 1.2f && ndcY > -1.2f && ndcY < 1.2f);
        }
    }

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    // Грани куба собраны тем же способом, что и бруски инструмента в руке, и их обход
    // не совпадает с общим правилом отсечения — иначе куб пропадает целиком.
    glDisable(GL_CULL_FACE);
    glBindVertexArray(partVao_);
    glBindBuffer(GL_ARRAY_BUFFER, partVbo_);
    // Буфер частиц рассчитан на 64 куба — больше предметов за раз на землю не кладут.
    size_t maxVerts = 64 * 36;
    if(verts.size() > maxVerts) verts.resize(maxVerts);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

// Подписи над выброшенными предметами и кнопка поднятия. Рисуются в HUD, поверх мира.
void GameClient::renderDropLabels(){
    if(drops_.empty() || overlay_ != Overlay::None) return;
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    char buf[96];
    // Подпись видна только вблизи: на другом конце поляны она превращалась в мусор
    // поверх пейзажа.
    const float LABEL_RANGE = 5.0f;
    Vec3 eye = player_->eyePosition();
    for(const DroppedItem& d : drops_){
        if(!d.onScreen) continue;
        float ddx = d.pos.x - eye.x, ddy = d.pos.y - eye.y, ddz = d.pos.z - eye.z;
        if(sqrtf(ddx * ddx + ddy * ddy + ddz * ddz) > LABEL_RANGE) continue;
        snprintf(buf, sizeof(buf), "%s x%d", itemDef(d.type).nameRu, d.count);
        float fh = 17.0f * s;
        float tw = textWidth(fh, buf);
        drawUIRect(d.screenX - tw * 0.5f - 8.0f * s, d.screenY - fh * 0.35f,
                   tw + 16.0f * s, fh * 1.6f, 0, 0.05f, 0.05f, 0.06f, 0.55f, false);
        drawTextCentered(d.screenX, d.screenY, fh, buf, 1, 1, 1, 0.95f);
    }

    int idx = pickupCandidate();
    if(idx < 0) return;
    float bx, by, bw, bh;
    pickupButtonRect(bx, by, bw, bh);
    if(texInteract_) drawUIRect(bx, by, bw, bh, texInteract_, 1, 1, 1, 0.95f, true);
    else {
        drawUICircle(bx + bw * 0.5f, by + bh * 0.5f, bw * 0.5f, 0.88f, 0.87f, 0.83f, 0.35f);
        drawUICircleOutline(bx + bw * 0.5f, by + bh * 0.5f, bw * 0.5f, 1, 1, 1, 0.8f, 2.0f);
    }
    drawTextCentered(bx + bw * 0.5f, by + bh + 4.0f * s, 17.0f * s, "ПОДНЯТЬ", 1, 1, 1, 0.9f);
}

void GameClient::renderParticles(const Mat4& view, const Mat4& proj){
    if(particles_.empty()) return;
    std::vector<VoxelVertex> verts;
    verts.reserve(particles_.size() * 36);
    for(const Particle& p : particles_)
        pushCube(verts, p.pos, p.size, p.r, p.g, p.b, p.layer);

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    glBindVertexArray(partVao_);
    glBindBuffer(GL_ARRAY_BUFFER, partVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
}

// Топорик в руке. Виден только когда он выбран в поясе: рука должна показывать ровно
// тот предмет, которым игрок сейчас работает. Глубину НЕ чистим — иначе следующий
// проход (вода) теряет глубину и рисуется поверх всего.
void GameClient::renderHeldItem(const Mat4& view, const Mat4& proj, Vec3 eye, Vec3 forward, float dt){
    const ItemStack& sel = inventory_.selectedStack();
    bool axe = !sel.empty() && sel.type == ItemType::Axe;
    bool torch = !sel.empty() && sel.type == ItemType::Torch;
    if(!axe && !torch) return;

    heldBobPhase_ += dt * (2.0f + player_->speed() * 1.6f);
    float bob = (player_->speed() > 0.4f && player_->onGround())
                ? sinf(heldBobPhase_ * 2.0f) * 0.012f : 0.0f;
    // Замах в три доли, как настоящий удар топором: короткий отвод назад, резкий
    // проход вперёд-вниз и мягкий возврат. Один синус давал одинаково вялое движение
    // туда и обратно и читался как дрожь, а не как удар.
    float phase = player_->swingPhase();
    float windup = smoothstepf(0.0f, 0.28f, phase);      // отвод назад
    float strike = smoothstepf(0.26f, 0.52f, phase);     // проход по цели
    float back   = smoothstepf(0.58f, 1.0f, phase);      // возврат в стойку
    // Знак положительный: удар идёт в ТУ ЖЕ сторону, куда смотрит лезвие. Раньше топор
    // лежал головой вправо, а замах уводил его влево — удар читался как промах мимо себя.
    float swing = (0.30f * windup - 1.30f * strike) * (1.0f - back);

    Vec3 right0 = v3norm(v3cross(forward, Vec3{0,1,0}));
    Vec3 up = v3cross(right0, forward);
    // Разворот инструмента вокруг вертикали ровно на 180 градусов: лезвие топора и
    // пламя факела теперь смотрят в другую сторону, а сам инструмент виден в профиль —
    // его форму видно целиком, а не с торца.
    const float TOOL_YAW = 3.14159265f;
    float cy2 = cosf(TOOL_YAW), sy2 = sinf(TOOL_YAW);
    Vec3 right{ right0.x * cy2 + forward.x * sy2,
                right0.y * cy2 + forward.y * sy2,
                right0.z * cy2 + forward.z * sy2 };
    Vec3 depth{ forward.x * cy2 - right0.x * sy2,
                forward.y * cy2 - right0.y * sy2,
                forward.z * cy2 - right0.z * sy2 };

    // Бруски топорика в своей плоской системе: x поперёк рукояти, y вдоль неё.
    struct Part { float cx, cy, hx, hy, hz; Block block; };
    // Пропорции сняты с картинок предметов. Топор: длинное топорище и широкая каменная
    // голова, насаженная сбоку и перевязанная у обуха. Факел: палка с горящим навершием.
    // Кисть — квадратный брусок у нижнего конца рукояти: без неё инструмент висел в
    // воздухе сам по себе. Сам топор крупнее прежнего.
    const Part axeParts[] = {
        { 0.000f, -0.150f, 0.0400f, 0.045f, 0.0400f, Block::Sand },   // кисть
        { 0.000f, -0.070f, 0.0135f, 0.115f, 0.0135f, Block::Wood },   // топорище, низ
        { 0.000f,  0.075f, 0.0125f, 0.075f, 0.0125f, Block::Wood },   // топорище, верх
        { -0.008f, 0.152f, 0.0195f, 0.024f, 0.0175f, Block::Wood },   // перевязка у обуха
        { -0.052f, 0.160f, 0.0470f, 0.039f, 0.0180f, Block::Stone },  // каменная голова
        { -0.108f, 0.162f, 0.0180f, 0.029f, 0.0135f, Block::Stone },  // скошенное лезвие
    };
    const Part torchParts[] = {
        { 0.000f, -0.150f, 0.0400f, 0.045f, 0.0400f, Block::Sand },   // кисть
        { 0.000f, -0.060f, 0.0130f, 0.115f, 0.0130f, Block::Wood },   // палка, низ
        { 0.000f,  0.070f, 0.0125f, 0.042f, 0.0125f, Block::Wood },   // палка, верх
        { 0.000f,  0.122f, 0.0180f, 0.020f, 0.0180f, Block::Wood },   // обмотка
        { 0.000f,  0.156f, 0.0160f, 0.023f, 0.0160f, Block::Sand },   // пламя, ядро
        { 0.000f,  0.192f, 0.0095f, 0.018f, 0.0095f, Block::Sand },   // пламя, язык
    };
    const Part* parts = axe ? axeParts : torchParts;
    const int partCount = 6;
    const float S = 0.80f;
    // Факел держат почти прямо: с наклоном топора пламя уезжает к центру экрана и
    // теряется в пейзаже.
    // В покое инструмент держат ПРЯМО, рукоятью вниз: наклон оставлен только удару —
    // на нём топор и факел клюют вперёд, отсюда и ощущение веса.
    float angle = -swing * 1.45f;
    float ca = cosf(angle), sa = sinf(angle);
    // Прямой инструмент занимает больше места по высоте, поэтому он отодвинут правее
    // и ниже: иначе рукоять с кистью уходила за нижний край, а голова топора лезла в
    // середину экрана.
    // На проходе удара swing отрицателен, поэтому знаки такие: инструмент уходит
    // вправо (куда смотрит лезвие), вниз и вперёд от лица.
    float offX = 0.205f - swing * 0.085f;
    float offZ = 0.52f - swing * 0.17f;
    float offY = (axe ? -0.205f : -0.185f) + bob + swing * 0.24f;

    std::vector<VoxelVertex> verts;
    verts.reserve(6 * 36);
    for(int pi = 0; pi < partCount; ++pi){
        const Part& part = parts[pi];
        float tr, tg, tb;
        blockTextureTint(part.block, tr, tg, tb);
        float layer = (float)blockTextureLayer(part.block);
        // Пламя факела — два верхних бруска: красим их в огонь и чуть качаем яркостью,
        // чтобы оно не выглядело жёлтым кубиком.
        // Кисть — телесного цвета, а не песочного.
        if(pi == 0){ tr = 0.86f; tg = 0.66f; tb = 0.50f; }
        if(torch && pi >= 4){
            float flick = 0.85f + 0.15f * sinf(heldBobPhase_ * 11.0f + (float)pi);
            tr = 1.9f * flick; tg = (pi == 4 ? 1.15f : 1.45f) * flick; tb = 0.35f * flick;
        }
        // Брусок собираем как «кубик» с разными полуразмерами: pushCube даёт куб, а
        // тут нужен вытянутый, поэтому строим грани здесь же по тем же правилам.
        // Место инструмента на экране считается в осях КАМЕРЫ (right0 — вправо по
        // экрану), а сами бруски раскладываются в осях самого инструмента. Пока это
        // было одно и то же, разворот инструмента уводил его и в другой угол экрана.
        float lx = part.cx * S * ca - part.cy * S * sa;
        float ly = part.cx * S * sa + part.cy * S * ca;
        Vec3 centre{
            eye.x + right.x * lx + up.x * (ly + offY) + right0.x * offX + forward.x * offZ,
            eye.y + right.y * lx + up.y * (ly + offY) + right0.y * offX + forward.y * offZ,
            eye.z + right.z * lx + up.z * (ly + offY) + right0.z * offX + forward.z * offZ
        };
        // Оси бруска в мировых координатах: рукоять повёрнута вместе с топором.
        Vec3 ax{ right.x * ca + up.x * sa, right.y * ca + up.y * sa, right.z * ca + up.z * sa };
        Vec3 ay{ -right.x * sa + up.x * ca, -right.y * sa + up.y * ca, -right.z * sa + up.z * ca };
        float hx = part.hx * S, hy = part.hy * S, hz = part.hz * S;
        static const int SX[6] = { 0, 0, 1, -1, 0, 0 };
        static const int SY[6] = { 1, -1, 0, 0, 0, 0 };
        static const int SZ[6] = { 0, 0, 0, 0, 1, -1 };
        for(int f = 0; f < 6; ++f){
            Vec3 n{ ax.x * SX[f] + ay.x * SY[f] + depth.x * SZ[f],
                    ax.y * SX[f] + ay.y * SY[f] + depth.y * SZ[f],
                    ax.z * SX[f] + ay.z * SY[f] + depth.z * SZ[f] };
            // Два касательных направления грани и их полуразмеры.
            Vec3 t1, t2; float h1, h2;
            if(SY[f] != 0){ t1 = ax; h1 = hx; t2 = depth; h2 = hz; }
            else if(SX[f] != 0){ t1 = ay; h1 = hy; t2 = depth; h2 = hz; }
            else { t1 = ax; h1 = hx; t2 = ay; h2 = hy; }
            Vec3 base{ centre.x + n.x * (SY[f] ? hy : SX[f] ? hx : hz),
                       centre.y + n.y * (SY[f] ? hy : SX[f] ? hx : hz),
                       centre.z + n.z * (SY[f] ? hy : SX[f] ? hx : hz) };
            float face = (SY[f] > 0) ? 1.0f : (SY[f] < 0 ? 0.55f : (SX[f] ? 0.78f : 0.9f));
            VoxelVertex q[4];
            for(int k = 0; k < 4; ++k){
                float s1 = (k == 0 || k == 3) ? -1.0f : 1.0f;
                float s2 = (k < 2) ? -1.0f : 1.0f;
                q[k] = VoxelVertex{
                    base.x + t1.x * h1 * s1 + t2.x * h2 * s2,
                    base.y + t1.y * h1 * s1 + t2.y * h2 * s2,
                    base.z + t1.z * h1 * s1 + t2.z * h2 * s2,
                    n.x, n.y, n.z,
                    tr * face, tg * face, tb * face,
                    (k == 1 || k == 2) ? 1.0f : 0.0f, (k >= 2) ? 1.0f : 0.0f, layer };
            }
            verts.push_back(q[0]); verts.push_back(q[1]); verts.push_back(q[2]);
            verts.push_back(q[0]); verts.push_back(q[2]); verts.push_back(q[3]);
        }
    }

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelFogDensityLoc, 0.0f);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    glDisable(GL_CULL_FACE);
    glBindVertexArray(heldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, heldVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(VoxelVertex)), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

void GameClient::initWorld(){
    WorldConfig cfg;
    cfg.seed = seedFromString(WORLD_SEED_TEXT);
    // Карта 1000x1000 метров: остров обходится за несколько минут, зоны биомов читаются
    // целиком, а мир целиком помещается в память вместе с правками игрока.
    cfg.size = 1000.0f;
    // Шаг сетки высот 2 м: в кубическом мире рельеф всё равно округляется до блоков, но
    // при более крупном шаге склоны превращались в широкие плоские террасы.
    cfg.heightGridStep = 2.0f;
    cfg.monumentCount = 0;   // локации отключены: пустые точки интереса только путали
    cfg.sanitize();

    world_.reset(new World(cfg));
    world_->generate();
    resources_.reset(new ResourceMap(*world_));
    resources_->generate();
    env_.reset(new Environment(cfg));
    if(startTimeOverride_ >= 0.0f) env_->setTimeOfDay(startTimeOverride_);

    voxels_.reset(new VoxelWorld(*world_, *resources_));
    chunks_.init(voxels_.get());

    // Любая правка мира (игрок сломал блок ИЛИ вода растеклась) помечает чанк на
    // пересборку. Подписываемся на сам мир, а не на игрока: у воды своего игрока нет.
    voxels_->onBlockChanged = [this](int x, int y, int z){
        chunks_.markDirty(x, y, z);
        // Всё, что построил или выбил ИГРОК, уезжает на сервер. Растекание воды в сеть
        // не шлём: оно детерминировано и повторится у каждого само.
        if(!netApplying_ && !netFelling_ && net_.connected()){
            Block b = voxels_->blockAt(x, y, z);
            if(b != Block::Water) net_.pushEdit(x, y, z, (int)b);
        }
    };
    player_.reset(new Survivor(*voxels_, *env_, inventory_));
    player_->onNodeBroken = [this](Block b, int x, int y, int z){ spawnBreakParticles(b, x, y, z); };
    // Дерево срублено: мир отдал его блоки целиком, дальше их роняет клиент.
    voxels_->onClusterFelled = [this](const std::vector<VoxelWorld::FelledCell>& cells, bool tree){
        if(tree && !netSilentFell_) spawnFallenTree(cells);
        // Своё сваленное дерево отправляем ОДНИМ событием: остальные повалят его сами,
        // мир-то у всех одинаковый. Иначе на сеть уезжали бы сотни правок блоков.
        if(!netFelling_ && !cells.empty() && net_.connected()){
            const VoxelWorld::FelledCell& base = cells.front();
            netSendEvent(net::EventType::TreeFell, 0, 0, 0,
                         Vec3{ (float)base.x + 0.5f, (float)base.y + 0.5f, (float)base.z + 0.5f });
        }
    };
    // Отсчёт вышел — поднимаем игрока на новом месте сами, без нажатия кнопки.
    player_->onOpenFurnace = [this](){ overlay_ = Overlay::Furnace; };
    // Удар по дому считает клиент: прочность детали живёт в его реестре построек.
    player_->onHitBuild = [this](Block b, int x, int y, int z){ hitBuildPiece(b, x, y, z); };
    // Метка попадания: короткая вспышка у прицела, чтобы удар читался как удар.
    player_->onHitLanded = [this](Block, int, int, int){ hitMarkAge_ = 0.0f; };
    // Середина замаха: смотрим, не оказался ли на пути другой игрок.
    player_->onSwingImpact = [this](){ onSwingImpact(); };
    // Шкаф и ящик: их содержимое тоже у клиента.
    player_->onOpenObject = [this](Block b, int x, int y, int z){
        if(b == Block::Box){
            for(size_t i = 0; i < boxes_.size(); ++i)
                if(boxes_[i].x == x && boxes_[i].y == y && boxes_[i].z == z){
                    openBox_ = (int)i; overlay_ = Overlay::Box; return;
                }
        } else if(b == Block::Cupboard){
            for(size_t i = 0; i < cupboards_.size(); ++i)
                if(cupboards_[i].x == x && cupboards_[i].y == y && cupboards_[i].z == z){
                    openCupboard_ = (int)i; overlay_ = Overlay::Cupboard; return;
                }
        }
    };
    player_->onObjectPlaced = [this](Block b, int x, int y, int z){
        if(b == Block::Box){ WorldBox nb; nb.x = x; nb.y = y; nb.z = z; boxes_.push_back(nb); }
        else if(b == Block::Cupboard){ WorldCupboard nc; nc.x = x; nc.y = y; nc.z = z; cupboards_.push_back(nc); }
    };
    player_->onRespawn = [this](){
        // Место гибели остаётся на карте: за вещами надо возвращаться.
        Vec3 died = player_->position();
        deathMark_ = Vec2{ died.x, died.z };
        deathMarkValid_ = true;
        Rng rr(splitMix64(world_->config().seed ^ 0x1234ULL ^ (uint64_t)SDL_GetTicks()));
        player_->spawn(world_->findSpawnPoint(rr));
    };

    Rng rng(splitMix64(cfg.seed ^ 0x5350ULL));
    Vec3 spawn = world_->findSpawnPoint(rng);
    if(startX_ >= 0.0f && startZ_ >= 0.0f){
        int top = voxels_->surfaceY((int)startX_, (int)startZ_);
        spawn = Vec3{ startX_, (float)(top + 2), startZ_ };
    }
    player_->spawn(spawn);
    yaw_ = (yawOverride_ > -100.0f) ? yawOverride_ : 0.0f;
    pitch_ = (pitchOverride_ > -100.0f) ? pitchOverride_ : -0.15f;

    // Стартовый набор: пара блоков, чтобы было чем строить с первой минуты.
    // Стартовый набор: топор и факел. Топор берётся в руки выбором в поясе — с ним
    // добыча идёт вдвое быстрее.
    // Стартовые топор и факел кладём ПРЯМО в пояс: добыча теперь уходит в рюкзак, и
    // через общий add они бы тоже улетели туда, оставив пояс пустым.
    inventory_.slot(0) = ItemStack{ ItemType::Axe, 1 };
    inventory_.slot(1) = ItemStack{ ItemType::Torch, 1 };
    if(debugKit_){
        // Отладочный набор: план стройки кладём прямо в пояс, чтобы режим стройки
        // включался ключом --slot 2 и его было видно на снимке.
        inventory_.slot(2) = ItemStack{ ItemType::BuildPlan, 1 };
        inventory_.add(ItemType::Wood, 300);
        inventory_.add(ItemType::Stone, 100);
        // Ящик и шкаф рядом со спавном: их окна надо чем-то открывать при проверке
        // интерфейса снимком.
        Vec3 pp = player_->position();
        int ox = (int)floorf(pp.x) + 2, oz = (int)floorf(pp.z);
        int oy = voxels_->surfaceY(ox, oz) + 1;
        voxels_->setBlock(ox, oy, oz, Block::Box);
        WorldBox nb; nb.x = ox; nb.y = oy; nb.z = oz;
        nb.slots[0] = ItemStack{ ItemType::Wood, 64 };
        nb.slots[1] = ItemStack{ ItemType::Stone, 20 };
        boxes_.push_back(nb);
        int cx2 = ox, cz2 = oz + 2;
        int cy2 = voxels_->surfaceY(cx2, cz2) + 1;
        voxels_->setBlock(cx2, cy2, cz2, Block::Cupboard);
        WorldCupboard nc; nc.x = cx2; nc.y = cy2; nc.z = cz2; nc.wood = 40;
        cupboards_.push_back(nc);
    }
    if(startSlot_ >= 0) inventory_.select(startSlot_);

    buildMinimapTexture();
    LOG_INFO("мир клиента готов: сид %llu, кубический слой включён",
             (unsigned long long)cfg.seed);
}

float GameClient::viewDistanceMeters() const {
    float meters = (float)settings.viewDistance * qualityViewDistanceScale();
    return clampf(meters, 32.0f, 224.0f);
}

void GameClient::buildMinimapTexture(){
    // 512x512 вместо 256: карту теперь можно приближать, и на четырёхкратном зуме
    // низкое разрешение расплывалось в кашу. Строится один раз при старте (~0.2 с).
    const int N = 512;
    std::vector<unsigned char> pixels((size_t)N * N * 4);
    const WorldConfig& cfg = world_->config();
    for(int y = 0; y < N; ++y){
        for(int x = 0; x < N; ++x){
            float wx = ((float)x + 0.5f) / (float)N * cfg.size;
            float wz = ((float)y + 0.5f) / (float)N * cfg.size;
            float h = world_->heightAt(wx, wz);
            const BiomeInfo& bi = biomeInfo(world_->biomeAt(wx, wz));
            // Рельефной подсветки нет намеренно: она давала на карте полосы вдоль
            // склонов — казалось, что по острову идут ямы и канавы. Высоту показываем
            // ровной, спокойной градацией яркости.
            float shade = 0.78f + clampf(h / cfg.maxHeight, 0.0f, 1.0f) * 0.34f;
            float r = bi.r * shade, g = bi.g * shade, b = bi.b * shade;
            // Дорога рисуется прямо на карте — по ней и ориентируются, как в Rust.
            if(voxels_->onRoad((int)wx, (int)wz)){ r = 196.0f; g = 158.0f; b = 116.0f; }
            if(voxels_->inGasStation((int)wx, (int)wz)){ r = 214.0f; g = 96.0f; b = 74.0f; }
            size_t i = ((size_t)y * N + x) * 4;
            pixels[i+0] = (unsigned char)clampf(r, 0, 255);
            pixels[i+1] = (unsigned char)clampf(g, 0, 255);
            pixels[i+2] = (unsigned char)clampf(b, 0, 255);
            pixels[i+3] = 255;
        }
    }
    glGenTextures(1, &minimapTex_);
    glBindTexture(GL_TEXTURE_2D, minimapTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ==================== ГЕОМЕТРИЯ ИНТЕРФЕЙСА ====================
// Приём перенесён из A.N.O.D.E: геометрия слотов считается ОДНОЙ функцией, которой
// пользуются и отрисовка, и проверка попадания пальца. Иначе нарисованная ячейка и
// нажимаемая зона неизбежно расходятся после первой же правки интерфейса.

void GameClient::hotbarGeometry(float& x, float& y, float& slot, float& gap) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Размер ячейки пояса — посередине между прежним поясом и ячейкой инвентаря:
    // мелкий пояс терялся внизу экрана, а размер инвентарной ячейки занимал полэкрана.
    slot = clampf(fminf((float)SCR_W, (float)SCR_H) * 0.115f, 52.0f, 118.0f * s);
    gap = slot * 0.10f;
    float total = slot * Inventory::HOTBAR + gap * (Inventory::HOTBAR - 1);
    x = ((float)SCR_W - total) * 0.5f;
    y = (float)SCR_H - slot - 14.0f * s;
}

// Пояс отделён от основной сетки: в Rust это две разные вещи, и слипшись в один
// прямоугольник они путают — непонятно, что окажется в руке.
float GameClient::inventoryBeltGap() const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    return 26.0f * s;
}

void GameClient::inventoryGeometry(float& x, float& y, float& slot, float& gap) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Ячейка крупнее прежней: в 44 пикселя пальцем не попасть, а место на экране есть.
    slot = clampf((float)SCR_H * 0.145f, 58.0f, 132.0f * s);
    gap = slot * 0.09f;
    float totalW = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
    float totalH = slot * Inventory::ROWS + gap * (Inventory::ROWS - 1) + inventoryBeltGap();
    x = ((float)SCR_W - totalW) * 0.5f;
    y = ((float)SCR_H - totalH) * 0.5f + 16.0f * s;
}

// Экранная позиция ячейки инвентаря с учётом отступа перед поясом.
// В окне инвентаря лежит ТОЛЬКО рюкзак — 24 ячейки. Пояс не дублируется: он и так
// нарисован внизу экрана и не пропадает при открытии рюкзака.
void GameClient::inventorySlotPos(int i, float& sx, float& sy) const {
    float gx, gy, slot, gap;
    inventoryGeometry(gx, gy, slot, gap);
    int backpack = i - Inventory::HOTBAR;         // 0..23
    int col = backpack % Inventory::COLS, row = backpack / Inventory::COLS;
    sx = gx + col * (slot + gap);
    sy = gy + row * (slot + gap);
}

// ==================== ВВОД ====================

bool GameClient::handleHotbarTouch(float x, float y){
    float hx, hy, slot, gap;
    hotbarGeometry(hx, hy, slot, gap);
    if(y < hy - gap || y > hy + slot + gap) return false;
    for(int i = 0; i < Inventory::HOTBAR; ++i){
        float sx = hx + i * (slot + gap);
        if(x >= sx && x <= sx + slot){
            inventory_.select(i);
            return true;
        }
    }
    return false;
}

// Геометрия крафта: сетка квадратных плиток слева, описание справа. Одна функция и для
// отрисовки, и для попаданий — иначе они разъезжаются при первом же правке.
const int CRAFT_COLS = 3;

void GameClient::craftGridGeometry(float& x, float& y, float& tile, float& gap) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    tile = clampf((float)SCR_H * 0.145f, 58.0f, 132.0f * s);
    gap = tile * 0.09f;
    int rows = (kRecipeCount + CRAFT_COLS - 1) / CRAFT_COLS;
    if(rows < 1) rows = 1;
    float gridH = tile * rows + gap * (rows - 1);
    // Сетка и панель описания стоят одним блоком по центру экрана, а не липнут к
    // левому краю: раньше окно съезжало в угол и выглядело незакончённым.
    float gridW = tile * CRAFT_COLS + gap * (CRAFT_COLS - 1);
    float panelW = fminf((float)SCR_W * 0.42f, 560.0f * s);
    float totalW = gridW + 28.0f * s + panelW;
    x = ((float)SCR_W - totalW) * 0.5f;
    y = ((float)SCR_H - gridH) * 0.5f + 10.0f * s;
}

// Панель описания: её ширина считается один раз и здесь, чтобы отрисовка и попадания
// не разъезжались.
float GameClient::craftPanelWidth() const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    return fminf((float)SCR_W * 0.42f, 560.0f * s);
}

void GameClient::craftTilePos(int i, float& tx, float& ty) const {
    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    tx = gx + (i % CRAFT_COLS) * (tile + gap);
    // Список прокручивается пальцем: рецептов со временем станет больше, чем влезает.
    ty = gy + (i / CRAFT_COLS) * (tile + gap) - craftScroll_;
}

void GameClient::craftButtonRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float dx, dy, dw, dh, notesY, tableY;
    craftPanelGeometry(dx, dy, dw, dh, notesY, tableY);
    // Кнопка стоит ВНУТРИ панели описания, в её правом нижнем углу: снаружи она
    // налезала на пояс быстрого доступа.
    w = fminf(dw * 0.45f, 300.0f * s);
    h = 54.0f * s;
    x = dx + dw - w - 16.0f * s;
    y = dy + dh - h - 16.0f * s;
}

// Панель состояния слева сверху: она же кнопка меню паузы. Геометрия совпадает с
// отрисовкой полос в renderHud.
void GameClient::statsPanelRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pad = 14.0f * s;
    x = pad; y = pad;
    w = 200.0f * s;
    h = (21.0f * s + 5.0f * s) * 4.0f + 22.0f * s;
}

const char* const PAUSE_ROWS[] = { "ПРОДОЛЖИТЬ", "КАРТА", "НАСТРОЙКИ", "ВЫЙТИ В МЕНЮ" };
const int PAUSE_ROW_COUNT = 4;

void GameClient::pauseRowRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Список идёт от верхней трети вниз по левому краю, а не висит по центру экрана:
    // так до него дотягивается большой палец, и он не закрывает вид.
    w = clampf((float)SCR_W * 0.28f, 240.0f, 420.0f);
    h = 56.0f * s;
    x = 44.0f * s;
    y = (float)SCR_H * 0.40f + i * (h + 10.0f * s);
}

void GameClient::renderPause(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Мир под меню виден, но приглушён: пауза — это не отдельный экран, а остановка.
    // Затемнение лёгкое: пауза не прячет игру, она её останавливает.
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.03f, 0.03f, 0.04f, 0.42f, false);
    drawText(44.0f * s, (float)SCR_H * 0.10f, 52.0f * s, "OSIL", 1, 1, 1, 0.97f);
    drawText(44.0f * s, (float)SCR_H * 0.19f, 26.0f * s, "SURVIVAL",
             UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f);
    for(int i = 0; i < PAUSE_ROW_COUNT; ++i){
        float x, y, w, h;
        pauseRowRect(i, x, y, w, h);
        // Строки одинаково белые и с подложкой: без неё «Настройки» тонули в светлом
        // куске пейзажа за меню, и строка казалась темнее соседних.
        drawUIRect(x - 10.0f * s, y, w, h, 0, 0.06f, 0.06f, 0.07f, 0.55f, false);
        drawText(x, y + h * 0.22f, 30.0f * s, PAUSE_ROWS[i], 1, 1, 1, 1.0f);
    }

    // ---- Справа сверху: на каком сервере играем и кто на нём есть. Плитки с никами —
    // как список онлайна в Rust: сразу видно, один ты на карте или нет.
    float panelW = clampf((float)SCR_W * 0.26f, 240.0f, 420.0f);
    float px = (float)SCR_W - panelW - 24.0f * s;
    float py = 24.0f * s;

    bool online = net_.connected();
    std::string title = online ? net_.serverName() : std::string("Одиночная игра");
    char buf[128];
    if(online) snprintf(buf, sizeof(buf), "%d / %d игроков", net_.onlineCount(), net_.maxPlayers());
    else       snprintf(buf, sizeof(buf), "локальный мир");

    drawUIRect(px, py, panelW, 62.0f * s, 0, 0.06f, 0.06f, 0.07f, 0.62f, false);
    uiThinFrame(px, py, panelW, 62.0f * s, online ? UI_ACCENT : UI_LINE, online ? 0.8f : 0.35f);
    // Длинное имя сервера обрезаем по ширине панели, а не выпускаем за край.
    {
        float nameH = 22.0f * s;
        std::string shown = title;
        while(!shown.empty() && textWidth(nameH, shown) > panelW - 24.0f * s){
            size_t n = shown.size();
            do { --n; } while(n > 0 && ((unsigned char)shown[n] & 0xC0) == 0x80);
            shown.resize(n);
        }
        if(shown.size() < title.size() && shown.size() > 1) shown += "…";
        drawText(px + 12.0f * s, py + 8.0f * s, nameH, shown, 1, 1, 1, 0.97f);
    }
    drawText(px + 12.0f * s, py + 36.0f * s, 17.0f * s, buf, 1, 1, 1, 0.6f);

    // Плитки с никами: сначала мы сами, потом остальные.
    float tileH = 34.0f * s, gap = 6.0f * s;
    float ty = py + 62.0f * s + 10.0f * s;
    auto nameTile = [&](const std::string& nick, bool me, int health){
        if(ty + tileH > (float)SCR_H - 20.0f * s) return;
        drawUIRect(px, ty, panelW, tileH, 0, 0.85f, 0.84f, 0.80f, me ? 0.20f : 0.11f, false);
        if(me) drawUIRect(px, ty, 4.0f * s, tileH, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.9f, false);
        drawText(px + 14.0f * s, ty + tileH * 0.22f, 19.0f * s, nick, 1, 1, 1, me ? 0.98f : 0.85f);
        // Здоровье игрока полоской у правого края плитки.
        float hw = 54.0f * s, hh = 5.0f * s;
        float hx = px + panelW - hw - 12.0f * s, hy = ty + tileH * 0.5f - hh * 0.5f;
        drawUIRect(hx, hy, hw, hh, 0, 0.12f, 0.12f, 0.13f, 0.7f, false);
        drawUIRect(hx, hy, hw * clampf(health / 100.0f, 0.0f, 1.0f), hh, 0,
                   0.75f, 0.25f, 0.22f, 0.9f, false);
        ty += tileH + gap;
    };

    nameTile(playerName_, true, (int)player_->health());
    if(online){
        for(const net::PlayerState& p : net_.players()) nameTile(p.name, false, p.health);
    }
}

bool GameClient::handlePauseTouch(float x, float y){
    for(int i = 0; i < PAUSE_ROW_COUNT; ++i){
        float rx, ry, rw, rh;
        pauseRowRect(i, rx, ry, rw, rh);
        if(x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
        switch(i){
            case 0: overlay_ = Overlay::None; break;
            case 1: overlay_ = Overlay::Map; break;
            case 2: overlay_ = Overlay::Settings; break;
            // Выход в меню рвёт связь с сервером: висеть в списке игроков «призраком»
            // до таймаута незачем.
            case 3: overlay_ = Overlay::None; net_.leave(); state_ = GameState::MainMenu; break;
        }
        return true;
    }
    return true;   // касание мимо строк меню не закрывает его
}

// Строки окна настроек считаются одной функцией и для отрисовки, и для попаданий.
namespace {
const int SETTINGS_ROWS = 10;
// Настройки — не окошко поверх игры, а отдельная сцена во весь экран: строки тянутся
// почти на всю ширину, сверху заголовок, справа сверху — крестик выхода.
float settingsRowH(int screenH){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    return 54.0f * s;   // крупнее прежнего: пальцем в строку в 40 пикселей не попасть
}
float settingsMargin(int screenW){
    return clampf((float)screenW * 0.055f, 18.0f, 160.0f);
}
float settingsPanelWidth(int screenW){
    return (float)screenW - 2.0f * settingsMargin(screenW);
}
float settingsTop(int screenH){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    float total = SETTINGS_ROWS * settingsRowH(screenH);
    // Заголовок занимает верхнюю полосу, поэтому список сдвинут вниз от центра.
    return clampf((float)screenH * 0.5f - total * 0.5f + 26.0f * s, 92.0f * s,
                  (float)screenH - total - 34.0f * s);
}
float settingsRowY(int i, int screenH){
    return settingsTop(screenH) + i * settingsRowH(screenH);
}
// Крестик выхода в правом верхнем углу сцены.
void settingsCloseRect(int screenW, int screenH, float& x, float& y, float& size){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    size = 58.0f * s;
    float m = settingsMargin(screenW);
    x = (float)screenW - m - size;
    y = 22.0f * s;
}
} // namespace

bool GameClient::handleSettingsTouch(float x, float y){
    float w = settingsPanelWidth(SCR_W);
    float px = settingsMargin(SCR_W);
    float rowH = settingsRowH(SCR_H);

    // Крестик в углу — такой же выход, как и нижняя строка списка.
    float cxb, cyb, csz;
    settingsCloseRect(SCR_W, SCR_H, cxb, cyb, csz);
    if(x >= cxb && x <= cxb + csz && y >= cyb && y <= cyb + csz){
        overlay_ = Overlay::None;
        saveSettings();
        return true;
    }
    for(int i = 0; i < SETTINGS_ROWS; ++i){
        float ry = settingsRowY(i, SCR_H);
        if(x < px || x > px + w || y < ry || y > ry + rowH - 6.0f) continue;
        switch(i){
            case 0: {   // качество по кругу
                int q = ((int)settings.quality + 1) % 4;
                settings.quality = (Quality)q;
                destroySceneFBO();   // изменился масштаб рендера
                break;
            }
            case 1: {   // дальность прорисовки чанков
                int cur = 0;
                for(int k = 0; k < VIEW_DISTANCE_OPTION_COUNT; ++k)
                    if(VIEW_DISTANCE_OPTIONS[k] == settings.viewDistance) cur = k;
                settings.viewDistance = VIEW_DISTANCE_OPTIONS[(cur + 1) % VIEW_DISTANCE_OPTION_COUNT];
                break;
            }
            case 2: {   // потолок кадров
                int cur = 0;
                for(int k = 0; k < FPS_LIMIT_OPTION_COUNT; ++k)
                    if(FPS_LIMIT_OPTIONS[k] == settings.fpsLimit) cur = k;
                settings.fpsLimit = FPS_LIMIT_OPTIONS[(cur + 1) % FPS_LIMIT_OPTION_COUNT];
                break;
            }
            case 3: settings.musicOn = !settings.musicOn; audioApplySettings(); break;
            case 4: settings.sfxOn = !settings.sfxOn; break;
            case 5: {
                float next = settings.lookSensitivity + 0.5f;
                if(next > 2.01f) next = 0.5f;
                settings.lookSensitivity = next;
                break;
            }
            case 6: settings.showDebugInfo = !settings.showDebugInfo; break;
            case 7:     // редактор раскладки кнопок
                // Кнопки живут в игре, а не в меню, поэтому из главного меню сначала
                // входим в игру: иначе строка нажималась, а расставлять было нечего.
                controls_.setEditMode(true);
                overlay_ = Overlay::None;
                if(state_ != GameState::Playing) state_ = GameState::Playing;
                break;
            case 8:     // сброс раскладки
                controls_.resetLayout();
                break;
            case 9:     // выход из настроек
                overlay_ = Overlay::None;
                break;
        }
        saveSettings();
        return true;
    }
    return true;   // касание внутри окна, но мимо строк — окно не закрываем
}

void GameClient::renderSettings(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = settingsPanelWidth(SCR_W);
    float px = settingsMargin(SCR_W);
    float top = settingsTop(SCR_H);
    float rowH = settingsRowH(SCR_H);

    // ---- Фон сцены: та же картинка, что и в главном меню, притушенная до читаемости.
    drawMenuBackground();
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.03f, 0.04f, 0.05f, 0.82f, false);

    // ---- Шапка сцены
    drawUIRect(0, 0, (float)SCR_W, 88.0f * s, 0, 0.06f, 0.07f, 0.08f, 0.9f, false);
    drawUIRect(0, 88.0f * s, (float)SCR_W, 2.0f * s, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.7f, false);
    drawText(px, 28.0f * s, 34.0f * s, "НАСТРОЙКИ", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);

    // ---- Крестик выхода
    float cxb, cyb, csz;
    settingsCloseRect(SCR_W, SCR_H, cxb, cyb, csz);
    if(texClose_){
        drawUIRect(cxb, cyb, csz, csz, texClose_, 1, 1, 1, 0.95f, true);
    } else {
        drawUIRect(cxb, cyb, csz, csz, 0, 0.20f, 0.10f, 0.10f, 0.9f, false);
        uiThinFrame(cxb, cyb, csz, csz, UI_ACCENT, 0.9f);
        drawText(cxb + csz * 0.28f, cyb + csz * 0.22f, 26.0f * s, "X",
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.95f);
    }

    char fpsBuf[32];
    if(settings.fpsLimit > 0) snprintf(fpsBuf, sizeof(fpsBuf), "%d", settings.fpsLimit);
    else                      snprintf(fpsBuf, sizeof(fpsBuf), "без ограничения");

    char storage[SETTINGS_ROWS][160];
    snprintf(storage[0], 160, "Качество графики: %s", qualityLabel());
    snprintf(storage[1], 160, "Дальность прорисовки: %d м (%d чанков)",
             settings.viewDistance, (settings.viewDistance + CHUNK_SIZE - 1) / CHUNK_SIZE);
    snprintf(storage[2], 160, "Потолок кадров: %s", fpsBuf);
    snprintf(storage[3], 160, "Музыка: %s", settings.musicOn ? "вкл" : "выкл");
    snprintf(storage[4], 160, "Звуки: %s", settings.sfxOn ? "вкл" : "выкл");
    snprintf(storage[5], 160, "Чувствительность обзора: %.1f", (double)settings.lookSensitivity);
    snprintf(storage[6], 160, "Отладочная строка: %s", settings.showDebugInfo ? "вкл" : "выкл");
    snprintf(storage[7], 160, "Расставить кнопки под свою руку");
    snprintf(storage[8], 160, "Сбросить раскладку кнопок");
    snprintf(storage[9], 160, "ВЫЙТИ ИЗ НАСТРОЕК");

    for(int i = 0; i < SETTINGS_ROWS; ++i){
        float ry = settingsRowY(i, SCR_H);
        bool action = (i >= 7);
        bool exitRow = (i == SETTINGS_ROWS - 1);
        drawUIRect(px, ry, w, rowH - 6.0f * s, 0,
                   exitRow ? 0.18f : UI_BG_SLOT.r, exitRow ? 0.22f : UI_BG_SLOT.g,
                   exitRow ? 0.18f : UI_BG_SLOT.b, 0.88f, false);
        uiThinFrame(px, ry, w, rowH - 6.0f * s, exitRow ? UI_ACCENT : UI_LINE, exitRow ? 0.9f : 0.5f);
        const UIColor& c = action ? UI_ACCENT : UI_TEXT;
        drawText(px + 18.0f * s, ry + 12.0f * s, 24.0f * s, storage[i], c.r, c.g, c.b, 0.97f);
    }
    drawText(px, (float)SCR_H - 26.0f * s, 17.0f * s,
             "Вертикальная синхронизация выключена — счётчик в углу показывает настоящую скорость",
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
}

bool GameClient::handleOverlayTouch(float x, float y){
    if(overlay_ == Overlay::Pause) return handlePauseTouch(x, y);
    if(overlay_ == Overlay::Furnace) return handleFurnaceTouch(x, y);
    if(overlay_ == Overlay::Box) return handleBoxTouch(x, y);
    if(overlay_ == Overlay::Cupboard) return handleCupboardTouch(x, y);
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);

    if(overlay_ == Overlay::Craft){
        float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        // Крестик выхода.
        float closeSize = 54.0f * s;
        float gx, gy, tile, gap;
        craftGridGeometry(gx, gy, tile, gap);
        float closeX = (float)SCR_W - closeSize - 46.0f * s, closeY = 64.0f * s;
        if(x >= closeX && x <= closeX + closeSize && y >= closeY && y <= closeY + closeSize){
            overlay_ = Overlay::None;
            return true;
        }
        craftDragging_ = true;   // палец на экране крафта: возможно, список будут листать
        // Плитка рецепта — выбор, а не мгновенный крафт: сначала посмотреть, что нужно.
        for(int i = 0; i < kRecipeCount; ++i){
            float tx, ty;
            craftTilePos(i, tx, ty);
            if(x >= tx && x <= tx + tile && y >= ty && y <= ty + tile){
                craftSelected_ = i;
                return true;
            }
        }
        // Кнопка «Создать».
        float bx, by, bw, bh;
        craftButtonRect(bx, by, bw, bh);
        if(x >= bx && x <= bx + bw && y >= by && y <= by + bh &&
           craftSelected_ >= 0 && craftSelected_ < kRecipeCount){
            const Recipe& r = kRecipes[craftSelected_];
            bool okA = inventory_.countOf(r.costA) >= r.costACount;
            bool okB = (r.costB == ItemType::None) || inventory_.countOf(r.costB) >= r.costBCount;
            if(okA && okB){
                inventory_.remove(r.costA, r.costACount);
                if(r.costB != ItemType::None) inventory_.remove(r.costB, r.costBCount);
                inventory_.add(r.result, r.resultCount);
            }
        }
        return true;
    }

    if(overlay_ != Overlay::Inventory) return false;

    // Обычный перенос: палец лёг на ячейку с предметом — берём её, ведём — предмет
    // едет за пальцем, отпустили над другой ячейкой — кладём туда.
    if(itemMenuSlot_ >= 0) return handleItemMenuTouch(x, y);

    // Крестик выхода: геометрия ОДНА и та же, что при отрисовке (inventoryCloseRect),
    // иначе попадание съезжает от нарисованной кнопки.
    {
        float cx, cy, cw, ch;
        inventoryCloseRect(cx, cy, cw, ch);
        // Запас вокруг крестика: палец толще картинки, и промах по краю раздражает.
        float pad = 12.0f * clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        if(x >= cx - pad && x <= cx + cw + pad && y >= cy - pad && y <= cy + ch + pad){
            overlay_ = Overlay::None;
            dragSlot_ = -1; dragActive_ = false;
            return true;
        }
    }

    int i = slotAtPoint(x, y);
    if(i >= 0 && !inventory_.slot(i).empty()){
        dragSlot_ = i;
        dragPos_ = Vec2{ x, y };
        dragActive_ = false;   // станет true, как только палец поедет
    } else {
        dragSlot_ = -1;
        dragActive_ = false;
    }
    return true;
}

// Ячейка под точкой экрана, или -1.
// ---- Окошко предмета. Короткое касание по ячейке открывает его: название, сколько
// в стаке и что с ним можно сделать. Перетаскивание при этом остаётся перетаскиванием —
// окно открывается только если палец не поехал.
void GameClient::itemMenuRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    w = clampf((float)SCR_W * 0.30f, 260.0f, 430.0f);
    h = 268.0f * s;   // выше прежнего: в 210 не влезали название и три кнопки
    x = ((float)SCR_W - w) * 0.5f;
    y = ((float)SCR_H - h) * 0.5f;
}

void GameClient::itemMenuButtonRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float mx, my, mw, mh;
    itemMenuRect(mx, my, mw, mh);
    w = mw - 24.0f * s;
    h = 42.0f * s;
    x = mx + 12.0f * s;
    y = my + 108.0f * s + i * (h + 10.0f * s);
}

void GameClient::renderItemMenu(){
    if(itemMenuSlot_ < 0) return;
    const ItemStack& st = inventory_.slot(itemMenuSlot_);
    if(st.empty()){ itemMenuSlot_ = -1; return; }

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float x, y, w, h;
    itemMenuRect(x, y, w, h);
    drawUIRect(x, y, w, h, 0, 0.10f, 0.10f, 0.11f, 0.92f, false);
    uiThinFrame(x, y, w, h, UIColor{1.0f, 1.0f, 1.0f}, 0.35f);

    const ItemDef& def = itemDef(st.type);
    drawText(x + 14.0f * s, y + 12.0f * s, 24.0f * s, def.nameRu, 1, 1, 1, 0.97f);
    char buf[64];
    snprintf(buf, sizeof(buf), "в стаке: %d", st.count);
    drawText(x + 14.0f * s, y + 46.0f * s, 17.0f * s, buf, 1, 1, 1, 0.65f);

    const char* labels[3] = { "ВЫБРОСИТЬ", "РАЗДЕЛИТЬ ПОПОЛАМ", "ЗАКРЫТЬ" };
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        itemMenuButtonRect(i, bx, by, bw, bh);
        bool can = (i != 1) || st.count >= 2;
        drawUIRect(bx, by, bw, bh, 0, 0.16f, 0.16f, 0.17f, can ? 0.92f : 0.55f, false);
        uiThinFrame(bx, by, bw, bh, UIColor{1.0f, 1.0f, 1.0f}, can ? 0.30f : 0.12f);
        drawText(bx + 14.0f * s, by + bh * 0.26f, 19.0f * s, labels[i], 1, 1, 1, can ? 0.95f : 0.4f);
    }
}

bool GameClient::handleItemMenuTouch(float x, float y){
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        itemMenuButtonRect(i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        // «Выбросить» кидает ВЕСЬ стак под ноги, а не стирает его: предмет ложится
        // перед игроком кубом, и его можно поднять обратно.
        if(i == 0)      dropStackToWorld(itemMenuSlot_);
        else if(i == 1) inventory_.splitSlot(itemMenuSlot_);
        itemMenuSlot_ = -1;
        return true;
    }
    // Касание мимо кнопок закрывает окно: отдельного крестика ему не нужно.
    float mx, my, mw, mh;
    itemMenuRect(mx, my, mw, mh);
    if(x < mx || x > mx + mw || y < my || y > my + mh) itemMenuSlot_ = -1;
    return true;
}

// Разбивает строку на строки по ширине. Своего переноса у drawText нет, а описание
// рецепта в одну строку не влезает и уезжает за край панели.
namespace {
std::vector<std::string> wrapText(const std::string& text, float fontH, float maxWidth){
    std::vector<std::string> lines;
    // Ширина символа оценивается как 0.5 кегля — этого хватает: шрифт узкий, а
    // точная метрика тут не нужна, важно не вылезти за панель.
    size_t perLine = (size_t)fmaxf(8.0f, maxWidth / (fontH * 0.5f));
    std::string line;
    size_t pos = 0;
    while(pos < text.size()){
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        // В UTF-8 русская буква занимает два байта, поэтому длину считаем по символам.
        auto charLen = [](const std::string& t){
            size_t n = 0;
            for(unsigned char c : t) if((c & 0xC0) != 0x80) ++n;
            return n;
        };
        if(!line.empty() && charLen(line) + charLen(word) + 1 > perLine){
            lines.push_back(line);
            line.clear();
        }
        if(!line.empty()) line += ' ';
        line += word;
        if(space == std::string::npos) break;
        pos = space + 1;
    }
    if(!line.empty()) lines.push_back(line);
    return lines;
}
} // namespace

// ==================== РЕЖИМ СТРОЙКИ ====================
// В руках план постройки — значит строим. Снизу лента с частями дома, справа кнопка
// подтверждения; пока не подтвердил, на месте будущей детали стоит полупрозрачный
// призрак, и его видно, куда он встанет.

namespace {
// Из чего и почём. Стоимость в дереве — как в ТЗ: фундамент 2x2 за 20 дерева.
struct BuildDef { const char* name; Block block; int cost; };
const BuildDef kBuildParts[] = {
    { "ФУНДАМЕНТ", Block::Foundation, 20 },
    { "СТЕНА",     Block::BuildWall,  10 },
    { "ПОТОЛОК",   Block::BuildFloor, 10 },
    { "ДВЕРЬ",     Block::BuildDoor,   8 },
};
const int kBuildPartCount = (int)(sizeof(kBuildParts)/sizeof(kBuildParts[0]));
} // namespace

bool GameClient::buildMode() const {
    if(state_ != GameState::Playing || overlay_ != Overlay::None) return false;
    const ItemStack& sel = inventory_.selectedStack();
    return !sel.empty() && sel.type == ItemType::BuildPlan;
}

void GameClient::buildPartRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    w = h = 76.0f * s;
    float gap = 10.0f * s;
    float total = w * kBuildPartCount + gap * (kBuildPartCount - 1);
    float hx, hy, slot, hgap;
    hotbarGeometry(hx, hy, slot, hgap);
    x = ((float)SCR_W - total) * 0.5f + i * (w + gap);
    y = hy - h - 46.0f * s;      // над поясом, чтобы не спорить с ним
}

void GameClient::buildAcceptRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    w = h = 96.0f * s;
    x = (float)SCR_W * 0.5f + 210.0f * s;
    float bx, by, bw, bh;
    buildPartRect(0, bx, by, bw, bh);
    y = by - h - 12.0f * s;
}

// Куда встанет деталь. Фундамент и потолок ложатся на клетку перед прицелом, стена и
// дверь — тоже, но проверка «на что опирается» разная.
bool GameClient::buildGhostTarget(int& bx, int& by, int& bz) const {
    RayHit hit = voxels_->raycast(player_->eyePosition(), player_->lookDirection(), 6.0f);
    if(!hit.hit) return false;
    bx = hit.prevX; by = hit.prevY; bz = hit.prevZ;
    return voxels_->blockAt(bx, by, bz) == Block::Air;
}

void GameClient::renderBuildGhost(const Mat4& view, const Mat4& proj){
    int bx, by, bz;
    if(!buildGhostTarget(bx, by, bz)) return;
    const BuildDef& def = kBuildParts[(int)buildPart_];

    // Призрак — тот же куб из частиц, только полупрозрачный и на месте будущей детали.
    std::vector<VoxelVertex> verts;
    float tr, tg, tb;
    blockTextureTint(def.block, tr, tg, tb);
    float layer = (float)blockTextureLayer(def.block);
    // Призрак показывает НАСТОЯЩИЙ размер детали: 4x4 у фундамента и крыши, 2x2 у
    // стены, 1x2 у двери. Раньше он был кубиком, и куда встанет дом, приходилось гадать.
    Block ghostBlock = def.block;
    Vec3 look = player_->lookDirection();
    bool alongX = fabsf(look.x) > fabsf(look.z);
    if(ghostBlock == Block::BuildWall && !alongX) ghostBlock = Block::BuildWallZ;
    if(ghostBlock == Block::BuildDoor && !alongX) ghostBlock = Block::BuildDoorZ;
    int gsx = 1, gsy = 1, gsz = 1;
    pieceFootprint(buildPart_, ghostBlock, gsx, gsy, gsz);
    for(int dx = 0; dx < gsx; ++dx)
        for(int dy = 0; dy < gsy; ++dy)
            for(int dz = 0; dz < gsz; ++dz)
                pushCube(verts, Vec3{ (float)(bx + dx) + 0.5f, (float)(by + dy) + 0.5f,
                                      (float)(bz + dz) + 0.5f },
                         0.5f, tr, tg, tb, layer);

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(voxelFogDensityLoc, 0.0f);
    glUniform1f(voxelAlphaLoc, 0.45f);
    bindBlockTextures();
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(partVao_);
    glBindBuffer(GL_ARRAY_BUFFER, partVbo_);
    size_t bytes = verts.size() * sizeof(VoxelVertex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glUniform1f(voxelAlphaLoc, 1.0f);
}

// Размер детали в блоках. Из ТЗ: фундамент 4x4, стена 2 в ширину и 2 в высоту,
// крыша 4x4, дверь 1 в ширину и 2 в высоту. Толщина стены и двери — сама пластина.
void GameClient::pieceFootprint(BuildPart part, Block block, int& sx, int& sy, int& sz) const {
    switch(part){
        case BuildPart::Foundation: sx = 4; sy = 1; sz = 4; break;
        case BuildPart::Floor:      sx = 4; sy = 1; sz = 4; break;
        case BuildPart::Wall:
            // Пластина поперёк X растёт по Z, и наоборот.
            if(block == Block::BuildWall){ sx = 1; sy = 2; sz = 2; }
            else                         { sx = 2; sy = 2; sz = 1; }
            break;
        case BuildPart::Door:       sx = 1; sy = 2; sz = 1; break;
        default:                    sx = sy = sz = 1; break;
    }
}

int GameClient::pieceIndexAt(int x, int y, int z) const {
    for(size_t i = 0; i < pieces_.size(); ++i){
        const BuildPiece& p = pieces_[i];
        if(x < p.x || x >= p.x + p.sx) continue;
        if(y < p.y || y >= p.y + p.sy) continue;
        if(z < p.z || z >= p.z + p.sz) continue;
        return (int)i;
    }
    return -1;
}

void GameClient::fillPieceCells(const BuildPiece& p, bool put){
    for(int dx = 0; dx < p.sx; ++dx)
        for(int dy = 0; dy < p.sy; ++dy)
            for(int dz = 0; dz < p.sz; ++dz)
                voxels_->setBlock(p.x + dx, p.y + dy, p.z + dz, put ? p.block : Block::Air);
}

// Удар топором по дому: минус единица прочности всей детали. Разбирается деталь
// целиком — половина стены в воздухе выглядела бы поломкой игры, а не постройкой.
void GameClient::hitBuildPiece(Block block, int x, int y, int z){
    int idx = pieceIndexAt(x, y, z);
    if(idx < 0){
        // Деталь поставили до появления реестра (или это шкаф с ящиком) — просто
        // убираем блок, чтобы построенное не оказалось вечным.
        if(block == Block::Cupboard || block == Block::Box) return;
        voxels_->setBlock(x, y, z, Block::Air);
        return;
    }
    BuildPiece& p = pieces_[(size_t)idx];
    p.health -= 1;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: прочность %d/%d", blockName(p.block), p.health, PIECE_MAX_HEALTH);
    SDL_Log("%s", buf);
    if(p.health <= 0){
        fillPieceCells(p, false);
        pieces_.erase(pieces_.begin() + idx);
    }
}

// Дверь: по кнопке «рука» она уходит из мира (открыта) и возвращается (закрыта).
// Закрывают её по близости — целиться в открытую дверь уже не во что.
bool GameClient::toggleDoorNear(){
    RayHit hit = voxels_->raycast(player_->eyePosition(), player_->lookDirection(), 4.0f);
    if(hit.hit && isDoorBlock(hit.block)){
        int idx = pieceIndexAt(hit.x, hit.y, hit.z);
        if(idx >= 0){
            BuildPiece& p = pieces_[(size_t)idx];
            fillPieceCells(p, false);
            p.open = true;
            return true;
        }
    }
    // Ничего не нашли лучом — ищем открытую дверь рядом и закрываем её.
    Vec3 pos = player_->position();
    int best = -1;
    float bestDist = 3.5f;
    for(size_t i = 0; i < pieces_.size(); ++i){
        const BuildPiece& p = pieces_[i];
        if(!p.open || !isDoorBlock(p.block)) continue;
        float dx = (float)p.x + 0.5f - pos.x, dz = (float)p.z + 0.5f - pos.z;
        float d = sqrtf(dx * dx + dz * dz);
        if(d < bestDist){ bestDist = d; best = (int)i; }
    }
    if(best < 0) return false;
    BuildPiece& p = pieces_[(size_t)best];
    // Не захлопываем дверь на самом игроке.
    if(fabsf((float)p.x + 0.5f - pos.x) < 0.75f && fabsf((float)p.z + 0.5f - pos.z) < 0.75f)
        return false;
    fillPieceCells(p, true);
    p.open = false;
    return true;
}

void GameClient::placeBuildPart(){
    int bx, by, bz;
    if(!buildGhostTarget(bx, by, bz)) return;
    const BuildDef& def = kBuildParts[(int)buildPart_];
    if(inventory_.countOf(ItemType::Wood) < def.cost) return;

    // Стена и дверь встают поперёк взгляда: смотрим вдоль X — пластина поперёк X.
    Block block = def.block;
    Vec3 look = player_->lookDirection();
    bool alongX = fabsf(look.x) > fabsf(look.z);
    if(block == Block::BuildWall && !alongX) block = Block::BuildWallZ;
    if(block == Block::BuildDoor && !alongX) block = Block::BuildDoorZ;

    BuildPiece p;
    p.block = block;
    p.x = bx; p.y = by; p.z = bz;
    pieceFootprint(buildPart_, block, p.sx, p.sy, p.sz);
    p.health = PIECE_MAX_HEALTH;

    // Крупная деталь встаёт от прицела, но если под неё не хватает воздуха — часть
    // клеток просто перезапишется; строить в скале и так незачем.
    fillPieceCells(p, true);
    pieces_.push_back(p);
    inventory_.remove(ItemType::Wood, def.cost);
}

void GameClient::renderBuildBar(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    GLuint icons[4] = { texCatFoundation_, texCatWall_ ? texCatWall_ : texCatFloor_,
                        texCatFloor_, texCatDoor_ };
    for(int i = 0; i < kBuildPartCount; ++i){
        float x, y, w, h;
        buildPartRect(i, x, y, w, h);
        bool on = (i == (int)buildPart_);
        drawUIRect(x, y, w, h, 0, 0.137f, 0.141f, 0.153f, on ? 0.92f : 0.62f, false);
        if(icons[i]){
            float ip = w * 0.12f;
            drawUIRect(x + ip, y + ip, w - ip * 2.0f, h - ip * 2.0f, icons[i], 1, 1, 1,
                       on ? 1.0f : 0.6f, true);
        }
        // Цена деталью снизу: без неё непонятно, почему кнопка не срабатывает.
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", kBuildParts[i].cost);
        bool afford = inventory_.countOf(ItemType::Wood) >= kBuildParts[i].cost;
        drawText(x + w * 0.5f - 8.0f * s, y + h - 20.0f * s, 16.0f * s, buf,
                 afford ? 0.75f : 0.9f, afford ? 0.9f : 0.3f, 0.4f, 0.95f);
        if(on) uiThinFrame(x, y, w, h, UI_ACCENT, 0.95f);
        else   uiThinFrame(x, y, w, h, UI_LINE, 0.35f);
    }

    // Кнопка подтверждения: ставит деталь на место призрака.
    float ax, ay, aw, ah;
    buildAcceptRect(ax, ay, aw, ah);
    GLuint icon = texBuildAccept_ ? texBuildAccept_ : texBuild_;
    if(icon) drawUIRect(ax, ay, aw, ah, icon, 1, 1, 1, 0.92f, true);
    else {
        drawUICircle(ax + aw * 0.5f, ay + ah * 0.5f, aw * 0.5f, 0.2f, 0.4f, 0.2f, 0.7f);
        drawText(ax + aw * 0.2f, ay + ah * 0.35f, 18.0f * s, "OK", 1, 1, 1, 0.95f);
    }
}

bool GameClient::handleBuildTouch(float x, float y){
    for(int i = 0; i < kBuildPartCount; ++i){
        float bx, by, bw, bh;
        buildPartRect(i, bx, by, bw, bh);
        if(x >= bx && x <= bx + bw && y >= by && y <= by + bh){
            buildPart_ = (BuildPart)i;
            return true;
        }
    }
    float ax, ay, aw, ah;
    buildAcceptRect(ax, ay, aw, ah);
    if(x >= ax && x <= ax + aw && y >= ay && y <= ay + ah){
        placeBuildPart();
        return true;
    }
    return false;
}

// ---- Окно печи. Плавка идёт по нажатию: кладём руду и дрова из инвентаря, получаем
// слиток. Очереди и таймеров пока нет — они появятся вместе с верстаками.
void GameClient::furnaceButtonRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pw = clampf((float)SCR_W * 0.46f, 400.0f, 680.0f);
    float ph = 330.0f * s;
    float px = ((float)SCR_W - pw) * 0.5f, py = ((float)SCR_H - ph) * 0.5f;
    w = pw - 40.0f * s;
    h = 46.0f * s;
    x = px + 20.0f * s;
    y = py + ph - (h + 14.0f * s) * (float)(4 - i);
}

// Плавка идёт сама, пока в печи есть руда: пять секунд на штуку.
void GameClient::updateFurnace(float dt){
    if(furnace_.oreCount <= 0) return;
    furnace_.progress += dt / 5.0f;
    if(furnace_.progress < 1.0f) return;
    furnace_.progress = 0.0f;
    furnace_.oreCount -= 1;
    furnace_.done += 1;
}

void GameClient::renderFurnace(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pw = clampf((float)SCR_W * 0.46f, 400.0f, 680.0f);
    float ph = 330.0f * s;
    float px = ((float)SCR_W - pw) * 0.5f, py = ((float)SCR_H - ph) * 0.5f;

    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.04f, 0.04f, 0.05f, 0.45f, false);
    drawUIRect(px, py, pw, ph, 0, 0.10f, 0.10f, 0.11f, 0.92f, false);
    uiThinFrame(px, py, pw, ph, UIColor{1.0f, 1.0f, 1.0f}, 0.35f);
    drawText(px + 20.0f * s, py + 14.0f * s, 26.0f * s, "ПЕЧЬ", 1, 1, 1, 0.97f);

    // Слот руды, полоса плавки и слот готового — слева направо, как в печи Rust.
    float slot = 74.0f * s;
    float sy = py + 56.0f * s;
    float sx = px + 24.0f * s;
    ItemStack inStack{ furnace_.ore, furnace_.oreCount };
    drawSlot(sx, sy, slot, furnace_.oreCount > 0 ? inStack : ItemStack{}, false);

    float barX = sx + slot + 18.0f * s, barW = pw - slot * 2.0f - 84.0f * s;
    drawUIRect(barX, sy + slot * 0.42f, barW, 14.0f * s, 0, 0.18f, 0.18f, 0.19f, 0.9f, false);
    drawUIRect(barX, sy + slot * 0.42f, barW * clampf(furnace_.progress, 0.0f, 1.0f), 14.0f * s,
               0, 0.95f, 0.55f, 0.18f, 0.95f, false);

    // Огонь у полосы плавки: пока идёт плавка, печь топится, и это должно быть видно.
    if(furnace_.oreCount > 0 && texFire_){
        float fsz = 30.0f * s;
        float flick = 0.75f + 0.25f * sinf(animTime_ * 9.0f);
        drawUIRect(barX + barW * 0.5f - fsz * 0.5f, sy + slot * 0.42f - fsz - 6.0f * s,
                   fsz, fsz, texFire_, 1, 1, 1, flick, true);
    }

    float outX = barX + barW + 18.0f * s;
    ItemStack outStack{ furnace_.result, furnace_.done };
    drawSlot(outX, sy, slot, furnace_.done > 0 ? outStack : ItemStack{}, false);

    char buf[128];
    snprintf(buf, sizeof(buf), "Дрова: %d      В печи: %d      Готово: %d",
             inventory_.countOf(ItemType::Wood), furnace_.oreCount, furnace_.done);
    drawText(px + 24.0f * s, sy + slot + 14.0f * s, 18.0f * s, buf, 1, 1, 1, 0.8f);

    const char* labels[4] = { "ЗАГРУЗИТЬ СЕРНУЮ РУДУ  (1 руда + 2 дерева)",
                              "ЗАГРУЗИТЬ ЖЕЛЕЗНУЮ РУДУ  (1 руда + 2 дерева)",
                              "ЗАБРАТЬ ГОТОВОЕ",
                              "ЗАКРЫТЬ" };
    bool wood = inventory_.countOf(ItemType::Wood) >= 2;
    bool can[4] = {
        wood && inventory_.countOf(ItemType::OreSulfur) >= 1 &&
            (furnace_.ore == ItemType::None || furnace_.ore == ItemType::OreSulfur || furnace_.oreCount == 0),
        wood && inventory_.countOf(ItemType::OreMetal) >= 1 &&
            (furnace_.ore == ItemType::None || furnace_.ore == ItemType::OreMetal || furnace_.oreCount == 0),
        furnace_.done > 0,
        true
    };
    for(int i = 0; i < 4; ++i){
        float bx, by, bw, bh;
        furnaceButtonRect(i, bx, by, bw, bh);
        drawUIRect(bx, by, bw, bh, 0, 0.16f, 0.16f, 0.17f, can[i] ? 0.92f : 0.5f, false);
        uiThinFrame(bx, by, bw, bh, UIColor{1.0f, 1.0f, 1.0f}, can[i] ? 0.3f : 0.12f);
        drawText(bx + 14.0f * s, by + bh * 0.26f, 18.0f * s, labels[i],
                 1, 1, 1, can[i] ? 0.95f : 0.4f);
    }
}

bool GameClient::handleFurnaceTouch(float x, float y){
    for(int i = 0; i < 4; ++i){
        float bx, by, bw, bh;
        furnaceButtonRect(i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if(i == 3){ overlay_ = Overlay::None; return true; }
        if(i == 2){
            if(furnace_.done > 0){
                inventory_.add(furnace_.result, furnace_.done);
                furnace_.done = 0;
                if(furnace_.oreCount == 0) furnace_.result = ItemType::None;
            }
            return true;
        }
        ItemType ore = (i == 0) ? ItemType::OreSulfur : ItemType::OreMetal;
        ItemType out = (i == 0) ? ItemType::Sulfur    : ItemType::MetalFrag;
        // В печи плавится один вид руды за раз: смешивать нечего, а путаницы много.
        if(furnace_.oreCount > 0 && furnace_.ore != ore) return true;
        if(inventory_.countOf(ore) < 1 || inventory_.countOf(ItemType::Wood) < 2) return true;
        inventory_.remove(ore, 1);
        inventory_.remove(ItemType::Wood, 2);
        furnace_.ore = ore;
        furnace_.result = out;
        furnace_.oreCount += 1;
        return true;
    }
    return true;
}

int GameClient::slotAtPoint(float x, float y) const {
    float gx, gy, slot, gap;
    inventoryGeometry(gx, gy, slot, gap);
    for(int i = Inventory::HOTBAR; i < Inventory::SIZE; ++i){
        float sx, sy;
        inventorySlotPos(i, sx, sy);
        if(x >= sx && x <= sx + slot && y >= sy && y <= sy + slot) return i;
    }
    // Ячейки пояса внизу экрана — часть того же инвентаря: в них тоже можно бросить.
    float hx, hy, hslot, hgap;
    hotbarGeometry(hx, hy, hslot, hgap);
    for(int i = 0; i < Inventory::HOTBAR; ++i){
        float sx = hx + i * (hslot + hgap);
        if(x >= sx && x <= sx + hslot && y >= hy && y <= hy + hslot) return i;
    }
    return -1;
}

// Метка на карте: короткое касание по пустому месту ставит флажок, касание по уже
// стоящему флажку его снимает. Радиус попадания в экранных пикселях, а не в метрах:
// на любом масштабе палец должен попадать по нарисованному значку, а не по точке.
void GameClient::toggleMapMark(float screenX, float screenY){
    const WorldConfig& cfg = world_->config();
    float aspect = (float)SCR_W / (float)SCR_H;
    float spanZ = cfg.size / mapZoom_;
    float spanX = spanZ * aspect;
    float x0 = mapCenterX_ - spanX * 0.5f, z0 = mapCenterZ_ - spanZ * 0.5f;

    float wx = x0 + screenX / (float)SCR_W * spanX;
    float wz = z0 + screenY / (float)SCR_H * spanZ;
    if(wx < 0.0f || wx > cfg.size || wz < 0.0f || wz > cfg.size) return;

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float hitPx = 26.0f * s;
    float hitX = hitPx / (float)SCR_W * spanX;
    float hitZ = hitPx / (float)SCR_H * spanZ;
    for(size_t i = 0; i < mapMarks_.size(); ++i){
        if(fabsf(mapMarks_[i].x - wx) <= hitX && fabsf(mapMarks_[i].y - wz) <= hitZ){
            mapMarks_.erase(mapMarks_.begin() + (long)i);
            return;
        }
    }
    // Меток не больше трёх: они висят ещё и на компасе, и четвёртая там уже не читается.
    // Ставим четвёртую — самая старая уходит.
    mapMarks_.push_back(Vec2{ wx, wz });
    if(mapMarks_.size() > 3) mapMarks_.erase(mapMarks_.begin());
}

bool GameClient::handleMapEvent(const SDL_Event& e){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float closeSize = 54.0f * s;
    float closeX = (float)SCR_W - closeSize - 12.0f * s, closeY = 12.0f * s;
    float closeW = closeSize, closeH = closeSize;

    auto pinchDistance = [&]() -> float {
        if(mapFingers_.size() < 2) return 0.0f;
        auto it = mapFingers_.begin();
        Vec2 a = it->second; ++it;
        Vec2 b = it->second;
        return v2dist(a, b);
    };

    switch(e.type){
        case SDL_FINGERDOWN: {
            float x = e.tfinger.x * (float)SCR_W, y = e.tfinger.y * (float)SCR_H;
            if(x >= closeX && x <= closeX + closeW && y >= closeY && y <= closeY + closeH){
                // Карту открывают из паузы, поэтому крестик возвращает туда же, а не
                // выкидывает сразу в игру.
                overlay_ = Overlay::Pause;
                mapFingers_.clear();
                return true;
            }
            mapFingers_[e.tfinger.fingerId] = Vec2{ x, y };
            if(mapFingers_.size() == 2){
                // Второй палец лёг — запоминаем базу щипка, чтобы масштаб менялся
                // от текущего, а не прыгал к какому-то абсолютному значению.
                pinchBaseDist_ = pinchDistance();
                pinchBaseZoom_ = mapZoom_;
                mapDragging_ = false;
            } else if(mapFingers_.size() == 1){
                mapDragging_ = true;
                mapFollowsPlayer_ = false;
                // Запоминаем начало касания: если палец почти не сдвинулся и быстро
                // поднялся — это тап по карте, а не перетаскивание.
                mapTapStart_ = Vec2{ x, y };
                mapTapTime_ = SDL_GetTicks();
                mapTapValid_ = true;
            } else {
                mapTapValid_ = false;
            }
            return true;
        }
        case SDL_FINGERMOTION: {
            auto it = mapFingers_.find(e.tfinger.fingerId);
            if(it == mapFingers_.end()) return true;
            float x = e.tfinger.x * (float)SCR_W, y = e.tfinger.y * (float)SCR_H;
            float dx = x - it->second.x, dy = y - it->second.y;
            it->second = Vec2{ x, y };

            if(mapFingers_.size() >= 2){
                float dist = pinchDistance();
                if(pinchBaseDist_ > 1.0f && dist > 1.0f)
                    mapZoom_ = clampf(pinchBaseZoom_ * (dist / pinchBaseDist_), 1.0f, 12.0f);
                return true;
            }
            if(mapTapValid_ && v2dist(Vec2{ x, y }, mapTapStart_) > 14.0f) mapTapValid_ = false;
            if(mapDragging_){
                float spanZ = world_->config().size / mapZoom_;
                float spanX = spanZ * (float)SCR_W / (float)SCR_H;
                mapCenterX_ -= dx / (float)SCR_W * spanX;
                mapCenterZ_ -= dy / (float)SCR_H * spanZ;
            }
            return true;
        }
        case SDL_FINGERUP: {
            float x = e.tfinger.x * (float)SCR_W, y = e.tfinger.y * (float)SCR_H;
            mapFingers_.erase(e.tfinger.fingerId);
            if(mapFingers_.size() < 2) pinchBaseDist_ = 0.0f;
            if(mapFingers_.empty()){
                mapDragging_ = false;
                if(mapTapValid_ && SDL_GetTicks() - mapTapTime_ < 400
                   && v2dist(Vec2{ x, y }, mapTapStart_) <= 14.0f){
                    toggleMapMark(x, y);
                }
            }
            mapTapValid_ = false;
            return true;
        }
        // ---- Мышь на ПК: перетаскивание и колесо.
        case SDL_MOUSEBUTTONDOWN: {
            if(e.button.which == SDL_TOUCH_MOUSEID) return true;
            float x = (float)e.button.x, y = (float)e.button.y;
            if(x >= closeX && x <= closeX + closeW && y >= closeY && y <= closeY + closeH){
                overlay_ = Overlay::Pause;
                return true;
            }
            mapDragging_ = true;
            mapFollowsPlayer_ = false;
            mapTapStart_ = Vec2{ x, y };
            mapTapTime_ = SDL_GetTicks();
            mapTapValid_ = true;
            return true;
        }
        case SDL_MOUSEMOTION: {
            if(e.motion.which == SDL_TOUCH_MOUSEID) return true;
            if(mapDragging_ && (e.motion.state & SDL_BUTTON_LMASK)){
                float spanZ = world_->config().size / mapZoom_;
                float spanX = spanZ * (float)SCR_W / (float)SCR_H;
                mapCenterX_ -= (float)e.motion.xrel / (float)SCR_W * spanX;
                mapCenterZ_ -= (float)e.motion.yrel / (float)SCR_H * spanZ;
            }
            return true;
        }
        case SDL_MOUSEBUTTONUP: {
            if(e.button.which != SDL_TOUCH_MOUSEID && mapTapValid_
               && SDL_GetTicks() - mapTapTime_ < 400
               && v2dist(Vec2{ (float)e.button.x, (float)e.button.y }, mapTapStart_) <= 14.0f){
                toggleMapMark((float)e.button.x, (float)e.button.y);
            }
            mapTapValid_ = false;
            mapDragging_ = false;
            return true;
        }
        case SDL_MOUSEWHEEL:
            mapZoom_ = clampf(mapZoom_ * (e.wheel.y > 0 ? 1.25f : 0.8f), 1.0f, 12.0f);
            return true;
        default: return false;
    }
}

void GameClient::handleOverlayDrag(float x, float y, float dx, float dy){
    if(overlay_ == Overlay::Craft && craftDragging_){
        float gx, gy, tile, gap;
        craftGridGeometry(gx, gy, tile, gap);
        int rows = (kRecipeCount + CRAFT_COLS - 1) / CRAFT_COLS;
        float contentH = rows * (tile + gap);
        float viewH = (float)SCR_H - gy - tile * 0.4f;
        float maxScroll = contentH > viewH ? contentH - viewH : 0.0f;
        craftScroll_ = clampf(craftScroll_ - dy, 0.0f, maxScroll);
        return;
    }
    if(overlay_ == Overlay::Inventory && dragSlot_ >= 0){
        dragPos_ = Vec2{ x, y };
        // Порог в несколько пикселей: случайное дрожание пальца не должно считаться
        // переносом, иначе простое касание ячейки уже «тащит».
        if(!dragActive_ && (fabsf(dx) + fabsf(dy)) > 3.0f) dragActive_ = true;
    }
}

void GameClient::handleOverlayRelease(){
    mapDragging_ = false;
    craftDragging_ = false;


    if(overlay_ == Overlay::Inventory && dragSlot_ >= 0){
        int target = slotAtPoint(dragPos_.x, dragPos_.y);
        if(dragActive_ && target >= 0 && target != dragSlot_){
            inventory_.moveOrSwap(dragSlot_, target);
        } else if(!dragActive_){
            // Палец не поехал — это не перенос, а тычок по предмету. Ячейка пояса
            // берётся в руку, ячейка рюкзака открывает окошко предмета.
            if(dragSlot_ < Inventory::HOTBAR) inventory_.select(dragSlot_);
            else                              itemMenuSlot_ = dragSlot_;
        }
        dragSlot_ = -1;
        dragActive_ = false;
    }
}

void GameClient::handleEvents(){
    SDL_Event e;
    while(SDL_PollEvent(&e)){
        if(e.type == SDL_QUIT){ running_ = false; continue; }
        // Свернули игру или потеряли фокус — система не пришлёт FINGERUP, и управление
        // залипло бы в последнем состоянии.
        if(e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_DIDENTERBACKGROUND ||
           (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)){
            controls_.releaseAllTouches();
            mapFingers_.clear();
            continue;
        }
        if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){
            SDL_GL_GetDrawableSize(win, &SCR_W, &SCR_H);
            controls_.layout(SCR_W, SCR_H);
            destroySceneFBO();
            continue;
        }
        // Кнопка «назад» на Android и Escape: в игре закрывают окно или уводят в меню,
        // в меню — выходят из игры. Случайный выход посреди выживания недопустим.
        if(e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE)){
            if(overlay_ != Overlay::None){ overlay_ = Overlay::None; dragSlot_ = -1; }
            else if(state_ == GameState::Playing){ net_.leave(); state_ = GameState::MainMenu; }
            else running_ = false;
            continue;
        }
        // Ввод адреса сервера и имени: экранная клавиатура присылает SDL_TEXTINPUT.
        if(e.type == SDL_TEXTINPUT && state_ == GameState::MainMenu){
            menuTextInput(e.text.text);
            continue;
        }
        if(e.type == SDL_KEYDOWN && state_ == GameState::MainMenu &&
           (menuAddOpen_ || menuEditName_)){
            if(e.key.keysym.sym == SDLK_BACKSPACE){ menuBackspace(); continue; }
            if(e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER){
                // Enter = «готово»: повторяем то же, что делает кнопка.
                float bx, by, bw, bh;
                menuActionRect(0, bx, by, bw, bh);
                float sc = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
                float pw = fminf((float)SCR_W * 0.6f, 760.0f * sc), ph = 150.0f * sc;
                float px = ((float)SCR_W - pw) * 0.5f, py = (float)SCR_H * 0.32f;
                handleMenuTouch(px + pw - 96.0f * sc, py + ph - 37.0f * sc);
                continue;
            }
        }
        if(e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_6){
            inventory_.select(e.key.keysym.sym - SDLK_1);
            continue;
        }

        // Приводим касание и настоящую мышь к одному виду. Синтетическая мышь от
        // касаний (SDL_TOUCH_MOUSEID) отбрасывается — иначе каждое касание приходит дважды.
        float tx = -1, ty = -1, mdx = 0, mdy = 0;
        bool down = false, motion = false, up = false;
        if(e.type == SDL_FINGERDOWN){
            tx = e.tfinger.x * (float)SCR_W; ty = e.tfinger.y * (float)SCR_H; down = true;
        } else if(e.type == SDL_FINGERMOTION){
            tx = e.tfinger.x * (float)SCR_W; ty = e.tfinger.y * (float)SCR_H;
            mdx = e.tfinger.dx * (float)SCR_W; mdy = e.tfinger.dy * (float)SCR_H; motion = true;
        } else if(e.type == SDL_FINGERUP){
            up = true;
        } else if(e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID){
            tx = (float)e.button.x; ty = (float)e.button.y; down = true;
        } else if(e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID &&
                  (e.motion.state & SDL_BUTTON_LMASK)){
            tx = (float)e.motion.x; ty = (float)e.motion.y;
            mdx = (float)e.motion.xrel; mdy = (float)e.motion.yrel; motion = true;
        } else if(e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID){
            up = true;
        }

        // ---- Главное меню забирает ввод целиком: мир за ним живёт, но не управляется.
        if(state_ == GameState::MainMenu){
            if(down){ handleMenuTouch(tx, ty); continue; }
            if(up){ handleOverlayRelease(); continue; }
            if(motion && overlay_ != Overlay::None){ handleOverlayDrag(tx, ty, mdx, mdy); continue; }
            continue;
        }

        if(controls_.editMode()){
            // Кнопка «ГОТОВО» в подсказке редактора: её геометрия повторяет отрисовку.
            if(down){
                float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
                float w = (float)SCR_W * 0.5f;
                float px = ((float)SCR_W - w) * 0.5f, py = 24.0f * s;
                if(tx >= px + 16.0f * s && tx <= px + 166.0f * s &&
                   ty >= py + 48.0f * s && ty <= py + 80.0f * s){
                    controls_.setEditMode(false);
                    continue;
                }
                if(tx >= px + 186.0f * s && tx <= px + 446.0f * s &&
                   ty >= py + 48.0f * s && ty <= py + 80.0f * s){
                    std::string text = controls_.layoutAsText();
                    SDL_SetClipboardText(text.c_str());
                    SDL_Log("Раскладка кнопок: %s", text.c_str());
                    continue;
                }
            }
            controls_.handleEvent(e);
            continue;
        }

        // ---- Открытое окно забирает касания себе: иначе палец на списке рецептов
        // одновременно листал бы его и крутил камеру.
        if(overlay_ == Overlay::Map){
            // Карте нужны идентификаторы пальцев (щипок), поэтому она разбирает
            // события сама, без общего приведения к «нажали в точке».
            if(handleMapEvent(e)) continue;
        }
        if(overlay_ != Overlay::None){
            if(down){ handleOverlayTouch(tx, ty); continue; }
            if(motion){ handleOverlayDrag(tx, ty, mdx, mdy); continue; }
            if(up){ handleOverlayRelease(); continue; }
            continue;
        }

        if(down && overlay_ == Overlay::None){
            // Тап по полосам состояния слева сверху — меню паузы. Отдельной кнопки
            // под него на экране нет: место занято, а полосы всё равно не нажимаются.
            float sx, sy, sw, sh;
            statsPanelRect(sx, sy, sw, sh);
            if(tx >= sx && tx <= sx + sw && ty >= sy && ty <= sy + sh){
                overlay_ = Overlay::Pause;
                continue;
            }
        }
        if(down && buildMode() && handleBuildTouch(tx, ty)) continue;
        // Кнопка-рука над выброшенным предметом: она появляется только когда рядом
        // что-то лежит, поэтому проверяется до кнопок управления.
        if(down && overlay_ == Overlay::None && handlePickupTouch(tx, ty)) continue;
        if(down && handleHotbarTouch(tx, ty)) continue;
        controls_.handleEvent(e);
    }
}

void GameClient::update(float dt){
    animTime_ += dt;

    // В главном меню мир живёт фоном: время идёт, чанки достраиваются, камера медленно
    // поворачивается — получается «живая» заставка вместо картинки.
    if(state_ == GameState::MainMenu){
        yaw_ += dt * 0.06f;
        env_->tick(dt);
        chunks_.update(player_->eyePosition(), viewDistanceMeters(), 2);
        controls_.endFrame();
        return;
    }

    float sens = 0.0045f * settings.lookSensitivity * (720.0f / (float)SCR_H) * 2.0f;
    yaw_   -= controls_.lookDX * sens;
    pitch_ -= controls_.lookDY * sens;
    pitch_ = clampf(pitch_, -1.50f, 1.50f);

    if(controls_.inventoryPressed()){
        // Кнопка инвентаря закрывает окно ВСЕГДА, даже если открыто окошко предмета
        // или палец что-то тащил: раньше эти состояния перехватывали нажатие.
        overlay_ = (overlay_ == Overlay::Inventory) ? Overlay::None : Overlay::Inventory;
        dragSlot_ = -1; dragActive_ = false; itemMenuSlot_ = -1;
    }
    // Открывая окно, отпускаем все касания: палец, лежавший на джойстике, иначе
    // остаётся «нажатым» и игрок продолжает идти, пока окно открыто.
    if(overlay_ != Overlay::None) controls_.releaseAllTouches();
    if(controls_.craftPressed())     overlay_ = (overlay_ == Overlay::Craft)     ? Overlay::None : Overlay::Craft;
    if(controls_.mapPressed())       overlay_ = (overlay_ == Overlay::Map)       ? Overlay::None : Overlay::Map;
    // Настройки в игре не открываются: их кнопки на экране нет, вход только из меню.
    (void)controls_.settingsPressed();

    SurvivorInput in;
    // В режиме расстановки кнопок игра стоит: иначе игрок, двигая кнопку «копать»,
    // копал бы ей же.
    if(overlay_ == Overlay::None && !controls_.editMode()){
        in.moveX = controls_.moveX();
        in.moveY = controls_.moveY();
        in.sprint = controls_.sprint();
        in.crouch = controls_.crouch();
        in.jump = controls_.jumpPressed();
        // Зажатая кнопка бьёт раз за разом, но не быстрее, чем идёт анимация замаха:
        // сам ритм ударов задаёт Survivor, а не частота кадров. Отдельно читаем
        // «нажали»: короткий тап короче кадра иначе потерялся бы.
        bool attack = controls_.attackHeld() || controls_.attackPressed();
        // С гранатой в руке та же кнопка не машет, а бросает — и ровно один раз.
        const ItemStack& held = inventory_.selectedStack();
        if(!held.empty() && held.type == ItemType::Grenade){
            if(controls_.attackPressed()) throwGrenade();
            attack = false;
        }
        in.attack = attack;
        in.place = controls_.placePressed();
        // «Рука» сначала пробует открыть или закрыть дверь: сама дверь — часть дома,
        // и мир о ней ничего не знает, её состояние держит реестр построек.
        bool wantAction = controls_.actionPressed();
        if(wantAction && toggleDoorNear()) wantAction = false;
        in.action = wantAction;
    }
    in.yaw = yaw_;
    in.pitch = pitch_;

    player_->update(in, dt);
    env_->tick(dt);
    // Растекание воды: небольшими порциями за тик, чтобы залив ямы был виден, но не
    // стоил кадра. Пока очередь пуста, вызов бесплатен.
    voxels_->updateWater(96);
    // Восстановление выбитых жил: пока очередь пуста, вызов ничего не стоит.
    voxels_->updateRespawn(dt);
    updateFurnace(dt);
    updateUpkeep();
    // Падение срубленного дерева и осколки от выработанной жилы.
    voxels_->updateFalling(dt);
    updateFallenTrees(dt);
    updateParticles(dt);
    updateDrops(dt);
    // Сеть: отдать своё состояние, применить чужие правки, сгладить чужие фигурки.
    netPumpState();
    netApplyEdits();
    netApplyEvents();
    updateRemotePlayers(dt);
    updateGrenades(dt);

    static float stepPhase = 0.0f;
    if(player_->onGround() && player_->speed() > 0.5f){
        stepPhase += dt * player_->speed();
        if(stepPhase > 2.2f){ stepPhase = 0.0f; audioPlayStep(); }
    }

    // Бюджет постройки чанков: при беге разрешаем больше, стоя на месте — меньше;
    // так мир успевает за игроком, но не тратит время впустую.
    float viewDist = viewDistanceMeters();
    int budget = (player_->speed() > 3.0f) ? 3 : 2;
    chunks_.update(player_->eyePosition(), viewDist, budget);
    controls_.endFrame();
}

// ==================== ОТРИСОВКА СЦЕНЫ ====================


// Привязывает массив текстур блоков к шейдеру. Вынесено в функцию, потому что делать
// это надо перед каждым проходом (непрозрачные блоки, вода), а забыть — легко: мир
// тогда рисуется прошлой текстурой или вовсе чёрным.
void GameClient::bindBlockTextures(){
    if(blockTexturesReady()){
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextureArray());
        glUniform1i(voxelBlocksLoc, 0);
        glUniform1f(voxelTexturedLoc, 1.0f);
    } else {
        glUniform1f(voxelTexturedLoc, 0.0f);
    }
}

void GameClient::renderScene(){
    float renderScale = qualityRenderScale();
    int renderW = (int)((float)SCR_W * renderScale);
    int renderH = (int)((float)SCR_H * renderScale);
    if(renderW < 320) renderW = 320;
    if(renderH < 180) renderH = 180;

    ensureSceneFBO(renderW, renderH);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glViewport(0, 0, renderW, renderH);

    float light = clampf(env_->lightLevel() * 1.45f + 0.02f, 0.07f, 1.15f);
    Vec3 fog{ 0.52f * light + 0.03f, 0.60f * light + 0.04f, 0.72f * light + 0.06f };
    switch(env_->weather()){
        case Weather::Fog:   fog = Vec3{ 0.62f*light+0.10f, 0.63f*light+0.10f, 0.64f*light+0.10f }; break;
        case Weather::Storm: fog = Vec3{ 0.28f*light+0.02f, 0.30f*light+0.02f, 0.34f*light+0.03f }; break;
        case Weather::Snow:  fog = Vec3{ 0.72f*light+0.08f, 0.75f*light+0.08f, 0.80f*light+0.09f }; break;
        default: break;
    }
    glClearColor(fog.x, fog.y, fog.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Vec3 eye = player_->eyePosition();
    // В меню камера приподнята и смотрит чуть вниз: с уровня глаз видны одни стволы,
    // а с высоты — пейзаж, ради которого заставка и нужна.
    Vec3 forward = player_->lookDirection();
    if(state_ == GameState::MainMenu){
        eye.y += 9.0f;
        forward = v3norm(Vec3{ -sinf(yaw_) * 0.94f, -0.34f, -cosf(yaw_) * 0.94f });
    }
    Mat4 view = mat4LookAt(eye, v3add(eye, forward), Vec3{0,1,0});
    float aspect = (float)renderW / (float)renderH;
    float viewDist = viewDistanceMeters();
    const float fovRad = 70.0f * 3.14159265f / 180.0f;
    Mat4 proj = mat4Perspective(fovRad, aspect, 0.1f, viewDist * 2.2f);

    // ---- Небо: полноэкранный треугольник, направление луча считает сам шейдер.
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProg);
    Vec3 camRight = v3norm(v3cross(forward, Vec3{0,1,0}));
    Vec3 camUp = v3cross(camRight, forward);
    glUniform3f(skyCamRightLoc, camRight.x, camRight.y, camRight.z);
    glUniform3f(skyCamUpLoc, camUp.x, camUp.y, camUp.z);
    glUniform3f(skyCamForwardLoc, forward.x, forward.y, forward.z);
    glUniform1f(skyTanHalfFovLoc, tanf(fovRad * 0.5f));
    glUniform1f(skyAspectLoc, aspect);
    glUniform1f(skyTimeLoc, animTime_);
    float sunAngle = (env_->timeOfDay() - 6.0f) / 12.0f * 3.14159265f;
    Vec3 sunDir = v3norm(Vec3{ cosf(sunAngle), sinf(env_->sunAltitude()), 0.30f });
    glUniform3f(skySunDirLoc, sunDir.x, sunDir.y, sunDir.z);
    glUniform1f(skyLightAmountLoc, clampf(light, 0.0f, 1.0f));
    glUniform3f(skyFogColorLoc, fog.x, fog.y, fog.z);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // ---- Кубический мир: непрозрачные блоки.
    // Смешивание для них ВЫКЛЮЧЕНО, а отсечение задних граней включено. Именно на этом
    // ловится «мир видно насквозь»: с включённым смешиванием и незакрытой глубиной
    // дальние грани просвечивают сквозь ближние.
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    Vec3 lightDir = sunDir;
    if(lightDir.y < 0.25f) lightDir.y = 0.25f;  // ночью светит луна с той же стороны
    lightDir = v3norm(lightDir);

    float fogDensity = 0.0085f;
    if(env_->weather() == Weather::Fog)   fogDensity = 0.030f * (0.5f + env_->weatherIntensity());
    if(env_->weather() == Weather::Rain)  fogDensity = 0.014f;
    if(env_->weather() == Weather::Storm) fogDensity = 0.020f;
    if(env_->weather() == Weather::Snow)  fogDensity = 0.018f;

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform3f(voxelLightDirLoc, lightDir.x, lightDir.y, lightDir.z);
    glUniform1f(voxelLightAmountLoc, light);
    glUniform3f(voxelFogColorLoc, fog.x, fog.y, fog.z);
    glUniform1f(voxelFogDensityLoc, fogDensity);
    glUniform3f(voxelCamPosLoc, eye.x, eye.y, eye.z);
    glUniform1f(voxelAlphaLoc, 1.0f);
    bindBlockTextures();
    chunks_.renderOpaque(view, proj, eye, viewDist);

    // Рамки блока под прицелом нет: она подсвечивала каждый куб рельефа, по которому
    // всё равно нельзя ударить, и только мешала смотреть.

    // ---- Осколки и предмет в руке. Оба прохода до воды и БЕЗ очистки глубины.
    renderParticles(view, proj);
    renderFallenTrees(view, proj);
    renderGrenades(view, proj);
    renderRemotePlayers(view, proj);
    if(state_ == GameState::Playing) renderDrops(view, proj);
    if(buildMode()) renderBuildGhost(view, proj);
    if(state_ == GameState::Playing)
        renderHeldItem(view, proj, eye, forward, 1.0f / 60.0f);

    // ---- Вода: полупрозрачная, БЕЗ записи глубины и вторым проходом — иначе она
    // закрывает собой дно, которое сквозь неё должно быть видно.
    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform3f(voxelLightDirLoc, lightDir.x, lightDir.y, lightDir.z);
    glUniform1f(voxelLightAmountLoc, light);
    glUniform3f(voxelFogColorLoc, fog.x, fog.y, fog.z);
    glUniform1f(voxelFogDensityLoc, fogDensity);
    glUniform3f(voxelCamPosLoc, eye.x, eye.y, eye.z);
    // Вода прозрачнее, чем была: сквозь неё должно быть видно дно и блоки под водой.
    glUniform1f(voxelAlphaLoc, 0.58f);
    bindBlockTextures();
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);   // воду видно и снизу, из-под поверхности
    chunks_.renderWater(view, proj, eye, viewDist);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    // ---- Перенос сцены на экран
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCR_W, SCR_H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    Mat4 blitProj = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
    if(postProgOk_){
        glUseProgram(postProg);
        glUniformMatrix4fv(postProjLoc, 1, GL_FALSE, blitProj.m);
        glUniform1f(postTimeLoc, animTime_);
        glUniform2f(postResLoc, (float)SCR_W, (float)SCR_H);
    } else {
        glUseProgram(uiProg);
        glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, blitProj.m);
        glUniform4f(uiColorLoc, 1,1,1,1);
        glUniform1i(uiUseTextureLoc, 1);
    }
    float verts[6][4] = {
        {0, 0, 0,1}, {(float)SCR_W, 0, 1,1}, {(float)SCR_W, (float)SCR_H, 1,0},
        {0, 0, 0,1}, {(float)SCR_W, (float)SCR_H, 1,0}, {0, (float)SCR_H, 0,0},
    };
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glUniform1i(postProgOk_ ? postTexLoc : uiTexLoc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
}

// ==================== ИНТЕРФЕЙС ====================

float GameClient::textWidth(float height, const std::string& text){
    if(!uiFont || text.empty()) return 0.0f;
    TextTexCache& cache = textCache_[text];
    SDL_Color color{ 255, 255, 255, 255 };
    updateTextTexture(cache, text, color);
    if(!cache.tex || cache.h <= 0) return 0.0f;
    return height * (float)cache.w / (float)cache.h;
}

void GameClient::drawTextCentered(float cx, float y, float height, const std::string& text,
                                  float r, float g, float b, float a){
    drawText(cx - textWidth(height, text) * 0.5f, y, height, text, r, g, b, a);
}

void GameClient::drawText(float x, float y, float height, const std::string& text,
                          float r, float g, float b, float a){
    if(!uiFont || text.empty()) return;
    TextTexCache& cache = textCache_[text];
    SDL_Color color{ (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255), 255 };
    updateTextTexture(cache, text, color);
    if(!cache.tex || cache.h <= 0) return;
    float w = height * (float)cache.w / (float)cache.h;
    drawUIRect(x, y, w, height, cache.tex, 1, 1, 1, a, true);
}

void GameClient::drawBar(float x, float y, float w, float h, float value01,
                         float r, float g, float b, const std::string& caption){
    drawUIRect(x, y, w, h, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.75f, false);
    float fill = clampf(value01, 0.0f, 1.0f) * (w - 4.0f);
    drawUIRect(x + 2.0f, y + 2.0f, fill, h - 4.0f, 0, r, g, b, 0.92f, false);
    uiThinFrame(x, y, w, h, UI_LINE, 0.8f);
    if(!caption.empty()) drawText(x + 6.0f, y + h * 0.12f, h * 0.76f, caption, UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.95f);
}

GLuint GameClient::itemIcon(ItemType t) const {
    int i = (int)t;
    if(i <= 0 || i >= (int)ItemType::COUNT) return 0;
    return texItems_[i];
}

void GameClient::drawSlot(float x, float y, float size, const ItemStack& stack, bool selected){
    // Ячейка светлая и прозрачная: сквозь неё виден мир, как в телефонных выживалках.
    // Тёмный непрозрачный квадрат превращал интерфейс в глухую панель.
    drawUIRect(x, y, size, size, 0, 0.78f, 0.77f, 0.73f, 0.34f, false);
    if(!stack.empty()){
        const ItemDef& def = itemDef(stack.type);
        GLuint icon = itemIcon(stack.type);
        if(icon){
            float ip = size * 0.08f;
            drawUIRect(x + ip, y + ip, size - ip * 2.0f, size - ip * 2.0f, icon, 1, 1, 1, 1.0f, true);
        } else {
            float pad = size * 0.18f;
            drawUIRect(x + pad, y + pad, size - pad * 2.0f, size - pad * 2.0f, 0,
                       def.r, def.g, def.b, 1.0f, false);
        }
        if(stack.count > 1){
            char buf[16];
            snprintf(buf, sizeof(buf), "x%d", stack.count);
            float fh = size * 0.26f;
            // Число прижато к правому нижнему углу: слева от него — сам предмет.
            float tw = fh * 0.55f * (float)strlen(buf);
            drawText(x + size - tw - size * 0.06f, y + size - fh * 1.15f, fh, buf, 1, 1, 1, 0.95f);
        }
    }
    // Выбранная ячейка — сплошная светлая рамка по контуру, а не полоса сбоку.
    if(selected){
        float t = fmaxf(2.0f, size * 0.045f);
        drawUIRect(x, y, size, t, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
        drawUIRect(x, y + size - t, size, t, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
        drawUIRect(x, y, t, size, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
        drawUIRect(x + size - t, y, t, size, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
    } else {
        uiThinFrame(x, y, size, size, UIColor{1.0f, 1.0f, 1.0f}, 0.30f);
    }
}

// Пояс рисуется отдельным проходом ПОСЛЕ окон: он виден всегда, в том числе поверх
// инвентаря, и не должен уходить под их затемнение.
void GameClient::renderHotbar(){
    // На карте и в паузе пояса нет: карта занимает весь экран, и полоса предметов
    // поверх неё только мешает.
    if(state_ != GameState::Playing) return;
    if(overlay_ == Overlay::Pause || overlay_ == Overlay::Map) return;
    glDisable(GL_DEPTH_TEST);
    Mat4 uiProjM = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
    glUseProgram(uiProg);
    glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, uiProjM.m);

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float hx, hy, slot, hgap;
    hotbarGeometry(hx, hy, slot, hgap);
    for(int i = 0; i < Inventory::HOTBAR; ++i){
        float sx = hx + i * (slot + hgap);
        ItemStack shown = (dragActive_ && i == dragSlot_) ? ItemStack{} : inventory_.slot(i);
        drawSlot(sx, hy, slot, shown, i == inventory_.selected());
    }
    const ItemStack& sel = inventory_.selectedStack();
    if(!sel.empty())
        drawText(hx, hy - 24.0f * s, 20.0f * s, itemDef(sel.type).nameRu,
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.9f);
}

void GameClient::renderHud(){
    // В паузе интерфейса игры на экране нет вообще: пауза — это отдельный экран, а не
    // окно поверх боя, и полосы с кнопками за ним только мешают читать меню.
    if(overlay_ == Overlay::Pause) return;

    glDisable(GL_DEPTH_TEST);
    Mat4 uiProjM = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
    glUseProgram(uiProg);
    glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, uiProjM.m);

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pad = 14.0f * s;
    float barW = 200.0f * s, barH = 21.0f * s, gap = 5.0f * s;
    char buf[128];

    // ---- Полосы состояния
    float y = pad;
    // Название слева, число — отдельным полем у правого края полосы, поэтому цифры всех
    // строк стоят на одной вертикали, а не разъезжаются вслед за длиной слова.
    auto statRow = [&](const char* name, float value, float r, float g, float b){
        drawBar(pad, y, barW, barH, value / 100.0f, r, g, b, name);
        char num[16];
        snprintf(num, sizeof(num), "%.0f", (double)value);
        // Число стоит по центру ПОСТОЯННОГО поля у правого края полосы: так «100», «10»
        // и «1» рисуются вокруг одной середины, а не съезжают влево вслед за длиной.
        float fieldW = 54.0f * s;
        float fieldX = pad + barW - fieldW - 6.0f * s;
        drawTextCentered(fieldX + fieldW * 0.5f, y + barH * 0.16f, barH * 0.82f, num,
                         1, 1, 1, 0.95f);
        y += barH + gap;
    };
    statRow("Здоровье", player_->health(),  0.62f, 0.18f, 0.16f);
    statRow("Голод",  player_->hunger(),  0.55f, 0.40f, 0.14f);
    statRow("Жажда",  player_->thirst(),  0.16f, 0.38f, 0.58f);
    statRow("Силы",   player_->stamina(), 0.32f, 0.48f, 0.24f);
    // Воздух показываем только когда он тратится: лишняя полоса на экране мешает.
    if(player_->headUnderwater() || player_->oxygen() < 99.5f){
        statRow("Воздух", player_->oxygen(), 0.30f, 0.62f, 0.72f);
    }

    Vec3 p = player_->position();
    // Строки «День 1, 08:27 | Ясно | Равнина» больше нет: время видно по небу, погоду —
    // по погоде, а биом — по тому, что вокруг.

    // ---- Счётчик кадров в правом верхнем углу. Вертикальная синхронизация выключена,
    // поэтому число показывает НАСТОЯЩУЮ скорость отрисовки, а не частоту экрана.
    const VoxelRenderStats& st = chunks_.stats();
    snprintf(buf, sizeof(buf), "%.0f FPS", (double)fps_);
    drawText((float)SCR_W - 92.0f * s, pad, 26.0f * s, buf,
             fps_ >= 50.0f ? 0.55f : 0.85f, fps_ >= 50.0f ? 0.85f : 0.55f, 0.45f, 0.95f);
    // Счётчик чанков — только в отладке: иначе строка налезает на ряд иконок справа.
    if(settings.showDebugInfo){
        snprintf(buf, sizeof(buf), "чанков %d/%d  граней %d", st.chunksDrawn, st.chunksLoaded,
                 st.trianglesDrawn / 2);
        drawText((float)SCR_W - 240.0f * s, pad + 28.0f * s, 16.0f * s, buf,
                 UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.75f);
    }

    // ---- Прицел. Пока открыто любое окно (крафт, инвентарь, печь, карта), его нет:
    // целиться некуда, а перекрестье посреди интерфейса только мешает.
    float cx = (float)SCR_W * 0.5f, cy = (float)SCR_H * 0.5f;
    if(overlay_ == Overlay::None && !player_->isDead()){
        drawUIRect(cx - 1.5f, cy - 9.0f, 3.0f, 18.0f, 0, 1,1,1, 0.55f, false);
        drawUIRect(cx - 9.0f, cy - 1.5f, 18.0f, 3.0f, 0, 1,1,1, 0.55f, false);
    }
    // Полосы добычи больше нет: удар стал дискретным, копить прогресс нечему.

    // ---- Метка попадания: живёт треть секунды и за это время расходится и гаснет.
    hitMarkAge_ += 1.0f / 60.0f;
    if(overlay_ == Overlay::None && hitMarkAge_ < 0.35f && texHitMark_){
        float k = clampf(hitMarkAge_ / 0.35f, 0.0f, 1.0f);
        float size = (44.0f + 26.0f * k) * s;
        drawUIRect(cx - size * 0.5f, cy - size * 0.5f, size, size, texHitMark_,
                   1, 1, 1, (1.0f - k) * 0.95f, true);
    }

    // ---- Подсказка у двери: её открывают и закрывают кнопкой «рука».
    if(overlay_ == Overlay::None){
        const RayHit& t = player_->target();
        bool atDoor = t.hit && isDoorBlock(t.block);
        bool nearOpen = false;
        if(!atDoor){
            Vec3 pp = player_->position();
            for(const BuildPiece& p : pieces_){
                if(!p.open || !isDoorBlock(p.block)) continue;
                float dx = (float)p.x + 0.5f - pp.x, dz = (float)p.z + 0.5f - pp.z;
                if(dx * dx + dz * dz < 12.0f){ nearOpen = true; break; }
            }
        }
        if(atDoor || nearOpen){
            float isz = 54.0f * s;
            float ix = cx - isz * 0.5f, iy = cy + 30.0f * s;
            if(texOpen_) drawUIRect(ix, iy, isz, isz, texOpen_, 1, 1, 1, 0.95f, true);
            drawTextCentered(cx, iy + isz + 2.0f * s, 17.0f * s,
                             atDoor ? "ОТКРЫТЬ" : "ЗАКРЫТЬ", 1, 1, 1, 0.9f);
        }
    }

    // ---- Радиация: значок с числом появляется, только когда она есть.
    if(player_->radiation() > 0.5f){
        float isz = 34.0f * s;
        float ix = pad, iy = y + 6.0f * s;
        if(texRadiation_) drawUIRect(ix, iy, isz, isz, texRadiation_, 1, 1, 1, 0.95f, true);
        snprintf(buf, sizeof(buf), "%.0f", (double)player_->radiation());
        drawText(ix + isz + 6.0f * s, iy + isz * 0.2f, 20.0f * s, buf, 0.85f, 0.95f, 0.45f, 0.95f);
    }

    // ---- Прочность детали дома под прицелом (только у побитой).
    renderBuildTargetInfo();

    // ---- Имена других игроков.
    renderRemoteLabels();

    // ---- Выброшенные предметы: подписи над кубами и кнопка «поднять».
    renderDropLabels();

    // ---- Последнее событие
    if(player_->messageAge() < 4.0f){
        float alpha = clampf(1.0f - (player_->messageAge() - 3.0f), 0.0f, 1.0f);
        drawText(pad, (float)SCR_H - 120.0f * s, 21.0f * s, player_->lastMessage(),
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, alpha);
    }

    // ---- Индикатор урона: кровь по краям экрана, гаснет за полторы секунды.
    float dmgAge = player_->damageAge();
    if(dmgAge < 1.5f && !player_->isDead()){
        float a = clampf(1.0f - dmgAge / 1.5f, 0.0f, 1.0f) * 0.75f;
        if(texBlood_) drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, texBlood_, 1, 1, 1, a, true);
        else          drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.55f, 0.03f, 0.03f, a * 0.5f, false);
    }

    // ---- Экран смерти: та же кровь во всю силу и отсчёт до возрождения.
    if(player_->isDead()){
        if(texBlood_) drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, texBlood_, 1, 1, 1, 0.95f, true);
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.22f, 0.01f, 0.01f, 0.45f, false);
        drawText(cx - 130.0f * s, cy - 46.0f * s, 46.0f * s, "ВЫ ПОГИБЛИ", 0.9f, 0.25f, 0.22f, 1.0f);
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "Возрождение через %d с",
                 (int)ceilf(fmaxf(player_->respawnLeft(), 0.0f)));
        float tw = 22.0f * s * 0.5f * (float)strlen(dbuf);
        drawText(cx - tw * 0.5f, cy + 24.0f * s, 22.0f * s, dbuf, 1, 1, 1, 0.95f);
    }

    if(settings.showDebugInfo){
        snprintf(buf, sizeof(buf), "XZ %.0f,%.0f Y %.1f | правок мира %zu | слотов занято %d",
                 (double)p.x, (double)p.z, (double)p.y, voxels_->editCount(), inventory_.usedSlots());
        drawText(pad, (float)SCR_H - 26.0f * s, 16.0f * s, buf,
                 UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
    }

    renderCompass();
    // Кнопки управления не нужны, пока открыто окно: они просвечивают сквозь него и
    // выглядят как часть окна.
    if(overlay_ == Overlay::None) renderTouchControls();
    // Лента стройки живёт только с планом в руках.
    if(buildMode()) renderBuildBar();
}

// ==================== СЕНСОРНОЕ УПРАВЛЕНИЕ: ИКОНКИ ====================
// Подписи на кнопках заменены иконками: слово «СТАВИТЬ» на круге в 11 мм читается плохо,
// а знак понятен без чтения и не зависит от языка. Если иконки не нашлось (сборка без
// ассетов), кнопка рисуется прежним кругом с текстом — игра обязана запускаться всегда.
void GameClient::renderTouchControls(){
    TouchControls::StickView stick = controls_.stickView();
    if(stick.active){
        float r = stick.radius;
        if(texJoyBase_){
            drawUIRect(stick.baseX - r, stick.baseY - r, r * 2.0f, r * 2.0f, texJoyBase_, 1, 1, 1, 0.55f, true);
        } else {
            drawUICircleOutline(stick.baseX, stick.baseY, r, UI_LINE.r, UI_LINE.g, UI_LINE.b, 0.35f, 3.0f);
        }
        float dx = stick.curX - stick.baseX, dy = stick.curY - stick.baseY;
        float len = sqrtf(dx*dx + dy*dy);
        if(len > r){ dx *= r / len; dy *= r / len; }
        float knob = r * 0.44f;
        if(texJoyStick_){
            drawUIRect(stick.baseX + dx - knob, stick.baseY + dy - knob, knob * 2.0f, knob * 2.0f,
                       texJoyStick_, 1, 1, 1, 0.85f, true);
        } else {
            drawUICircle(stick.baseX + dx, stick.baseY + dy, knob, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.45f);
        }
    }

    for(const TouchControls::ButtonView& b : controls_.buttonViews()){
        std::string label = b.label;
        GLuint icon = 0;
        if(label == "КОПАТЬ")        icon = texDig_;
        else if(label == "СТАВИТЬ")  icon = texPlace_;
        else if(label == "E")        icon = texInteract_;
        else if(label == "ИНВ")      icon = texInventory_;
        else if(label == "КРАФТ")    icon = texCraft_;
        else if(label == "КАРТА")    icon = texMap_;
        else if(label == "НАСТР")    icon = texSettings_;
        else if(label == "ПРЫЖОК")   icon = texJump_;
        else if(label == "БЕГ")      icon = texRun_;
        else if(label == "СЕСТЬ")    icon = texCrouch_;

        // Кнопка белая в покое и темнеет при нажатии: так видно, что палец попал.
        float alpha = b.active ? 0.45f : 1.0f;
        if(icon){
            drawUIRect(b.cx - b.radius, b.cy - b.radius, b.radius * 2.0f, b.radius * 2.0f,
                       icon, 1, 1, 1, alpha, true);
            continue;
        }

        // Запасной вариант, если картинки кнопки нет: знак рисуется примитивами —
        // стрелка вверх (прыжок), стрелка вниз (присед), «бегущие» полоски (бег).
        drawUICircle(b.cx, b.cy, b.radius, UI_BG_PANEL.r, UI_BG_PANEL.g, UI_BG_PANEL.b, b.active ? 0.5f : 0.3f);
        drawUICircleOutline(b.cx, b.cy, b.radius, 1, 1, 1, alpha * 0.8f, 2.5f);
        float k = b.radius * 0.5f;
        auto arrow = [&](float dirY){
            // «Ёлочка» из трёх полосок: вершина и два крыла.
            drawUIRect(b.cx - 2.0f, b.cy - k * dirY, 4.0f, k * 2.0f * (dirY > 0 ? 1.0f : 1.0f), 0, 1,1,1, alpha, false);
            for(int i = 0; i < 5; ++i){
                float t = (float)i / 4.0f;
                float w = k * 0.9f * (1.0f - t);
                float yy = b.cy - k * dirY + t * k * 0.8f * dirY;
                drawUIRect(b.cx - w, yy, w * 2.0f, 3.0f, 0, 1,1,1, alpha, false);
            }
        };
        if(label == "ПРЫЖОК") arrow(1.0f);
        else if(label == "СЕСТЬ") arrow(-1.0f);
        else if(label == "БЕГ"){
            for(int i = 0; i < 2; ++i){
                float ox = b.cx - k * 0.5f + i * k * 0.7f;
                for(int j = 0; j < 5; ++j){
                    float t = (float)j / 4.0f;
                    float h = k * (1.0f - fabsf(t - 0.5f) * 2.0f) * 0.9f + 3.0f;
                    drawUIRect(ox + t * k * 0.5f, b.cy - h * 0.5f, 3.0f, h, 0, 1,1,1, alpha, false);
                }
            }
        }
    }
}

void GameClient::renderOverlay(){
    // Подсказка редактора раскладки поверх игры.
    if(controls_.editMode()){
        float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        float w = (float)SCR_W * 0.5f, h = 92.0f * s;
        float px = ((float)SCR_W - w) * 0.5f, py = 24.0f * s;
        uiPanel(px, py, w, h, 0.9f);
        drawText(px + 16.0f * s, py + 12.0f * s, 22.0f * s,
                 "Перетащите кнопки пальцем под свою руку", UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
        drawUIRect(px + 16.0f * s, py + 48.0f * s, 150.0f * s, 32.0f * s, 0,
                   UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.9f, false);
        drawText(px + 52.0f * s, py + 54.0f * s, 22.0f * s, "ГОТОВО",
                 UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
        // Вторая кнопка: кладёт координаты всех кнопок в буфер обмена одной строкой —
        // её можно вставить в переписку и попросить сделать такую раскладку стандартной.
        drawUIRect(px + 186.0f * s, py + 48.0f * s, 260.0f * s, 32.0f * s, 0,
                   UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.9f, false);
        drawText(px + 200.0f * s, py + 54.0f * s, 20.0f * s, "СКОПИРОВАТЬ КООРДИНАТЫ",
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
        return;
    }
    if(overlay_ == Overlay::None) return;
    if(overlay_ == Overlay::Pause){ renderPause(); return; }
    if(overlay_ == Overlay::Furnace){ renderFurnace(); return; }
    if(overlay_ == Overlay::Box){ renderBox(); return; }
    if(overlay_ == Overlay::Cupboard){ renderCupboard(); return; }
    if(overlay_ == Overlay::Settings){ renderSettings(); return; }
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);

    if(overlay_ == Overlay::Inventory){
        float gx, gy, slot, gap;
        inventoryGeometry(gx, gy, slot, gap);
        float w = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
        float mainH = slot * (Inventory::ROWS - 1) + gap * (Inventory::ROWS - 2);
        // Экран не затемняется в глухую: за интерфейсом остаётся видна игра.
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.05f, 0.06f, 0.30f, false);
        drawText(gx, gy - 46.0f * s, 30.0f * s, "ИНВЕНТАРЬ", 1, 1, 1, 0.96f);

        float closeX, closeY, closeW, closeH;
        inventoryCloseRect(closeX, closeY, closeW, closeH);
        if(texClose_) drawUIRect(closeX, closeY, closeW, closeH, texClose_, 1, 1, 1, 0.9f, true);
        else {
            drawUIRect(closeX, closeY, closeW, closeH, 0, 0.20f, 0.10f, 0.10f, 0.9f, false);
            drawTextCentered(closeX + closeW * 0.5f, closeY + closeH * 0.25f, 26.0f * s, "X", 1, 1, 1, 0.95f);
        }

        drawUIRect(gx - 4.0f * s, gy - 4.0f * s, w + 8.0f * s, mainH + 8.0f * s, 0,
                   0.85f, 0.83f, 0.79f, 0.20f, false);

        // Только рюкзак: пояс живёт внизу экрана и рисуется отдельным проходом.
        for(int i = Inventory::HOTBAR; i < Inventory::SIZE; ++i){
            float sx, sy;
            inventorySlotPos(i, sx, sy);
            ItemStack shown = (dragActive_ && i == dragSlot_) ? ItemStack{} : inventory_.slot(i);
            drawSlot(sx, sy, slot, shown, false);
        }
        if(dragActive_ && dragSlot_ >= 0){
            // Под пальцем едет ТОЛЬКО значок предмета, без плитки и рамки: подложка
            // выглядела как лишняя ячейка. Значок стоит по центру пальца, а не выше.
            const ItemStack& drag = inventory_.slot(dragSlot_);
            if(!drag.empty()){
                float dsz = slot * 0.80f;
                GLuint icon = itemIcon(drag.type);
                if(icon)
                    drawUIRect(dragPos_.x - dsz * 0.5f, dragPos_.y - dsz * 0.5f, dsz, dsz,
                               icon, 1, 1, 1, 0.95f, true);
                else {
                    const ItemDef& dd = itemDef(drag.type);
                    drawUIRect(dragPos_.x - dsz * 0.5f, dragPos_.y - dsz * 0.5f, dsz, dsz,
                               0, dd.r, dd.g, dd.b, 0.95f, false);
                }
            }
        }
        renderItemMenu();
        return;
    }

    if(overlay_ == Overlay::Craft){ renderCraft(); return; }
    renderMap();
}

// ==================== КРАФТ ====================
// Список прокручивается пальцем — как в Rust: рецептов со временем станет несколько
// десятков, и они не влезут ни в один экран телефона.

// Разметка панели описания. Раньше значок предмета и текст стояли на фиксированных
// отступах, а таблица стоимости — на своём: при длинном названии они наезжали друг на
// друга. Теперь каждый следующий блок начинается там, где кончился предыдущий.
void GameClient::craftPanelGeometry(float& dx, float& dy, float& dw, float& dh,
                                    float& notesY, float& tableY) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    float gridW = tile * CRAFT_COLS + gap * (CRAFT_COLS - 1);
    dx = gx + gridW + 28.0f * s;
    dy = gy;
    dw = craftPanelWidth();

    int sel = craftSelected_;
    if(sel < 0) sel = 0;
    if(sel >= kRecipeCount) sel = kRecipeCount - 1;
    const Recipe& r = kRecipes[sel];

    // Значок предмета и шапка стали компактнее, а таблица стоимости поднялась к ним:
    // после прошлой правки она уехала слишком низко и панель выглядела полупустой.
    float bigIcon = tile * 0.50f;
    float headerH = fmaxf(bigIcon, 40.0f * s) + 8.0f * s;
    notesY = dy + 12.0f * s + headerH;

    float noteH = 18.0f * s;
    size_t lineCount = wrapText(r.note, noteH, dw - 36.0f * s).size();
    tableY = notesY + (float)lineCount * noteH * 1.35f + 12.0f * s;

    int rows = 1 + (r.costB != ItemType::None ? 1 : 0);
    float tableH = 30.0f * s + (float)rows * 28.0f * s;
    dh = (tableY + tableH + 16.0f * s + 54.0f * s + 16.0f * s) - dy;
    float minH = tile * 3.0f;
    if(dh < minH) dh = minH;
}

void GameClient::renderCraft(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Экран крафта: слева квадратные плитки рецептов, справа — описание выбранного.
    // Строчки во всю ширину читались как список настроек, а не как крафт.
    // Затемнения нет: панели крафта и так полупрозрачные, а глухая шторка поверх мира
    // в образце отсутствует.
    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    drawText(gx, gy - 46.0f * s, 30.0f * s, "КРАФТ", 1, 1, 1, 0.96f);

    // Крестик — В САМОМ ВЕРХУ экрана справа, а не у сетки: так он не спорит с панелью.
    // Крестик ниже и левее: наверху справа стоит счётчик кадров, и они перекрывались.
    float closeSize = 54.0f * s;
    float closeX = (float)SCR_W - closeSize - 46.0f * s, closeY = 64.0f * s;
    if(texClose_) drawUIRect(closeX, closeY, closeSize, closeSize, texClose_, 1, 1, 1, 0.9f, true);
    else {
        drawUIRect(closeX, closeY, closeSize, closeSize, 0, 0.20f, 0.10f, 0.10f, 0.9f, false);
        drawText(closeX + closeSize * 0.3f, closeY + closeSize * 0.2f, 26.0f * s, "X", 1, 1, 1, 0.95f);
    }

    if(craftSelected_ >= kRecipeCount) craftSelected_ = kRecipeCount - 1;
    if(craftSelected_ < 0) craftSelected_ = 0;

    char buf[192];
    for(int i = 0; i < kRecipeCount; ++i){
        float tx, ty;
        craftTilePos(i, tx, ty);
        const Recipe& r = kRecipes[i];
        bool ok = inventory_.countOf(r.costA) >= r.costACount &&
                  (r.costB == ItemType::None || inventory_.countOf(r.costB) >= r.costBCount);
        const ItemDef& res = itemDef(r.result);

        drawUIRect(tx, ty, tile, tile, 0, 0.78f, 0.77f, 0.73f, 0.34f, false);
        GLuint icon = itemIcon(r.result);
        float ip = tile * 0.12f;
        if(icon) drawUIRect(tx + ip, ty + ip, tile - ip * 2.0f, tile - ip * 2.0f, icon,
                            1, 1, 1, ok ? 1.0f : 0.35f, true);
        else     drawUIRect(tx + ip, ty + ip, tile - ip * 2.0f, tile - ip * 2.0f, 0,
                            res.r, res.g, res.b, ok ? 1.0f : 0.35f, false);
        if(r.resultCount > 1){
            snprintf(buf, sizeof(buf), "x%d", r.resultCount);
            float fh = tile * 0.22f;
            drawText(tx + tile - fh * 1.5f, ty + tile - fh * 1.2f, fh, buf, 1, 1, 1, 0.95f);
        }
        // Название — полосой ВНУТРИ плитки по нижнему краю: под плиткой соседние
        // подписи налезали друг на друга, потому что плитки стоят вплотную.
        float nameH = tile * 0.135f;
        drawUIRect(tx, ty + tile - nameH * 1.7f, tile, nameH * 1.7f, 0,
                   0.10f, 0.10f, 0.11f, 0.45f, false);
        float tw = nameH * 0.5f * (float)strlen(res.nameRu);
        float nx = tw > tile ? tx + 3.0f * s : tx + tile * 0.5f - tw * 0.5f;
        drawText(nx, ty + tile - nameH * 1.4f, nameH, res.nameRu, 1, 1, 1, ok ? 0.9f : 0.45f);

        // Выбранная плитка обведена, недоступная — приглушена.
        if(i == craftSelected_){
            float t = fmaxf(2.0f, tile * 0.045f);
            drawUIRect(tx, ty, tile, t, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
            drawUIRect(tx, ty + tile - t, tile, t, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
            drawUIRect(tx, ty, t, tile, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
            drawUIRect(tx + tile - t, ty, t, tile, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
        } else {
            uiThinFrame(tx, ty, tile, tile, UIColor{1.0f, 1.0f, 1.0f}, 0.30f);
        }
    }

    // ---- Описание выбранного рецепта справа от сетки.
    const Recipe& r = kRecipes[craftSelected_];
    const ItemDef& res = itemDef(r.result);
    // Панель описания не тянется до края экрана: вытянутая на всю ширину полоса
    // выглядела шапкой сайта, а не окном крафта.
    float dx, dy, dw, dh, notesY, ly;
    craftPanelGeometry(dx, dy, dw, dh, notesY, ly);
    drawUIRect(dx, dy, dw, dh, 0, 0.22f, 0.23f, 0.25f, 0.60f, false);

    // Заголовок описания: название слева, крупный значок предмета справа. Название
    // переносится по словам и не залезает под значок.
    float bigIcon = tile * 0.50f;
    GLuint resIcon = itemIcon(r.result);
    if(resIcon)
        drawUIRect(dx + dw - bigIcon - 16.0f * s, dy + 12.0f * s, bigIcon, bigIcon,
                   resIcon, 1, 1, 1, 1.0f, true);
    snprintf(buf, sizeof(buf), "%s x%d", res.nameRu, r.resultCount);
    {
        float titleH = 25.0f * s;
        float titleW = dw - bigIcon - 44.0f * s;
        std::vector<std::string> tl = wrapText(buf, titleH, titleW);
        float ty2 = dy + 16.0f * s;
        for(const std::string& line : tl){
            drawText(dx + 18.0f * s, ty2, titleH, line.c_str(), 1, 1, 1, 0.96f);
            ty2 += titleH * 1.25f;
        }
    }

    {
        // Описание переносится по словам: в одну строку оно не влезало и уезжало за
        // край панели.
        float noteH = 18.0f * s;
        std::vector<std::string> lines = wrapText(r.note, noteH, dw - 36.0f * s);
        float ny = notesY;
        for(const std::string& line : lines){
            drawText(dx + 18.0f * s, ny, noteH, line.c_str(), 0.90f, 0.90f, 0.88f, 0.95f);
            ny += noteH * 1.35f;
        }
    }

    // Таблица стоимости с шапкой: сколько нужно, чего и сколько есть на руках. Её
    // шапка стоит НИЖЕ описания и значка — раньше они налезали друг на друга.
    drawUIRect(dx + 14.0f * s, ly, dw - 28.0f * s, 26.0f * s, 0,
               0.85f, 0.84f, 0.80f, 0.28f, false);
    drawText(dx + 22.0f * s, ly + 3.0f * s, 16.0f * s, "НУЖНО", 0.15f, 0.15f, 0.16f, 0.95f);
    drawText(dx + 100.0f * s, ly + 3.0f * s, 16.0f * s, "МАТЕРИАЛ", 0.15f, 0.15f, 0.16f, 0.95f);
    drawText(dx + dw - 96.0f * s, ly + 3.0f * s, 16.0f * s, "ЕСТЬ", 0.15f, 0.15f, 0.16f, 0.95f);
    ly += 34.0f * s;
    for(int k = 0; k < 2; ++k){
        ItemType cost = (k == 0) ? r.costA : r.costB;
        int need = (k == 0) ? r.costACount : r.costBCount;
        if(cost == ItemType::None) continue;
        int have = inventory_.countOf(cost);
        bool enough = have >= need;
        snprintf(buf, sizeof(buf), "%d", need);
        drawText(dx + 22.0f * s, ly, 20.0f * s, buf, 1, 1, 1, 0.9f);
        drawText(dx + 100.0f * s, ly, 20.0f * s, itemDef(cost).nameRu, 1, 1, 1, 0.9f);
        snprintf(buf, sizeof(buf), "%d", have);
        // Хватает — зелёным, не хватает — красным: это всё, что нужно знать до крафта.
        drawText(dx + dw - 96.0f * s, ly, 20.0f * s, buf,
                 enough ? 0.55f : 0.90f, enough ? 0.85f : 0.35f, 0.40f, 0.95f);
        ly += 28.0f * s;
    }

    // Кнопка «Создать» — она же и есть действие крафта.
    bool ok = inventory_.countOf(r.costA) >= r.costACount &&
              (r.costB == ItemType::None || inventory_.countOf(r.costB) >= r.costBCount);
    float bx, by, bw, bh;
    craftButtonRect(bx, by, bw, bh);
    drawUIRect(bx, by, bw, bh, 0, ok ? 0.24f : 0.20f, ok ? 0.34f : 0.20f, ok ? 0.24f : 0.21f,
               ok ? 0.72f : 0.55f, false);
    uiThinFrame(bx, by, bw, bh, ok ? UI_ACCENT : UI_LINE, ok ? 0.9f : 0.4f);
    drawTextCentered(bx + bw * 0.5f, by + bh * 0.28f, 23.0f * s, "СОЗДАТЬ",
             ok ? UI_ACCENT.r : UI_TEXT_DIM.r, ok ? UI_ACCENT.g : UI_TEXT_DIM.g,
             ok ? UI_ACCENT.b : UI_TEXT_DIM.b, ok ? 1.0f : 0.7f);
}

// ==================== КАРТА ====================
// Карта во весь экран, с приближением, панорамой, координатной сеткой A1/B2 и стрелкой,
// показывающей, куда игрок смотрит. Ровно то, чем в Rust пользуются для встреч и меток.

void GameClient::mapViewport(float& x, float& y, float& size) const {
    // Карта занимает ВЕСЬ экран: на телефоне это единственный способ разглядеть остров
    // целиком и при этом читать подписи квадратов. Размер здесь — высота окна; ширину
    // берём из экрана, а мир не растягивается (см. renderMap: область по X считается
    // через соотношение сторон).
    x = 0.0f;
    y = 0.0f;
    size = (float)SCR_H;
}

void GameClient::renderMap(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    const WorldConfig& cfg = world_->config();

    // Фон на весь экран, чтобы игра под картой не отвлекала.
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, UI_BG_DEEP.r, UI_BG_DEEP.g, UI_BG_DEEP.b, 0.96f, false);

    Vec3 p = player_->position();
    if(mapFollowsPlayer_){ mapCenterX_ = p.x; mapCenterZ_ = p.z; }

    // Показываемая область: по высоте — вся карта, делённая на масштаб; по ширине —
    // столько же, умноженное на соотношение сторон. Так остров не сплющивается.
    float aspect = (float)SCR_W / (float)SCR_H;
    float spanZ = cfg.size / mapZoom_;
    float spanX = spanZ * aspect;
    // Центр держим так, чтобы карта не уезжала за край мира, но при мелком масштабе,
    // когда мир уже целиком в кадре, центрируем его.
    if(spanX >= cfg.size) mapCenterX_ = cfg.size * 0.5f;
    else mapCenterX_ = clampf(mapCenterX_, spanX * 0.5f, cfg.size - spanX * 0.5f);
    if(spanZ >= cfg.size) mapCenterZ_ = cfg.size * 0.5f;
    else mapCenterZ_ = clampf(mapCenterZ_, spanZ * 0.5f, cfg.size - spanZ * 0.5f);

    float x0 = mapCenterX_ - spanX * 0.5f, z0 = mapCenterZ_ - spanZ * 0.5f;
    drawUIRectUV(0, 0, (float)SCR_W, (float)SCR_H, minimapTex_,
                 x0 / cfg.size, z0 / cfg.size,
                 (x0 + spanX) / cfg.size, (z0 + spanZ) / cfg.size, 1.0f);

    auto toScreenX = [&](float wx){ return (wx - x0) / spanX * (float)SCR_W; };
    auto toScreenY = [&](float wz){ return (wz - z0) / spanZ * (float)SCR_H; };

    // ---- Сетка 10x10 с подписью прямо в квадрате.
    // Сетка живёт только внутри мира: на вытянутом экране телефона мир по ширине занимает
    // не весь кадр, и линии, уходящие в пустоту по краям, выглядели браком.
    const float CELL = cfg.size / 10.0f;
    float worldL = clampf(toScreenX(0.0f), 0.0f, (float)SCR_W);
    float worldR = clampf(toScreenX(cfg.size), 0.0f, (float)SCR_W);
    float worldT = clampf(toScreenY(0.0f), 0.0f, (float)SCR_H);
    float worldB = clampf(toScreenY(cfg.size), 0.0f, (float)SCR_H);
    char label[16];
    for(int c = 0; c <= 10; ++c){
        float lx = toScreenX((float)c * CELL);
        if(lx >= 0.0f && lx <= (float)SCR_W)
            drawUIRect(lx, worldT, 1.0f, worldB - worldT, 0, 1, 1, 1, 0.25f, false);
    }
    for(int r = 0; r <= 10; ++r){
        float ly = toScreenY((float)r * CELL);
        if(ly >= 0.0f && ly <= (float)SCR_H)
            drawUIRect(worldL, ly, worldR - worldL, 1.0f, 0, 1, 1, 1, 0.25f, false);
    }
    // Подписей A1/B2 в клетках больше нет: они рябили поверх карты. Вместо них — номера
    // рядов справа и столбцов сверху, как на бумажной карте.
    float cellW = CELL / spanX * (float)SCR_W;
    float cellH = CELL / spanZ * (float)SCR_H;
    float fontH = clampf(fminf(cellW, cellH) * 0.20f, 14.0f * s, 34.0f * s);
    for(int r = 0; r < 10; ++r){
        float ly = toScreenY(((float)r + 0.5f) * CELL);
        if(ly < worldT || ly > worldB) continue;
        snprintf(label, sizeof(label), "%d", r + 1);
        drawText((float)SCR_W - 34.0f * s, ly - fontH * 0.5f, fontH, label, 1, 1, 1, 0.55f);
    }
    for(int c = 0; c < 10; ++c){
        float lx = toScreenX(((float)c + 0.5f) * CELL);
        if(lx < worldL || lx > worldR) continue;
        snprintf(label, sizeof(label), "%c", 'A' + c);
        drawText(lx - fontH * 0.3f, 10.0f * s, fontH, label, 1, 1, 1, 0.55f);
    }

    // ---- Заправка: значок и подпись. Пока это единственная локация, и без подписи
    // красное пятно на карте ничего не говорит.
    {
        float sx, sz;
        voxels_->gasStationCentre(sx, sz);
        float gx = toScreenX(sx), gy2 = toScreenY(sz);
        if(gx > worldL && gx < worldR && gy2 > worldT && gy2 < worldB){
            float r0 = 7.0f * s;
            drawUICircleOutline(gx, gy2, r0, 1.0f, 0.55f, 0.35f, 0.95f, 2.5f);
            drawUIRect(gx - r0 * 0.35f, gy2 - r0 * 0.35f, r0 * 0.7f, r0 * 0.7f, 0,
                       1.0f, 0.55f, 0.35f, 0.95f, false);
            drawText(gx + r0 * 1.6f, gy2 - 9.0f * s, 16.0f * s, "Заправка", 1, 0.8f, 0.7f, 0.95f);
        }
    }

    // ---- Метки игрока: касание по карте ставит флажок, касание по нему — снимает.
    float markR = clampf(9.0f * s * SDL_powf(mapZoom_, 0.35f), 7.0f * s, 26.0f * s);
    // Метка смерти: где игрока убило в прошлый раз. Ставится сама и живёт до
    // следующей смерти — за вещами надо возвращаться.
    if(deathMarkValid_){
        float dxs = toScreenX(deathMark_.x), dys = toScreenY(deathMark_.y);
        if(dxs > worldL && dxs < worldR && dys > worldT && dys < worldB){
            float dr = 14.0f * s;
            if(texDeathMark_)
                drawUIRect(dxs - dr, dys - dr * 2.0f, dr * 2.0f, dr * 2.0f, texDeathMark_,
                           1, 1, 1, 0.95f, true);
            else
                drawUICircleOutline(dxs, dys, dr, 0.9f, 0.2f, 0.2f, 0.95f, 3.0f);
        }
    }

    for(const Vec2& m : mapMarks_){
        float mx = toScreenX(m.x), my = toScreenY(m.y);
        if(mx < -markR || mx > (float)SCR_W + markR) continue;
        if(my < -markR || my > (float)SCR_H + markR) continue;
        if(texMapMark_){
            // Своя картинка булавки: её острие смотрит в точку метки.
            float pinW = markR * 1.6f, pinH = pinW * 1.12f;
            drawUIRect(mx - pinW * 0.5f, my - pinH, pinW, pinH, texMapMark_, 1, 1, 1, 0.95f, true);
            continue;
        }
        // Флажок: ножка и треугольное полотнище. Рисуем примитивами — своей картинки
        // для метки в наборе нет, а цветное пятно на карте читается и так.
        drawUIRect(mx - 1.5f * s, my - markR, 3.0f * s, markR, 0, 1.0f, 0.95f, 0.35f, 0.95f, false);
        for(int i = 0; i < 8; ++i){
            float t = (float)i / 8.0f;
            float wq = markR * 0.62f * (1.0f - t);
            drawUIRect(mx + 1.5f * s, my - markR + t * markR * 0.55f, wq, markR * 0.08f + 1.0f,
                       0, 1.0f, 0.35f, 0.30f, 0.95f, false);
        }
        drawUICircleOutline(mx, my, markR * 0.30f, 1.0f, 0.95f, 0.35f, 0.9f, 2.0f);
    }

    // ---- Игрок. Метка своя (self.png) и уже показывает направление взгляда — стрелку
    // поверх неё не рисуем. Картинка нарисована «носом вверх», поэтому её надо
    // повернуть на текущий курс. Размер растёт с приближением, но не бесконечно:
    // на дальнем масштабе метку всё равно должно быть видно, на ближнем — не должна
    // закрывать квадрат.
    float ax = toScreenX(p.x), ay = toScreenY(p.z);
    if(ax >= 0.0f && ax <= (float)SCR_W && ay >= 0.0f && ay <= (float)SCR_H){
        float m = clampf(11.0f * s * SDL_powf(mapZoom_, 0.40f), 9.0f * s, 34.0f * s);
        if(texPlayerMarker_){
            // Знак минус обязателен: на карте ось Y растёт вниз, поэтому поворот экранного
            // спрайта идёт против курса — без него право и лево на метке менялись местами.
            drawUIRectRotated(ax, ay, m * 2.0f, m * 2.0f, texPlayerMarker_, -yaw_, 1.0f);
        } else {
            drawUICircle(ax, ay, m * 0.5f, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0f);
        }
    }

    // ---- Заголовок и кнопка закрытия.
    int cellCol = clampi((int)floorf(p.x / CELL), 0, 9);
    int cellRow = clampi((int)floorf(p.z / CELL), 0, 9);
    char header[160];
    snprintf(header, sizeof(header), "%c%d   X %.0f  Z %.0f   x%.1f",
             'A' + cellCol, cellRow + 1, (double)p.x, (double)p.z, (double)mapZoom_);
    drawUIRect(10.0f * s, 8.0f * s, 320.0f * s, 34.0f * s, 0, 0, 0, 0, 0.45f, false);
    drawText(18.0f * s, 12.0f * s, 25.0f * s, header, 1, 1, 1, 0.95f);
    drawText(18.0f * s, (float)SCR_H - 30.0f * s, 17.0f * s,
             "Щипок — масштаб, перетаскивание — сдвиг, касание — метка (по метке — снять)",
             1, 1, 1, 0.55f);

    float closeSize = 54.0f * s;
    float closeX = (float)SCR_W - closeSize - 12.0f * s, closeY = 12.0f * s;
    if(texClose_) drawUIRect(closeX, closeY, closeSize, closeSize, texClose_, 1, 1, 1, 0.85f, true);
    else {
        drawUIRect(closeX, closeY, closeSize, closeSize, 0, 1, 1, 1, 0.2f, false);
        drawText(closeX + closeSize * 0.3f, closeY + closeSize * 0.25f, closeSize * 0.5f, "X", 1, 1, 1, 0.9f);
    }
}

// ==================== КОМПАС ====================
// Полоса курса сверху, как в Rust: по ней договариваются о направлении («иди на 90»),
// а квадрат карты и градусы вместе заменяют полноценную навигацию.
void GameClient::renderCompass(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = clampf((float)SCR_W * 0.46f, 300.0f, 780.0f);
    float h = 26.0f * s;
    float x = ((float)SCR_W - w) * 0.5f;
    float y = 6.0f * s;

    // Ни рамки, ни подложки: полоса белая и полупрозрачная, чтобы не перекрывать вид.
    // Шкала едет, неподвижная палка по центру показывает, на какой курс смотрит игрок.
    // Знак минус обязателен: в мире рост yaw поворачивает игрока ВЛЕВО (взгляд
    // -sin(yaw), -cos(yaw)), а курс по компасу растёт вправо. Без него шкала ехала в
    // противоположную сторону, и метки на ней вставали зеркально.
    float heading = -yaw_ * 180.0f / 3.14159265f;
    heading = fmodf(heading, 360.0f);
    if(heading < 0.0f) heading += 360.0f;

    const float VISIBLE = 100.0f;   // градусов в ширину полосы
    char label[8];
    for(int deg = 0; deg < 360; deg += 10){
        float delta = (float)deg - heading;
        while(delta > 180.0f) delta -= 360.0f;
        while(delta < -180.0f) delta += 360.0f;
        if(fabsf(delta) > VISIBLE * 0.5f) continue;
        float px = x + w * 0.5f + delta / VISIBLE * w;
        // Края полосы гасим, чтобы деления не обрывались резкой линией.
        float edgeFade = 1.0f - clampf((fabsf(delta) / (VISIBLE * 0.5f) - 0.72f) / 0.28f, 0.0f, 1.0f);

        bool cardinal = (deg % 90) == 0;
        bool major = (deg % 30) == 0;
        float tickH = cardinal ? h * 0.42f : (major ? h * 0.30f : h * 0.18f);
        drawUIRect(px, y + h - tickH, 2.0f, tickH, 0, 1, 1, 1, (cardinal ? 0.85f : 0.5f) * edgeFade, false);

        if(major){
            if(cardinal){
                const char* name = (deg == 0) ? "N" : (deg == 90) ? "E" : (deg == 180) ? "S" : "W";
                drawText(px - 5.0f * s, y - 1.0f * s, 17.0f * s, name, 1, 1, 1, 0.9f * edgeFade);
            } else {
                snprintf(label, sizeof(label), "%d", deg);
                float tw = 8.0f * s * (float)strlen(label) * 0.55f;
                drawText(px - tw * 0.5f, y + 1.0f * s, 14.0f * s, label, 1, 1, 1, 0.55f * edgeFade);
            }
        }
    }

    // ---- Метки с карты на той же шкале: жёлтый штрих на своём азимуте и метры до
    // неё. Без этого метку ставишь и тут же теряешь — на карту приходится лезть заново.
    Vec3 p = player_->position();
    char distBuf[16];
    for(const Vec2& m : mapMarks_){
        float dx = m.x - p.x, dz = m.y - p.z;
        float dist = sqrtf(dx * dx + dz * dz);
        // Азимут метки в той же системе, что и курс игрока: 0 — на север (-Z).
        float bearing = atan2f(dx, -dz) * 180.0f / 3.14159265f;
        float delta = bearing - heading;
        while(delta > 180.0f) delta -= 360.0f;
        while(delta < -180.0f) delta += 360.0f;
        if(fabsf(delta) > VISIBLE * 0.5f) continue;

        float px = x + w * 0.5f + delta / VISIBLE * w;
        drawUIRect(px - 2.0f, y - 2.0f * s, 4.0f, h + 4.0f * s, 0,
                   1.0f, 0.85f, 0.25f, 0.95f, false);
        snprintf(distBuf, sizeof(distBuf), "%d м", (int)(dist + 0.5f));
        float tw = 9.0f * s * (float)strlen(distBuf) * 0.55f;
        drawText(px - tw * 0.5f, y + h + 2.0f * s, 16.0f * s, distBuf, 1.0f, 0.9f, 0.45f, 0.95f);
    }

    // Палка курса: она и есть «куда смотрит игрок». Точного числа градусов под ней нет —
    // курс читается по шкале, а цифра под палкой была лишней.
    drawUIRect(x + w * 0.5f - 1.5f, y - 3.0f * s, 3.0f, h + 5.0f * s, 0, 1, 1, 1, 0.95f, false);
}

void GameClient::inventoryCloseRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float gx, gy, slot, gap;
    inventoryGeometry(gx, gy, slot, gap);
    float gw = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
    w = h = 54.0f * s;
    x = gx + gw + 16.0f * s;
    y = gy - 30.0f * s;
    // Если сетка почти во всю ширину, крестик прижимается к краю экрана, а не уезжает
    // за него.
    if(x + w > (float)SCR_W - 8.0f * s) x = (float)SCR_W - w - 8.0f * s;
}

// ==================== ГЛАВНОЕ МЕНЮ: БРАУЗЕР СЕРВЕРОВ ====================
// Меню сделано как список серверов, а не как три кнопки: игра теперь и одиночная, и
// сетевая, и выбирать надо именно сервер. Слева — вкладки, справа — список с игроками
// и пингом, снизу — «обновить», «добавить сервер» и «играть». Никаких подсказок и
// пояснений на экране: список говорит сам за себя.

// Официальный сервер: он всегда в списке первой строкой после одиночной игры, чтобы
// игроку не приходилось никуда вписывать адрес — зашёл и играешь.
const char* OFFICIAL_SERVER = "https://osil-survival1.onrender.com";

void GameClient::loadServerList(){
    servers_.clear();
    // Первая строка всегда одна и та же: локальный мир без сети.
    ServerRow local;
    local.name = "Одиночная игра";
    local.map = "Survival Island";
    local.local = true;
    local.max = 1;
    local.online = true;
    servers_.push_back(local);

    ServerRow official;
    official.address = OFFICIAL_SERVER;
    official.name = "Официальный сервер";
    official.map = "Survival Island";
    servers_.push_back(official);

    // Остальное — то, что игрок добавил сам. Файл лежит рядом с настройками.
    std::string path = g_writableDir + "servers.txt";
    FILE* f = fopen(path.c_str(), "rb");
    if(f){
        char line[512];
        while(fgets(line, sizeof(line), f)){
            std::string a(line);
            while(!a.empty() && (a.back() == '\n' || a.back() == '\r' || a.back() == ' ')) a.pop_back();
            if(a.empty()) continue;
            if(a.rfind("name=", 0) == 0){ playerName_ = a.substr(5); continue; }
            if(a == OFFICIAL_SERVER) continue;   // он уже стоит в списке
            ServerRow r;
            r.address = a;
            r.name = a;
            servers_.push_back(r);
        }
        fclose(f);
    }
}

void GameClient::saveServerList(){
    std::string path = g_writableDir + "servers.txt";
    FILE* f = fopen(path.c_str(), "wb");
    if(!f) return;
    fprintf(f, "name=%s\n", playerName_.c_str());
    for(const ServerRow& r : servers_){
        if(r.local || r.address.empty()) continue;
        if(r.address == OFFICIAL_SERVER) continue;   // он и так добавляется сам
        fprintf(f, "%s\n", r.address.c_str());
    }
    fclose(f);
}

// Опрос всех адресов из списка. Идёт в отдельном потоке: сервер за океаном отвечает
// полсекунды, и меню на это время не должно замирать.
void GameClient::refreshServers(){
    menuNotice_ = "обновляем список...";
    menuNoticeAge_ = 0.0f;
    std::vector<std::string> addresses;
    for(const ServerRow& r : servers_) if(!r.local) addresses.push_back(r.address);
    if(addresses.empty()){
        menuNotice_ = "серверов в списке нет";
        return;
    }
    std::thread([this, addresses]{
        for(const std::string& a : addresses){
            NetClient::Info info = NetClient::query(a);
            for(ServerRow& r : servers_){
                if(r.address != a) continue;
                r.online = info.ok;
                r.players = info.players;
                r.max = info.max ? info.max : 100;
                r.ping = info.ping;
                if(info.ok && !info.name.empty()) r.name = info.name;
                if(info.ok && !info.map.empty()) r.map = info.map;
            }
        }
        menuNotice_ = "список обновлён";
        menuNoticeAge_ = 0.0f;
    }).detach();
}

void GameClient::menuListArea(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    x = (float)SCR_W * 0.26f;
    y = 34.0f * s;
    w = (float)SCR_W * 0.63f - x;
    h = (float)SCR_H - y - 24.0f * s;
}

void GameClient::menuRowRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float lx, ly, lw, lh;
    menuListArea(lx, ly, lw, lh);
    h = 56.0f * s;
    x = lx;
    w = lw;
    y = ly + 26.0f * s + (float)i * (h + 4.0f * s);
}

void GameClient::menuTabRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    x = (float)SCR_W * 0.075f + 8.0f * s;
    w = (float)SCR_W * 0.26f - x - 8.0f * s;
    h = 62.0f * s;
    y = 20.0f * s + (float)i * (h + 6.0f * s);
}

void GameClient::menuActionRect(int i, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    x = (float)SCR_W * 0.075f + 8.0f * s;
    w = (float)SCR_W * 0.26f - x - 8.0f * s;
    h = 56.0f * s;
    // Кнопки прижаты к низу колонки вкладок, как в образце.
    y = (float)SCR_H - (float)(3 - i) * (h + 10.0f * s) - 20.0f * s;
}

void GameClient::renderMainMenu(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Список опрашивается сам при первом показе меню: игрок должен сразу видеть, живы
    // ли сервера и сколько там народу, а не жать «обновить».
    if(!menuRefreshed_){
        menuRefreshed_ = true;
        refreshServers();
    }
    if(texMenuBg_){
        drawMenuBackground();
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.03f, 0.03f, 0.55f, false);
    } else {
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.03f, 0.03f, 0.75f, false);
    }

    // ---- Левая полоса со значками: она же место для логотипа.
    float railW = (float)SCR_W * 0.075f;
    drawUIRect(0, 0, railW, (float)SCR_H, 0, 0.07f, 0.07f, 0.08f, 0.92f, false);
    float logo = railW * 0.62f;
    drawUIRect(railW * 0.19f, railW * 0.19f, logo, logo, 0, UI_ACCENT.r * 0.5f,
               UI_ACCENT.g * 0.6f, UI_ACCENT.b * 0.5f, 0.95f, false);
    uiThinFrame(railW * 0.19f, railW * 0.19f, logo, logo, UI_ACCENT, 0.9f);
    GLuint railIcons[3] = { texInventory_, texCraft_, texSettings_ };
    for(int i = 0; i < 3; ++i){
        float isz = railW * 0.44f;
        float ix = railW * 0.28f, iy = railW * 1.15f + (float)i * (isz + 22.0f * s);
        if(railIcons[i]) drawUIRect(ix, iy, isz, isz, railIcons[i], 1, 1, 1, 0.55f, true);
    }

    // ---- Колонка вкладок.
    drawUIRect(railW, 0, (float)SCR_W * 0.26f - railW, (float)SCR_H, 0,
               0.11f, 0.11f, 0.12f, 0.78f, false);
    const char* tabs[4] = { "Сервера", "Друзья", "Любимые", "История" };
    int online = 0, playersTotal = 0;
    for(const ServerRow& r : servers_){ if(r.online && !r.local) ++online; playersTotal += r.players; }
    char buf[160];
    for(int i = 0; i < 4; ++i){
        float x, y, w, h;
        menuTabRect(i, x, y, w, h);
        bool on = (i == menuTab_);
        drawUIRect(x, y, w, h, 0, 0.88f, 0.87f, 0.83f, on ? 0.16f : 0.06f, false);
        drawText(x + 12.0f * s, y + 8.0f * s, 24.0f * s, tabs[i], 1, 1, 1, on ? 0.98f : 0.65f);
        if(i == 0) snprintf(buf, sizeof(buf), "%d сервер(ов), %d игрок(ов)", online, playersTotal);
        else       snprintf(buf, sizeof(buf), "пусто");
        drawText(x + 12.0f * s, y + 34.0f * s, 16.0f * s, buf, 1, 1, 1, 0.45f);
    }

    // ---- Кнопки внизу колонки.
    const char* actions[3] = { "ДОБАВИТЬ СЕРВЕР", "ОБНОВИТЬ", "ИГРАТЬ" };
    for(int i = 0; i < 3; ++i){
        float x, y, w, h;
        menuActionRect(i, x, y, w, h);
        bool primary = (i == 2);
        drawUIRect(x, y, w, h, 0, 0.88f, 0.87f, 0.83f, primary ? 0.26f : 0.12f, false);
        uiThinFrame(x, y, w, h, primary ? UI_ACCENT : UI_LINE, primary ? 0.9f : 0.4f);
        drawTextCentered(x + w * 0.5f, y + h * 0.28f, h * 0.38f, actions[i],
                         primary ? UI_ACCENT.r : UI_TEXT.r,
                         primary ? UI_ACCENT.g : UI_TEXT.g,
                         primary ? UI_ACCENT.b : UI_TEXT.b, 1.0f);
    }

    // ---- Список серверов: шапка и строки.
    float lx, ly, lw, lh;
    menuListArea(lx, ly, lw, lh);
    drawText(lx, ly - 24.0f * s, 19.0f * s, "названия серверов", 1, 1, 1, 0.55f);
    drawText(lx + lw - 190.0f * s, ly - 24.0f * s, 19.0f * s, "игроки", 1, 1, 1, 0.55f);
    drawText(lx + lw - 70.0f * s, ly - 24.0f * s, 19.0f * s, "пинг", 1, 1, 1, 0.55f);

    for(size_t i = 0; i < servers_.size(); ++i){
        float x, y, w, h;
        menuRowRect((int)i, x, y, w, h);
        if(y + h > (float)SCR_H - 10.0f * s) break;
        const ServerRow& r = servers_[i];
        bool sel = ((int)i == menuSelected_);
        drawUIRect(x, y, w, h, 0, 0.85f, 0.84f, 0.80f, sel ? 0.22f : 0.10f, false);
        if(sel) uiThinFrame(x, y, w, h, UI_ACCENT, 0.9f);
        drawText(x + 44.0f * s, y + 7.0f * s, 21.0f * s, r.name, 1, 1, 1,
                 (r.online || r.local) ? 0.97f : 0.5f);
        drawText(x + 44.0f * s, y + 32.0f * s, 15.0f * s, r.map, 1, 1, 1, 0.45f);
        // Звёздочка слева — как в образце; сейчас она просто метка строки.
        drawUIRect(x + 16.0f * s, y + h * 0.5f - 5.0f * s, 10.0f * s, 10.0f * s, 0,
                   1, 1, 1, sel ? 0.9f : 0.35f, false);
        if(r.local) snprintf(buf, sizeof(buf), "0/1");
        else        snprintf(buf, sizeof(buf), "%d/%d", r.players, r.max);
        drawText(x + w - 190.0f * s, y + 18.0f * s, 19.0f * s, buf, 1, 1, 1, 0.85f);
        snprintf(buf, sizeof(buf), "%d", r.ping);
        drawText(x + w - 70.0f * s, y + 18.0f * s, 19.0f * s, buf, 1, 1, 1, 0.85f);
    }

    // ---- Карточка игрока справа и версия внизу.
    float cardX = (float)SCR_W * 0.90f;
    drawUIRect(cardX, 0, (float)SCR_W - cardX, (float)SCR_H, 0, 0.07f, 0.07f, 0.08f, 0.85f, false);
    float av = ((float)SCR_W - cardX) * 0.55f;
    drawUIRect(cardX + av * 0.4f, 18.0f * s, av, av, 0, 0.55f, 0.55f, 0.58f, 0.9f, false);
    drawTextCentered(cardX + ((float)SCR_W - cardX) * 0.5f, 18.0f * s + av + 6.0f * s,
                     19.0f * s, playerName_, 1, 1, 1, 0.95f);
    drawTextCentered(cardX + ((float)SCR_W - cardX) * 0.5f, (float)SCR_H - 30.0f * s,
                     15.0f * s, "Open alfa 0.2", 1, 1, 1, 0.55f);

    // ---- Ввод адреса или имени: одна строка поверх списка.
    if(menuAddOpen_ || menuEditName_){
        float bw = fminf((float)SCR_W * 0.6f, 760.0f * s), bh = 150.0f * s;
        float bx = ((float)SCR_W - bw) * 0.5f, by = (float)SCR_H * 0.32f;
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.02f, 0.03f, 0.55f, false);
        drawUIRect(bx, by, bw, bh, 0, 0.14f, 0.14f, 0.15f, 0.96f, false);
        uiThinFrame(bx, by, bw, bh, UI_ACCENT, 0.9f);
        drawText(bx + 18.0f * s, by + 12.0f * s, 20.0f * s,
                 menuAddOpen_ ? "адрес сервера" : "имя игрока", 1, 1, 1, 0.7f);
        std::string shown = menuInput_ + "_";
        drawText(bx + 18.0f * s, by + 48.0f * s, 26.0f * s, shown, 1, 1, 1, 0.98f);
        float okW = 160.0f * s, okH = 46.0f * s;
        drawUIRect(bx + bw - okW - 16.0f * s, by + bh - okH - 14.0f * s, okW, okH, 0,
                   0.88f, 0.87f, 0.83f, 0.24f, false);
        uiThinFrame(bx + bw - okW - 16.0f * s, by + bh - okH - 14.0f * s, okW, okH, UI_ACCENT, 0.9f);
        drawTextCentered(bx + bw - okW * 0.5f - 16.0f * s, by + bh - okH - 2.0f * s,
                         22.0f * s, "ГОТОВО", 1, 1, 1, 0.95f);
        float cancelW = 130.0f * s;
        drawUIRect(bx + 16.0f * s, by + bh - okH - 14.0f * s, cancelW, okH, 0,
                   0.88f, 0.87f, 0.83f, 0.12f, false);
        drawTextCentered(bx + 16.0f * s + cancelW * 0.5f, by + bh - okH - 2.0f * s,
                         22.0f * s, "ОТМЕНА", 1, 1, 1, 0.8f);
    }

    // ---- Короткое сообщение о последнем действии (не подсказка, а ответ игре).
    menuNoticeAge_ += 1.0f / 60.0f;
    if(!menuNotice_.empty() && menuNoticeAge_ < 6.0f)
        drawText(lx, (float)SCR_H - 26.0f * s, 17.0f * s, menuNotice_, 1, 1, 1, 0.6f);
}

// Запуск выбранной строки: локальный мир или подключение к серверу.
void GameClient::startSelectedServer(){
    if(menuSelected_ < 0 || menuSelected_ >= (int)servers_.size()) return;
    const ServerRow row = servers_[(size_t)menuSelected_];
    if(row.local){
        net_.leave();
        state_ = GameState::Playing;
        menuNotice_.clear();
        return;
    }
    menuNotice_ = "подключаемся к " + row.address + "...";
    menuNoticeAge_ = 0.0f;
    if(net_.join(row.address, playerName_)){
        state_ = GameState::Playing;
        menuNotice_.clear();
        // Мир у всех строится из сида, и если сервер живёт на другом — постройки и
        // деревья окажутся не там, где их видят соседи. Молчать об этом нельзя.
        unsigned long long mine = seedFromString(WORLD_SEED_TEXT);
        if(net_.seed() != 0 && net_.seed() != mine)
            SDL_Log("сеть: сид сервера %llu не совпадает с сидом клиента %llu",
                    (unsigned long long)net_.seed(), mine);
    } else {
        menuNotice_ = net_.statusText();
        menuNoticeAge_ = 0.0f;
    }
}

void GameClient::menuTextInput(const char* text){
    if(!menuAddOpen_ && !menuEditName_) return;
    if(menuInput_.size() > 120) return;
    menuInput_ += text;
}

void GameClient::menuBackspace(){
    if(menuInput_.empty()) return;
    // UTF-8: сносим целый символ, а не байт, иначе в строке остаётся мусор.
    size_t n = menuInput_.size();
    do { --n; } while(n > 0 && ((unsigned char)menuInput_[n] & 0xC0) == 0x80);
    menuInput_.resize(n);
}

bool GameClient::handleMenuTouch(float x, float y){
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);

    // Ввод адреса или имени перехватывает всё: пока строка не закрыта, меню под ней
    // не нажимается.
    if(menuAddOpen_ || menuEditName_){
        float bw = fminf((float)SCR_W * 0.6f, 760.0f * s), bh = 150.0f * s;
        float bx = ((float)SCR_W - bw) * 0.5f, by = (float)SCR_H * 0.32f;
        float okW = 160.0f * s, okH = 46.0f * s;
        float okX = bx + bw - okW - 16.0f * s, okY = by + bh - okH - 14.0f * s;
        float cancelW = 130.0f * s, cancelX = bx + 16.0f * s;
        if(x >= okX && x <= okX + okW && y >= okY && y <= okY + okH){
            std::string value = menuInput_;
            while(!value.empty() && value.back() == ' ') value.pop_back();
            if(menuAddOpen_ && !value.empty()){
                ServerRow r;
                r.address = value;
                r.name = value;
                servers_.push_back(r);
                menuSelected_ = (int)servers_.size() - 1;
                saveServerList();
                refreshServers();
            } else if(menuEditName_ && !value.empty()){
                playerName_ = value;
                saveServerList();
            }
            menuAddOpen_ = menuEditName_ = false;
            menuInput_.clear();
            SDL_StopTextInput();
            return true;
        }
        if(x >= cancelX && x <= cancelX + cancelW && y >= okY && y <= okY + okH){
            menuAddOpen_ = menuEditName_ = false;
            menuInput_.clear();
            SDL_StopTextInput();
            return true;
        }
        return true;
    }

    // Вкладки.
    for(int i = 0; i < 4; ++i){
        float bx, by, bw, bh;
        menuTabRect(i, bx, by, bw, bh);
        if(x >= bx && x <= bx + bw && y >= by && y <= by + bh){ menuTab_ = i; return true; }
    }

    // Кнопки: добавить сервер, обновить, играть.
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        menuActionRect(i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if(i == 0){
            menuAddOpen_ = true;
            menuInput_.clear();
            SDL_StartTextInput();   // на телефоне это и поднимает экранную клавиатуру
        } else if(i == 1){
            refreshServers();
        } else {
            startSelectedServer();
        }
        return true;
    }

    // Карточка игрока справа — по ней меняют имя.
    if(x > (float)SCR_W * 0.90f && y < (float)SCR_H * 0.25f){
        menuEditName_ = true;
        menuInput_ = playerName_;
        SDL_StartTextInput();
        return true;
    }

    // Строки списка: первое касание выбирает, второе по той же строке — запускает.
    for(size_t i = 0; i < servers_.size(); ++i){
        float bx, by, bw, bh;
        menuRowRect((int)i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if(menuSelected_ == (int)i) startSelectedServer();
        else menuSelected_ = (int)i;
        return true;
    }
    return true;
}

// ==================== ЗАГРУЗКА, СНИМКИ, ЦИКЛ ====================

void GameClient::drawLoadingScreen(const char* text){
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCR_W, SCR_H);
    glDisable(GL_DEPTH_TEST);
    glClearColor(UI_BG_DEEP.r, UI_BG_DEEP.g, UI_BG_DEEP.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Mat4 uiProjM = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
    glUseProgram(uiProg);
    glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, uiProjM.m);
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    drawText((float)SCR_W * 0.5f - 150.0f * s, (float)SCR_H * 0.44f, 34.0f * s, "OSIL SURVIVAL",
             UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0f);
    drawText((float)SCR_W * 0.5f - 150.0f * s, (float)SCR_H * 0.53f, 22.0f * s, text,
             UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 1.0f);
    SDL_GL_SwapWindow(win);
}

void GameClient::render(){
    renderScene();
    if(state_ == GameState::Playing){
        renderHud();
        renderOverlay();
        renderHotbar();
    } else {
        // В меню поверх сцены рисуем только само меню (и настройки, если открыты).
        glDisable(GL_DEPTH_TEST);
        Mat4 uiProjM = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
        glUseProgram(uiProg);
        glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, uiProjM.m);
        renderMainMenu();
        if(overlay_ == Overlay::Settings) renderSettings();
    }
    SDL_GL_SwapWindow(win);
}

int GameClient::run(int argc, char** argv){
    // Ключи нужны только настольной отладке и проверке в CI: на телефоне клиент
    // запускается без единого аргумента.
    for(int i = 1; i < argc; ++i){
        std::string a = argv[i];
        if(a == "--screenshot" && i + 1 < argc){
            screenshotPath_ = argv[++i];
            screenshotFrame_ = 120;   // дать миру собрать чанки вокруг игрока
        } else if(a == "--frames" && i + 1 < argc){
            screenshotFrame_ = atoi(argv[++i]);
        } else if(a == "--time" && i + 1 < argc){
            startTimeOverride_ = (float)atof(argv[++i]);
        } else if(a == "--yaw" && i + 1 < argc){
            yawOverride_ = (float)atof(argv[++i]) * 3.14159265f / 180.0f;
        } else if(a == "--pitch" && i + 1 < argc){
            pitchOverride_ = (float)atof(argv[++i]) * 3.14159265f / 180.0f;
        } else if(a == "--overlay" && i + 1 < argc){
            // Только для проверки интерфейса снимком: открыть окно сразу при запуске.
            std::string what = argv[++i];
            if(what == "inventory") overlayOverride_ = Overlay::Inventory;
            else if(what == "craft") overlayOverride_ = Overlay::Craft;
            else if(what == "map")   overlayOverride_ = Overlay::Map;
            else if(what == "settings") overlayOverride_ = Overlay::Settings;
            else if(what == "box")      overlayOverride_ = Overlay::Box;
            else if(what == "cupboard") overlayOverride_ = Overlay::Cupboard;
            else if(what == "pause")    overlayOverride_ = Overlay::Pause;
        } else if(a == "--dig" && i + 1 < argc){
            digDepth_ = atoi(argv[++i]);
        } else if(a == "--server" && i + 1 < argc){
            // Отладка и проверка сети: сразу войти на сервер, минуя меню.
            joinOnStart_ = argv[++i];
        } else if(a == "--name" && i + 1 < argc){
            playerName_ = argv[++i];
        } else if(a == "--menu"){
            stayInMenu_ = true;    // снять главное меню на скриншот
        } else if(a == "--play"){
            startInGame_ = true;   // пропустить главное меню (отладка)
        } else if(a == "--debug"){
            settings.showDebugInfo = true;
            debugKit_ = true;
        } else if(a == "--slot" && i + 1 < argc){
            startSlot_ = atoi(argv[++i]);   // отладка: что взять в руку на старте
        } else if(a == "--pos" && i + 1 < argc){
            // Отладка: встать в заданной точке карты, чтобы снять зиму или пустыню,
            // не бегая туда полчаса.
            float x = 0, z = 0;
            if(sscanf(argv[++i], "%f,%f", &x, &z) == 2){ startX_ = x; startZ_ = z; }
        } else if(a == "--size" && i + 1 < argc){
            int w = 0, h = 0;
            if(sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0){
                forcedW_ = w; forcedH_ = h;
            }
        }
    }

    if(!initPlatform()) return 1;
    if(!initGraphics()) return 1;
    controls_.layout(SCR_W, SCR_H);

    drawLoadingScreen("Строим кубический мир 1000x1000...");
    initWorld();
    // Вход на сервер из командной строки: удобно проверять сеть двумя окнами.
    if(!joinOnStart_.empty()){
        if(net_.join(joinOnStart_, playerName_))
            SDL_Log("сеть: вошли на %s", joinOnStart_.c_str());
        else
            SDL_Log("сеть: не удалось войти на %s (%s)", joinOnStart_.c_str(),
                    net_.statusText().c_str());
    }
    overlay_ = overlayOverride_;
    // Окна ящика и шкафа знают, ЧТО открыто: без выбранного объекта они пустые.
    if(overlay_ == Overlay::Box && !boxes_.empty()) openBox_ = 0;
    if(overlay_ == Overlay::Cupboard && !cupboards_.empty()) openCupboard_ = 0;
    // Снимок экрана снимается из игры, а не из меню: иначе проверять нечего.
    if((!screenshotPath_.empty() || startInGame_) && !stayInMenu_) state_ = GameState::Playing;
    audioApplySettings();

    // Отладка: выкопать под игроком яму заданной глубины и встать на её край.
    if(digDepth_ > 0){
        // Копаем колодец 2x2 прямо под игроком и опускаем его на дно: так стенки ямы
        // оказываются прямо перед камерой, и видно, есть у них грани или нет.
        Vec3 p = player_->position();
        int bx = (int)floorf(p.x), bz = (int)floorf(p.z);
        int top = voxels_->surfaceY(bx, bz);
        for(int d = 0; d < digDepth_; ++d)
            for(int dx = 0; dx < 2; ++dx)
                for(int dz = 0; dz < 2; ++dz)
                    voxels_->setBlock(bx + dx, top - d, bz + dz, Block::Air);
        player_->spawn(Vec3{ (float)bx + 0.5f, (float)(top - digDepth_ + 1), (float)bz + 0.5f });
    }

    // Первые чанки собираем до входа в игру: иначе игрок появляется в пустоте и падает.
    for(int i = 0; i < 40; ++i)
        chunks_.update(player_->eyePosition(), viewDistanceMeters(), 12);

    int64_t last = nowMillis();
    while(running_){
        int64_t now = nowMillis();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        if(dt > 0.1f) dt = 0.1f;

        // Счётчик кадров усредняется за полсекунды: мгновенное значение прыгает так,
        // что прочитать его на ходу невозможно.
        fpsAccum_ += dt;
        ++fpsFrames_;
        if(fpsAccum_ >= 0.5f){
            fps_ = (float)fpsFrames_ / fpsAccum_;
            fpsAccum_ = 0.0f;
            fpsFrames_ = 0;
        }

        handleEvents();

        if(state_ == GameState::MainMenu){
            update(dt);
        } else if(player_->isDead() && controls_.actionPressed()){
            Rng rng(splitMix64((uint64_t)nowMillis()));
            Vec3 spawn = world_->findSpawnPoint(rng);
            player_->spawn(spawn);
        } else if(!player_->isDead()){
            update(dt);
        } else {
            controls_.endFrame();
        }

        render();
        ++frameCounter_;

        if(!screenshotPath_.empty() && frameCounter_ >= screenshotFrame_){
            std::vector<uint8_t> pixels((size_t)SCR_W * SCR_H * 4);
            glReadPixels(0, 0, SCR_W, SCR_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::vector<uint8_t> rgb((size_t)SCR_W * SCR_H * 3);
            for(int y = 0; y < SCR_H; ++y){
                const uint8_t* src = &pixels[(size_t)(SCR_H - 1 - y) * SCR_W * 4];
                uint8_t* dst = &rgb[(size_t)y * SCR_W * 3];
                for(int x = 0; x < SCR_W; ++x){
                    dst[x*3+0] = src[x*4+0];
                    dst[x*3+1] = src[x*4+1];
                    dst[x*3+2] = src[x*4+2];
                }
            }
            if(writePng(screenshotPath_, SCR_W, SCR_H, rgb)) SDL_Log("Снимок экрана: %s", screenshotPath_.c_str());
            running_ = false;
        }

        // Потолок кадров: прямая экономия батареи и нагрева на телефоне. Вертикальная
        // синхронизация выключена, поэтому темп задаёт только он.
        if(settings.fpsLimit > 0){
            int64_t frameMs = 1000 / settings.fpsLimit;
            int64_t spent = nowMillis() - now;
            if(spent < frameMs) sleepMillis(frameMs - spent);
        }
    }

    saveSettings();
    chunks_.shutdown();
    audioShutdown();
    if(uiFont) TTF_CloseFont(uiFont);
    TTF_Quit();
    IMG_Quit();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

// ==================== ТЕКСТУРЫ ИНТЕРФЕЙСА ====================
// Иконки кнопок, джойстик, маркер игрока и фон меню. Все файлы необязательны: чего нет,
// то рисуется прежним способом (кругом с подписью) — правило проекта «игра запускается
// без ассетов» никуда не делось.
// Фон меню и настроек. Картинка вписывается «по накрытию»: сохраняем пропорции и
// срезаем лишнее по длинной стороне — растянутый на другой экран фон выглядит кривым.
void GameClient::drawMenuBackground(){
    if(!texMenuBg_){
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.06f, 0.07f, 1.0f, false);
        return;
    }
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if(menuBgW_ > 0 && menuBgH_ > 0){
        float srcAspect = (float)menuBgW_ / (float)menuBgH_;
        float dstAspect = (float)SCR_W / (float)SCR_H;
        if(srcAspect > dstAspect){          // картинка шире экрана — срезаем бока
            float keep = dstAspect / srcAspect;
            u0 = (1.0f - keep) * 0.5f;
            u1 = u0 + keep;
        } else if(srcAspect < dstAspect){   // картинка выше экрана — срезаем верх и низ
            float keep = srcAspect / dstAspect;
            v0 = (1.0f - keep) * 0.5f;
            v1 = v0 + keep;
        }
    }
    drawUIRectUV(0, 0, (float)SCR_W, (float)SCR_H, texMenuBg_, u0, v0, u1, v1, 1.0f);
}

void GameClient::loadInterfaceTextures(){
    struct { const char* file; GLuint* target; } items[] = {
        { "ui_dig.png",            &texDig_ },
        { "ui_place.png",          &texPlace_ },
        { "ui_interact.png",       &texInteract_ },
        { "ui_inventory.png",      &texInventory_ },
        { "ui_craft.png",          &texCraft_ },
        { "ui_map.png",            &texMap_ },
        { "ui_settings.png",       &texSettings_ },
        { "ui_close.png",          &texClose_ },
        { "ui_jump.png",           &texJump_ },
        { "ui_run.png",            &texRun_ },
        { "ui_crouch.png",         &texCrouch_ },
        { "ui_joystick_base.png",  &texJoyBase_ },
        { "ui_joystick_stick.png", &texJoyStick_ },
        { "ui_player_marker.png",  &texPlayerMarker_ },
        { "menu_bg.png",           &texMenuBg_ },
        { "ui_blood.png",          &texBlood_ },
        { "ui_build.png",          &texBuild_ },
        { "ui_build_accept.png",   &texBuildAccept_ },
        { "ui_cat_foundation.png", &texCatFoundation_ },
        { "ui_cat_floor.png",      &texCatFloor_ },
        { "ui_cat_door.png",       &texCatDoor_ },
        { "ui_cat_wall.png",       &texCatWall_ },
        { "ui_hitmark.png",        &texHitMark_ },
        { "ui_open.png",           &texOpen_ },
        { "ui_fire.png",           &texFire_ },
        { "ui_radiation.png",      &texRadiation_ },
        { "ui_map_mark.png",       &texMapMark_ },
        { "ui_death_mark.png",     &texDeathMark_ },
    };
    int loaded = 0;
    for(const auto& it : items){
        int tw = 0, th = 0;
        *it.target = loadTextureFromFile(assetPath(it.file).c_str(), &tw, &th);
        if(it.target == &texMenuBg_){ menuBgW_ = tw; menuBgH_ = th; }
        if(*it.target) ++loaded;
    }
    SDL_Log("Иконки интерфейса: загружено %d из %d", loaded, (int)(sizeof(items)/sizeof(items[0])));

    // Значки предметов: файл на каждый вид. Чего нет — останется нулём, и ячейка
    // нарисуется цветом предмета.
    struct { ItemType type; const char* file; } itemIcons[] = {
        { ItemType::Wood,      "item_wood.png" },
        { ItemType::OreMetal,  "item_iron.png" },
        { ItemType::OreSulfur, "item_sulfur.png" },
        { ItemType::Scrap,     "item_scrap.png" },
        { ItemType::Axe,       "item_axe.png" },
        { ItemType::Torch,     "item_torch.png" },
        { ItemType::Furnace,   "item_furnace.png" },
        { ItemType::Sulfur,    "item_sulfur_dust.png" },
        { ItemType::MetalFrag, "item_metal_frag.png" },
        { ItemType::Cloth,     "item_cloth.png" },
        { ItemType::Gunpowder, "item_powder.png" },
        { ItemType::BuildPlan, "item_plan.png" },
        { ItemType::Stone,     "item_stone.png" },
        { ItemType::Leaves,    "item_leaves.png" },
        { ItemType::Planks,    "item_planks.png" },
        { ItemType::Dirt,      "item_dirt.png" },
        { ItemType::Sand,      "item_sand.png" },
        { ItemType::Box,       "item_box.png" },
        { ItemType::Cupboard,  "item_cupboard.png" },
    };
    int itemsLoaded = 0;
    for(const auto& it : itemIcons){
        texItems_[(int)it.type] = loadTextureFromFile(assetPath(it.file).c_str(), nullptr, nullptr);
        if(texItems_[(int)it.type]) ++itemsLoaded;
    }
    SDL_Log("Значки предметов: загружено %d из %d", itemsLoaded,
            (int)(sizeof(itemIcons)/sizeof(itemIcons[0])));
}
