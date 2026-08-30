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
#include <cstring>

namespace {
// Сид мира: одиночная игра идёт на той же карте, что и сервер с этим сидом.
const char* WORLD_SEED_TEXT = "osil";

// Дальность прорисовки задаётся в настройках; уровень качества дополнительно её
// поджимает. В кубическом мире это главный расход: каждый метр — ещё кольцо чанков,
// которое надо собрать и нарисовать.

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
            // Рельефная подсветка: склон, обращённый к «солнцу» карты, светлее. Без неё
            // карта плоская и по ней не читается, где горы, а где равнина.
            float step = cfg.size / (float)N;
            float hx = world_->heightAt(wx + step, wz) - world_->heightAt(wx - step, wz);
            float hz = world_->heightAt(wx, wz + step) - world_->heightAt(wx, wz - step);
            float relief = clampf(0.5f - (hx + hz) * 0.02f, 0.0f, 1.4f);
            float shade = (0.62f + clampf(h / cfg.maxHeight, 0.0f, 1.0f) * 0.45f) * (0.72f + relief * 0.55f);
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

// Геометрия окна крафта: как и везде, одна функция и для отрисовки, и для попаданий.
namespace {
float craftRowH(int screenH){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    return 62.0f * s;
}
void craftPanelRect(int screenW, int screenH, float& x, float& y, float& w, float& h){
    w = clampf((float)screenW * 0.74f, 420.0f, 980.0f);
    h = (float)screenH * 0.78f;
    x = ((float)screenW - w) * 0.5f;
    y = ((float)screenH - h) * 0.5f;
}
} // namespace

// Строки окна настроек считаются одной функцией и для отрисовки, и для попаданий.
namespace {
const int SETTINGS_ROWS = 10;
float settingsRowH(int screenH){
    float s = clampf((float)screenH / 720.0f, 0.7f, 2.2f);
    return 54.0f * s;   // крупнее прежнего: пальцем в строку в 40 пикселей не попасть
}
float settingsTop(int screenH){
    return (float)screenH * 0.5f - (SETTINGS_ROWS * settingsRowH(screenH)) * 0.5f + 10.0f;
}
float settingsRowY(int i, int screenH){
    return settingsTop(screenH) + i * settingsRowH(screenH);
}
float settingsPanelWidth(int screenW){
    return clampf((float)screenW * 0.72f, 420.0f, 900.0f);
}
} // namespace

bool GameClient::handleSettingsTouch(float x, float y){
    float w = settingsPanelWidth(SCR_W);
    float px = ((float)SCR_W - w) * 0.5f;
    float rowH = settingsRowH(SCR_H);
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
                controls_.setEditMode(true);
                overlay_ = Overlay::None;
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
    float px = ((float)SCR_W - w) * 0.5f;
    float top = settingsTop(SCR_H);
    float rowH = settingsRowH(SCR_H);
    uiPanel(px - 22.0f * s, top - 62.0f * s, w + 44.0f * s,
            rowH * SETTINGS_ROWS + 118.0f * s, 0.97f);
    drawText(px, top - 50.0f * s, 30.0f * s, "НАСТРОЙКИ", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);

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
    snprintf(storage[9], 160, "ЗАКРЫТЬ НАСТРОЙКИ");

    for(int i = 0; i < SETTINGS_ROWS; ++i){
        float ry = settingsRowY(i, SCR_H);
        bool action = (i >= 7);
        bool exitRow = (i == SETTINGS_ROWS - 1);
        drawUIRect(px, ry, w, rowH - 6.0f * s, 0,
                   exitRow ? 0.18f : UI_BG_SLOT.r, exitRow ? 0.22f : UI_BG_SLOT.g,
                   exitRow ? 0.18f : UI_BG_SLOT.b, 0.8f, false);
        uiThinFrame(px, ry, w, rowH - 6.0f * s, exitRow ? UI_ACCENT : UI_LINE, exitRow ? 0.9f : 0.5f);
        const UIColor& c = action ? UI_ACCENT : UI_TEXT;
        drawText(px + 18.0f * s, ry + 12.0f * s, 24.0f * s, storage[i], c.r, c.g, c.b, 0.97f);
    }
    drawText(px, top + rowH * SETTINGS_ROWS + 18.0f * s, 17.0f * s,
             "Вертикальная синхронизация выключена — счётчик в углу показывает настоящую скорость",
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.8f);
}

bool GameClient::handleOverlayTouch(float x, float y){
    if(overlay_ == Overlay::Settings) return handleSettingsTouch(x, y);

    if(overlay_ == Overlay::Map){
        float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        float mx, my, size;
        mapViewport(mx, my, size);

        // Кнопки под картой: приблизить, отдалить, вернуться к игроку, закрыть.
        float btn = 46.0f * s;
        float by = my + size + 12.0f * s;
        if(y >= by && y <= by + btn){
            for(int i = 0; i < 4; ++i){
                float bw = (i == 3) ? btn * 3.2f : btn;
                float bx = mx + (i == 3 ? size - bw : (float)i * (btn + 10.0f * s));
                if(x < bx || x > bx + bw) continue;
                if(i == 0) mapZoom_ = clampf(mapZoom_ * 1.6f, 1.0f, 12.0f);
                else if(i == 1) mapZoom_ = clampf(mapZoom_ / 1.6f, 1.0f, 12.0f);
                else if(i == 2){ mapFollowsPlayer_ = true; }
                else overlay_ = Overlay::None;
                return true;
            }
            return true;
        }

        // Касание по самой карте — начало панорамы: карта перестаёт следовать за игроком.
        if(x >= mx && x <= mx + size && y >= my && y <= my + size){
            mapDragging_ = true;
            mapFollowsPlayer_ = false;
            return true;
        }
        overlay_ = Overlay::None;
        return true;
    }

    if(overlay_ == Overlay::Craft){
        float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
        float px, py, w, h;
        craftPanelRect(SCR_W, SCR_H, px, py, w, h);
        if(x < px || x > px + w || y < py || y > py + h){ overlay_ = Overlay::None; return true; }

        // «ЗАКРЫТЬ» в правом верхнем углу панели.
        if(y <= py + 50.0f * s && x >= px + w - 170.0f * s){ overlay_ = Overlay::None; return true; }

        float rowH = craftRowH(SCR_H);
        float listTop = py + 62.0f * s;
        float listH = h - 78.0f * s;
        if(y < listTop || y > listTop + listH) return true;

        craftDragging_ = true;   // палец на списке: возможно, его будут листать

        int index = (int)((y - listTop + craftScroll_) / rowH);
        if(index < 0 || index >= kRecipeCount) return true;
        const Recipe& r = kRecipes[index];
        bool okA = inventory_.countOf(r.costA) >= r.costACount;
        bool okB = (r.costB == ItemType::None) || inventory_.countOf(r.costB) >= r.costBCount;
        if(okA && okB){
            inventory_.remove(r.costA, r.costACount);
            if(r.costB != ItemType::None) inventory_.remove(r.costB, r.costBCount);
            inventory_.add(r.result, r.resultCount);
        }
        return true;
    }

    if(overlay_ != Overlay::Inventory) return false;

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

void GameClient::handleOverlayDrag(float x, float y, float dx, float dy){
    (void)x; (void)y;
    if(overlay_ == Overlay::Map && mapDragging_){
        // Тянем карту «за бумагу»: содержимое едет вслед за пальцем, а не наоборот.
        float mx, my, size;
        mapViewport(mx, my, size);
        float span = world_->config().size / mapZoom_;
        mapCenterX_ -= dx / size * span;
        mapCenterZ_ -= dy / size * span;
        return;
    }
    if(overlay_ == Overlay::Craft && craftDragging_){
        craftScroll_ -= dy;
        return;
    }
}

void GameClient::handleOverlayRelease(){
    mapDragging_ = false;
    craftDragging_ = false;
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
        if(overlay_ != Overlay::None){
            if(down){ handleOverlayTouch(tx, ty); continue; }
            if(motion){ handleOverlayDrag(tx, ty, mdx, mdy); continue; }
            if(up){ handleOverlayRelease(); continue; }
            continue;
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
    float viewDist = viewDistanceMeters();
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

    // Название блока под прицелом не выводится намеренно: подпись висела в центре
    // экрана постоянно и мешала смотреть. Что за блок — видно по цвету, а что удар
    // засчитан — по рамке и полосе добычи.
    if(player_->miningProgress() > 0.01f){
        float w = 160.0f * s;
        drawBar(cx - w * 0.5f, cy + 46.0f * s, w, 12.0f * s, player_->miningProgress(),
                0.75f, 0.65f, 0.25f, "");
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

    renderCompass();

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

    if(overlay_ == Overlay::Craft){ renderCraft(); return; }
    renderMap();
}

// ==================== КРАФТ ====================
// Список прокручивается пальцем — как в Rust: рецептов со временем станет несколько
// десятков, и они не влезут ни в один экран телефона.

void GameClient::renderCraft(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float px, py, w, h;
    craftPanelRect(SCR_W, SCR_H, px, py, w, h);
    uiPanel(px, py, w, h, 0.97f);
    drawText(px + 22.0f * s, py + 16.0f * s, 28.0f * s, "КРАФТ", UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
    drawText(px + w - 150.0f * s, py + 20.0f * s, 20.0f * s, "ЗАКРЫТЬ",
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.9f);

    float rowH = craftRowH(SCR_H);
    float listTop = py + 62.0f * s;
    float listH = h - 78.0f * s;
    float contentH = rowH * (float)kRecipeCount;
    // Зажимаем прокрутку: список не должен уезжать выше первой строки и ниже последней.
    float maxScroll = contentH > listH ? contentH - listH : 0.0f;
    craftScroll_ = clampf(craftScroll_, 0.0f, maxScroll);

    char buf[192];
    for(int i = 0; i < kRecipeCount; ++i){
        float ry = listTop + i * rowH - craftScroll_;
        if(ry + rowH < listTop || ry > listTop + listH) continue;   // строка вне окна списка
        const Recipe& r = kRecipes[i];
        bool ok = inventory_.countOf(r.costA) >= r.costACount &&
                  (r.costB == ItemType::None || inventory_.countOf(r.costB) >= r.costBCount);

        drawUIRect(px + 14.0f * s, ry, w - 28.0f * s, rowH - 8.0f * s, 0,
                   UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, ok ? 0.8f : 0.45f, false);
        uiThinFrame(px + 14.0f * s, ry, w - 28.0f * s, rowH - 8.0f * s, ok ? UI_ACCENT : UI_LINE, ok ? 0.85f : 0.45f);

        const ItemDef& res = itemDef(r.result);
        float icon = rowH - 26.0f * s;
        drawUIRect(px + 26.0f * s, ry + 9.0f * s, icon, icon, 0, res.r, res.g, res.b, ok ? 1.0f : 0.4f, false);

        snprintf(buf, sizeof(buf), "%s x%d", res.nameRu, r.resultCount);
        const UIColor& c = ok ? UI_TEXT : UI_TEXT_DIM;
        drawText(px + 34.0f * s + icon, ry + 8.0f * s, 23.0f * s, buf, c.r, c.g, c.b, ok ? 1.0f : 0.7f);

        snprintf(buf, sizeof(buf), "нужно: %s x%d  (в наличии %d) — %s",
                 itemDef(r.costA).nameRu, r.costACount, inventory_.countOf(r.costA), r.note);
        drawText(px + 34.0f * s + icon, ry + 32.0f * s, 17.0f * s, buf,
                 UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.85f);
    }

    // Полоса прокрутки справа — единственный признак, что список длиннее экрана.
    if(maxScroll > 0.0f){
        float barX = px + w - 12.0f * s;
        drawUIRect(barX, listTop, 5.0f * s, listH, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.6f, false);
        float thumbH = listH * (listH / contentH);
        float thumbY = listTop + (listH - thumbH) * (craftScroll_ / maxScroll);
        drawUIRect(barX, thumbY, 5.0f * s, thumbH, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.85f, false);
    }

    drawText(px + 22.0f * s, py + h - 26.0f * s, 17.0f * s,
             "Проведите пальцем, чтобы листать. Верстаки 1-3 и очередь крафта — этап 3",
             UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.85f);
}

// ==================== КАРТА ====================
// Карта во весь экран, с приближением, панорамой, координатной сеткой A1/B2 и стрелкой,
// показывающей, куда игрок смотрит. Ровно то, чем в Rust пользуются для встреч и меток.

void GameClient::mapViewport(float& x, float& y, float& size) const {
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    // Снизу обязательно оставляем полосу под кнопки приближения — иначе они уезжают
    // за край экрана и картой нельзя управлять.
    float reserveTop = 62.0f * s, reserveBottom = 76.0f * s;
    size = fminf((float)SCR_W * 0.70f, (float)SCR_H - reserveTop - reserveBottom);
    x = ((float)SCR_W - size) * 0.5f;
    y = reserveTop;
}

void GameClient::renderMap(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    const WorldConfig& cfg = world_->config();
    float mx, my, size;
    mapViewport(mx, my, size);

    uiPanel(mx - 18.0f * s, my - 56.0f * s, size + 36.0f * s,
            (float)SCR_H - (my - 56.0f * s) - 8.0f * s, 0.97f);

    // Следование за игроком, пока карту не таскали: открыл — сразу видно себя.
    Vec3 p = player_->position();
    if(mapFollowsPlayer_){ mapCenterX_ = p.x; mapCenterZ_ = p.z; }

    float span = cfg.size / mapZoom_;                 // сколько метров помещается в окно
    float half = span * 0.5f;
    mapCenterX_ = clampf(mapCenterX_, half, cfg.size - half);
    mapCenterZ_ = clampf(mapCenterZ_, half, cfg.size - half);

    float u0 = (mapCenterX_ - half) / cfg.size, u1 = (mapCenterX_ + half) / cfg.size;
    float v0 = (mapCenterZ_ - half) / cfg.size, v1 = (mapCenterZ_ + half) / cfg.size;
    drawUIRectUV(mx, my, size, size, minimapTex_, u0, v0, u1, v1, 1.0f);
    uiThinFrame(mx, my, size, size, UI_LINE, 0.9f);

    // Мир -> экран для текущего окна карты.
    auto toScreenX = [&](float wx){ return mx + (wx - (mapCenterX_ - half)) / span * size; };
    auto toScreenY = [&](float wz){ return my + (wz - (mapCenterZ_ - half)) / span * size; };

    // ---- Координатная сетка. Ячейка 150 м — столбцы буквами, строки цифрами, как в Rust.
    const float CELL = 150.0f;
    int firstCol = (int)floorf((mapCenterX_ - half) / CELL);
    int lastCol  = (int)floorf((mapCenterX_ + half) / CELL);
    int firstRow = (int)floorf((mapCenterZ_ - half) / CELL);
    int lastRow  = (int)floorf((mapCenterZ_ + half) / CELL);
    char label[8];
    for(int c = firstCol; c <= lastCol; ++c){
        float lx = toScreenX((float)c * CELL);
        if(lx < mx || lx > mx + size) continue;
        drawUIRect(lx, my, 1.0f, size, 0, UI_LINE.r, UI_LINE.g, UI_LINE.b, 0.28f, false);
        // Подпись столбца ставим по центру клетки, а не на линии.
        float cellCenter = toScreenX(((float)c + 0.5f) * CELL);
        // На мелком масштабе подписываем через одну: иначе буквы налезают друг на друга.
        int labelStep = (mapZoom_ < 1.5f) ? 2 : 1;
        if(c >= 0 && c < 26 && (c % labelStep) == 0 && cellCenter > mx && cellCenter < mx + size){
            snprintf(label, sizeof(label), "%c", 'A' + c);
            drawText(cellCenter - 6.0f * s, my - 26.0f * s, 21.0f * s, label,
                     UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.95f);
        }
    }
    for(int r = firstRow; r <= lastRow; ++r){
        float ly = toScreenY((float)r * CELL);
        if(ly < my || ly > my + size) continue;
        drawUIRect(mx, ly, size, 1.0f, 0, UI_LINE.r, UI_LINE.g, UI_LINE.b, 0.28f, false);
        float cellCenter = toScreenY(((float)r + 0.5f) * CELL);
        int labelStepRow = (mapZoom_ < 1.5f) ? 2 : 1;
        if(r >= 0 && (r % labelStepRow) == 0 && cellCenter > my && cellCenter < my + size){
            snprintf(label, sizeof(label), "%d", r + 1);
            drawText(mx - 26.0f * s, cellCenter - 10.0f * s, 20.0f * s, label,
                     UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.95f);
        }
    }

    // ---- Монументы
    for(const Monument& m : monuments_->monuments()){
        float ax = toScreenX(m.pos.x), ay = toScreenY(m.pos.z);
        if(ax < mx || ax > mx + size || ay < my || ay > my + size) continue;
        bool hot = m.radiation > 0.0f;
        drawUICircleOutline(ax, ay, 7.0f * s, hot ? UI_DANGER.r : UI_TEXT_DIM.r,
                            hot ? UI_DANGER.g : UI_TEXT_DIM.g, hot ? UI_DANGER.b : UI_TEXT_DIM.b, 0.95f, 2.0f);
        if(mapZoom_ >= 2.0f)
            drawText(ax + 10.0f * s, ay - 9.0f * s, 15.0f * s, m.name,
                     UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 0.85f);
    }

    // ---- Игрок: точка и стрелка направления взгляда. Стрелка собрана из отрезков —
    // поворот в 2D-интерфейсе иначе не выразить, а знать, куда ты смотришь, важнее,
    // чем аккуратность фигуры.
    float ax = toScreenX(p.x), ay = toScreenY(p.z);
    if(ax >= mx && ax <= mx + size && ay >= my && ay <= my + size){
        float dirX = -sinf(yaw_), dirZ = -cosf(yaw_);
        float len = 22.0f * s;
        for(int i = 2; i <= 10; ++i){
            float t = (float)i / 10.0f;
            float px2 = ax + dirX * len * t;
            float py2 = ay + dirZ * len * t;
            float thick = (1.0f - t) * 5.0f * s + 1.5f;
            drawUIRect(px2 - thick * 0.5f, py2 - thick * 0.5f, thick, thick, 0,
                       UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
        }
        drawUICircle(ax, ay, 6.0f * s, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0f);
        drawUICircleOutline(ax, ay, 6.0f * s, 0.05f, 0.05f, 0.05f, 0.9f, 2.0f);
    }

    // ---- Заголовок, координаты, кнопки приближения
    int cellCol = (int)floorf(p.x / CELL);
    int cellRow = (int)floorf(p.z / CELL);
    char header[160];
    snprintf(header, sizeof(header), "КАРТА  |  квадрат %c%d  |  X %.0f  Z %.0f  |  x%.0f",
             (cellCol >= 0 && cellCol < 26) ? ('A' + cellCol) : '?', cellRow + 1,
             (double)p.x, (double)p.z, (double)mapZoom_);
    drawText(mx, my - 44.0f * s, 24.0f * s, header, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);

    float btn = 46.0f * s;
    float by = my + size + 12.0f * s;
    const char* labels[4] = { "+", "-", "Я", "ЗАКРЫТЬ" };
    for(int i = 0; i < 4; ++i){
        float bw = (i == 3) ? btn * 3.2f : btn;
        float bx = mx + (i == 3 ? size - bw : (float)i * (btn + 10.0f * s));
        drawUIRect(bx, by, bw, btn, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.85f, false);
        uiThinFrame(bx, by, bw, btn, UI_LINE, 0.8f);
        float textW = 12.0f * s * (float)strlen(labels[i]) * 0.6f;
        drawText(bx + bw * 0.5f - textW * 0.5f, by + btn * 0.22f, btn * 0.5f, labels[i],
                 UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f);
    }
}

// ==================== КОМПАС ====================
// Полоса курса сверху, как в Rust: по ней договариваются о направлении («иди на 90»),
// а квадрат карты и градусы вместе заменяют полноценную навигацию.
void GameClient::renderCompass(){
    float s = clampf((float)SCR_H / 720.0f, 0.7f, 2.2f);
    float w = clampf((float)SCR_W * 0.52f, 320.0f, 860.0f);
    float h = 40.0f * s;
    float x = ((float)SCR_W - w) * 0.5f;
    float y = 8.0f * s;

    drawUIRect(x, y, w, h, 0, UI_BG_DEEP.r, UI_BG_DEEP.g, UI_BG_DEEP.b, 0.55f, false);
    uiThinFrame(x, y, w, h, UI_LINE, 0.55f);

    // Курс: 0 — север (-Z), растёт по часовой стрелке.
    float heading = yaw_ * 180.0f / 3.14159265f;
    heading = fmodf(heading, 360.0f);
    if(heading < 0.0f) heading += 360.0f;

    const float VISIBLE = 120.0f;   // сколько градусов помещается в полосу
    for(int deg = 0; deg < 360; deg += 15){
        float delta = (float)deg - heading;
        while(delta > 180.0f) delta -= 360.0f;
        while(delta < -180.0f) delta += 360.0f;
        if(fabsf(delta) > VISIBLE * 0.5f) continue;
        float px = x + w * 0.5f + delta / VISIBLE * w;

        bool cardinal = (deg % 90) == 0;
        bool major = (deg % 45) == 0;
        float tickH = cardinal ? h * 0.55f : (major ? h * 0.4f : h * 0.25f);
        drawUIRect(px, y + h - tickH - 2.0f, 2.0f, tickH, 0,
                   UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, cardinal ? 0.95f : 0.5f, false);
        if(major){
            const char* name = "";
            switch(deg){
                case 0: name = "С"; break;   case 45: name = "СВ"; break;
                case 90: name = "В"; break;  case 135: name = "ЮВ"; break;
                case 180: name = "Ю"; break; case 225: name = "ЮЗ"; break;
                case 270: name = "З"; break; case 315: name = "СЗ"; break;
            }
            float tw = 11.0f * s * (float)strlen(name) * 0.5f;
            drawText(px - tw * 0.5f, y + 3.0f * s, 20.0f * s, name,
                     cardinal ? UI_ACCENT.r : UI_TEXT_DIM.r,
                     cardinal ? UI_ACCENT.g : UI_TEXT_DIM.g,
                     cardinal ? UI_ACCENT.b : UI_TEXT_DIM.b, 0.95f);
        }
    }

    // Текущий курс числом — под полосой, и метка ровно по центру.
    drawUIRect(x + w * 0.5f - 1.0f, y, 2.0f, h, 0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f, false);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f°", (double)heading);
    drawText(x + w * 0.5f - 22.0f * s, y + h + 2.0f * s, 22.0f * s, buf,
             UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.95f);
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
    // Фоном идёт живой мир (он уже нарисован), поверх — затемнение, иначе текст теряется.
    drawUIRect(0, 0, (float)SCR_W, (float)SCR_H, 0, 0.02f, 0.03f, 0.03f, 0.40f, false);

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
        }
    }

    if(!initPlatform()) return 1;
    if(!initGraphics()) return 1;
    controls_.layout(SCR_W, SCR_H);

    drawLoadingScreen("Строим кубический мир 4000x4000...");
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
