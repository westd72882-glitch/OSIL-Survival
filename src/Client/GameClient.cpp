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
#include <cmath>
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
    makeDynamicVoxelBuffer(heldVao_, heldVbo_, 6 * 36);

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
    float swing = player_->miningProgress() > 0.01f ? sinf(heldBobPhase_ * 9.0f) * 0.10f : 0.0f;

    Vec3 right = v3norm(v3cross(forward, Vec3{0,1,0}));
    Vec3 up = v3cross(right, forward);

    // Бруски топорика в своей плоской системе: x поперёк рукояти, y вдоль неё.
    struct Part { float cx, cy, hx, hy, hz; Block block; };
    // Пропорции сняты с картинок предметов. Топор: длинное топорище и широкая каменная
    // голова, насаженная сбоку и перевязанная у обуха. Факел: палка с горящим навершием.
    const Part axeParts[] = {
        { 0.000f, -0.105f, 0.0105f, 0.090f, 0.0105f, Block::Wood },   // топорище, низ
        { 0.000f,  0.040f, 0.0095f, 0.062f, 0.0095f, Block::Wood },   // топорище, верх
        { -0.006f, 0.112f, 0.0150f, 0.018f, 0.0135f, Block::Wood },   // перевязка у обуха
        { -0.040f, 0.118f, 0.0360f, 0.030f, 0.0140f, Block::Stone },  // каменная голова
        { -0.082f, 0.120f, 0.0140f, 0.022f, 0.0105f, Block::Stone },  // скошенное лезвие
    };
    const Part torchParts[] = {
        { 0.000f, -0.095f, 0.0100f, 0.100f, 0.0100f, Block::Wood },   // палка, низ
        { 0.000f,  0.035f, 0.0095f, 0.032f, 0.0095f, Block::Wood },   // палка, верх
        { 0.000f,  0.078f, 0.0140f, 0.016f, 0.0140f, Block::Wood },   // обмотка под пламенем
        { 0.000f,  0.104f, 0.0125f, 0.018f, 0.0125f, Block::Sand },   // пламя, ядро
        { 0.000f,  0.130f, 0.0075f, 0.014f, 0.0075f, Block::Sand },   // пламя, язык
    };
    const Part* parts = axe ? axeParts : torchParts;
    const int partCount = 5;
    const float S = 0.86f;
    // Факел держат почти прямо: с наклоном топора пламя уезжает к центру экрана и
    // теряется в пейзаже.
    float angle = (axe ? 0.42f : 0.20f) - swing * 3.2f;
    float ca = cosf(angle), sa = sinf(angle);
    float offX = 0.172f, offZ = 0.52f;
    float offY = (axe ? -0.186f : -0.150f) + bob - swing * 0.35f;

    std::vector<VoxelVertex> verts;
    verts.reserve(4 * 36);
    for(int pi = 0; pi < partCount; ++pi){
        const Part& part = parts[pi];
        float tr, tg, tb;
        blockTextureTint(part.block, tr, tg, tb);
        float layer = (float)blockTextureLayer(part.block);
        // Пламя факела — два верхних бруска: красим их в огонь и чуть качаем яркостью,
        // чтобы оно не выглядело жёлтым кубиком.
        if(torch && pi >= 3){
            float flick = 0.85f + 0.15f * sinf(heldBobPhase_ * 11.0f + (float)pi);
            tr = 1.9f * flick; tg = (pi == 3 ? 1.15f : 1.45f) * flick; tb = 0.35f * flick;
        }
        // Брусок собираем как «кубик» с разными полуразмерами: pushCube даёт куб, а
        // тут нужен вытянутый, поэтому строим грани здесь же по тем же правилам.
        float lx = part.cx * S * ca - part.cy * S * sa + offX;
        float ly = part.cx * S * sa + part.cy * S * ca + offY;
        Vec3 centre{
            eye.x + right.x * lx + up.x * ly + forward.x * offZ,
            eye.y + right.y * lx + up.y * ly + forward.y * offZ,
            eye.z + right.z * lx + up.z * ly + forward.z * offZ
        };
        // Оси бруска в мировых координатах: рукоять повёрнута вместе с топором.
        Vec3 ax{ right.x * ca + up.x * sa, right.y * ca + up.y * sa, right.z * ca + up.z * sa };
        Vec3 ay{ -right.x * sa + up.x * ca, -right.y * sa + up.y * ca, -right.z * sa + up.z * ca };
        float hx = part.hx * S, hy = part.hy * S, hz = part.hz * S;
        static const int SX[6] = { 0, 0, 1, -1, 0, 0 };
        static const int SY[6] = { 1, -1, 0, 0, 0, 0 };
        static const int SZ[6] = { 0, 0, 0, 0, 1, -1 };
        for(int f = 0; f < 6; ++f){
            Vec3 n{ ax.x * SX[f] + ay.x * SY[f] + forward.x * SZ[f],
                    ax.y * SX[f] + ay.y * SY[f] + forward.y * SZ[f],
                    ax.z * SX[f] + ay.z * SY[f] + forward.z * SZ[f] };
            // Два касательных направления грани и их полуразмеры.
            Vec3 t1, t2; float h1, h2;
            if(SY[f] != 0){ t1 = ax; h1 = hx; t2 = forward; h2 = hz; }
            else if(SX[f] != 0){ t1 = ay; h1 = hy; t2 = forward; h2 = hz; }
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
    voxels_->onBlockChanged = [this](int x, int y, int z){ chunks_.markDirty(x, y, z); };
    player_.reset(new Survivor(*voxels_, *env_, inventory_));
    player_->onNodeBroken = [this](Block b, int x, int y, int z){ spawnBreakParticles(b, x, y, z); };

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
    inventory_.add(ItemType::Axe, 1);
    inventory_.add(ItemType::Torch, 1);
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
    slot = clampf(fminf((float)SCR_W, (float)SCR_H) * 0.085f, 44.0f, 96.0f * s);
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
void GameClient::inventorySlotPos(int i, float& sx, float& sy) const {
    float gx, gy, slot, gap;
    inventoryGeometry(gx, gy, slot, gap);
    int col = i % Inventory::COLS, row = i / Inventory::COLS;
    // Пояс — ПОСЛЕДНИЙ ряд и стоит ниже с отступом; основная сетка выше него.
    bool belt = (row == 0);
    int visualRow = belt ? (Inventory::ROWS - 1) : (row - 1);
    sx = gx + col * (slot + gap);
    sy = gy + visualRow * (slot + gap) + (belt ? inventoryBeltGap() : 0.0f);
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
const int CRAFT_COLS = 4;

void GameClient::craftGridGeometry(float& x, float& y, float& tile, float& gap) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    tile = clampf((float)SCR_H * 0.145f, 58.0f, 132.0f * s);
    gap = tile * 0.09f;
    int rows = (kRecipeCount + CRAFT_COLS - 1) / CRAFT_COLS;
    if(rows < 1) rows = 1;
    float gridH = tile * rows + gap * (rows - 1);
    x = 40.0f * s;
    y = ((float)SCR_H - gridH) * 0.5f + 16.0f * s;
}

void GameClient::craftTilePos(int i, float& tx, float& ty) const {
    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    tx = gx + (i % CRAFT_COLS) * (tile + gap);
    ty = gy + (i / CRAFT_COLS) * (tile + gap);
}

void GameClient::craftButtonRect(float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    float gridW = tile * CRAFT_COLS + gap * (CRAFT_COLS - 1);
    // Кнопка стоит ВНУТРИ панели описания, в её правом нижнем углу: снаружи она
    // налезала на пояс быстрого доступа.
    float dx = gx + gridW + 28.0f * s;
    float dw = (float)SCR_W - dx - 24.0f * s;
    w = fminf(dw * 0.45f, 300.0f * s);
    h = 54.0f * s;
    x = dx + dw - w - 16.0f * s;
    y = gy + tile * 2.6f - h - 16.0f * s;
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
        drawText(x, y + h * 0.22f, 30.0f * s, PAUSE_ROWS[i], 1, 1, 1, 0.94f);
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
            case 3: overlay_ = Overlay::None; state_ = GameState::MainMenu; break;
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
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);

    if(overlay_ == Overlay::Craft){
        float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        // Крестик выхода.
        float closeSize = 54.0f * s;
        float gx, gy, tile, gap;
        craftGridGeometry(gx, gy, tile, gap);
        float closeX = (float)SCR_W - closeSize - 24.0f * s, closeY = gy - 52.0f * s;
        if(x >= closeX && x <= closeX + closeSize && y >= closeY && y <= closeY + closeSize){
            overlay_ = Overlay::None;
            return true;
        }
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
    // Крестик выхода: его геометрия повторяет отрисовку.
    {
        float gx, gy, slot, gap;
        inventoryGeometry(gx, gy, slot, gap);
        float sc = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        float w = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
        float closeSize = 54.0f * sc;
        float cx = gx + w - closeSize, cy = gy - 52.0f * sc;
        if(x >= cx && x <= cx + closeSize && y >= cy && y <= cy + closeSize){
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
int GameClient::slotAtPoint(float x, float y) const {
    float gx, gy, slot, gap;
    inventoryGeometry(gx, gy, slot, gap);
    for(int i = 0; i < Inventory::SIZE; ++i){
        float sx, sy;
        inventorySlotPos(i, sx, sy);
        if(x >= sx && x <= sx + slot && y >= sy && y <= sy + slot) return i;
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
                overlay_ = Overlay::None;
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
                overlay_ = Overlay::None;
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
    if(overlay_ == Overlay::Inventory && dragSlot_ >= 0){
        dragPos_ = Vec2{ x, y };
        // Порог в несколько пикселей: случайное дрожание пальца не должно считаться
        // переносом, иначе простое касание ячейки уже «тащит».
        if(!dragActive_ && (fabsf(dx) + fabsf(dy)) > 3.0f) dragActive_ = true;
    }
}

void GameClient::handleOverlayRelease(){
    mapDragging_ = false;


    if(overlay_ == Overlay::Inventory && dragSlot_ >= 0){
        int target = slotAtPoint(dragPos_.x, dragPos_.y);
        if(dragActive_ && target >= 0 && target != dragSlot_){
            inventory_.moveOrSwap(dragSlot_, target);
        } else if(!dragActive_ && dragSlot_ < Inventory::COLS){
            // Короткое касание по ячейке пояса — выбрать её в руку.
            inventory_.select(dragSlot_);
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
            else if(state_ == GameState::Playing) state_ = GameState::MainMenu;
            else running_ = false;
            continue;
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

    if(controls_.inventoryPressed()){ overlay_ = (overlay_ == Overlay::Inventory) ? Overlay::None : Overlay::Inventory; dragSlot_ = -1; }
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
        in.attack = controls_.attackHeld();
        in.place = controls_.placePressed();
        in.action = controls_.actionPressed();
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
    // Падение срубленного дерева и осколки от выработанной жилы.
    voxels_->updateFalling(dt);
    updateParticles(dt);

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
    snprintf(buf, sizeof(buf), "HP %.0f", (double)player_->health());
    drawBar(pad, y, barW, barH, player_->health() / 100.0f, 0.62f, 0.18f, 0.16f, buf); y += barH + gap;
    snprintf(buf, sizeof(buf), "Голод %.0f", (double)player_->hunger());
    drawBar(pad, y, barW, barH, player_->hunger() / 100.0f, 0.55f, 0.40f, 0.14f, buf); y += barH + gap;
    snprintf(buf, sizeof(buf), "Жажда %.0f", (double)player_->thirst());
    drawBar(pad, y, barW, barH, player_->thirst() / 100.0f, 0.16f, 0.38f, 0.58f, buf); y += barH + gap;
    snprintf(buf, sizeof(buf), "Силы %.0f", (double)player_->stamina());
    drawBar(pad, y, barW, barH, player_->stamina() / 100.0f, 0.32f, 0.48f, 0.24f, buf); y += barH + gap;
    // Воздух показываем только когда он тратится: лишняя полоса на экране мешает.
    if(player_->headUnderwater() || player_->oxygen() < 99.5f){
        snprintf(buf, sizeof(buf), "Воздух %.0f", (double)player_->oxygen());
        drawBar(pad, y, barW, barH, player_->oxygen() / 100.0f, 0.30f, 0.62f, 0.72f, buf);
        y += barH + gap;
    }

    Vec3 p = player_->position();
    snprintf(buf, sizeof(buf), "%s | %s | %s", env_->timeString(), weatherName(env_->weather()),
             biomeName(world_->biomeAt(p.x, p.z)));
    drawText(pad, y + 2.0f * s, 19.0f * s, buf, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.9f);

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

    // ---- Прицел и подсказка по блоку
    float cx = (float)SCR_W * 0.5f, cy = (float)SCR_H * 0.5f;
    drawUIRect(cx - 1.5f, cy - 9.0f, 3.0f, 18.0f, 0, 1,1,1, 0.55f, false);
    drawUIRect(cx - 9.0f, cy - 1.5f, 18.0f, 3.0f, 0, 1,1,1, 0.55f, false);

    // Название блока под прицелом не выводится намеренно: подпись висела в центре
    // экрана постоянно и мешала смотреть. Что за блок — видно по цвету, а что удар
    // засчитан — по рамке и полосе добычи.
    if(player_->miningProgress() > 0.01f){
        float w = 160.0f * s;
        drawBar(cx - w * 0.5f, cy + 46.0f * s, w, 12.0f * s, player_->miningProgress(),
                0.75f, 0.65f, 0.25f, "");
    }

    // ---- Пояс быстрого доступа. В открытом инвентаре его не рисуем: у окна свой ряд
    // пояса, и игровой просвечивал сквозь затемнение вторым, притухшим.
    float hx, hy, slot, hgap;
    hotbarGeometry(hx, hy, slot, hgap);
    bool showHotbar = (overlay_ != Overlay::Inventory);
    if(showHotbar){
        for(int i = 0; i < Inventory::HOTBAR; ++i){
            float sx = hx + i * (slot + hgap);
            drawSlot(sx, hy, slot, inventory_.slot(i), i == inventory_.selected());
        }
    }
    const ItemStack& sel = inventory_.selectedStack();
    if(showHotbar && !sel.empty()){
        const char* name = itemDef(sel.type).nameRu;
        drawText(hx, hy - 24.0f * s, 20.0f * s, name, UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.9f);
    }

    // ---- Последнее событие
    if(player_->messageAge() < 4.0f){
        float alpha = clampf(1.0f - (player_->messageAge() - 3.0f), 0.0f, 1.0f);
        drawText(pad, hy - 34.0f * s, 21.0f * s, player_->lastMessage(),
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, alpha);
    }

    if(player_->isDead()){
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.35f, 0.02f, 0.02f, 0.55f, false);
        drawText(cx - 120.0f * s, cy - 40.0f * s, 44.0f * s, "ВЫ ПОГИБЛИ", 0.9f, 0.35f, 0.3f, 1.0f);
        drawText(cx - 190.0f * s, cy + 20.0f * s, 24.0f * s,
                 "Нажмите E, чтобы возродиться", UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 1.0f);
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

        float alpha = b.active ? 0.95f : 0.62f;
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
        return;
    }
    if(overlay_ == Overlay::None) return;
    if(overlay_ == Overlay::Pause){ renderPause(); return; }
    if(overlay_ == Overlay::Settings){ renderSettings(); return; }
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);

    if(overlay_ == Overlay::Inventory){
        float gx, gy, slot, gap;
        inventoryGeometry(gx, gy, slot, gap);
        float w = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
        float mainH = slot * (Inventory::ROWS - 1) + gap * (Inventory::ROWS - 2);
        // Экран инвентаря: затемнение на весь экран, заголовок, крестик выхода, сетка
        // рюкзака и отдельной полосой ниже — пояс. Подсказок нет.
        // Экран не затемняется в глухую: за интерфейсом остаётся видна игра.
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.05f, 0.06f, 0.30f, false);
        drawText(gx, gy - 46.0f * s, 30.0f * s, "ИНВЕНТАРЬ", 1, 1, 1, 0.96f);

        // Крестик выхода — там же, где на карте и в настройках.
        float closeSize = 54.0f * s;
        float closeX = gx + w - closeSize, closeY = gy - 52.0f * s;
        if(texClose_) drawUIRect(closeX, closeY, closeSize, closeSize, texClose_, 1, 1, 1, 0.9f, true);
        else {
            drawUIRect(closeX, closeY, closeSize, closeSize, 0, 0.20f, 0.10f, 0.10f, 0.9f, false);
            drawText(closeX + closeSize * 0.3f, closeY + closeSize * 0.2f, 26.0f * s, "X", 1, 1, 1, 0.95f);
        }

        // Подложки: отдельная у рюкзака, отдельная у пояса — это разные вещи.
        drawUIRect(gx - 4.0f * s, gy - 4.0f * s, w + 8.0f * s, mainH + 8.0f * s, 0,
                   0.85f, 0.83f, 0.79f, 0.20f, false);
        float beltY = gy + mainH + gap + inventoryBeltGap();
        drawUIRect(gx - 4.0f * s, beltY - 4.0f * s, w + 8.0f * s, slot + 8.0f * s, 0,
                   0.95f, 0.88f, 0.60f, 0.22f, false);

        for(int i = 0; i < Inventory::SIZE; ++i){
            float sx, sy;
            inventorySlotPos(i, sx, sy);
            // Пока предмет тащат, его ячейка стоит пустой: он «в руке» у пальца.
            ItemStack shown = (dragActive_ && i == dragSlot_) ? ItemStack{} : inventory_.slot(i);
            drawSlot(sx, sy, slot, shown, i < Inventory::HOTBAR && i == inventory_.selected());
        }
        if(dragActive_ && dragSlot_ >= 0){
            // Предмет под пальцем, со смещением вверх: иначе его закрывает сам палец.
            float dsz = slot * 0.92f;
            drawSlot(dragPos_.x - dsz * 0.5f, dragPos_.y - dsz * 1.15f, dsz,
                     inventory_.slot(dragSlot_), false);
        }
        return;
    }

    if(overlay_ == Overlay::Craft){ renderCraft(); return; }
    renderMap();
}

// ==================== КРАФТ ====================
// Список прокручивается пальцем — как в Rust: рецептов со временем станет несколько
// десятков, и они не влезут ни в один экран телефона.

void GameClient::renderCraft(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Экран крафта: слева квадратные плитки рецептов, справа — описание выбранного.
    // Строчки во всю ширину читались как список настроек, а не как крафт.
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.05f, 0.05f, 0.06f, 0.30f, false);

    float gx, gy, tile, gap;
    craftGridGeometry(gx, gy, tile, gap);
    float gridW = tile * CRAFT_COLS + gap * (CRAFT_COLS - 1);
    drawText(gx, gy - 46.0f * s, 30.0f * s, "КРАФТ", 1, 1, 1, 0.96f);

    float closeSize = 54.0f * s;
    float closeX = (float)SCR_W - closeSize - 24.0f * s, closeY = gy - 52.0f * s;
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
    float dx = gx + gridW + 28.0f * s;
    float dw = (float)SCR_W - dx - 24.0f * s;
    float dy = gy;
    drawUIRect(dx, dy, dw, tile * 2.6f, 0, 0.22f, 0.23f, 0.25f, 0.60f, false);

    // Заголовок описания: название слева, крупный значок предмета справа.
    snprintf(buf, sizeof(buf), "%s x%d", res.nameRu, r.resultCount);
    drawText(dx + 18.0f * s, dy + 16.0f * s, 27.0f * s, buf, 1, 1, 1, 0.96f);
    float bigIcon = tile * 0.9f;
    GLuint resIcon = itemIcon(r.result);
    if(resIcon)
        drawUIRect(dx + dw - bigIcon - 16.0f * s, dy + 12.0f * s, bigIcon, bigIcon,
                   resIcon, 1, 1, 1, 1.0f, true);

    drawText(dx + 18.0f * s, dy + 50.0f * s, 18.0f * s, r.note, 0.90f, 0.90f, 0.88f, 0.95f);

    // Таблица стоимости с шапкой: сколько нужно, чего и сколько есть на руках.
    float ly = dy + 92.0f * s;
    drawUIRect(dx + 14.0f * s, ly - 6.0f * s, dw - 28.0f * s, 26.0f * s, 0,
               0.85f, 0.84f, 0.80f, 0.28f, false);
    drawText(dx + 22.0f * s, ly - 3.0f * s, 16.0f * s, "НУЖНО", 0.15f, 0.15f, 0.16f, 0.95f);
    drawText(dx + 100.0f * s, ly - 3.0f * s, 16.0f * s, "МАТЕРИАЛ", 0.15f, 0.15f, 0.16f, 0.95f);
    drawText(dx + dw - 96.0f * s, ly - 3.0f * s, 16.0f * s, "ЕСТЬ", 0.15f, 0.15f, 0.16f, 0.95f);
    ly += 28.0f * s;
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
    drawText(bx + bw * 0.5f - 46.0f * s, by + bh * 0.28f, 23.0f * s, "СОЗДАТЬ",
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

    // ---- Метки игрока: касание по карте ставит флажок, касание по нему — снимает.
    float markR = clampf(9.0f * s * SDL_powf(mapZoom_, 0.35f), 7.0f * s, 26.0f * s);
    for(const Vec2& m : mapMarks_){
        float mx = toScreenX(m.x), my = toScreenY(m.y);
        if(mx < -markR || mx > (float)SCR_W + markR) continue;
        if(my < -markR || my > (float)SCR_H + markR) continue;
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
    float heading = yaw_ * 180.0f / 3.14159265f;
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

// ==================== ГЛАВНОЕ МЕНЮ ====================
void GameClient::menuButtonRect(int index, float& x, float& y, float& w, float& h) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    w = clampf((float)SCR_W * 0.34f, 260.0f, 460.0f);
    h = 62.0f * s;
    x = ((float)SCR_W - w) * 0.5f;
    y = (float)SCR_H * 0.44f + (float)index * (h + 14.0f * s);
}

void GameClient::renderMainMenu(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Фон меню — своя картинка на весь экран; если её нет, остаётся живой мир под затемнением.
    if(texMenuBg_){
        drawMenuBackground();
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.03f, 0.03f, 0.35f, false);
    } else {
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.03f, 0.03f, 0.40f, false);
    }

    float titleW = clampf((float)SCR_W * 0.5f, 320.0f, 640.0f);
    float titleX = ((float)SCR_W - titleW) * 0.5f;
    drawText(titleX, (float)SCR_H * 0.20f, 62.0f * s, "OSIL SURVIVAL", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
    drawText(titleX, (float)SCR_H * 0.30f, 22.0f * s,
             "Кубическое выживание: ломай, строй, выживай",
             UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.9f);

    const char* labels[3] = { "ИГРАТЬ", "НАСТРОЙКИ", "ВЫХОД" };
    for(int i = 0; i < 3; ++i){
        float x, y, w, h;
        menuButtonRect(i, x, y, w, h);
        drawUIRect(x, y, w, h, 0, UI_BG_PANEL.r, UI_BG_PANEL.g, UI_BG_PANEL.b, 0.9f, false);
        uiDoubleFrame(x, y, w, h, 0.95f);
        float tw = h * 0.42f * 0.55f * (float)strlen(labels[i]);
        drawText(x + w * 0.5f - tw * 0.5f, y + h * 0.26f, h * 0.42f, labels[i],
                 i == 0 ? UI_ACCENT.r : UI_TEXT.r, i == 0 ? UI_ACCENT.g : UI_TEXT.g,
                 i == 0 ? UI_ACCENT.b : UI_TEXT.b, 1.0f);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "сид мира: %s   |   %.0f FPS", WORLD_SEED_TEXT, (double)fps_);
    drawText(titleX, (float)SCR_H - 40.0f * s, 17.0f * s, buf,
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
}

bool GameClient::handleMenuTouch(float x, float y){
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);
    for(int i = 0; i < 3; ++i){
        float bx, by, bw, bh;
        menuButtonRect(i, bx, by, bw, bh);
        if(x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if(i == 0) state_ = GameState::Playing;
        else if(i == 1) overlay_ = Overlay::Settings;
        else running_ = false;
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
        } else if(a == "--dig" && i + 1 < argc){
            digDepth_ = atoi(argv[++i]);
        } else if(a == "--menu"){
            stayInMenu_ = true;    // снять главное меню на скриншот
        } else if(a == "--play"){
            startInGame_ = true;   // пропустить главное меню (отладка)
        } else if(a == "--debug"){
            settings.showDebugInfo = true;
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
    overlay_ = overlayOverride_;
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
        { ItemType::Stone,     "item_stone.png" },
        { ItemType::OreMetal,  "item_iron.png" },
        { ItemType::OreSulfur, "item_sulfur.png" },
        { ItemType::Scrap,     "item_scrap.png" },
        { ItemType::Axe,       "item_axe.png" },
        { ItemType::Torch,     "item_torch.png" },
        { ItemType::Leaves,    "item_leaves.png" },
        { ItemType::Planks,    "item_planks.png" },
        { ItemType::Dirt,      "item_dirt.png" },
        { ItemType::Sand,      "item_sand.png" },
    };
    int itemsLoaded = 0;
    for(const auto& it : itemIcons){
        texItems_[(int)it.type] = loadTextureFromFile(assetPath(it.file).c_str(), nullptr, nullptr);
        if(texItems_[(int)it.type]) ++itemsLoaded;
    }
    SDL_Log("Значки предметов: загружено %d из %d", itemsLoaded,
            (int)(sizeof(itemIcons)/sizeof(itemIcons[0])));
}
