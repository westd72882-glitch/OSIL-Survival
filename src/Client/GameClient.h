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
#include "NetClient.h"
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
#include <unordered_map>
#include <vector>

// Какое окно открыто поверх игры.
enum class Overlay { None, Inventory, Craft, Map, Settings, Pause, Furnace, Box, Cupboard };

// Состояние клиента. Главное меню — не «окно поверх игры», а отдельный режим: мир в нём
// уже построен и медленно вращается фоном, но игрок не управляется и время не идёт.
enum class GameState { Auth, MainMenu, Playing };

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
    // ---- Главное меню: браузер серверов. Строка «Одиночная игра» — это локальный мир,
    // всё остальное — адреса серверов, которые игрок добавил сам.
    struct ServerRow {
        std::string address;       // пусто — локальная одиночная игра
        std::string name;
        std::string map = "Survival Island";
        int  players = 0, max = 100, ping = 0;
        int  protocol = 0;          // версия протокола сервера
        bool online = false;
        bool local = false;
    };
    std::vector<ServerRow> servers_;
    int  menuTab_ = 0;             // Сервера / Друзья / Любимые / История
    int  menuSelected_ = 0;
    // ---- Вход в игру. Закрытый бета-тест: ников ровно четыре, они прошиты и в
    // клиенте (чтобы отказ был мгновенным), и на сервере (чтобы его нельзя было обойти).
    std::string authNick_, authPassword_, authNotice_;
    int  authField_ = 0;          // 0 — ник, 1 — пароль
    bool authBusy_ = false;
    bool forceAuthScreen_ = false;   // ключ --auth: показать экран входа на снимке
    bool demoHouse_ = false;         // ключ --demohouse: стенд построек всех трёх уровней
    bool adminMode_ = false;      // ник AdminTester: ресурсы бесконечны
    float adminRefillTimer_ = 0.0f;
    void renderAuth();
    bool handleAuthTouch(float x, float y);
    void authFieldRect(int i, float& x, float& y, float& w, float& h) const;
    void authButtonRect(int i, float& x, float& y, float& w, float& h) const;
    void authSubmit(bool registerNew);
    static bool nickAllowed(const std::string& nick);
    void loadProfile();
    void saveProfile();

    // ---- Монеты и магазин. Валюта закрытого теста: за неё в окне крафта покупают
    // сырьё, чтобы не гриндить одно и то же ради проверки механик.
    int  coins_ = 0;
    bool shopMode_ = false;       // в окне крафта открыт магазин, а не рецепты
    int  shopSelected_ = 0;
    void buySelected();

    bool menuRefreshed_ = false;   // список уже опрашивали в этом запуске
    bool menuAddOpen_ = false;     // открыт ввод адреса
    bool menuEditName_ = false;    // вводится имя игрока
    std::string menuInput_;
    std::string playerName_ = "выживший";
    std::string joinOnStart_;      // ключ --server: войти сразу, минуя меню
    // Ключ --autoattack: бить раз в полторы секунды без нажатий. Нужен, чтобы
    // проверять бой между игроками безголовыми клиентами, где нажать некому.
    float swingDebugPhase_ = 0.0f;   // ключ --swingphase: заморозить фазу удара
    bool  autoAttack_ = false;
    float autoAttackTimer_ = 0.0f;
    std::string menuNotice_;       // короткая строка о результате действия
    float menuNoticeAge_ = 99.0f;
    void loadServerList();
    void saveServerList();
    void refreshServers();
    void menuRowRect(int i, float& x, float& y, float& w, float& h) const;
    void menuTabRect(int i, float& x, float& y, float& w, float& h) const;
    void menuActionRect(int i, float& x, float& y, float& w, float& h) const;
    void menuListArea(float& x, float& y, float& w, float& h) const;
    void startSelectedServer();
    void menuTextInput(const char* text);
    void menuBackspace();

    // ---- Сеть: живые игроки на сервере.
    NetClient net_;
    bool netApplying_ = false;     // применяем чужую правку — обратно её слать не надо
    bool netFelling_ = false;      // валим дерево локально: блоки не шлём, шлём событие
    bool netSilentFell_ = false;   // дерево упало до нашего входа — убираем без анимации
    void netPumpState();
    void netApplyEdits();
    void netApplyEvents();
    void netSendEvent(net::EventType type, int id, int a, int b, Vec3 pos);
    // Кого задел удар: возвращает id игрока перед лицом или 0.
    int  remotePlayerInFront(float reach) const;
    void onSwingImpact();
    // Цифра урона над тем, кого мы задели: без неё попадание было видно только по
    // чужой полоске здоровья, и казалось, что удар не проходит.
    struct DamageMark { int target = 0; int damage = 0; float age = 0.0f; };
    std::vector<DamageMark> damageMarks_;

    // ---- Гранаты. Летит, тикает три секунды, взрывается: постройке минус 50 прочности,
    // живым — до 150 здоровья, чем ближе, тем больше.
    struct Grenade {
        Vec3 pos{}, vel{};
        float fuse = 3.0f;
        float spin = 0.0f;
        // Ракета летит прямо и рвётся о первое препятствие, граната — по дуге и по
        // запалу. Всё остальное у них общее, поэтому это один список, а не два.
        bool  rocket = false;
        int   buildDamage = 50;
    };
    std::vector<Grenade> grenades_;
    void throwGrenade();
    void fireRocket();
    void fireRifle();
    void updateGrenades(float dt);
    void renderGrenades(const Mat4& view, const Mat4& proj);
    // Взрыв в точке: урон постройкам, игрокам и себе. remote — взрыв пришёл по сети,
    // тогда постройки уже посчитал тот, кто бросал.
    void explode(Vec3 at, int maxDamage, bool remote, int buildDamage = 50);
    struct RemoteView {
        int id = 0;
        std::string name;
        Vec3 pos{}, target{};
        // Углы тоже сглаживаются: обмен идёт десять раз в секунду, и без этого чужой
        // игрок «щёлкал» головой на каждом пакете.
        float yaw = 0, pitch = 0, targetYaw = 0, targetPitch = 0;
        float speed = 0, smoothSpeed = 0, swing = 0, phase = 0;
        int   held = 0, pose = 0, health = 100;
        float screenX = 0, screenY = 0;
        bool  onScreen = false;
    };
    std::vector<RemoteView> remote_;
    void updateRemotePlayers(float dt);
    void renderRemotePlayers(const Mat4& view, const Mat4& proj);
    void renderRemoteLabels();
    GLuint remoteVao_ = 0, remoteVbo_ = 0;
    // Крестик выхода из инвентаря: одна геометрия и для отрисовки, и для попадания.
    // Пока их считали в двух местах, кнопка нажималась левее того места, где нарисована.
    void inventoryCloseRect(float& x, float& y, float& w, float& h) const;
    void mapViewport(float& x, float& y, float& size) const;
    void drawLoadingScreen(const char* text);
    // Настоящая ширина строки в пикселях (берётся из готовой текстуры текста): без неё
    // «по центру» считалось по числу байт UTF-8 и всё уезжало влево.
    float textWidth(float height, const std::string& text);
    void drawTextCentered(float cx, float y, float height, const std::string& text,
                          float r = 1, float g = 1, float b = 1, float a = 1);
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
    float craftPanelWidth() const;
    // Разметка панели описания: где начинается текст, где таблица стоимости и какой
    // высоты вся панель. Считается один раз, чтобы значок, описание и таблица не
    // налезали друг на друга.
    void craftPanelGeometry(float& dx, float& dy, float& dw, float& dh,
                            float& notesY, float& tableY) const;
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
    // ---- Постройки игрока. Деталь дома — это ОДИН предмет со своей прочностью, а не
    // россыпь кубов: фундамент 4x4, стена 2 в ширину и 2 в высоту, крыша 4x4, дверь
    // 1x2. Прочность у всего деревянного 100, удар топором снимает единицу.
    struct BuildPiece {
        Block block;
        int x = 0, y = 0, z = 0;      // младший угол детали
        int sx = 1, sy = 1, sz = 1;   // размеры в блоках
        int health = 100;
        bool open = false;            // только для двери
        // Хозяин детали. Дверь открывает только он: иначе в чужой дом заходят как к
        // себе, и весь смысл рейда пропадает.
        std::string owner;
    };
    std::vector<BuildPiece> pieces_;
    static const int PIECE_MAX_HEALTH = 100;
    // Прочность по уровню: дерево 100, камень 200, железо 400.
    static int pieceMaxHealth(int tier){ return tier <= 0 ? 100 : (tier == 1 ? 200 : 400); }
    // Улучшение киянкой: цена одного шага и ресурс, которым платят.
    static const int UPGRADE_COST = 40;
    // Улучшить деталь под прицелом на уровень вверх. Возвращает текст для игрока.
    bool upgradePieceAimed();
    // Снести киянкой свою деталь под прицелом (шкаф для этого не нужен).
    bool demolishPieceAimed();
    // Короткая подсказка в центре экрана: чем ответила киянка, шкаф или дверь.
    std::string buildNotice_;
    float buildNoticeAge_ = 99.0f;
    void notice(const std::string& text){ buildNotice_ = text; buildNoticeAge_ = 0.0f; }
    void pieceFootprint(BuildPart part, Block block, int& sx, int& sy, int& sz) const;
    int  pieceIndexAt(int x, int y, int z) const;
    void fillPieceCells(const BuildPiece& p, bool put);
    void hitBuildPiece(Block block, int x, int y, int z);
    // ---- Чужие постройки. Их строил другой клиент, и в нашем реестре деталей их нет:
    // по сети едут только блоки. Чтобы рейд вообще работал, урон по чужой детали
    // копится по её СВЯЗНОЙ группе клеток — так ракета разбирает чужую стену целиком,
    // а не выгрызает по кубику, и прочность уровня при этом честно учитывается.
    std::unordered_map<long long, int> foreignDamage_;
    // Собирает связную группу одинаковых блоков постройки вокруг клетки (не больше 64).
    // Возвращает false, если клетка принадлежит нашей детали или это не постройка.
    bool foreignGroup(int x, int y, int z, std::vector<long long>& cells, Block& block) const;
    // Урон по чужим постройкам в радиусе. Возвращает, сколько групп задело.
    int  damageForeignBuild(Vec3 at, float radius, int damage);
    // Дверь открывается и закрывается кнопкой «рука». Открытая дверь исчезает из мира,
    // поэтому закрывают её по близости, а не по прицелу — целиться уже не во что.
    bool toggleDoorNear();
    void renderBuildTargetInfo();

    // ---- Шкаф и ящики. Шкаф держит оплату за дом, ящик — просто хранилище.
    static const int BOX_SLOTS = 12;
    struct WorldBox { int x = 0, y = 0, z = 0; ItemStack slots[BOX_SLOTS]; };
    // В шкафу теперь три ресурса: деревянные детали дома съедают дерево, каменные —
    // камень, железные — металл. Иначе улучшенный дом стоил бы столько же, сколько
    // сарай из досок.
    struct WorldCupboard { int x = 0, y = 0, z = 0; int wood = 0, stone = 0, metal = 0; };
    std::vector<WorldBox> boxes_;
    std::vector<WorldCupboard> cupboards_;
    int openBox_ = -1, openCupboard_ = -1;
    int lastUpkeepDay_ = 0;
    void updateUpkeep();
    void renderBox();
    bool handleBoxTouch(float x, float y);
    void boxSlotPos(int i, float& x, float& y, float& slot) const;
    void renderCupboard();
    bool handleCupboardTouch(float x, float y);
    void cupboardButtonRect(int i, float& x, float& y, float& w, float& h) const;
    // Аренда: по 10 единиц за деталь, но каждому уровню — свой ресурс.
    void upkeepPerDay(int& wood, int& stone, int& metal) const;
    int  upkeepPerDay() const { return (int)pieces_.size() * 10; }
    void  renderBuildGhost(const Mat4& view, const Mat4& proj);
    void  placeBuildPart();
    bool  handleFurnaceTouch(float x, float y);
    void  furnaceButtonRect(int i, float& x, float& y, float& w, float& h) const;
    void  furnaceGeometry(float& px, float& py, float& pw, float& ph) const;
    void  furnaceBagSlotPos(int i, float& x, float& y, float& slot) const;
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
    // Индикатор нанесённого урона: крупная цифра со значком в центре экрана.
    GLuint texDamage_ = 0;
    // Вспышка выстрела: сколько секунд назад стреляли из винтовки.
    float  shotFlash_ = 99.0f;
    GLuint texMapMark_ = 0, texDeathMark_ = 0;
    Vec2  deathMark_{0,0};
    bool  deathMarkValid_ = false;
    GLuint texBuild_ = 0, texBuildAccept_ = 0;
    GLuint texCatFoundation_ = 0, texCatFloor_ = 0, texCatDoor_ = 0, texCatWall_ = 0;
    // Метка попадания, значок «открыть» у двери, огонь в печи и значок радиации.
    GLuint texHitMark_ = 0, texOpen_ = 0, texFire_ = 0, texRadiation_ = 0;
    // Маркеры пинга: жёлтый после 200 мс, красный после 300.
    GLuint texPingMid_ = 0, texPingHigh_ = 0;
    float  hitMarkAge_ = 99.0f;   // сколько секунд назад удар попал в цель
    // Значок на каждый вид предмета: индекс — ItemType. Чему картинки нет, тот
    // рисуется цветом.
    GLuint texItems_[(int)ItemType::COUNT] = {};
    GLuint texJoyBase_ = 0, texJoyStick_ = 0, texPlayerMarker_ = 0, texMenuBg_ = 0;
    int menuBgW_ = 0, menuBgH_ = 0;   // размеры фона: рисуем его без растяжения
    // ---- Выброшенные предметы. Выброшенный стак не исчезает: перед игроком падает
    // маленький вращающийся куб с названием и количеством, и его можно поднять обратно
    // кнопкой-рукой. Это ровно то поведение, которое ждут от выживания.
    struct DroppedItem {
        Vec3 pos{};
        float vy = 0.0f;        // падает, пока не встанет на землю
        float spin = 0.0f;      // угол вращения куба
        float age = 0.0f;
        ItemType type = ItemType::None;
        int count = 0;
        int netId = 0;          // метка для сети (0 — дроп только наш, до отправки)
        // Куда куб спроецировался на экран в этом кадре — по этому HUD рисует подпись.
        float screenX = 0.0f, screenY = 0.0f;
        bool  onScreen = false;
    };
    std::vector<DroppedItem> drops_;
    // Метка дропа в сети: у каждого своя нумерация, к ней подмешан номер игрока —
    // так метки не сталкиваются даже без согласования с сервером.
    int nextDropId_ = 1;
    int makeDropId();
    void dropStackToWorld(int slotIndex);
    // Тот же дроп, но пришедший от другого игрока.
    void spawnRemoteDrop(int netId, ItemType type, int count, Vec3 pos);
    void updateDrops(float dt);
    void renderDrops(const Mat4& view, const Mat4& proj);
    void renderDropLabels();
    // Ближайший предмет в радиусе поднятия, или -1.
    int  pickupCandidate() const;
    void pickupButtonRect(float& x, float& y, float& w, float& h) const;
    bool handlePickupTouch(float x, float y);

    // ---- Срубленное дерево. Оно не рассыпается по кубикам на месте: ствол с кроной
    // накреняется, падает целиком и лежит ещё пятнадцать секунд, а потом исчезает.
    struct FallenTree {
        struct Cell { float ox, oy, oz; float r, g, b; float layer; };
        std::vector<Cell> cells;
        Vec3 base{};
        float dirX = 1.0f, dirZ = 0.0f;   // куда валится
        float t = 0.0f;                   // сколько секунд прошло с удара
    };
    std::vector<FallenTree> fallenTrees_;
    void spawnFallenTree(const std::vector<VoxelWorld::FelledCell>& cells);
    void updateFallenTrees(float dt);
    void renderFallenTrees(const Mat4& view, const Mat4& proj);
    GLuint fallVao_ = 0, fallVbo_ = 0;

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
