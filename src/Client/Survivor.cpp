#include "Survivor.h"
#include "../Core/Log.h"

#include <cmath>
#include <cstdio>

namespace {
// Скорости из расчёта «Rust-подобное выживание»: шаг заметно медленнее бега, присед
// вдвое медленнее шага, в воде — как присед.
const float SPEED_WALK   = 3.6f;
const float SPEED_SPRINT = 6.4f;
const float SPEED_CROUCH = 1.7f;
const float SPEED_SWIM   = 2.0f;

const float GRAVITY      = 20.0f;   // м/с² — «игровая» гравитация, вдвое сильнее реальной
const float JUMP_SPEED   = 6.2f;    // даёт прыжок примерно на 1 м
const float EYE_HEIGHT   = 1.68f;
const float EYE_CROUCH   = 1.05f;

// Метаболизм: полная шкала голода уходит за ~90 минут, жажды — за ~55.
const float HUNGER_PER_SEC = 100.0f / (90.0f * 60.0f);
const float THIRST_PER_SEC = 100.0f / (55.0f * 60.0f);
const float STAMINA_DRAIN  = 14.0f; // в секунду при беге
const float STAMINA_REGEN  = 9.0f;

const float GATHER_RANGE   = 3.2f;  // на каком расстоянии можно ударить по объекту
const float HIT_INTERVAL   = 0.55f; // темп ударов: примерно как замах топором
} // namespace

Survivor::Survivor(const World& world, ResourceMap& resources, const Environment& env)
    : world_(world), resources_(resources), env_(env) {}

void Survivor::spawn(Vec3 position){
    pos_ = position;
    pos_.y = world_.heightAt(position.x, position.z);
    velY_ = 0.0f;
    fallStartY_ = pos_.y;
    health_ = hunger_ = thirst_ = stamina_ = 100.0f;
    radiation_ = 0.0f;
    loot_ = Gathered{};
    say("Вы очнулись на берегу. Соберите дерево и камень.");
}

Vec3 Survivor::eyePosition() const {
    return Vec3{ pos_.x, pos_.y + (crouch_ ? EYE_CROUCH : EYE_HEIGHT), pos_.z };
}

void Survivor::update(const SurvivorInput& in, float dt){
    if(dt <= 0.0f) return;
    yaw_ = in.yaw;
    pitch_ = in.pitch;
    updateMovement(in, dt);
    updateMetabolism(dt);
    updateGathering(in, dt);
    messageAge_ += dt;
}

void Survivor::updateMovement(const SurvivorInput& in, float dt){
    const WorldConfig& cfg = world_.config();
    float groundY = world_.heightAt(pos_.x, pos_.z);
    float waterDepth = cfg.waterLevel - groundY;
    inWater_ = (pos_.y < cfg.waterLevel - 0.4f) && waterDepth > 0.5f;

    crouch_ = in.crouch && !inWater_;
    // Бежать можно, только если есть выносливость и игрок реально движется вперёд.
    bool wantsSprint = in.sprint && !crouch_ && stamina_ > 1.0f && (fabsf(in.moveX) + fabsf(in.moveY)) > 0.1f;
    sprinting_ = wantsSprint;

    float speed = inWater_ ? SPEED_SWIM : (crouch_ ? SPEED_CROUCH : (sprinting_ ? SPEED_SPRINT : SPEED_WALK));
    // Голодный и обезвоженный персонаж двигается медленнее — это первый сигнал игроку,
    // что пора искать еду, раньше, чем начнёт капать здоровье.
    if(hunger_ < 20.0f || thirst_ < 20.0f) speed *= 0.75f;

    // Направление: вперёд по взгляду (без наклона), вправо — перпендикуляр.
    float sinY = sinf(yaw_), cosY = cosf(yaw_);
    Vec3 forward{ -sinY, 0.0f, -cosY };
    Vec3 right{ cosY, 0.0f, -sinY };

    Vec3 wish{
        forward.x * in.moveY + right.x * in.moveX,
        0.0f,
        forward.z * in.moveY + right.z * in.moveX
    };
    float wishLen = sqrtf(wish.x*wish.x + wish.z*wish.z);
    if(wishLen > 1.0f){ wish.x /= wishLen; wish.z /= wishLen; wishLen = 1.0f; }

    float nextX = pos_.x + wish.x * speed * dt;
    float nextZ = pos_.z + wish.z * speed * dt;
    // Границы карты: за краем только океан и «стена мира».
    nextX = clampf(nextX, 1.0f, cfg.size - 1.0f);
    nextZ = clampf(nextZ, 1.0f, cfg.size - 1.0f);

    // Крутой склон не пускает: иначе по горам можно ходить как по лестнице, и любая
    // скала становится дорогой. Порог тот же, что у строительства.
    float nextGround = world_.heightAt(nextX, nextZ);
    float climb = nextGround - groundY;
    float horizontal = sqrtf((nextX-pos_.x)*(nextX-pos_.x) + (nextZ-pos_.z)*(nextZ-pos_.z));
    bool tooSteep = horizontal > 0.0001f && (climb / horizontal) > 1.4f; // ~54°
    if(!tooSteep || inWater_){
        pos_.x = nextX;
        pos_.z = nextZ;
        groundY = nextGround;
    }
    currentSpeed_ = wishLen * speed;

    if(inWater_){
        // В воде барахтаемся: тонуть некуда, но и прыгать нельзя.
        float targetY = cfg.waterLevel - 0.9f;
        pos_.y += (targetY - pos_.y) * clampf(dt * 3.0f, 0.0f, 1.0f);
        velY_ = 0.0f;
        onGround_ = false;
        fallStartY_ = pos_.y;
        stamina_ = clampf(stamina_ - dt * 4.0f, 0.0f, 100.0f);
        return;
    }

    // Гравитация и приземление.
    velY_ -= GRAVITY * dt;
    pos_.y += velY_ * dt;
    if(pos_.y <= groundY){
        if(!onGround_){
            // Урон падения: безопасны первые 4 метра, дальше примерно по 12 HP на метр.
            float fallen = fallStartY_ - groundY;
            if(fallen > 4.0f){
                float damage = (fallen - 4.0f) * 12.0f;
                health_ = clampf(health_ - damage, 0.0f, 100.0f);
                char buf[96];
                snprintf(buf, sizeof(buf), "Падение с %.0f м: -%.0f HP", (double)fallen, (double)damage);
                say(buf);
            }
        }
        pos_.y = groundY;
        velY_ = 0.0f;
        onGround_ = true;
        fallStartY_ = pos_.y;
    } else {
        onGround_ = false;
    }

    if(in.jump && onGround_ && stamina_ > 8.0f){
        velY_ = JUMP_SPEED;
        onGround_ = false;
        fallStartY_ = pos_.y;
        stamina_ = clampf(stamina_ - 8.0f, 0.0f, 100.0f);
    }

    if(sprinting_) stamina_ = clampf(stamina_ - STAMINA_DRAIN * dt, 0.0f, 100.0f);
    else           stamina_ = clampf(stamina_ + STAMINA_REGEN * dt, 0.0f, 100.0f);
}

void Survivor::updateMetabolism(float dt){
    // Голод и жажда. В пустыне пить хочется заметно чаще — множитель берётся из биома.
    const BiomeInfo& bi = biomeInfo(world_.biomeAt(pos_.x, pos_.z));
    float effort = 1.0f + (sprinting_ ? 0.8f : 0.0f);
    hunger_ = clampf(hunger_ - HUNGER_PER_SEC * effort * dt, 0.0f, 100.0f);
    thirst_ = clampf(thirst_ - THIRST_PER_SEC * effort * bi.thirstRate * dt, 0.0f, 100.0f);

    // Температура тела: биом + время суток и погода. Ниже 34° и выше 39° — урон.
    float ambient = bi.ambientTemp + env_.temperatureModifier();
    if(inWater_) ambient -= 8.0f;
    float target = 36.6f + (ambient - 20.0f) * 0.10f;
    bodyTemp_ += (target - bodyTemp_) * clampf(dt * 0.05f, 0.0f, 1.0f);

    // Радиация копится в зоне и медленно спадает вне её.
    if(ambientRadiation_ > 0.0f) radiation_ = clampf(radiation_ + ambientRadiation_ * dt, 0.0f, 100.0f);
    else                         radiation_ = clampf(radiation_ - dt * 0.35f, 0.0f, 100.0f);

    // Урон от голода, жажды, холода, жары и радиации.
    float damage = 0.0f;
    if(hunger_ <= 0.0f) damage += 0.6f;
    if(thirst_ <= 0.0f) damage += 1.0f;   // без воды умирают быстрее, чем без еды
    if(bodyTemp_ < 34.0f) damage += (34.0f - bodyTemp_) * 0.9f;
    if(bodyTemp_ > 39.0f) damage += (bodyTemp_ - 39.0f) * 0.9f;
    if(radiation_ > 25.0f) damage += (radiation_ - 25.0f) * 0.05f;
    if(damage > 0.0f) health_ = clampf(health_ - damage * dt, 0.0f, 100.0f);

    // Регенерация ровно по ТЗ: 1 HP/с при сытости и питье выше 80.
    if(damage <= 0.0f && hunger_ > 80.0f && thirst_ > 80.0f)
        health_ = clampf(health_ + 1.0f * dt, 0.0f, 100.0f);
}

void Survivor::updateGathering(const SurvivorInput& in, float dt){
    // Цель — ближайший объект добычи перед игроком. Полноценный рейкаст по хитбоксам —
    // 3-й этап; здесь достаточно проверки расстояния и угла.
    std::vector<const ResourceNode*> near = resources_.query(pos_.x, pos_.z, GATHER_RANGE);
    const ResourceNode* best = nullptr;
    float bestDist = 1e9f;
    float sinY = sinf(yaw_), cosY = cosf(yaw_);
    Vec3 forward{ -sinY, 0.0f, -cosY };
    for(const ResourceNode* n : near){
        float dx = n->pos.x - pos_.x, dz = n->pos.z - pos_.z;
        float d = sqrtf(dx*dx + dz*dz);
        if(d < 0.01f) continue;
        float dot = (dx/d) * forward.x + (dz/d) * forward.z;
        if(dot < 0.4f) continue;      // объект должен быть примерно перед игроком
        if(d < bestDist){ bestDist = d; best = n; }
    }

    target_ = best;
    targetName_ = best ? resourceInfo(best->kind).nameRu : std::string();

    if(gatherCooldown_ > 0.0f) gatherCooldown_ -= dt;

    if(!best || !in.attack){
        gatherProgress_ = 0.0f;
        return;
    }

    // Мелочь (ягоды, куст, камни) собирается руками сразу, крупное требует ударов.
    const ResourceInfo& info = resourceInfo(best->kind);
    if(gatherCooldown_ > 0.0f) return;
    gatherCooldown_ = HIT_INTERVAL;

    // Пока инструментов нет (3-й этап), удар голыми руками даёт малую долю выхода.
    int yield = info.requiresTool ? (info.yieldAmount / 24) : info.yieldAmount;
    if(yield < 1) yield = 1;
    gatherProgress_ = clampf(gatherProgress_ + 0.12f, 0.0f, 1.0f);
    stamina_ = clampf(stamina_ - 2.0f, 0.0f, 100.0f);

    const char* item = info.yieldItem;
    char buf[128];
    if(std::string(item) == "wood"){ loot_.wood += yield; snprintf(buf, sizeof(buf), "+%d дерево (%s)", yield, info.nameRu); }
    else if(std::string(item) == "stone"){ loot_.stone += yield; snprintf(buf, sizeof(buf), "+%d камень (%s)", yield, info.nameRu); }
    else if(std::string(item) == "metal_ore"){ loot_.metalOre += yield; snprintf(buf, sizeof(buf), "+%d руда", yield); }
    else if(std::string(item) == "sulfur_ore"){ loot_.sulfurOre += yield; snprintf(buf, sizeof(buf), "+%d сера", yield); }
    else if(std::string(item) == "cloth"){ loot_.cloth += yield; snprintf(buf, sizeof(buf), "+%d ткань", yield); }
    else {
        loot_.food += yield;
        hunger_ = clampf(hunger_ + 8.0f, 0.0f, 100.0f);
        thirst_ = clampf(thirst_ + 4.0f, 0.0f, 100.0f);
        snprintf(buf, sizeof(buf), "Съедено: %s (+8 сытость)", info.nameRu);
    }
    say(buf);
}

void Survivor::drinkWater(){
    if(thirst_ >= 99.0f){ say("Пить больше не хочется"); return; }
    thirst_ = clampf(thirst_ + 25.0f, 0.0f, 100.0f);
    // Грязная вода: примерно каждый четвёртый глоток стоит здоровья. Числа условные —
    // на 3-м этапе это станет полноценным отравлением с эффектом и лечением.
    if(((int)(thirst_ * 7.0f) % 4) == 0){
        health_ = clampf(health_ - 3.0f, 0.0f, 100.0f);
        say("Вы напились грязной воды: +25 жажда, -3 HP");
    } else {
        say("Вы напились: +25 жажда");
    }
}

void Survivor::say(const std::string& text){
    message_ = text;
    messageAge_ = 0.0f;
    LOG_DEBUG("%s", text.c_str());
}
