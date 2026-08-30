#include "TouchControls.h"
#include "../Engine/Render/UIDraw.h"
#include "../Engine/Render/UIStyle.h"
#include "../Core/Math.h"

#include <cmath>

// ==================== ОТРИСОВКА СЕНСОРНОГО УПРАВЛЕНИЯ ====================
// Отрисовка вынесена из TouchControls.cpp намеренно: логика ввода (какой палец что
// делает) не должна тянуть за собой OpenGL. Благодаря этому её можно прогнать тестами
// без окна и графики — см. tests/test_input.cpp, где проверяется, что камера не едет
// от касания левой половины экрана и что блок ломается только по своей кнопке.

void TouchControls::render(){
    // Джойстик рисуется только когда палец на экране: пустой круг в углу мешает смотреть.
    if(stickActive_){
        drawUICircleOutline(stickBaseX_, stickBaseY_, stickRadius_, UI_LINE.r, UI_LINE.g, UI_LINE.b, 0.35f, 3.0f);
        float dx = stickCurX_ - stickBaseX_, dy = stickCurY_ - stickBaseY_;
        float len = sqrtf(dx*dx + dy*dy);
        if(len > stickRadius_){ dx *= stickRadius_ / len; dy *= stickRadius_ / len; }
        drawUICircle(stickBaseX_ + dx, stickBaseY_ + dy, stickRadius_ * 0.38f,
                     UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.45f);
    }

    TouchButton* all[] = { &attack_, &place_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_, &options_ };
    for(TouchButton* b : all){
        if(!b->visible) continue;
        bool on = b->active;
        drawUICircle(b->cx, b->cy, b->radius, UI_BG_PANEL.r, UI_BG_PANEL.g, UI_BG_PANEL.b, on ? 0.55f : 0.32f);
        const UIColor& line = on ? UI_ACCENT : UI_LINE;
        drawUICircleOutline(b->cx, b->cy, b->radius, line.r, line.g, line.b, 0.75f, 2.5f);
    }
}
