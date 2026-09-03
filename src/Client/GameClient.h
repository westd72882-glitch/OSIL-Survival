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
#include "../World/Resources.h"
#include "../World/VoxelWorld.h"
#include "../World/World.h"

#include <SDL2/SDL.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Какое окно открыто поверх игры.
enum class Overlay { None, Inventory, Craft, Map, Settings, Pause, Furnace };

// Состояние клиента. Главное меню — не «окно поверх игры», а отдельный режим: мир в нём
// уже построен и медленно вращается фоном, но игрок не управляется и время не идёт.
enum class GameState { MainMenu, Playing };

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
    void loadInterfaceTextures();
    // Метки игрока на карте: касание ставит, касание по метке снимает.
    std::vector<Vec2> mapMarks_;
    Vec2  mapTapStart_{0,0};
    Uint32 mapTapTime_ = 0;
    bool  mapTapValid_ = false;
    void  toggleMapMark(float screenX, float screenY);
    void drawMenuBackground();
    // Частицы от разбитой жилы и модель топора в руке рисуются одним и тем же
    // воксельным шейдером: это те же кубы, только маленькие.
    void spawnBreakParticles(Block block, int x, int y, int z);
    void updateParticles(float dt);
    void renderParticles(const Mat4& view, const Mat4& proj);
    void renderHeldItem(const Mat4& view, const Mat4& proj, Vec3 eye, Vec3 forward, float dt);
    GLuint itemIcon(ItemType t) const;
    void bindBlockTextures();
    // Дальность прорисовки в метрах: настройка игрока, зажатая уровнем качества.
    float viewDistanceMeters() const;

    void handleEvents();
    bool handleOverlayTouch(float x, float y);   // true — событие «съедено» окном
    void handleOverlayDrag(float x, float y, float dx, float dy);
    void handleOverlayRelease();
    bool handleHotbarTouch(float x, float y);
    // Карта обрабатывает касания сама: ей нужны идентификаторы пальцев для щипка.
    bool handleMapEvent(const SDL_Event& e);
    bool handleSettingsTouch(float x, float y);
    bool handleMenuTouch(float x, float y);      // главное меню
    void update(float dt);

    void render();
    void renderScene();
    // Предмет в руке: каменный топорик, собранный из кубов. Рисуется в пространстве
    // камеры, поэтому всегда перед лицом и не проваливается сквозь стены.
    void renderHud();
    void renderHotbar();
    void renderOverlay();
    void renderSettings();
    void renderMap();
    void renderCraft();
    void renderCompass();
    void renderTouchControls();
    void renderMainMenu();
    // Геометрия кнопок главного меню — одна на отрисовку и на попадания пальца.
    void menuButtonRect(int index, float& x, float& y, float& w, float& h) const;
    void mapViewport(float& x, float& y, float& size) const;
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
    std::unique_ptr<Environment> env_;
    std::unique_ptr<VoxelWorld> voxels_;
    std::unique_ptr<Survivor> player_;
    VoxelRenderer chunks_;
    Inventory inventory_;

    TouchControls controls_;
    GameState state_ = GameState::MainMenu;
    Overlay overlay_ = Overlay::None;

    // ---- Карта: приближение и панорама
    float mapZoom_ = 1.0f;            // 1 — весь остров, 8 — крупный план
    float mapCenterX_ = 2000.0f;      // центр обзора карты в метрах
    float mapCenterZ_ = 2000.0f;
    bool  mapFollowsPlayer_ = true;   // пока карту не таскали, она следит за игроком
    bool  mapDragging_ = false;
    // Щипок двумя пальцами: сохраняем позиции пальцев и базу масштаба.
    std::map<SDL_FingerID, Vec2> mapFingers_;
    float pinchBaseDist_ = 0.0f;
    float pinchBaseZoom_ = 1.0f;
    // ---- Крафт: прокрутка списка
    float craftScroll_ = 0.0f;
    bool  craftDragging_ = false;
    int   craftSelected_ = 0;    // какой рецепт открыт в описании справа
    void  craftGridGeometry(float& x, float& y, float& tile, float& gap) const;
    void  craftTilePos(int i, float& tx, float& ty) const;
    void  craftButtonRect(float& x, float& y, float& w, float& h) const;
    void  craftInfoRect(float& x, float& y, float& w, float& h) const;
    float craftPanelWidth() const;
    // Меню паузы: открывается тапом по полосам состояния слева сверху — отдельной
    // кнопки под него на экране нет.
    void  renderPause();
    bool  handlePauseTouch(float x, float y);
    void  pauseRowRect(int i, float& x, float& y, float& w, float& h) const;
    void  statsPanelRect(float& x, float& y, float& w, float& h) const;
    Overlay overlayOverride_ = Overlay::None;  // ключ --overlay: снять окно на скриншот
    int dragSlot_ = -1;          // ячейка, которую тащим пальцем
    Vec2 dragPos_{0,0};          // где сейчас палец: под ним рисуется сам предмет
    bool dragActive_ = false;    // палец действительно тащит, а не просто коснулся
    int  slotAtPoint(float x, float y) const;
    // Окошко предмета: открывается коротким касанием по ячейке в инвентаре.
    int  itemMenuSlot_ = -1;
    void itemMenuRect(float& x, float& y, float& w, float& h) const;
    void itemMenuButtonRect(int i, float& x, float& y, float& w, float& h) const;
    void renderItemMenu();
    // Окно печи: плавка руды за дрова.
    // Печь: в слот кладут руду, она плавится по 5 секунд за штуку, топливо — 2 дерева
    // на каждую. Состояние живёт в клиенте: печей пока одна на игрока.
    struct FurnaceState {
        ItemType ore = ItemType::None;
        int   oreCount = 0;      // сколько ещё осталось переплавить
        float progress = 0.0f;   // 0..1 текущей плавки
        int   done = 0;          // готовые слитки, которые можно забрать
        ItemType result = ItemType::None;
    };
    FurnaceState furnace_;
    void  updateFurnace(float dt);
    void  renderFurnace();
    // ---- Режим стройки. Включается сам, когда в руках план постройки: снизу
    // появляется выбор части (фундамент, стена, потолок, дверь), а справа — кнопка
    // подтверждения. Пока не подтвердил, стоит полупрозрачный призрак.
    enum class BuildPart { Foundation = 0, Wall, Floor, Door, COUNT };
    BuildPart buildPart_ = BuildPart::Foundation;
    bool  buildMode() const;
    void  renderBuildBar();
    bool  handleBuildTouch(float x, float y);
    void  buildPartRect(int i, float& x, float& y, float& w, float& h) const;
    void  buildAcceptRect(float& x, float& y, float& w, float& h) const;
    bool  buildGhostTarget(int& bx, int& by, int& bz) const;
    void  renderBuildGhost(const Mat4& view, const Mat4& proj);
    void  placeBuildPart();
    bool  handleFurnaceTouch(float x, float y);
    void  furnaceButtonRect(int i, float& x, float& y, float& w, float& h) const;
    bool handleItemMenuTouch(float x, float y);
    float inventoryBeltGap() const;
    void inventorySlotPos(int i, float& sx, float& sy) const;

    GLuint skyVao_ = 0;   // пустой VAO для полноэкранного треугольника неба
    GLuint minimapTex_ = 0;
    // Иконки интерфейса. 0 — файла не нашлось, рисуем как раньше.
    GLuint texDig_ = 0, texPlace_ = 0, texInteract_ = 0, texInventory_ = 0;
    GLuint texCraft_ = 0, texMap_ = 0, texSettings_ = 0, texClose_ = 0;
    GLuint texJump_ = 0, texRun_ = 0, texCrouch_ = 0;
    GLuint texBlood_ = 0;
    GLuint texMapMark_ = 0, texDeathMark_ = 0;
    Vec2  deathMark_{0,0};
    bool  deathMarkValid_ = false;
    GLuint texBuild_ = 0, texBuildAccept_ = 0;
    GLuint texCatFoundation_ = 0, texCatFloor_ = 0, texCatDoor_ = 0;
    // Значок на каждый вид предмета: индекс — ItemType. Чему картинки нет, тот
    // рисуется цветом.
    GLuint texItems_[(int)ItemType::COUNT] = {};
    GLuint texJoyBase_ = 0, texJoyStick_ = 0, texPlayerMarker_ = 0, texMenuBg_ = 0;
    int menuBgW_ = 0, menuBgH_ = 0;   // размеры фона: рисуем его без растяжения
    struct Particle { Vec3 pos, vel; float life, size; float r, g, b; float layer; };
    std::vector<Particle> particles_;
    GLuint partVao_ = 0, partVbo_ = 0;
    GLuint heldVao_ = 0, heldVbo_ = 0;
    float  heldBobPhase_ = 0.0f;

    bool debugKit_ = false;   // --debug: выдать план стройки и дерево
    int startSlot_ = -1;
    int forcedW_ = 0, forcedH_ = 0;   // --size: проверка раскладки под экран телефона
    float startX_ = -1.0f, startZ_ = -1.0f;   // --pos: старт в заданной точке карты
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
    bool startInGame_ = false;   // ключ --play: сразу в игру, минуя меню
    bool stayInMenu_ = false;    // ключ --menu: остаться в меню (для снимка)
    int  digDepth_ = 0;          // ключ --dig: выкопать яму рядом (проверка стенок)
    float yawOverride_ = -999.0f, pitchOverride_ = -999.0f; // ключи --yaw/--pitch для проверки картинки

    std::map<std::string, TextTexCache> textCache_;
};
