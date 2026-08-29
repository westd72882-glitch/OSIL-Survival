#pragma once
// ==================== КЛИЕНТ ИГРЫ (ANDROID) ====================
// Основная платформа — телефон: SDL2 + OpenGL ES 3.0, тот же движок, что в A.N.O.D.E.
// Клиент строит мир САМ, из сида, по тем же правилам, что и выделенный сервер (см.
// src/World). На 1-м этапе это одиночный режим: мир, ходьба, добыча, метаболизм,
// сутки и погода. На 2-м этапе сюда добавится сетевой слой, и расчёт переедет на сервер,
// а клиент оставит себе предсказание — структура к этому готова: игровое состояние
// (Survivor) уже отделено от ввода (TouchControls) и от отрисовки.
#include "Survivor.h"
#include "TouchControls.h"
#include "../Engine/Render/Mesh.h"
#include "../Engine/Render/UIDraw.h"
#include "../World/Environment.h"
#include "../World/Monuments.h"
#include "../World/Resources.h"
#include "../World/World.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// Модель объекта мира: ствол/основание и крона/верхушка рисуются отдельно, потому что
// у них разный цвет, а тинт в основном шейдере один на вызов отрисовки.
struct PropModel {
    Mesh base;
    Vec3 baseTint{0.35f, 0.26f, 0.18f};
    Mesh top;
    Vec3 topTint{0.20f, 0.42f, 0.18f};
    float scale = 1.0f;
};

// Какое окно открыто поверх игры.
enum class Overlay { None, Inventory, Craft, Map };

class GameClient {
public:
    int run(int argc, char** argv);

private:
    bool initPlatform();
    bool initGraphics();
    void initWorld();
    void buildPropModels();
    void buildMinimapTexture();

    void handleEvents();
    void update(float dt);
    void render();
    void renderScene();
    void renderHud();
    void renderOverlay();
    void drawLoadingScreen(const char* text);
    // Снимок экрана в PNG. Нужен не только игроку: без него единственный способ
    // проверить, что рендер вообще что-то рисует, — собрать APK и посмотреть глазами
    // на телефоне. С ним картинку можно получить и на сборочной машине, без экрана.
    void saveScreenshot(const std::string& path);
    void drawText(float x, float y, float height, const std::string& text,
                  float r, float g, float b, float a = 1.0f);
    void drawBar(float x, float y, float w, float h, float value01,
                 float r, float g, float b, const std::string& caption);

    // Высота рельефа для стримингового террейна движка (см. Terrain.h).
    static float terrainHeightBridge(float x, float z);

    std::unique_ptr<World> world_;
    std::unique_ptr<ResourceMap> resources_;
    std::unique_ptr<MonumentMap> monuments_;
    std::unique_ptr<Environment> env_;
    std::unique_ptr<Survivor> player_;

    TouchControls controls_;
    Overlay overlay_ = Overlay::None;

    Mesh skyMesh_{};
    Mesh waterMesh_{};
    PropModel propTree_, propOak_, propDead_, propRock_, propOre_, propBush_;
    GLuint groundTex_ = 0;
    GLuint minimapTex_ = 0;

    float yaw_ = 0.0f, pitch_ = 0.0f;
    float animTime_ = 0.0f;
    bool running_ = true;
    bool postProgOk_ = false;

    std::string screenshotPath_;   // ключ --screenshot: снять кадр и выйти
    int screenshotFrame_ = 0;      // на каком кадре снимать (миру нужно догрузиться)
    int frameCounter_ = 0;
    float startTimeOverride_ = -1.0f; // ключ --time: с какого часа начать (для проверки картинки)
    int chunksDrawn_ = 0, propsDrawn_ = 0;
    float fps_ = 0.0f;

    // Текстуры текста кэшируются по самой строке: пересоздавать их каждый кадр
    // (а на HUD десяток надписей) — верный способ уронить частоту кадров на телефоне.
    std::map<std::string, TextTexCache> textCache_;
};
