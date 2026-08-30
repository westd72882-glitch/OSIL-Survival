// ==================== ТЕСТЫ СЕНСОРНОГО УПРАВЛЕНИЯ ====================
// Проверяем правила, которые легко сломать незаметно и которые сразу портят игру на
// телефоне: камера крутится ТОЛЬКО правой половиной экрана и только мимо кнопок, а блок
// ломается ТОЛЬКО кнопкой «копать». Тесты работают без окна и без графики — подаём
// готовые структуры событий SDL прямо в обработчик.
#include "TestHarness.h"
#include "../src/Client/TouchControls.h"

namespace {
const int W = 1280, H = 720;

SDL_Event finger(Uint32 type, SDL_FingerID id, float nx, float ny){
    SDL_Event e{};
    e.type = type;
    e.tfinger.fingerId = id;
    e.tfinger.x = nx;
    e.tfinger.y = ny;
    return e;
}

TouchControls makeControls(){
    TouchControls c;
    c.layout(W, H);
    return c;
}
} // namespace

TEST(обзор_не_работает_левой_половиной){
    TouchControls c = makeControls();
    // Палец лёг слева и поехал вправо: это джойстик движения, камера стоять на месте.
    c.handleEvent(finger(SDL_FINGERDOWN, 1, 0.20f, 0.60f));
    c.handleEvent(finger(SDL_FINGERMOTION, 1, 0.30f, 0.60f));
    CHECK_NEAR(c.lookDX, 0.0, 1e-6);
    CHECK_NEAR(c.lookDY, 0.0, 1e-6);
    CHECK_MSG(c.moveX() > 0.1f, "джойстик не отреагировал на движение пальца вправо");
}

TEST(обзор_работает_правой_половиной){
    TouchControls c = makeControls();
    // Пустое место справа (выше кнопок) — обзор.
    c.handleEvent(finger(SDL_FINGERDOWN, 2, 0.62f, 0.30f));
    c.handleEvent(finger(SDL_FINGERMOTION, 2, 0.70f, 0.34f));
    CHECK_MSG(c.lookDX > 1.0f, "камера не повернулась от перетаскивания справа");
    CHECK_MSG(c.lookDY > 1.0f, "камера не наклонилась от перетаскивания справа");
}

TEST(обзор_не_стартует_с_кнопки){
    TouchControls c = makeControls();
    // Находим кнопку «копать» и жмём точно в её центр.
    float cx = 0, cy = 0;
    bool found = false;
    for(const TouchControls::ButtonView& b : c.buttonViews()){
        if(std::string(b.label) == "КОПАТЬ"){ cx = b.cx; cy = b.cy; found = true; }
    }
    CHECK(found);

    c.handleEvent(finger(SDL_FINGERDOWN, 3, cx / (float)W, cy / (float)H));
    c.handleEvent(finger(SDL_FINGERMOTION, 3, (cx + 60.0f) / (float)W, cy / (float)H));
    CHECK_MSG(c.lookDX == 0.0f, "камера поехала от пальца, лежащего на кнопке");
    CHECK_MSG(c.attackHeld(), "кнопка «копать» не нажалась");
}

TEST(ломание_только_по_своей_кнопке){
    TouchControls c = makeControls();
    // Касание пустого места справа (обзор) не должно ломать блоки.
    c.handleEvent(finger(SDL_FINGERDOWN, 4, 0.60f, 0.25f));
    CHECK_MSG(!c.attackHeld(), "добыча включилась от касания пустого места");
    c.handleEvent(finger(SDL_FINGERUP, 4, 0.60f, 0.25f));

    // Касание слева (джойстик) — тоже не ломает.
    c.handleEvent(finger(SDL_FINGERDOWN, 5, 0.15f, 0.70f));
    CHECK_MSG(!c.attackHeld(), "добыча включилась от джойстика движения");
    c.handleEvent(finger(SDL_FINGERUP, 5, 0.15f, 0.70f));
}

TEST(копание_держится_пока_палец_на_кнопке){
    TouchControls c = makeControls();
    float cx = 0, cy = 0;
    for(const TouchControls::ButtonView& b : c.buttonViews())
        if(std::string(b.label) == "КОПАТЬ"){ cx = b.cx; cy = b.cy; }

    c.handleEvent(finger(SDL_FINGERDOWN, 6, cx / (float)W, cy / (float)H));
    CHECK(c.attackHeld());
    c.endFrame();
    CHECK_MSG(c.attackHeld(), "добыча оборвалась, хотя палец с кнопки не убирали");
    c.handleEvent(finger(SDL_FINGERUP, 6, cx / (float)W, cy / (float)H));
    CHECK_MSG(!c.attackHeld(), "добыча продолжается после отпускания кнопки");
}

TEST(два_пальца_работают_одновременно){
    // Бежать и одновременно крутить камеру — обычное дело, а не исключение.
    TouchControls c = makeControls();
    c.handleEvent(finger(SDL_FINGERDOWN, 10, 0.18f, 0.65f));
    c.handleEvent(finger(SDL_FINGERDOWN, 11, 0.65f, 0.30f));
    c.handleEvent(finger(SDL_FINGERMOTION, 10, 0.18f, 0.55f));  // джойстик вперёд
    c.handleEvent(finger(SDL_FINGERMOTION, 11, 0.72f, 0.30f));  // обзор вправо
    CHECK_MSG(c.moveY() > 0.1f, "движение вперёд не сработало вторым пальцем");
    CHECK_MSG(c.lookDX > 1.0f, "обзор не сработал вторым пальцем");
}

TEST(кнопки_бега_и_приседа_переключаются){
    TouchControls c = makeControls();
    float cx = 0, cy = 0;
    for(const TouchControls::ButtonView& b : c.buttonViews())
        if(std::string(b.label) == "БЕГ"){ cx = b.cx; cy = b.cy; }

    CHECK(!c.sprint());
    c.handleEvent(finger(SDL_FINGERDOWN, 7, cx / (float)W, cy / (float)H));
    c.handleEvent(finger(SDL_FINGERUP, 7, cx / (float)W, cy / (float)H));
    CHECK_MSG(c.sprint(), "бег не включился тумблером");
    c.handleEvent(finger(SDL_FINGERDOWN, 8, cx / (float)W, cy / (float)H));
    c.handleEvent(finger(SDL_FINGERUP, 8, cx / (float)W, cy / (float)H));
    CHECK_MSG(!c.sprint(), "бег не выключился повторным нажатием");
}

TEST(синтетическая_мышь_от_касаний_игнорируется){
    // На Android SDL дублирует касания событиями мыши. Если их обрабатывать, палец
    // одновременно крутит камеру и держит «копать» — ровно тот баг, из-за которого
    // осмотр работал по всему экрану.
    TouchControls c = makeControls();
    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.which = SDL_TOUCH_MOUSEID;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 300; down.button.y = 400;
    CHECK(!c.handleEvent(down));
    CHECK_MSG(!c.attackHeld(), "добыча включилась от синтетического события мыши");

    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.which = SDL_TOUCH_MOUSEID;
    motion.motion.xrel = 40; motion.motion.yrel = 10;
    CHECK(!c.handleEvent(motion));
    CHECK_MSG(c.lookDX == 0.0f, "камера поехала от синтетического события мыши");
}
