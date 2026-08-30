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

namespace {
// Сид мира: одиночная игра идёт на той же карте, что и сервер с этим сидом.
const char* WORLD_SEED_TEXT = "osil";

// Дальность прорисовки в метрах (она же в блоках). В кубическом мире это главный
// расход: каждый метр дальности — ещё кольцо чанков, которое надо собрать и нарисовать.
const float VIEW_DISTANCE = 112.0f;

// Рецепты: минимальный набор, чтобы добытое сырьё уже приносило пользу.
const Recipe kRecipes[] = {
    { ItemType::Planks,     4, ItemType::Wood,      1, ItemType::None,  0, "Из дерева" },
    { ItemType::StoneBrick, 1, ItemType::Stone,     2, ItemType::None,  0, "Прочнее досок" },
    { ItemType::MetalFrag,  1, ItemType::OreMetal,  2, ItemType::None,  0, "Переплавка руды" },
    { ItemType::Sulfur,     1, ItemType::OreSulfur, 2, ItemType::None,  0, "Для пороха (этап 4)" },
    { ItemType::Cloth,      2, ItemType::Leaves,    3, ItemType::None,  0, "Из листвы" },
};
const int kRecipeCount = (int)(sizeof(kRecipes)/sizeof(kRecipes[0]));

TTF_Font* openAnyFont(int size){
    // Своего шрифта в репозитории нет (лицензии), берём системный: на Android это
    // Roboto, на настольной Linux — DejaVu. Нет ни одного — игра идёт без надписей.
    const char* candidates[] = {
        "font.ttf", "font.otf",
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
    SCR_W = 1280; SCR_H = 720;
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

    skyProg = linkProgram(skyVS, skyFS);
    skyViewLoc        = glGetUniformLocation(skyProg, "uView");
    skyProjLoc        = glGetUniformLocation(skyProg, "uProj");
    skyTimeLoc        = glGetUniformLocation(skyProg, "uTime");
    skySunDirLoc      = glGetUniformLocation(skyProg, "uSunDir");
    skyLightAmountLoc = glGetUniformLocation(skyProg, "uLightAmount");

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
    skyMesh_ = buildSkyCube(1.0f);

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
    return true;
}

void GameClient::initWorld(){
    WorldConfig cfg;
    cfg.seed = seedFromString(WORLD_SEED_TEXT);
    // Шаг сетки высот 4 м — как на сервере. В кубическом мире это важнее, чем в
    // сглаженном: при шаге 8 м рельеф превращался в широкие плоские террасы, и остров
    // выглядел столом. Лишние 0.3 с генерации того стоят.
    cfg.heightGridStep = 4.0f;
    cfg.sanitize();

    world_.reset(new World(cfg));
    world_->generate();
    resources_.reset(new ResourceMap(*world_));
    resources_->generate();
    monuments_.reset(new MonumentMap(*world_));
    monuments_->generate();
    env_.reset(new Environment(cfg));
    if(startTimeOverride_ >= 0.0f) env_->setTimeOfDay(startTimeOverride_);

    voxels_.reset(new VoxelWorld(*world_, *resources_));
    chunks_.init(voxels_.get());

    player_.reset(new Survivor(*voxels_, *env_, inventory_));
    player_->onBlockChanged = [this](int x, int y, int z){ chunks_.markDirty(x, y, z); };

    Rng rng(splitMix64(cfg.seed ^ 0x5350ULL));
    Vec3 spawn = world_->findSpawnPoint(rng);
    for(int i = 0; i < 24 && !monuments_->isSafeSpawn(spawn.x, spawn.z); ++i)
        spawn = world_->findSpawnPoint(rng);
    player_->spawn(spawn);
    yaw_ = (yawOverride_ > -100.0f) ? yawOverride_ : 0.0f;
    pitch_ = (pitchOverride_ > -100.0f) ? pitchOverride_ : -0.15f;

    // Стартовый набор: пара блоков, чтобы было чем строить с первой минуты.
    inventory_.add(ItemType::Planks, 16);
    inventory_.add(ItemType::Stone, 8);

    buildMinimapTexture();
    LOG_INFO("мир клиента готов: сид %llu, кубический слой включён",
             (unsigned long long)cfg.seed);
}

void GameClient::buildMinimapTexture(){
    const int N = 256;
    std::vector<unsigned char> pixels((size_t)N * N * 4);
    const WorldConfig& cfg = world_->config();
    for(int y = 0; y < N; ++y){
        for(int x = 0; x < N; ++x){
            float wx = ((float)x + 0.5f) / (float)N * cfg.size;
            float wz = ((float)y + 0.5f) / (float)N * cfg.size;
            float h = world_->heightAt(wx, wz);
            const BiomeInfo& bi = biomeInfo(world_->biomeAt(wx, wz));
            float shade = 0.75f + clampf(h / cfg.maxHeight, 0.0f, 1.0f) * 0.5f;
            size_t i = ((size_t)y * N + x) * 4;
            pixels[i+0] = (unsigned char)clampf(bi.r * shade, 0, 255);
            pixels[i+1] = (unsigned char)clampf(bi.g * shade, 0, 255);
            pixels[i+2] = (unsigned char)clampf(bi.b * shade, 0, 255);
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

void GameClient::inventoryGeometry(float& x, float& y, float& slot, float& gap) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    slot = clampf(fminf((float)SCR_W, (float)SCR_H) * 0.095f, 44.0f, 104.0f * s);
    gap = slot * 0.12f;
    float totalW = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
    float totalH = slot * Inventory::ROWS + gap * (Inventory::ROWS - 1);
    x = ((float)SCR_W - totalW) * 0.5f;
    y = ((float)SCR_H - totalH) * 0.5f + 10.0f * s;
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

// Строки окна настроек считаются одной функцией и для отрисовки, и для попаданий.
namespace {
const int SETTINGS_ROWS = 8;
float settingsRowY(int i, int screenH){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    return (float)screenH * 0.5f - (SETTINGS_ROWS * 46.0f * s) * 0.5f + i * 46.0f * s;
}
} // namespace

bool GameClient::handleSettingsTouch(float x, float y){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = (float)SCR_W * 0.62f;
    float px = ((float)SCR_W - w) * 0.5f;
    float rowH = 42.0f * s;
    for(int i = 0; i < SETTINGS_ROWS; ++i){
        float ry = settingsRowY(i, SCR_H);
        if(x < px || x > px + w || y < ry || y > ry + rowH) continue;
        switch(i){
            case 0: { // качество по кругу
                int q = ((int)settings.quality + 1) % 4;
                settings.quality = (Quality)q;
                destroySceneFBO();   // изменился масштаб рендера
                break;
            }
            case 1: { // потолок кадров
                int cur = 0;
                for(int k = 0; k < FPS_LIMIT_OPTION_COUNT; ++k)
                    if(FPS_LIMIT_OPTIONS[k] == settings.fpsLimit) cur = k;
                settings.fpsLimit = FPS_LIMIT_OPTIONS[(cur + 1) % FPS_LIMIT_OPTION_COUNT];
                break;
            }
            case 2: settings.musicOn = !settings.musicOn; audioApplySettings(); break;
            case 3: settings.sfxOn = !settings.sfxOn; break;
            case 4: {
                float next = settings.lookSensitivity + 0.5f;
                if(next > 2.01f) next = 0.5f;
                settings.lookSensitivity = next;
                break;
            }
            case 5: settings.showDebugInfo = !settings.showDebugInfo; break;
            case 6: // редактор раскладки кнопок
                controls_.setEditMode(true);
                overlay_ = Overlay::None;
                break;
            case 7: // сброс раскладки
                controls_.resetLayout();
                break;
        }
        saveSettings();
        return true;
    }
    return true;   // касание внутри окна, но мимо строк — окно не закрываем
}

bool GameClient::handleOverlayTouch(float x, float y){
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);
    if(overlay_ != Overlay::Inventory && overlay_ != Overlay::Craft) return false;

    if(overlay_ == Overlay::Inventory){
        float gx, gy, slot, gap;
        inventoryGeometry(gx, gy, slot, gap);
        for(int i = 0; i < Inventory::SIZE; ++i){
            int col = i % Inventory::COLS, row = i / Inventory::COLS;
            float sx = gx + col * (slot + gap);
            float sy = gy + row * (slot + gap);
            if(x < sx || x > sx + slot || y < sy || y > sy + slot) continue;
            // Перенос в ДВА касания вместо перетаскивания: на телефоне палец закрывает
            // собой ячейку, и классический drag&drop промахивается мимо цели.
            if(dragSlot_ < 0){
                if(!inventory_.slot(i).empty()) dragSlot_ = i;
            } else {
                inventory_.moveOrSwap(dragSlot_, i);
                dragSlot_ = -1;
            }
            return true;
        }
        dragSlot_ = -1;
        return true;
    }

    // Крафт: строки рецептов, нажатие — сделать одну штуку.
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = (float)SCR_W * 0.7f, h = (float)SCR_H * 0.7f;
    float px = ((float)SCR_W - w) * 0.5f, py = ((float)SCR_H - h) * 0.5f;
    float lineH = 44.0f * s;
    float top = py + 60.0f * s;
    for(int i = 0; i < kRecipeCount; ++i){
        float ly = top + i * lineH;
        if(x < px || x > px + w || y < ly || y > ly + lineH) continue;
        const Recipe& r = kRecipes[i];
        bool okA = inventory_.countOf(r.costA) >= r.costACount;
        bool okB = (r.costB == ItemType::None) || inventory_.countOf(r.costB) >= r.costBCount;
        if(okA && okB){
            inventory_.remove(r.costA, r.costACount);
            if(r.costB != ItemType::None) inventory_.remove(r.costB, r.costBCount);
            inventory_.add(r.result, r.resultCount);
        }
        return true;
    }
    return true;
}

void GameClient::handleEvents(){
    SDL_Event e;
    while(SDL_PollEvent(&e)){
        if(e.type == SDL_QUIT){ running_ = false; continue; }
        if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){
            SDL_GL_GetDrawableSize(win, &SCR_W, &SCR_H);
            controls_.layout(SCR_W, SCR_H);
            destroySceneFBO();
            continue;
        }
        // Кнопка «назад» на Android и Escape закрывают окно, а не игру: случайный выход
        // из выживания — худшее, что может случиться на телефоне.
        if(e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE)){
            if(overlay_ != Overlay::None){ overlay_ = Overlay::None; dragSlot_ = -1; }
            else running_ = false;
            continue;
        }
        // Цифры 1-6 выбирают ячейку пояса (отладка на ПК).
        if(e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_6){
            inventory_.select(e.key.keysym.sym - SDLK_1);
            continue;
        }

        // Касания по интерфейсу разбираются ДО управления: иначе нажатие на ячейку
        // пояса заодно дёргало бы камеру.
        float tx = -1, ty = -1;
        if(e.type == SDL_FINGERDOWN){ tx = e.tfinger.x * (float)SCR_W; ty = e.tfinger.y * (float)SCR_H; }
        else if(e.type == SDL_MOUSEBUTTONDOWN){ tx = (float)e.button.x; ty = (float)e.button.y; }
        if(tx >= 0.0f && controls_.editMode()){
            // Кнопка «ГОТОВО» в подсказке редактора: её геометрия повторяет отрисовку.
            float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
            float w = (float)SCR_W * 0.5f;
            float px = ((float)SCR_W - w) * 0.5f, py = 24.0f * s;
            if(tx >= px + 16.0f * s && tx <= px + 166.0f * s &&
               ty >= py + 48.0f * s && ty <= py + 80.0f * s){
                controls_.setEditMode(false);
                continue;
            }
        }
        if(tx >= 0.0f && !controls_.editMode()){
            if(overlay_ != Overlay::None){
                if(handleOverlayTouch(tx, ty)) continue;
                // Касание мимо панели закрывает окно.
                overlay_ = Overlay::None; dragSlot_ = -1;
                continue;
            }
            if(handleHotbarTouch(tx, ty)) continue;
        }

        controls_.handleEvent(e);
    }
}

void GameClient::update(float dt){
    animTime_ += dt;

    float sens = 0.0045f * settings.lookSensitivity * (720.0f / (float)SCR_H) * 2.0f;
    yaw_   -= controls_.lookDX * sens;
    pitch_ -= controls_.lookDY * sens;
    pitch_ = clampf(pitch_, -1.50f, 1.50f);

    if(controls_.inventoryPressed()){ overlay_ = (overlay_ == Overlay::Inventory) ? Overlay::None : Overlay::Inventory; dragSlot_ = -1; }
    if(controls_.craftPressed())     overlay_ = (overlay_ == Overlay::Craft)     ? Overlay::None : Overlay::Craft;
    if(controls_.mapPressed())       overlay_ = (overlay_ == Overlay::Map)       ? Overlay::None : Overlay::Map;
    if(controls_.settingsPressed())  overlay_ = (overlay_ == Overlay::Settings)  ? Overlay::None : Overlay::Settings;

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

    player_->setAmbientRadiation(monuments_->radiationAt(player_->position().x, player_->position().z));
    player_->update(in, dt);
    env_->tick(dt);

    static float stepPhase = 0.0f;
    if(player_->onGround() && player_->speed() > 0.5f){
        stepPhase += dt * player_->speed();
        if(stepPhase > 2.2f){ stepPhase = 0.0f; audioPlayStep(); }
    }

    // Бюджет постройки чанков: при беге разрешаем больше, стоя на месте — меньше;
    // так мир успевает за игроком, но не тратит время впустую.
    float viewDist = VIEW_DISTANCE * qualityViewDistanceScale();
    int budget = (player_->speed() > 3.0f) ? 3 : 2;
    chunks_.update(player_->eyePosition(), viewDist, budget);
    controls_.endFrame();
}

// ==================== ОТРИСОВКА СЦЕНЫ ====================

void GameClient::renderBlockHighlight(const Mat4& view, const Mat4& proj, Vec3 camPos){
    const RayHit& t = player_->target();
    if(!t.hit) return;

    // Рамка ровно по граням блока, чуть наружу — иначе линии тонут в самой грани
    // (z-fighting) и мерцают при движении головы.
    const float e = 0.004f;
    float x0 = (float)t.x - e, y0 = (float)t.y - e, z0 = (float)t.z - e;
    float x1 = (float)t.x + 1.0f + e, y1 = (float)t.y + 1.0f + e, z1 = (float)t.z + 1.0f + e;

    // Цвет зависит от прогресса добычи: чёрная рамка светлеет к белой, когда блок
    // вот-вот развалится — это единственная подсказка игроку, что удар засчитан.
    float p = player_->miningProgress();
    float c = 0.05f + p * 0.9f;

    VoxelVertex v[24];
    int n = 0;
    auto line = [&](float ax, float ay, float az, float bx, float by, float bz){
        v[n++] = VoxelVertex{ ax, ay, az, 0,1,0, c, c, c };
        v[n++] = VoxelVertex{ bx, by, bz, 0,1,0, c, c, c };
    };
    line(x0,y0,z0, x1,y0,z0); line(x1,y0,z0, x1,y0,z1); line(x1,y0,z1, x0,y0,z1); line(x0,y0,z1, x0,y0,z0);
    line(x0,y1,z0, x1,y1,z0); line(x1,y1,z0, x1,y1,z1); line(x1,y1,z1, x0,y1,z1); line(x0,y1,z1, x0,y1,z0);
    line(x0,y0,z0, x0,y1,z0); line(x1,y0,z0, x1,y1,z0); line(x1,y0,z1, x1,y1,z1); line(x0,y0,z1, x0,y1,z1);

    glUseProgram(voxelProg);
    glUniformMatrix4fv(voxelViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(voxelProjLoc, 1, GL_FALSE, proj.m);
    glUniform3f(voxelCamPosLoc, camPos.x, camPos.y, camPos.z);
    glUniform1f(voxelAlphaLoc, 1.0f);
    glUniform1f(voxelFogDensityLoc, 0.0f);   // рамку туман не съедает
    glUniform1f(voxelLightAmountLoc, 1.2f);
    glBindVertexArray(highlightVao_);
    glBindBuffer(GL_ARRAY_BUFFER, highlightVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
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
    Vec3 forward = player_->lookDirection();
    Mat4 view = mat4LookAt(eye, v3add(eye, forward), Vec3{0,1,0});
    float aspect = (float)renderW / (float)renderH;
    float viewDist = VIEW_DISTANCE * qualityViewDistanceScale();
    Mat4 proj = mat4Perspective(70.0f * 3.14159265f / 180.0f, aspect, 0.1f, viewDist * 2.2f);

    // ---- Небо (без записи глубины, всегда позади всего)
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProg);
    glUniformMatrix4fv(skyViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(skyProjLoc, 1, GL_FALSE, proj.m);
    glUniform1f(skyTimeLoc, animTime_);
    float sunAngle = (env_->timeOfDay() - 6.0f) / 12.0f * 3.14159265f;
    Vec3 sunDir = v3norm(Vec3{ cosf(sunAngle), sinf(env_->sunAltitude()), 0.30f });
    glUniform3f(skySunDirLoc, sunDir.x, sunDir.y, sunDir.z);
    glUniform1f(skyLightAmountLoc, clampf(light, 0.0f, 1.0f));
    glBindVertexArray(skyMesh_.vao);
    glDrawArrays(GL_TRIANGLES, 0, skyMesh_.vertexCount);
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
    chunks_.renderOpaque(view, proj, eye, viewDist);

    // ---- Рамка блока под прицелом
    renderBlockHighlight(view, proj, eye);

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
    glUniform1f(voxelAlphaLoc, 0.72f);
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

void GameClient::drawSlot(float x, float y, float size, const ItemStack& stack, bool selected){
    drawUIRect(x, y, size, size, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.82f, false);
    if(!stack.empty()){
        const ItemDef& def = itemDef(stack.type);
        // Иконка предмета — квадрат цвета блока: текстур у игры нет, а цвет однозначно
        // читается, потому что тот же цвет у блока в мире.
        float pad = size * 0.16f;
        drawUIRect(x + pad, y + pad, size - pad*2.0f, size - pad*2.0f, 0, def.r, def.g, def.b, 1.0f, false);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", stack.count);
        drawText(x + size * 0.06f, y + size * 0.62f, size * 0.30f, buf, 1.0f, 1.0f, 1.0f, 0.95f);
    }
    uiThinFrame(x, y, size, size, selected ? UI_ACCENT : UI_LINE, selected ? 1.0f : 0.7f);
    if(selected) uiThinFrame(x - 2.0f, y - 2.0f, size + 4.0f, size + 4.0f, UI_ACCENT, 0.9f);
}

void GameClient::renderHud(){
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
    if(player_->radiation() > 0.5f){
        snprintf(buf, sizeof(buf), "РАДИАЦИЯ %.0f", (double)player_->radiation());
        drawBar(pad, y, barW, barH, player_->radiation() / 100.0f, 0.62f, 0.58f, 0.12f, buf);
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
    snprintf(buf, sizeof(buf), "чанков %d/%d  граней %d", st.chunksDrawn, st.chunksLoaded, st.trianglesDrawn / 2);
    drawText((float)SCR_W - 240.0f * s, pad + 28.0f * s, 16.0f * s, buf,
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.75f);

    // ---- Прицел и подсказка по блоку
    float cx = (float)SCR_W * 0.5f, cy = (float)SCR_H * 0.5f;
    drawUIRect(cx - 1.5f, cy - 9.0f, 3.0f, 18.0f, 0, 1,1,1, 0.55f, false);
    drawUIRect(cx - 9.0f, cy - 1.5f, 18.0f, 3.0f, 0, 1,1,1, 0.55f, false);

    const RayHit& t = player_->target();
    if(t.hit){
        snprintf(buf, sizeof(buf), "%s", blockName(t.block));
        drawText(cx - 40.0f * s, cy + 30.0f * s, 20.0f * s, buf, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.9f);
        if(player_->miningProgress() > 0.01f){
            float w = 160.0f * s;
            drawBar(cx - w * 0.5f, cy + 56.0f * s, w, 12.0f * s, player_->miningProgress(),
                    0.75f, 0.65f, 0.25f, "");
        }
    }

    // ---- Пояс быстрого доступа
    float hx, hy, slot, hgap;
    hotbarGeometry(hx, hy, slot, hgap);
    for(int i = 0; i < Inventory::HOTBAR; ++i){
        float sx = hx + i * (slot + hgap);
        drawSlot(sx, hy, slot, inventory_.slot(i), i == inventory_.selected());
    }
    const ItemStack& sel = inventory_.selectedStack();
    if(!sel.empty()){
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

    controls_.render();
    for(const TouchControls::ButtonView& b : controls_.buttonViews()){
        std::string label = b.label;
        // Подпись подгоняется под кнопку: в русском тексте буква занимает примерно 0.55
        // высоты, и длинное слово («СТАВИТЬ») на маленькой кнопке иначе вылезает наружу.
        float h = b.radius * 0.34f;
        float charW = 0.55f;
        float maxW = b.radius * 1.7f;
        float w = h * charW * (float)label.size();
        if(w > maxW){ h *= maxW / w; w = maxW; }
        const UIColor& c = b.active ? UI_ACCENT : UI_TEXT_DIM;
        drawText(b.cx - w * 0.5f, b.cy - h * 0.5f, h, label, c.r, c.g, c.b, 0.95f);
    }
}

void GameClient::renderSettings(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = (float)SCR_W * 0.62f;
    float px = ((float)SCR_W - w) * 0.5f;
    float top = settingsRowY(0, SCR_H);
    float rowH = 42.0f * s;
    uiPanel(px - 18.0f * s, top - 56.0f * s, w + 36.0f * s,
            rowH * SETTINGS_ROWS + 118.0f * s, 0.96f);
    drawText(px, top - 44.0f * s, 26.0f * s, "НАСТРОЙКИ", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);

    char buf[160];
    const char* fpsText = settings.fpsLimit > 0 ? nullptr : "без ограничения";
    char fpsBuf[32];
    if(!fpsText){ snprintf(fpsBuf, sizeof(fpsBuf), "%d", settings.fpsLimit); fpsText = fpsBuf; }

    const char* rows[SETTINGS_ROWS];
    char storage[SETTINGS_ROWS][160];
    snprintf(storage[0], 160, "Качество графики: %s", qualityLabel());
    snprintf(storage[1], 160, "Потолок кадров: %s", fpsText);
    snprintf(storage[2], 160, "Музыка: %s", settings.musicOn ? "вкл" : "выкл");
    snprintf(storage[3], 160, "Звуки: %s", settings.sfxOn ? "вкл" : "выкл");
    snprintf(storage[4], 160, "Чувствительность обзора: %.1f", (double)settings.lookSensitivity);
    snprintf(storage[5], 160, "Отладочная строка: %s", settings.showDebugInfo ? "вкл" : "выкл");
    snprintf(storage[6], 160, "Расставить кнопки под свою руку");
    snprintf(storage[7], 160, "Сбросить раскладку кнопок");
    for(int i = 0; i < SETTINGS_ROWS; ++i) rows[i] = storage[i];

    for(int i = 0; i < SETTINGS_ROWS; ++i){
        float ry = settingsRowY(i, SCR_H);
        bool action = (i >= 6);
        drawUIRect(px, ry, w, rowH - 6.0f * s, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.75f, false);
        const UIColor& c = action ? UI_ACCENT : UI_TEXT;
        drawText(px + 14.0f * s, ry + 8.0f * s, 21.0f * s, rows[i], c.r, c.g, c.b, 0.95f);
    }
    snprintf(buf, sizeof(buf), "Вертикальная синхронизация выключена — счётчик в углу показывает настоящую скорость");
    drawText(px, top + rowH * SETTINGS_ROWS + 16.0f * s, 16.0f * s, buf,
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
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
    if(overlay_ == Overlay::Settings){ renderSettings(); return; }
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    char buf[160];

    if(overlay_ == Overlay::Inventory){
        float gx, gy, slot, gap;
        inventoryGeometry(gx, gy, slot, gap);
        float w = slot * Inventory::COLS + gap * (Inventory::COLS - 1);
        float h = slot * Inventory::ROWS + gap * (Inventory::ROWS - 1);
        uiPanel(gx - 20.0f * s, gy - 56.0f * s, w + 40.0f * s, h + 116.0f * s, 0.96f);
        drawText(gx, gy - 46.0f * s, 25.0f * s, "ИНВЕНТАРЬ — 30 слотов", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
        for(int i = 0; i < Inventory::SIZE; ++i){
            int col = i % Inventory::COLS, row = i / Inventory::COLS;
            float sx = gx + col * (slot + gap);
            float sy = gy + row * (slot + gap);
            drawSlot(sx, sy, slot, inventory_.slot(i), i == dragSlot_ || (row == 0 && col == inventory_.selected()));
        }
        drawText(gx, gy + h + 14.0f * s, 18.0f * s,
                 dragSlot_ >= 0 ? "Коснитесь второй ячейки, чтобы перенести"
                                : "Первый ряд — пояс быстрого доступа; коснитесь ячейки для переноса",
                 UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.85f);
        return;
    }

    if(overlay_ == Overlay::Craft){
        float w = (float)SCR_W * 0.7f, h = (float)SCR_H * 0.7f;
        float px = ((float)SCR_W - w) * 0.5f, py = ((float)SCR_H - h) * 0.5f;
        uiPanel(px, py, w, h, 0.96f);
        drawText(px + 20.0f * s, py + 18.0f * s, 26.0f * s, "КРАФТ", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
        float lineH = 44.0f * s;
        float top = py + 60.0f * s;
        for(int i = 0; i < kRecipeCount; ++i){
            const Recipe& r = kRecipes[i];
            bool ok = inventory_.countOf(r.costA) >= r.costACount &&
                      (r.costB == ItemType::None || inventory_.countOf(r.costB) >= r.costBCount);
            float ly = top + i * lineH;
            drawUIRect(px + 12.0f * s, ly, w - 24.0f * s, lineH - 6.0f * s, 0,
                       UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, ok ? 0.75f : 0.45f, false);
            const ItemDef& res = itemDef(r.result);
            drawUIRect(px + 20.0f * s, ly + 6.0f * s, lineH - 18.0f * s, lineH - 18.0f * s, 0,
                       res.r, res.g, res.b, ok ? 1.0f : 0.4f, false);
            snprintf(buf, sizeof(buf), "%s x%d  <-  %s x%d  (%s)",
                     res.nameRu, r.resultCount, itemDef(r.costA).nameRu, r.costACount, r.note);
            const UIColor& c = ok ? UI_TEXT : UI_TEXT_DIM;
            drawText(px + 20.0f * s + lineH, ly + 10.0f * s, 20.0f * s, buf, c.r, c.g, c.b, ok ? 1.0f : 0.7f);
        }
        drawText(px + 20.0f * s, py + h - 34.0f * s, 18.0f * s,
                 "Верстаки 1-3, очередь крафта и стол исследований — этап 3",
                 UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.85f);
        return;
    }

    // Карта
    float w = (float)SCR_W * 0.7f, h = (float)SCR_H * 0.82f;
    float px = ((float)SCR_W - w) * 0.5f, py = ((float)SCR_H - h) * 0.5f;
    uiPanel(px, py, w, h, 0.96f);
    drawText(px + 20.0f * s, py + 14.0f * s, 24.0f * s, "КАРТА ОСТРОВА", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
    float mapSize = h - 70.0f * s;
    float mx = px + (w - mapSize) * 0.5f, my = py + 52.0f * s;
    drawUIRect(mx, my, mapSize, mapSize, minimapTex_, 1, 1, 1, 1.0f, true);
    uiThinFrame(mx, my, mapSize, mapSize, UI_LINE, 0.9f);
    const WorldConfig& cfg = world_->config();
    for(const Monument& m : monuments_->monuments()){
        float ax = mx + m.pos.x / cfg.size * mapSize;
        float ay = my + m.pos.z / cfg.size * mapSize;
        bool hot = m.radiation > 0.0f;
        drawUICircleOutline(ax, ay, 6.0f * s, hot ? UI_DANGER.r : UI_TEXT_DIM.r,
                            hot ? UI_DANGER.g : UI_TEXT_DIM.g, hot ? UI_DANGER.b : UI_TEXT_DIM.b, 0.9f, 2.0f);
    }
    Vec3 p = player_->position();
    drawUICircle(mx + p.x / cfg.size * mapSize, my + p.z / cfg.size * mapSize, 5.0f * s,
                 UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0f);
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
    renderHud();
    renderOverlay();
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
        } else if(a == "--debug"){
            settings.showDebugInfo = true;
        }
    }

    if(!initPlatform()) return 1;
    if(!initGraphics()) return 1;
    controls_.layout(SCR_W, SCR_H);

    drawLoadingScreen("Строим кубический мир 4000x4000...");
    initWorld();
    overlay_ = overlayOverride_;
    audioApplySettings();

    // Первые чанки собираем до входа в игру: иначе игрок появляется в пустоте и падает.
    for(int i = 0; i < 40; ++i)
        chunks_.update(player_->eyePosition(), VIEW_DISTANCE * qualityViewDistanceScale(), 12);

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

        if(player_->isDead() && controls_.actionPressed()){
            Rng rng(splitMix64((uint64_t)nowMillis()));
            Vec3 spawn = world_->findSpawnPoint(rng);
            for(int i = 0; i < 24 && !monuments_->isSafeSpawn(spawn.x, spawn.z); ++i)
                spawn = world_->findSpawnPoint(rng);
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
