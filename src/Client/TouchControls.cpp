#include "TouchControls.h"
#include "../Engine/Core/Settings.h"
#include "../Engine/Core/Window.h"
#include "../Engine/Render/UIDraw.h"
#include "../Engine/Render/UIStyle.h"
#include "../Core/Math.h"

#include <cmath>

namespace {
// Физический размер кнопки. 9 мм — нижняя граница, при которой палец попадает уверенно
// (рекомендации по мобильным интерфейсам дают 7-10 мм); берём с запасом, потому что в
// игре по кнопке бьют не глядя.
const float BUTTON_MM = 11.0f;
const float BIG_BUTTON_MM = 15.0f;

float screenDpi(){
    float ddpi = 160.0f, hdpi = 160.0f, vdpi = 160.0f;
    if(SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) != 0 || ddpi <= 1.0f) return 160.0f;
    return ddpi;
}
float mmToPx(float mm){ return mm / 25.4f * screenDpi(); }
} // namespace

void TouchControls::layout(int screenW, int screenH){
    screenW_ = screenW;
    screenH_ = screenH;
    uiScale_ = (float)screenH / 720.0f;
    if(uiScale_ < 0.6f) uiScale_ = 0.6f;

    float r     = mmToPx(BUTTON_MM) * 0.5f;
    float rBig  = mmToPx(BIG_BUTTON_MM) * 0.5f;
    // На очень мелком экране кнопки не должны слипаться, на очень крупном — раздуваться.
    r    = clampf(r,    28.0f * uiScale_, 70.0f * uiScale_);
    rBig = clampf(rBig, 38.0f * uiScale_, 92.0f * uiScale_);

    stickRadius_ = clampf(mmToPx(22.0f), 90.0f * uiScale_, 190.0f * uiScale_);

    float pad = r * 0.55f;
    float rightX = (float)screenW - rBig - pad;
    float bottomY = (float)screenH - rBig - pad;

    auto place = [&](TouchButton& b, float nx, float ny, float radius, const char* label, bool toggle){
        b.radius = radius;
        b.label = label;
        b.toggle = toggle;
        // Сохранённая раскладка из настроек: -1 по Y означает «позиция не задана».
        if(ny >= 0.0f){ b.cx = nx * (float)screenW; b.cy = ny * (float)screenH; }
    };

    // Правая нижняя зона — основные действия под большой палец правой руки.
    attack_.cx = rightX;                 attack_.cy = bottomY;
    place(attack_, settings.attackNormX, settings.attackNormY, rBig, "УДАР", false);

    jump_.cx = rightX;                   jump_.cy = bottomY - rBig * 2.3f;
    place(jump_, settings.jumpNormX, settings.jumpNormY, r, "ПРЫЖОК", false);

    action_.cx = rightX - rBig * 2.2f;   action_.cy = bottomY;
    place(action_, settings.actionNormX, settings.actionNormY, r, "E", false);

    sprint_.cx = rightX - rBig * 2.2f;   sprint_.cy = bottomY - rBig * 2.0f;
    place(sprint_, settings.sprintNormX, settings.sprintNormY, r, "БЕГ", true);

    crouch_.cx = rightX - rBig * 3.9f;   crouch_.cy = bottomY;
    place(crouch_, settings.crouchNormX, settings.crouchNormY, r, "СЕСТЬ", true);

    // Правый верх — окна (инвентарь, крафт, карта).
    float topY = r + pad * 2.0f;
    inventory_.cx = (float)screenW - r - pad;      inventory_.cy = topY;
    place(inventory_, settings.invNormX, settings.invNormY, r * 0.8f, "ИНВ", false);
    craft_.cx = (float)screenW - r * 3.0f - pad;   craft_.cy = topY;
    place(craft_, settings.craftNormX, settings.craftNormY, r * 0.8f, "КРАФТ", false);
    map_.cx = (float)screenW - r * 5.0f - pad;     map_.cy = topY;
    place(map_, settings.mapNormX, settings.mapNormY, r * 0.8f, "КАРТА", false);
}

TouchButton* TouchControls::buttonAt(float x, float y){
    TouchButton* all[] = { &attack_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_ };
    for(TouchButton* b : all){
        if(!b->visible) continue;
        float dx = x - b->cx, dy = y - b->cy;
        // Зона попадания на 25% больше нарисованной: палец толще пикселя, и промах по
        // краю кнопки в бою ощущается как «игра не отреагировала».
        float rr = b->radius * 1.25f;
        if(dx*dx + dy*dy <= rr*rr) return b;
    }
    return nullptr;
}

bool TouchControls::handleEvent(const SDL_Event& e){
    switch(e.type){
        // ---------- Сенсор ----------
        case SDL_FINGERDOWN: {
            float x = e.tfinger.x * (float)screenW_;
            float y = e.tfinger.y * (float)screenH_;
            TouchButton* b = buttonAt(x, y);
            if(b){
                b->finger = e.tfinger.fingerId;
                if(b->toggle) b->active = !b->active;
                else { b->active = true; b->justPressed = true; }
                if(b == &jump_) jumpQueued_ = true;
                if(b == &action_) actionQueued_ = true;
                if(b == &inventory_) invQueued_ = true;
                if(b == &craft_) craftQueued_ = true;
                if(b == &map_) mapQueued_ = true;
                return true;
            }
            if(x < (float)screenW_ * 0.5f && !stickActive_){
                stickActive_ = true;
                stickFinger_ = e.tfinger.fingerId;
                stickBaseX_ = stickCurX_ = x;
                stickBaseY_ = stickCurY_ = y;
                return true;
            }
            if(!lookActive_){
                lookActive_ = true;
                lookFinger_ = e.tfinger.fingerId;
                lookLastX_ = x; lookLastY_ = y;
                return true;
            }
            return false;
        }
        case SDL_FINGERMOTION: {
            float x = e.tfinger.x * (float)screenW_;
            float y = e.tfinger.y * (float)screenH_;
            if(stickActive_ && e.tfinger.fingerId == stickFinger_){
                stickCurX_ = x; stickCurY_ = y;
                return true;
            }
            if(lookActive_ && e.tfinger.fingerId == lookFinger_){
                lookDX += (x - lookLastX_);
                lookDY += (y - lookLastY_);
                lookLastX_ = x; lookLastY_ = y;
                return true;
            }
            return false;
        }
        case SDL_FINGERUP: {
            if(stickActive_ && e.tfinger.fingerId == stickFinger_){
                stickActive_ = false; stickFinger_ = -1;
                return true;
            }
            if(lookActive_ && e.tfinger.fingerId == lookFinger_){
                lookActive_ = false; lookFinger_ = -1;
                return true;
            }
            TouchButton* all[] = { &attack_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_ };
            for(TouchButton* b : all){
                if(b->finger == e.tfinger.fingerId){
                    b->finger = -1;
                    if(!b->toggle) b->active = false;
                    return true;
                }
            }
            return false;
        }

        // ---------- Мышь и клавиатура: отладка на ПК ----------
        case SDL_MOUSEBUTTONDOWN: {
            float x = (float)e.button.x, y = (float)e.button.y;
            TouchButton* b = buttonAt(x, y);
            if(b){
                if(b->toggle) b->active = !b->active; else b->active = true;
                if(b == &jump_) jumpQueued_ = true;
                if(b == &action_) actionQueued_ = true;
                if(b == &inventory_) invQueued_ = true;
                if(b == &craft_) craftQueued_ = true;
                if(b == &map_) mapQueued_ = true;
                return true;
            }
            if(e.button.button == SDL_BUTTON_LEFT){ keyAttack_ = true; mouseLook_ = true; }
            if(e.button.button == SDL_BUTTON_RIGHT) mouseLook_ = true;
            return true;
        }
        case SDL_MOUSEBUTTONUP:
            if(e.button.button == SDL_BUTTON_LEFT){ keyAttack_ = false; attack_.active = false; }
            mouseLook_ = false;
            return true;
        case SDL_MOUSEMOTION:
            if(mouseLook_){ lookDX += (float)e.motion.xrel; lookDY += (float)e.motion.yrel; }
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool down = (e.type == SDL_KEYDOWN);
            switch(e.key.keysym.sym){
                case SDLK_w: case SDLK_UP:    keyForward_ = down; return true;
                case SDLK_s: case SDLK_DOWN:  keyBack_ = down; return true;
                case SDLK_a: case SDLK_LEFT:  keyLeft_ = down; return true;
                case SDLK_d: case SDLK_RIGHT: keyRight_ = down; return true;
                case SDLK_LSHIFT:             keySprint_ = down; return true;
                case SDLK_LCTRL:              keyCrouch_ = down; return true;
                case SDLK_SPACE:              if(down) jumpQueued_ = true; return true;
                case SDLK_e:                  if(down) actionQueued_ = true; return true;
                case SDLK_TAB:                if(down) invQueued_ = true; return true;
                case SDLK_c:                  if(down) craftQueued_ = true; return true;
                case SDLK_m:                  if(down) mapQueued_ = true; return true;
                default: return false;
            }
        }
        default: return false;
    }
}

void TouchControls::endFrame(){
    lookDX = 0; lookDY = 0;
    attack_.justPressed = jump_.justPressed = action_.justPressed = false;
}

float TouchControls::moveX() const {
    if(stickActive_){
        float dx = stickCurX_ - stickBaseX_;
        float len = sqrtf(dx*dx + (stickCurY_ - stickBaseY_)*(stickCurY_ - stickBaseY_));
        if(len > stickRadius_) dx *= stickRadius_ / len;
        return clampf(dx / stickRadius_, -1.0f, 1.0f);
    }
    return (keyRight_ ? 1.0f : 0.0f) - (keyLeft_ ? 1.0f : 0.0f);
}

float TouchControls::moveY() const {
    if(stickActive_){
        float dy = stickCurY_ - stickBaseY_;
        float len = sqrtf((stickCurX_ - stickBaseX_)*(stickCurX_ - stickBaseX_) + dy*dy);
        if(len > stickRadius_) dy *= stickRadius_ / len;
        // Экранный Y растёт вниз, а «вперёд» — это вверх по экрану.
        return clampf(-dy / stickRadius_, -1.0f, 1.0f);
    }
    return (keyForward_ ? 1.0f : 0.0f) - (keyBack_ ? 1.0f : 0.0f);
}

bool TouchControls::jumpPressed(){ bool v = jumpQueued_; jumpQueued_ = false; return v; }
bool TouchControls::actionPressed(){ bool v = actionQueued_; actionQueued_ = false; return v; }
bool TouchControls::inventoryPressed(){ bool v = invQueued_; invQueued_ = false; return v; }
bool TouchControls::craftPressed(){ bool v = craftQueued_; craftQueued_ = false; return v; }
bool TouchControls::mapPressed(){ bool v = mapQueued_; mapQueued_ = false; return v; }

std::vector<TouchControls::ButtonView> TouchControls::buttonViews() const {
    const TouchButton* all[] = { &attack_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_ };
    std::vector<ButtonView> out;
    for(const TouchButton* b : all){
        if(!b->visible) continue;
        out.push_back(ButtonView{ b->cx, b->cy, b->radius, b->label.c_str(), b->active });
    }
    return out;
}

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

    TouchButton* all[] = { &attack_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_ };
    for(TouchButton* b : all){
        if(!b->visible) continue;
        bool on = b->active;
        drawUICircle(b->cx, b->cy, b->radius, UI_BG_PANEL.r, UI_BG_PANEL.g, UI_BG_PANEL.b, on ? 0.55f : 0.32f);
        const UIColor& line = on ? UI_ACCENT : UI_LINE;
        drawUICircleOutline(b->cx, b->cy, b->radius, line.r, line.g, line.b, 0.75f, 2.5f);
    }
}
