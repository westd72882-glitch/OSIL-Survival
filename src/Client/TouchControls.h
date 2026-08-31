#pragma once
// ==================== СЕНСОРНОЕ УПРАВЛЕНИЕ (ТЕЛЕФОН) ====================
// Основная платформа — Android, поэтому управление проектируется под большой палец, а
// не под мышь. Принятые решения и причины:
//
//   * Джойстик ДИНАМИЧЕСКИЙ: центр появляется там, где палец коснулся левой половины
//     экрана. Фиксированный джойстик заставляет игрока смотреть на экран и «нащупывать»
//     его — в выживании, где часто бегут от волка, это проигрышная механика.
//   * Обзор — перетаскивание по правой половине, кроме зон кнопок. Проверка «попал ли
//     палец в кнопку» идёт ДО обзора, иначе нажатие на кнопку одновременно дёргает камеру.
//   * Каждый палец отслеживается по SDL_FingerID: бежать, крутить камеру и бить одним
//     касанием — норма, а не исключение. Без привязки к идентификатору второй палец
//     «угоняет» джойстик.
//   * Бег и присед — ТУМБЛЕРЫ: держать палец на кнопке бега весь путь невозможно, если
//     этой же рукой надо крутить камеру.
//   * Размеры кнопок задаются в миллиметрах через DPI экрана, а не в пикселях: кнопка
//     в 100 px на планшете и на телефоне — это разные кнопки.
//
// Для отладки на ПК те же действия дублируются мышью и клавиатурой (WASD, Space, Shift,
// Ctrl, E, ЛКМ, Tab, C, M).
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct TouchButton {
    float cx = 0, cy = 0, radius = 0;
    std::string label;      // короткая подпись на кнопке
    bool toggle = false;    // тумблер (бег, присед) или обычное нажатие
    bool active = false;    // состояние тумблера / нажата ли сейчас
    bool justPressed = false;
    SDL_FingerID finger = -1;
    bool visible = true;
};

class TouchControls {
public:
    // Пересчитывает раскладку под текущий размер экрана. Вызывать при старте и при
    // повороте экрана.
    void layout(int screenW, int screenH);

    // Обработка одного события SDL. Возвращает true, если событие «съедено» управлением.
    bool handleEvent(const SDL_Event& e);

    // Вызывать раз в кадр ПОСЛЕ обработки событий: сбрасывает одноразовые флаги.
    void endFrame();

    void render(); // джойстик и кнопки поверх сцены

    // Кнопки для подписи их текстом. Сам текст рисует клиент: движок для отрисовки
    // строки требует шрифт и кэш текстур, а управлению незачем про них знать.
    struct ButtonView { float cx, cy, radius; const char* label; bool active; };
    std::vector<ButtonView> buttonViews() const;

    // Состояние джойстика для отрисовки его текстурой (движок рисовать не умеет —
    // ему незачем знать про ассеты игры).
    struct StickView { bool active; float baseX, baseY, curX, curY, radius; };
    StickView stickView() const;

    // ---- Состояние ввода
    float moveX() const;      // -1..1 (вправо-влево)
    float moveY() const;      // -1..1 (вперёд-назад)
    float lookDX = 0, lookDY = 0; // накопленный поворот камеры за кадр (пиксели)

    bool sprint() const { return sprint_.active || keySprint_; }
    bool crouch() const { return crouch_.active || keyCrouch_; }
    bool attackHeld() const { return attack_.active || keyAttack_; }
    bool placePressed();      // одноразовое: поставить блок
    bool jumpPressed();       // одноразовое: прыжок
    bool actionPressed();     // одноразовое: действие (E)
    bool inventoryPressed();
    bool craftPressed();
    bool mapPressed();
    bool settingsPressed();

    // ---- Редактор раскладки (перенесён из A.N.O.D.E): кнопки можно расставить под свою
    // руку. Разные телефоны и разный хват — единственная раскладка всем не подходит,
    // а в выживании неудобная кнопка стоит жизни.
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }
    void resetLayout();          // вернуть стандартные позиции
    // Сбросить все активные касания. Вызывается, когда приложение теряет фокус или
    // открывается окно: иначе система «съедает» FINGERUP, и джойстик остаётся зажатым —
    // игрок бежит сам по себе, пока не ткнёт в экран ещё раз.
    void releaseAllTouches();
    void saveLayout() const;     // записать текущие позиции в настройки

private:
    TouchButton* buttonAt(float x, float y);

    int screenW_ = 1280, screenH_ = 720;
    float uiScale_ = 1.0f;

    // Джойстик движения
    bool stickActive_ = false;
    SDL_FingerID stickFinger_ = -1;
    float stickBaseX_ = 0, stickBaseY_ = 0, stickCurX_ = 0, stickCurY_ = 0, stickRadius_ = 120.0f;

    // Обзор
    bool lookActive_ = false;
    SDL_FingerID lookFinger_ = -1;
    float lookLastX_ = 0, lookLastY_ = 0;

    TouchButton jump_, sprint_, crouch_, action_, attack_, place_, inventory_, craft_, map_, options_;
    bool jumpQueued_ = false, actionQueued_ = false, placeQueued_ = false;
    bool optionsQueued_ = false;
    bool editMode_ = false;
    TouchButton* dragged_ = nullptr;   // кнопка, которую сейчас тащат в редакторе
    bool invQueued_ = false, craftQueued_ = false, mapQueued_ = false;

    // Клавиатура (отладка на ПК)
    bool keyForward_ = false, keyBack_ = false, keyLeft_ = false, keyRight_ = false;
    bool keySprint_ = false, keyCrouch_ = false, keyAttack_ = false;
    bool mouseLook_ = false;
};
