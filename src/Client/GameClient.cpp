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
#include "../Engine/Render/Terrain.h"
#include "../Engine/Render/UIStyle.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
// Сид мира. Одиночный режим играет ту же карту, что и сервер с этим сидом, — так игрок
// может сначала изучить остров в одиночку, а потом зайти на сервер и узнать местность.
const char* WORLD_SEED_TEXT = "osil";

// Дальность прорисовки. На телефоне это главный расход: террейн, деревья и туман.
// 260 м — компромисс, при котором видно соседний холм, но кадр не проседает.
const float VIEW_DISTANCE = 260.0f;

GameClient* g_instance = nullptr;

// Шрифт: своего в репозитории нет (лицензии), поэтому берём системный. На Android это
// Roboto, на настольной Linux — DejaVu. Если ничего не нашлось, игра всё равно
// запускается, просто без надписей.
TTF_Font* openAnyFont(int size){
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

float GameClient::terrainHeightBridge(float x, float z){
    // Мост между движком (ему нужна простая функция высоты) и игрой (у неё карта мира).
    if(!g_instance || !g_instance->world_) return 0.0f;
    return g_instance->world_->heightAt(x, z);
}

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

    // На телефоне окно всегда во весь экран; на настольной машине — окно для отладки.
#ifdef __ANDROID__
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI;
    SCR_W = 1280; SCR_H = 720;
#else
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    SCR_W = 1280; SCR_H = 720;
#endif
    win = SDL_CreateWindow("OSIL Survival", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           SCR_W, SCR_H, flags);
    if(!win){ SDL_Log("SDL_CreateWindow: %s", SDL_GetError()); return false; }

    glCtx = SDL_GL_CreateContext(win);
    if(!glCtx){ SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError()); return false; }
    SDL_GL_GetDrawableSize(win, &SCR_W, &SCR_H);
    SDL_GL_SetSwapInterval(1);

    uiFont = openAnyFont(28);
    return true;
}

bool GameClient::initGraphics(){
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mainProg = linkProgram(mainVS, mainFS);
    uModelLoc     = glGetUniformLocation(mainProg, "uModel");
    uViewLoc      = glGetUniformLocation(mainProg, "uView");
    uProjLoc      = glGetUniformLocation(mainProg, "uProj");
    uNormalMatLoc = glGetUniformLocation(mainProg, "uNormalMat");
    uTexLoc       = glGetUniformLocation(mainProg, "uTex");
    uLightDirLoc  = glGetUniformLocation(mainProg, "uLightDir");
    uTintColorLoc = glGetUniformLocation(mainProg, "uTintColor");
    uUseTextureLoc= glGetUniformLocation(mainProg, "uUseTexture");
    uFogColorLoc  = glGetUniformLocation(mainProg, "uFogColor");
    uFogDensityLoc= glGetUniformLocation(mainProg, "uFogDensity");
    uCamPosLoc    = glGetUniformLocation(mainProg, "uCamPos");
    uOpacityLoc   = glGetUniformLocation(mainProg, "uOpacity");
    uUnlitLoc     = glGetUniformLocation(mainProg, "uUnlit");
    uLightAmountLoc = glGetUniformLocation(mainProg, "uLightAmount");

    skyProg = linkProgram(skyVS, skyFS);
    skyViewLoc = glGetUniformLocation(skyProg, "uView");
    skyProjLoc = glGetUniformLocation(skyProg, "uProj");
    skyTimeLoc = glGetUniformLocation(skyProg, "uTime");
    skySunDirLoc = glGetUniformLocation(skyProg, "uSunDir");
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
    if(!postProgOk_) SDL_Log("postProg не слинковался — сцена пойдёт на экран без цветокоррекции");

    initUIQuad();
    initUICircle();
    skyMesh_ = buildSkyCube(1.0f);

    // Текстура земли необязательна: без неё рельеф красится тинтом биома.
    groundTex_ = loadTextureFromFile("ground.png");
    return true;
}

void GameClient::initWorld(){
    WorldConfig cfg;
    cfg.seed = seedFromString(WORLD_SEED_TEXT);
    // На телефоне шаг сетки 8 м вместо 4: карта высот вчетверо легче и строится вдвое
    // быстрее, а разница в детализации рельефа на глаз незаметна — мелкие неровности всё
    // равно ниже роста игрока. Сервер держит шаг 4 м, потому что по нему считает попадания.
    cfg.heightGridStep = 8.0f;
    cfg.sanitize();

    world_.reset(new World(cfg));
    world_->generate();

    resources_.reset(new ResourceMap(*world_));
    // Полный рассев по всей карте на телефоне не нужен: ResourceMap умеет отдавать
    // содержимое отдельных ячеек, но для добычи и отрисовки нам нужен пространственный
    // индекс, поэтому строим его один раз (около 200 тыс. объектов, ~10 МБ).
    resources_->generate();

    monuments_.reset(new MonumentMap(*world_));
    monuments_->generate();

    env_.reset(new Environment(cfg));
    if(startTimeOverride_ >= 0.0f) env_->setTimeOfDay(startTimeOverride_);
    player_.reset(new Survivor(*world_, *resources_, *env_));

    Rng rng(splitMix64(cfg.seed ^ 0x5350ULL));
    Vec3 spawn = world_->findSpawnPoint(rng);
    for(int i = 0; i < 24 && !monuments_->isSafeSpawn(spawn.x, spawn.z); ++i)
        spawn = world_->findSpawnPoint(rng);
    player_->spawn(spawn);
    yaw_ = 0.0f; pitch_ = -0.1f;

    TerrainStreamConfig tcfg;
    tcfg.worldSize = cfg.size;
    tcfg.chunkSize = 64.0f;
    tcfg.chunkRes  = (settings.quality == Quality::TURBO) ? 12 : 20;
    tcfg.buildsPerFrame = 2;
    terrainInit(tcfg, &GameClient::terrainHeightBridge);

    // Вода — одна плоскость на всю карту: под ней рельеф уходит в океан, а сверху она
    // рисуется полупрозрачной. Отдельная сетка волн — 5-й этап.
    {
        std::vector<Vertex> v;
        float s = cfg.size;
        float y = cfg.waterLevel;
        float uv = s * 0.02f;
        v.push_back({0,y,0, 0,0, 0,1,0});
        v.push_back({s,y,0, uv,0, 0,1,0});
        v.push_back({s,y,s, uv,uv, 0,1,0});
        v.push_back({0,y,0, 0,0, 0,1,0});
        v.push_back({s,y,s, uv,uv, 0,1,0});
        v.push_back({0,y,s, 0,uv, 0,1,0});
        waterMesh_ = uploadMesh(v, 0);
    }

    buildPropModels();
    buildMinimapTexture();
    LOG_INFO("мир клиента готов: сид %llu", (unsigned long long)cfg.seed);
}

void GameClient::buildPropModels(){
    // Модели процедурные: внешних .obj у проекта нет, а деревья и камни нужны уже сейчас.
    // Формы намеренно простые — на экране их сотни, и каждая лишняя тысяча треугольников
    // умножается на это количество.
    auto makeMesh = [](const std::vector<Vertex>& v){ return uploadMesh(v, 0); };

    { // Сосна: узкий ствол и конус кроны
        std::vector<Vertex> trunk, crown;
        appendTaperedVerts(trunk, mat4Translate(Vec3{0, 3.0f, 0}), 0.35f, 0.18f, 3.0f, 8, true);
        appendTaperedVerts(crown, mat4Translate(Vec3{0, 7.5f, 0}), 2.2f, 0.05f, 3.6f, 9, false);
        appendTaperedVerts(crown, mat4Translate(Vec3{0, 5.2f, 0}), 2.9f, 0.9f, 1.8f, 9, false);
        propTree_.base = makeMesh(trunk);
        propTree_.top  = makeMesh(crown);
        propTree_.baseTint = Vec3{0.32f, 0.22f, 0.15f};
        propTree_.topTint  = Vec3{0.13f, 0.33f, 0.16f};
    }
    { // Дуб/берёза: толстый ствол и шар кроны
        std::vector<Vertex> trunk, crown;
        appendTaperedVerts(trunk, mat4Translate(Vec3{0, 2.4f, 0}), 0.5f, 0.35f, 2.4f, 8, true);
        appendEllipsoidVerts(crown, mat4Translate(Vec3{0, 6.2f, 0}), Vec3{3.0f, 2.4f, 3.0f}, 10, 6);
        propOak_.base = makeMesh(trunk);
        propOak_.top  = makeMesh(crown);
        propOak_.baseTint = Vec3{0.38f, 0.28f, 0.18f};
        propOak_.topTint  = Vec3{0.22f, 0.42f, 0.18f};
    }
    { // Сухостой: только ствол с обрубками веток
        std::vector<Vertex> trunk;
        appendTaperedVerts(trunk, mat4Translate(Vec3{0, 2.6f, 0}), 0.4f, 0.12f, 2.6f, 7, true);
        appendBoxXformVerts(trunk, mat4Multiply(mat4Translate(Vec3{0.7f, 4.0f, 0}), mat4RotateZ(0.7f)),
                            Vec3{0.9f, 0.08f, 0.08f});
        propDead_.base = makeMesh(trunk);
        propDead_.top  = Mesh{};
        propDead_.baseTint = Vec3{0.42f, 0.36f, 0.28f};
    }
    { // Валун
        std::vector<Vertex> rock;
        appendEllipsoidVerts(rock, mat4Translate(Vec3{0, 0.7f, 0}), Vec3{1.5f, 1.0f, 1.3f}, 9, 5);
        appendEllipsoidVerts(rock, mat4Translate(Vec3{0.9f, 0.4f, 0.5f}), Vec3{0.7f, 0.5f, 0.6f}, 7, 4);
        propRock_.base = makeMesh(rock);
        propRock_.top = Mesh{};
        propRock_.baseTint = Vec3{0.48f, 0.47f, 0.45f};
    }
    { // Рудная жила: камень с яркими вкраплениями сверху
        std::vector<Vertex> rock, ore;
        appendEllipsoidVerts(rock, mat4Translate(Vec3{0, 0.6f, 0}), Vec3{1.2f, 0.9f, 1.2f}, 9, 5);
        appendSphereVerts(ore, mat4Translate(Vec3{0.2f, 1.3f, 0.1f}), 0.42f, 7, 4);
        propOre_.base = makeMesh(rock);
        propOre_.top  = makeMesh(ore);
        propOre_.baseTint = Vec3{0.42f, 0.40f, 0.38f};
        propOre_.topTint  = Vec3{0.72f, 0.55f, 0.25f};
    }
    { // Куст / ягоды / трава — низкий шар
        std::vector<Vertex> bush;
        appendEllipsoidVerts(bush, mat4Translate(Vec3{0, 0.45f, 0}), Vec3{0.7f, 0.5f, 0.7f}, 8, 4);
        propBush_.base = makeMesh(bush);
        propBush_.top = Mesh{};
        propBush_.baseTint = Vec3{0.25f, 0.40f, 0.20f};
    }
}

void GameClient::buildMinimapTexture(){
    // Мини-карта — заранее посчитанная картинка биомов: строить её каждый кадр из мира
    // нельзя (это сотни тысяч выборок), а один раз при старте — 256x256 и меньше 50 мс.
    const int N = 256;
    std::vector<unsigned char> pixels((size_t)N * N * 4);
    const WorldConfig& cfg = world_->config();
    for(int y = 0; y < N; ++y){
        for(int x = 0; x < N; ++x){
            float wx = ((float)x + 0.5f) / (float)N * cfg.size;
            float wz = ((float)y + 0.5f) / (float)N * cfg.size;
            float h = world_->heightAt(wx, wz);
            Biome b = world_->biomeAt(wx, wz);
            const BiomeInfo& bi = biomeInfo(b);
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

// ==================== ЦИКЛ ====================

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
        // Кнопка «назад» на Android и Escape закрывают открытое окно, а не игру:
        // случайный выход из игры на телефоне — худшее, что может случиться в выживании.
        if(e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE)){
            if(overlay_ != Overlay::None) overlay_ = Overlay::None;
            else running_ = false;
            continue;
        }
        controls_.handleEvent(e);
    }
}

void GameClient::update(float dt){
    animTime_ += dt;

    // Поворот камеры. Чувствительность нормирована на высоту экрана, иначе на планшете
    // тот же жест поворачивает камеру заметно сильнее, чем на телефоне.
    float sens = 0.0045f * settings.lookSensitivity * (720.0f / (float)SCR_H) * 2.0f;
    yaw_   -= controls_.lookDX * sens;
    pitch_ -= controls_.lookDY * sens;
    pitch_ = clampf(pitch_, -1.45f, 1.45f);

    if(controls_.inventoryPressed()) overlay_ = (overlay_ == Overlay::Inventory) ? Overlay::None : Overlay::Inventory;
    if(controls_.craftPressed())     overlay_ = (overlay_ == Overlay::Craft)     ? Overlay::None : Overlay::Craft;
    if(controls_.mapPressed())       overlay_ = (overlay_ == Overlay::Map)       ? Overlay::None : Overlay::Map;

    SurvivorInput in;
    // Пока открыто окно, персонаж стоит: иначе игрок «убегает» в инвентаре.
    if(overlay_ == Overlay::None){
        in.moveX = controls_.moveX();
        in.moveY = controls_.moveY();
        in.sprint = controls_.sprint();
        in.crouch = controls_.crouch();
        in.jump = controls_.jumpPressed();
        in.attack = controls_.attackHeld();
        in.action = controls_.actionPressed();
    }
    in.yaw = yaw_;
    in.pitch = pitch_;

    player_->setAmbientRadiation(monuments_->radiationAt(player_->position().x, player_->position().z));
    player_->update(in, dt);
    env_->tick(dt);

    // Действие у воды — напиться. Полная система жажды (фляги, грязная вода, диарея) —
    // 3-й этап, здесь только сам источник.
    if(in.action){
        Vec3 p = player_->position();
        if(world_->isWater(p.x, p.z) || player_->inWater()) player_->drinkWater();
    }

    // Шаги: звук привязан к скорости, а не к таймеру, иначе бег и шаг звучат одинаково.
    static float stepPhase = 0.0f;
    if(player_->onGround() && player_->speed() > 0.5f){
        stepPhase += dt * player_->speed();
        if(stepPhase > 2.2f){ stepPhase = 0.0f; audioPlayStep(); }
    }

    terrainUpdate(player_->eyePosition(), VIEW_DISTANCE * qualityViewDistanceScale());
    controls_.endFrame();
}

// ==================== ОТРИСОВКА ====================

void GameClient::renderScene(){
    const WorldConfig& cfg = world_->config();
    float renderScale = qualityRenderScale();
    int renderW = (int)((float)SCR_W * renderScale);
    int renderH = (int)((float)SCR_H * renderScale);
    if(renderW < 320) renderW = 320;
    if(renderH < 180) renderH = 180;

    ensureSceneFBO(renderW, renderH);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glViewport(0, 0, renderW, renderH);

    // Цвет неба и тумана ведёт освещённость: в грозу и ночью мир буквально темнеет.
    // lightLevel() держит ночной минимум 0.06, чтобы игра не превращалась в чёрный экран;
    // для картинки этого мало — растягиваем в «яркость сцены», где полдень даёт единицу.
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
    Vec3 forward{ -sinf(yaw_) * cosf(pitch_), sinf(pitch_), -cosf(yaw_) * cosf(pitch_) };
    Mat4 view = mat4LookAt(eye, v3add(eye, forward), Vec3{0,1,0});
    float aspect = (float)renderW / (float)renderH;
    float viewDist = VIEW_DISTANCE * qualityViewDistanceScale();
    Mat4 proj = mat4Perspective(62.0f * 3.14159265f / 180.0f, aspect, 0.15f, viewDist * 1.6f);

    // ---- Небо
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProg);
    glUniformMatrix4fv(skyViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(skyProjLoc, 1, GL_FALSE, proj.m);
    // Время суток передаём в шейдер как позицию по циклу: он сам разворачивает его в
    // положение солнца и цвет зари.
    glUniform1f(skyTimeLoc, animTime_);
    // Направление на солнце: восход на востоке (+X), закат на западе. Высота — из
    // Environment, то есть небо и освещение сцены всегда согласованы между собой.
    float sunAngle = (env_->timeOfDay() - 6.0f) / 12.0f * 3.14159265f;
    Vec3 sunDir = v3norm(Vec3{ cosf(sunAngle), sinf(env_->sunAltitude()), 0.30f });
    glUniform3f(skySunDirLoc, sunDir.x, sunDir.y, sunDir.z);
    glUniform1f(skyLightAmountLoc, clampf(light, 0.0f, 1.0f));
    glBindVertexArray(skyMesh_.vao);
    glDrawArrays(GL_TRIANGLES, 0, skyMesh_.vertexCount);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // ---- Основная программа: рельеф, объекты, вода
    glUseProgram(mainProg);
    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, proj.m);
    // Свет сцены идёт от того же солнца, что нарисовано на небе; ночью источником
    // остаётся луна — направление то же, а сила падает до минимума.
    Vec3 lightDir = sunDir;
    if(lightDir.y < 0.12f) lightDir.y = 0.12f; // солнце под горизонтом не должно светить снизу
    lightDir = v3norm(lightDir);
    glUniform3f(uLightDirLoc, lightDir.x, lightDir.y, lightDir.z);
    glUniform1f(uLightAmountLoc, clampf(light, 0.0f, 1.15f));
    glUniform3f(uFogColorLoc, fog.x, fog.y, fog.z);
    // Плотность тумана растёт в дождь и особенно в туман — это и атмосфера, и способ
    // честно обрезать дальность прорисовки без «выныривающих» из пустоты объектов.
    float fogDensity = 0.0035f;
    if(env_->weather() == Weather::Fog)   fogDensity = 0.016f * (0.5f + env_->weatherIntensity());
    if(env_->weather() == Weather::Rain)  fogDensity = 0.007f;
    if(env_->weather() == Weather::Storm) fogDensity = 0.010f;
    if(env_->weather() == Weather::Snow)  fogDensity = 0.009f;
    glUniform1f(uFogDensityLoc, fogDensity);
    glUniform3f(uCamPosLoc, eye.x, eye.y, eye.z);
    glUniform1f(uOpacityLoc, 1.0f);
    glUniform1f(uUnlitLoc, 0.0f);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Рельеф. Тинт берём у биома под игроком и мягко смешиваем с серым — так пустыня
    // выглядит песчаной, а снежник белым, даже без текстурных карт биомов.
    const BiomeInfo& bi = biomeInfo(world_->biomeAt(eye.x, eye.z));
    Mat4 identity = mat4Identity();
    float nrm[9]; mat4ToMat3(identity, nrm);
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, identity.m);
    glUniformMatrix3fv(uNormalMatLoc, 1, GL_FALSE, nrm);
    // Цвета биомов заданы для КАРТЫ, где важна различимость, а не для трёхмерной сцены:
    // как альбедо они слишком тёмные и земля выглядит почти чёрной. Осветляем их здесь,
    // а не в таблице биомов, чтобы предпросмотр карты остался читаемым.
    glUniform3f(uTintColorLoc,
                clampf(bi.r / 255.0f * 1.85f, 0.0f, 1.0f),
                clampf(bi.g / 255.0f * 1.85f, 0.0f, 1.0f),
                clampf(bi.b / 255.0f * 1.85f, 0.0f, 1.0f));
    if(groundTex_){
        glUniform1i(uUseTextureLoc, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, groundTex_);
        glUniform1i(uTexLoc, 0);
    } else {
        glUniform1i(uUseTextureLoc, 0);
    }
    chunksDrawn_ = terrainRender(eye, viewDist);

    // ---- Объекты мира вокруг игрока
    glUniform1i(uUseTextureLoc, 0);
    propsDrawn_ = 0;
    float propRadius = clampf(viewDist * 0.42f, 60.0f, 130.0f);
    std::vector<const ResourceNode*> nearby = resources_->query(eye.x, eye.z, propRadius);
    // Ограничение сверху: в густом лесу в радиус попадает больше тысячи объектов, а
    // телефон столько вызовов отрисовки за кадр не вывезет. Мелочь отсекаем первой.
    const int MAX_PROPS = (settings.quality == Quality::TURBO) ? 120 : 320;
    for(const ResourceNode* n : nearby){
        if(propsDrawn_ >= MAX_PROPS) break;
        const PropModel* model = nullptr;
        switch(n->kind){
            case ResourceKind::TreePine:  model = &propTree_; break;
            case ResourceKind::TreeOak:
            case ResourceKind::TreeBirch: model = &propOak_; break;
            case ResourceKind::TreeDead:  model = &propDead_; break;
            case ResourceKind::Boulder:
            case ResourceKind::RockCluster:
            case ResourceKind::StoneNode: model = &propRock_; break;
            case ResourceKind::MetalOre:
            case ResourceKind::SulfurOre: model = &propOre_; break;
            default:                      model = &propBush_; break;
        }
        // Мелочь видно только вблизи — иначе кусты съедают весь лимит объектов.
        float dx = n->pos.x - eye.x, dz = n->pos.z - eye.z;
        float dist2 = dx*dx + dz*dz;
        if(model == &propBush_ && dist2 > 45.0f * 45.0f) continue;

        Mat4 m = mat4Multiply(mat4Translate(n->pos),
                              mat4Multiply(mat4RotateY(n->rotationY), mat4Scale(n->scale)));
        float nm[9]; mat4ToMat3(m, nm);
        glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, m.m);
        glUniformMatrix3fv(uNormalMatLoc, 1, GL_FALSE, nm);

        if(model->base.vao){
            glUniform3f(uTintColorLoc, model->baseTint.x, model->baseTint.y, model->baseTint.z);
            glBindVertexArray(model->base.vao);
            glDrawArrays(GL_TRIANGLES, 0, model->base.vertexCount);
        }
        if(model->top.vao){
            glUniform3f(uTintColorLoc, model->topTint.x, model->topTint.y, model->topTint.z);
            glBindVertexArray(model->top.vao);
            glDrawArrays(GL_TRIANGLES, 0, model->top.vertexCount);
        }
        ++propsDrawn_;
    }
    glBindVertexArray(0);

    // ---- Вода: полупрозрачная плоскость поверх всего, что ниже уровня моря
    glDisable(GL_CULL_FACE);
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, identity.m);
    glUniformMatrix3fv(uNormalMatLoc, 1, GL_FALSE, nrm);
    glUniform1i(uUseTextureLoc, 0);
    glUniform3f(uTintColorLoc, 0.16f * light + 0.02f, 0.34f * light + 0.03f, 0.46f * light + 0.05f);
    glUniform1f(uOpacityLoc, 0.72f);
    glDepthMask(GL_FALSE);
    glBindVertexArray(waterMesh_.vao);
    glDrawArrays(GL_TRIANGLES, 0, waterMesh_.vertexCount);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glUniform1f(uOpacityLoc, 1.0f);
    (void)cfg;

    // ---- Перенос сцены на экран
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCR_W, SCR_H);
    glDisable(GL_DEPTH_TEST);
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
    // Текстура FBO хранится «верх вниз» относительно UI-квада — переворачиваем по V.
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

void GameClient::renderHud(){
    glDisable(GL_DEPTH_TEST);
    Mat4 uiProjM = mat4Ortho(0, (float)SCR_W, (float)SCR_H, 0, -1, 1);
    glUseProgram(uiProg);
    glUniformMatrix4fv(uiProjLoc, 1, GL_FALSE, uiProjM.m);

    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float pad = 14.0f * s;
    float barW = 210.0f * s, barH = 22.0f * s, gap = 6.0f * s;

    // ---- Полосы состояния. Порядок сверху вниз — по важности: здоровье, потом то, что
    // это здоровье отнимает.
    float y = pad;
    char buf[96];
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

    // ---- Время, погода, биом — строка под полосами.
    Vec3 p = player_->position();
    snprintf(buf, sizeof(buf), "%s  |  %s  |  %s", env_->timeString(), weatherName(env_->weather()),
             biomeName(world_->biomeAt(p.x, p.z)));
    drawText(pad, y + 2.0f * s, 20.0f * s, buf, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.9f);

    // ---- Ресурсы: то, что заменяет инвентарь до 3-го этапа.
    const Gathered& g = player_->gathered();
    snprintf(buf, sizeof(buf), "Дерево %d   Камень %d   Руда %d   Сера %d   Ткань %d",
             g.wood, g.stone, g.metalOre, g.sulfurOre, g.cloth);
    drawText(pad, y + 26.0f * s, 20.0f * s, buf, UI_GOLD.r, UI_GOLD.g, UI_GOLD.b, 0.9f);

    // ---- Подсказка по цели перед игроком.
    if(!player_->targetName().empty()){
        snprintf(buf, sizeof(buf), "%s — держите УДАР, чтобы добывать", player_->targetName().c_str());
        float tw = 420.0f * s;
        drawText((float)SCR_W * 0.5f - tw * 0.35f, (float)SCR_H * 0.62f, 22.0f * s, buf,
                 UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f);
    }

    // ---- Последнее событие (собрал, упал, напился) — гаснет через 4 секунды.
    if(player_->messageAge() < 4.0f){
        float alpha = clampf(1.0f - (player_->messageAge() - 3.0f), 0.0f, 1.0f);
        drawText(pad, (float)SCR_H - 64.0f * s, 22.0f * s, player_->lastMessage(),
                 UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, alpha);
    }

    // ---- Прицел-точка по центру.
    drawUIRect((float)SCR_W * 0.5f - 2.0f, (float)SCR_H * 0.5f - 2.0f, 4.0f, 4.0f, 0,
               UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.55f, false);

    if(player_->isDead()){
        drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.35f, 0.02f, 0.02f, 0.55f, false);
        drawText((float)SCR_W * 0.5f - 120.0f * s, (float)SCR_H * 0.45f, 44.0f * s, "ВЫ ПОГИБЛИ",
                 0.9f, 0.35f, 0.3f, 1.0f);
        drawText((float)SCR_W * 0.5f - 190.0f * s, (float)SCR_H * 0.55f, 24.0f * s,
                 "Нажмите ДЕЙСТВИЕ (E), чтобы возродиться", UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 1.0f);
    }

    if(settings.showDebugInfo){
        snprintf(buf, sizeof(buf), "%.0f fps | чанков %d/%d | объектов %d | XZ %.0f,%.0f  Y %.1f",
                 (double)fps_, chunksDrawn_, terrainLoadedChunks(), propsDrawn_,
                 (double)p.x, (double)p.z, (double)p.y);
        drawText(pad, (float)SCR_H - 30.0f * s, 18.0f * s, buf, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
    }

    controls_.render();

    // Подписи на кнопках. Без них сенсорное управление превращается в угадайку: игрок не
    // обязан помнить, какой из шести кругов — присед.
    for(const TouchControls::ButtonView& b : controls_.buttonViews()){
        std::string label = b.label;
        float h = b.radius * 0.42f;
        // Приблизительная ширина строки: точная считается по текстуре, но подпись надо
        // отцентрировать ДО её создания, а ошибка в пару пикселей здесь незаметна.
        float w = h * 0.58f * (float)label.size();
        const UIColor& c = b.active ? UI_ACCENT : UI_TEXT_DIM;
        drawText(b.cx - w * 0.5f, b.cy - h * 0.5f, h, label, c.r, c.g, c.b, 0.95f);
    }
}

void GameClient::renderOverlay(){
    if(overlay_ == Overlay::None) return;
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = (float)SCR_W * 0.74f, h = (float)SCR_H * 0.74f;
    float x = ((float)SCR_W - w) * 0.5f, y = ((float)SCR_H - h) * 0.5f;
    uiPanel(x, y, w, h, 0.95f);

    float lineH = 26.0f * s;
    float ty = y + 18.0f * s;
    auto line = [&](const std::string& text, const UIColor& c){
        drawText(x + 22.0f * s, ty, lineH * 0.86f, text, c.r, c.g, c.b, 0.95f);
        ty += lineH;
    };

    const Gathered& g = player_->gathered();
    char buf[160];
    if(overlay_ == Overlay::Inventory){
        line("ИНВЕНТАРЬ", UI_ACCENT);
        snprintf(buf, sizeof(buf), "Дерево: %d", g.wood);        line(buf, UI_TEXT);
        snprintf(buf, sizeof(buf), "Камень: %d", g.stone);       line(buf, UI_TEXT);
        snprintf(buf, sizeof(buf), "Металлическая руда: %d", g.metalOre);  line(buf, UI_TEXT);
        snprintf(buf, sizeof(buf), "Сера: %d", g.sulfurOre);     line(buf, UI_TEXT);
        snprintf(buf, sizeof(buf), "Ткань: %d", g.cloth);        line(buf, UI_TEXT);
        snprintf(buf, sizeof(buf), "Еда: %d", g.food);           line(buf, UI_TEXT);
        ty += lineH * 0.5f;
        line("30 слотов, стаки, одежда и броня — этап 3.", UI_TEXT_DIM);
    } else if(overlay_ == Overlay::Craft){
        line("КРАФТ", UI_ACCENT);
        line("Доступно без верстака:", UI_TEXT_DIM);
        snprintf(buf, sizeof(buf), "Каменный топор — 100 дерева, 50 камня   [%s]",
                 (g.wood >= 100 && g.stone >= 50) ? "хватает" : "не хватает");
        line(buf, (g.wood >= 100 && g.stone >= 50) ? UI_ACCENT : UI_TEXT_DIM);
        snprintf(buf, sizeof(buf), "Каменная кирка — 100 дерева, 100 камня  [%s]",
                 (g.wood >= 100 && g.stone >= 100) ? "хватает" : "не хватает");
        line(buf, (g.wood >= 100 && g.stone >= 100) ? UI_ACCENT : UI_TEXT_DIM);
        snprintf(buf, sizeof(buf), "Спальный мешок — 30 ткани              [%s]",
                 (g.cloth >= 30) ? "хватает" : "не хватает");
        line(buf, (g.cloth >= 30) ? UI_ACCENT : UI_TEXT_DIM);
        ty += lineH * 0.5f;
        line("Очередь крафта, верстаки 1-3 и стол исследований — этап 3.", UI_TEXT_DIM);
    } else if(overlay_ == Overlay::Map){
        line("КАРТА ОСТРОВА", UI_ACCENT);
        float mapSize = h - 90.0f * s;
        float mx = x + (w - mapSize) * 0.5f, my = ty + 6.0f * s;
        drawUIRect(mx, my, mapSize, mapSize, minimapTex_, 1, 1, 1, 1.0f, true);
        uiThinFrame(mx, my, mapSize, mapSize, UI_LINE, 0.9f);
        // Игрок и монументы поверх карты.
        const WorldConfig& cfg = world_->config();
        for(const Monument& m : monuments_->monuments()){
            float px = mx + m.pos.x / cfg.size * mapSize;
            float py = my + m.pos.z / cfg.size * mapSize;
            bool hot = m.radiation > 0.0f;
            drawUICircleOutline(px, py, 6.0f * s, hot ? UI_DANGER.r : UI_TEXT_DIM.r,
                                hot ? UI_DANGER.g : UI_TEXT_DIM.g, hot ? UI_DANGER.b : UI_TEXT_DIM.b, 0.9f, 2.0f);
        }
        Vec3 p = player_->position();
        drawUICircle(mx + p.x / cfg.size * mapSize, my + p.z / cfg.size * mapSize, 5.0f * s,
                     UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0f);
    }
}

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
    drawText((float)SCR_W * 0.5f - 140.0f * s, (float)SCR_H * 0.53f, 24.0f * s, text,
             UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 1.0f);
    SDL_GL_SwapWindow(win);
}

void GameClient::saveScreenshot(const std::string& path){
    std::vector<uint8_t> pixels((size_t)SCR_W * SCR_H * 4);
    glReadPixels(0, 0, SCR_W, SCR_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL отдаёт строки снизу вверх, PNG хранит сверху вниз — переворачиваем.
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
    if(writePng(path, SCR_W, SCR_H, rgb)) SDL_Log("Снимок экрана: %s", path.c_str());
    else                                  SDL_Log("Не удалось записать снимок: %s", path.c_str());
}

void GameClient::render(){
    renderScene();
    renderHud();
    renderOverlay();
    SDL_GL_SwapWindow(win);
}

// ==================== ТОЧКА ВХОДА КЛИЕНТА ====================

int GameClient::run(int argc, char** argv){
    g_instance = this;

    // Ключи командной строки нужны только настольной отладке и проверке в CI: на телефоне
    // их никто не передаёт, и клиент запускается без единого аргумента.
    for(int i = 1; i < argc; ++i){
        std::string a = argv[i];
        if(a == "--screenshot" && i + 1 < argc){
            screenshotPath_ = argv[++i];
            screenshotFrame_ = 90;   // дать миру догрузить чанки вокруг игрока
        } else if(a == "--frames" && i + 1 < argc){
            screenshotFrame_ = atoi(argv[++i]);
        } else if(a == "--time" && i + 1 < argc){
            startTimeOverride_ = (float)atof(argv[++i]);   // проверка вида в разное время суток
        } else if(a == "--debug"){
            settings.showDebugInfo = true;
        }
    }

    if(!initPlatform()) return 1;
    if(!initGraphics()) return 1;
    controls_.layout(SCR_W, SCR_H);

    // Генерация занимает секунды — сначала показываем экран загрузки, иначе Android
    // решает, что приложение зависло, и предлагает его закрыть.
    drawLoadingScreen("Генерация мира 4000x4000 м...");
    initWorld();
    audioApplySettings();

    int64_t last = nowMillis();
    while(running_){
        int64_t now = nowMillis();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        // Ограничение шага: после сворачивания приложения dt может быть в минутах, и без
        // зажима игрок «телепортируется» вперёд на всё это время.
        if(dt > 0.1f) dt = 0.1f;
        if(dt > 0.0f) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;

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

        // Автоматический снимок: сняли кадр — и вышли. Так проверка рендера укладывается
        // в одну команду и не требует ни экрана, ни телефона.
        if(!screenshotPath_.empty() && frameCounter_ >= screenshotFrame_){
            saveScreenshot(screenshotPath_);
            running_ = false;
        }

        // Потолок кадров: на телефоне это прямая экономия батареи и нагрева.
        if(settings.fpsLimit > 0){
            int64_t frameMs = 1000 / settings.fpsLimit;
            int64_t spent = nowMillis() - now;
            if(spent < frameMs) sleepMillis(frameMs - spent);
        }
    }

    saveSettings();
    terrainShutdown();
    audioShutdown();
    if(uiFont) TTF_CloseFont(uiFont);
    TTF_Quit();
    IMG_Quit();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
