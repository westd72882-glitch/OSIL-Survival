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

    // Джойстик уменьшен: прежние 22 мм занимали четверть экрана телефона и большой
    // палец не доставал до края зоны, отчего движение «упиралось» в половину скорости.
    stickRadius_ = clampf(mmToPx(15.0f), 62.0f * uiScale_, 128.0f * uiScale_);

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

    // Правая нижняя зона — основные действия под большой палец правой руки. Копать и
    // ставить рядом и крупные: это две самые частые операции в кубическом мире.
    attack_.cx = rightX;                 attack_.cy = bottomY;
    place(attack_, settings.attackNormX, settings.attackNormY, rBig, "КОПАТЬ", false);

    place_.cx = rightX - rBig * 2.15f;   place_.cy = bottomY;
    place(place_, settings.placeNormX, settings.placeNormY, rBig * 0.82f, "СТАВИТЬ", false);

    jump_.cx = rightX;                   jump_.cy = bottomY - rBig * 2.2f;
    place(jump_, settings.jumpNormX, settings.jumpNormY, r, "ПРЫЖОК", false);

    action_.cx = rightX - rBig * 2.15f;  action_.cy = bottomY - rBig * 2.0f;
    place(action_, settings.actionNormX, settings.actionNormY, r, "E", false);

    sprint_.cx = rightX - rBig * 3.8f;   sprint_.cy = bottomY;
    place(sprint_, settings.sprintNormX, settings.sprintNormY, r, "БЕГ", true);

    crouch_.cx = rightX - rBig * 3.8f;   crouch_.cy = bottomY - rBig * 2.0f;
    place(crouch_, settings.crouchNormX, settings.crouchNormY, r, "СЕСТЬ", true);

    // Правый верх — окна (инвентарь, крафт, карта).
    float topY = r + pad * 2.0f;
    inventory_.cx = (float)screenW - r - pad;      inventory_.cy = topY;
    place(inventory_, settings.invNormX, settings.invNormY, r * 0.8f, "ИНВ", false);
    craft_.cx = (float)screenW - r * 3.0f - pad;   craft_.cy = topY;
    place(craft_, settings.craftNormX, settings.craftNormY, r * 0.8f, "КРАФТ", false);
    map_.cx = (float)screenW - r * 5.0f - pad;     map_.cy = topY;
    place(map_, settings.mapNormX, settings.mapNormY, r * 0.8f, "КАРТА", false);
    options_.cx = (float)screenW - r * 7.0f - pad;  options_.cy = topY;
    place(options_, settings.optionsNormX, settings.optionsNormY, r * 0.8f, "НАСТР", false);
}

TouchButton* TouchControls::buttonAt(float x, float y){
    TouchButton* all[] = { &attack_, &place_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_, &options_ };
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
    // ---- Режим редактора раскладки: пальцем двигаем кнопки, игра при этом стоит.
    if(editMode_){
        float x = -1, y = -1;
        bool down = false, motion = false, up = false;
        if(e.type == SDL_FINGERDOWN){ x = e.tfinger.x * screenW_; y = e.tfinger.y * screenH_; down = true; }
        else if(e.type == SDL_FINGERMOTION){ x = e.tfinger.x * screenW_; y = e.tfinger.y * screenH_; motion = true; }
        else if(e.type == SDL_FINGERUP){ up = true; }
        else if(e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID){ x = (float)e.button.x; y = (float)e.button.y; down = true; }
        else if(e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID && (e.motion.state & SDL_BUTTON_LMASK)){ x = (float)e.motion.x; y = (float)e.motion.y; motion = true; }
        else if(e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID){ up = true; }
        else return false;

        if(down){ dragged_ = buttonAt(x, y); return dragged_ != nullptr; }
        if(motion && dragged_){
            // Кнопку нельзя утащить за край экрана — иначе её не вернуть обратно.
            dragged_->cx = clampf(x, dragged_->radius, (float)screenW_ - dragged_->radius);
            dragged_->cy = clampf(y, dragged_->radius, (float)screenH_ - dragged_->radius);
            return true;
        }
        if(up && dragged_){ dragged_ = nullptr; return true; }
        return false;
    }

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
                if(b == &place_) placeQueued_ = true;
                if(b == &inventory_) invQueued_ = true;
                if(b == &craft_) craftQueued_ = true;
                if(b == &map_) mapQueued_ = true;
                if(b == &options_) optionsQueued_ = true;
                return true;
            }
            // Половины экрана разделены строго: слева ТОЛЬКО джойстик движения,
            // справа ТОЛЬКО обзор. Раньше палец, не попавший в джойстик, доставался
            // обзору где угодно — и камера ехала от касания по левой половине и по
            // интерфейсу. Кнопки проверены выше и сюда не доходят.
            if(x < (float)screenW_ * 0.5f){
                // Новое касание слева ВСЕГДА перехватывает джойстик, даже если он
                // считается активным. Так лечится залипание: если система потеряла
                // FINGERUP (свернули игру, пришёл звонок), игрок просто касается экрана
                // заново, и управление возвращается — а не «бежит само».
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
            }
            return true;
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
            TouchButton* all[] = { &attack_, &place_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_, &options_ };
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
        // ВАЖНО: SDL на Android по умолчанию дублирует КАЖДОЕ касание ещё и событием
        // мыши. Если их обрабатывать, палец на экране одновременно крутит камеру
        // (mouseLook_) и держит «удар» (keyAttack_) — ровно то, на что жаловались.
        // Такие события помечены which == SDL_TOUCH_MOUSEID и отбрасываются.
        case SDL_MOUSEBUTTONDOWN: {
            if(e.button.which == SDL_TOUCH_MOUSEID) return false;
            float x = (float)e.button.x, y = (float)e.button.y;
            TouchButton* b = buttonAt(x, y);
            if(b){
                if(b->toggle) b->active = !b->active; else b->active = true;
                if(b == &jump_) jumpQueued_ = true;
                if(b == &action_) actionQueued_ = true;
                if(b == &place_) placeQueued_ = true;
                if(b == &inventory_) invQueued_ = true;
                if(b == &craft_) craftQueued_ = true;
                if(b == &map_) mapQueued_ = true;
                if(b == &options_) optionsQueued_ = true;
                return true;
            }
            if(e.button.button == SDL_BUTTON_LEFT){ keyAttack_ = true; mouseLook_ = true; }
            if(e.button.button == SDL_BUTTON_RIGHT) mouseLook_ = true;
            return true;
        }
        case SDL_MOUSEBUTTONUP:
            if(e.button.which == SDL_TOUCH_MOUSEID) return false;
            if(e.button.button == SDL_BUTTON_LEFT){ keyAttack_ = false; attack_.active = false; }
            if(e.button.button == SDL_BUTTON_RIGHT) placeQueued_ = true; // ПКМ — поставить блок
            mouseLook_ = false;
            return true;
        case SDL_MOUSEMOTION:
            if(e.motion.which == SDL_TOUCH_MOUSEID) return false;
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
                case SDLK_q: case SDLK_f:     if(down) placeQueued_ = true; return true;
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
    attack_.justPressed = jump_.justPressed = action_.justPressed = place_.justPressed = false;
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
bool TouchControls::placePressed(){ bool v = placeQueued_; placeQueued_ = false; return v; }
bool TouchControls::inventoryPressed(){ bool v = invQueued_; invQueued_ = false; return v; }
bool TouchControls::craftPressed(){ bool v = craftQueued_; craftQueued_ = false; return v; }
bool TouchControls::mapPressed(){ bool v = mapQueued_; mapQueued_ = false; return v; }
bool TouchControls::settingsPressed(){ bool v = optionsQueued_; optionsQueued_ = false; return v; }

void TouchControls::releaseAllTouches(){
    stickActive_ = false; stickFinger_ = -1;
    lookActive_ = false;  lookFinger_ = -1;
    lookDX = lookDY = 0.0f;
    TouchButton* all[] = { &attack_, &place_, &jump_, &action_, &sprint_, &crouch_,
                           &inventory_, &craft_, &map_, &options_ };
    for(TouchButton* b : all){
        b->finger = -1;
        if(!b->toggle) b->active = false;   // тумблеры (бег, присед) состояние сохраняют
    }
}

void TouchControls::setEditMode(bool on){
    editMode_ = on;
    dragged_ = nullptr;
    if(!on) saveLayout();
}

void TouchControls::resetLayout(){
    // Сентинел -1 по Y означает «позиция не задана»: layout() тогда берёт стандартную.
    settings.stickNormY = settings.jumpNormY = settings.sprintNormY = -1.0f;
    settings.crouchNormY = settings.actionNormY = settings.attackNormY = -1.0f;
    settings.placeNormY = settings.invNormY = settings.craftNormY = -1.0f;
    settings.mapNormY = settings.optionsNormY = -1.0f;
    layout(screenW_, screenH_);
    saveSettings();
}

void TouchControls::saveLayout() const {
    auto store = [&](const TouchButton& b, float& nx, float& ny){
        nx = b.cx / (float)screenW_;
        ny = b.cy / (float)screenH_;
    };
    store(attack_, settings.attackNormX, settings.attackNormY);
    store(place_,  settings.placeNormX,  settings.placeNormY);
    store(jump_,   settings.jumpNormX,   settings.jumpNormY);
    store(action_, settings.actionNormX, settings.actionNormY);
    store(sprint_, settings.sprintNormX, settings.sprintNormY);
    store(crouch_, settings.crouchNormX, settings.crouchNormY);
    store(inventory_, settings.invNormX, settings.invNormY);
    store(craft_,  settings.craftNormX,  settings.craftNormY);
    store(map_,    settings.mapNormX,    settings.mapNormY);
    store(options_, settings.optionsNormX, settings.optionsNormY);
    saveSettings();
}

std::vector<TouchControls::ButtonView> TouchControls::buttonViews() const {
    const TouchButton* all[] = { &attack_, &place_, &jump_, &action_, &sprint_, &crouch_, &inventory_, &craft_, &map_, &options_ };
    std::vector<ButtonView> out;
    for(const TouchButton* b : all){
        if(!b->visible) continue;
        out.push_back(ButtonView{ b->cx, b->cy, b->radius, b->label.c_str(), b->active });
    }
    return out;
}
