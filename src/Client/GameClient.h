#pragma once
// ==================== КЛИЕНТ ИГРЫ (ANDROID, КУБИЧЕСКИЙ МИР) ====================
// Игра кубическая: мир состоит из блоков, их можно ломать и ставить. Движок — тот же
// SDL2 + OpenGL ES 3.0, перенесённый из A.N.O.D.E, но рельеф рисуется не сеткой высот,
// а чанками из видимых граней (см. Engine/Render/VoxelChunks.h).
//
// Клиент строит мир САМ, из сида, по тем же правилам, что и выделенный сервер
// (src/World). Сейчас это одиночный режим; на 2-м этапе добавится сеть, и расчёт
// переедет на сервер — состояние игрока (Survivor), инвентарь и ввод (TouchControls)
// уже разделены именно ради этого.
#include "Inventory.h"
#include "Survivor.h"
#include "TouchControls.h"
#include "../Engine/Render/Mesh.h"
#include "../Engine/Render/UIDraw.h"
#include "../Engine/Render/VoxelChunks.h"
#include "../World/Environment.h"
#include "../World/Monuments.h"
#include "../World/Resources.h"
#include "../World/VoxelWorld.h"
#include "../World/World.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// Какое окно открыто поверх игры.
enum class Overlay { None, Inventory, Craft, Map, Settings };

// Рецепт крафта. Полноценная система с верстаками и очередью — 3-й этап; здесь
// минимум, который делает добытое сырьё полезным уже сейчас.
struct Recipe {
    ItemType result;
    int resultCount;
    ItemType costA; int costACount;
    ItemType costB; int costBCount;   // ItemType::None — второго ингредиента нет
    const char* note;
};

class GameClient {
public:
    int run(int argc, char** argv);

private:
    bool initPlatform();
    bool initGraphics();
    void initWorld();
    void buildMinimapTexture();

    void handleEvents();
    bool handleOverlayTouch(float x, float y);   // true — событие «съедено» окном
    bool handleHotbarTouch(float x, float y);
    bool handleSettingsTouch(float x, float y);
    void update(float dt);

    void render();
    void renderScene();
    void renderBlockHighlight(const Mat4& view, const Mat4& proj, Vec3 camPos);
    void renderHud();
    void renderOverlay();
    void renderSettings();
    void drawLoadingScreen(const char* text);
    void drawText(float x, float y, float height, const std::string& text,
                  float r, float g, float b, float a = 1.0f);
    void drawBar(float x, float y, float w, float h, float value01,
                 float r, float g, float b, const std::string& caption);
    void drawSlot(float x, float y, float size, const ItemStack& stack, bool selected);
    // Геометрия пояса быстрого доступа — одна функция и для отрисовки, и для попаданий
    // пальца, чтобы нарисованное и нажимаемое никогда не разъезжались (приём из A.N.O.D.E).
    void hotbarGeometry(float& x, float& y, float& slot, float& gap) const;
    void inventoryGeometry(float& x, float& y, float& slot, float& gap) const;

    std::unique_ptr<World> world_;
    std::unique_ptr<ResourceMap> resources_;
    std::unique_ptr<MonumentMap> monuments_;
    std::unique_ptr<Environment> env_;
    std::unique_ptr<VoxelWorld> voxels_;
    std::unique_ptr<Survivor> player_;
    VoxelRenderer chunks_;
    Inventory inventory_;

    TouchControls controls_;
    Overlay overlay_ = Overlay::None;
    Overlay overlayOverride_ = Overlay::None;  // ключ --overlay: снять окно на скриншот
    int dragSlot_ = -1;          // выбранный в инвентаре слот (перенос в два касания)

    Mesh skyMesh_{};
    GLuint minimapTex_ = 0;
    GLuint highlightVao_ = 0, highlightVbo_ = 0;

    float yaw_ = 0.0f, pitch_ = 0.0f;
    float animTime_ = 0.0f;
    bool running_ = true;
    bool postProgOk_ = false;

    float fps_ = 0.0f;
    float fpsAccum_ = 0.0f;
    int fpsFrames_ = 0;

    std::string screenshotPath_;
    int screenshotFrame_ = 0;
    int frameCounter_ = 0;
    float startTimeOverride_ = -1.0f;
    float yawOverride_ = -999.0f, pitchOverride_ = -999.0f; // ключи --yaw/--pitch для проверки картинки

    std::map<std::string, TextTexCache> textCache_;
};
