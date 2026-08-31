#include "Survivor.h"
#include "../Core/Log.h"

#include <cmath>
#include <cstdio>

namespace {
// Размеры игрока в блоках: 0.6 в поперечнике и 1.8 в высоту — те же пропорции, что в
// кубических играх, и они не случайны: с шириной 0.6 игрок проходит в проём в один блок,
// а с высотой 1.8 не проходит под потолком в один блок, но проходит в два.
const float HALF_WIDTH  = 0.30f;
const float HEIGHT      = 1.80f;
const float CROUCH_HEIGHT = 1.30f;
const float EYE_OFFSET  = 1.62f;
const float EYE_CROUCH  = 1.15f;

const float SPEED_WALK   = 4.2f;
const float SPEED_SPRINT = 6.6f;
const float SPEED_CROUCH = 1.9f;
const float SPEED_SWIM   = 2.6f;
const float GRAVITY      = 24.0f;
const float JUMP_SPEED   = 8.0f;   // прыжок ровно на один блок с запасом
const float STEP_HEIGHT  = 1.02f;  // автоматический шаг на блок вверх

const float HUNGER_PER_SEC = 100.0f / (90.0f * 60.0f);
const float THIRST_PER_SEC = 100.0f / (55.0f * 60.0f);
const float STAMINA_DRAIN  = 14.0f;
const float STAMINA_REGEN  = 9.0f;

const float REACH = 5.0f;          // на сколько метров дотягивается рука
const float PLACE_COOLDOWN = 0.18f;
} // namespace

Survivor::Survivor(VoxelWorld& voxels, const Environment& env, Inventory& inventory)
    : voxels_(voxels), env_(env), inventory_(inventory) {}

void Survivor::spawn(Vec3 position){
    int x = (int)floorf(position.x), z = (int)floorf(position.z);
    int y = voxels_.surfaceY(x, z) + 1;
    // Если точка оказалась внутри дерева или валуна — поднимаемся до свободного места.
    while(y < voxels_.maxHeightBlocks() && (voxels_.isSolidAt(x, y, z) || voxels_.isSolidAt(x, y + 1, z))) ++y;
    pos_ = Vec3{ (float)x + 0.5f, (float)y, (float)z + 0.5f };
    velY_ = 0.0f;
    fallStartY_ = pos_.y;
    health_ = hunger_ = thirst_ = stamina_ = 100.0f;
    radiation_ = 0.0f;
    oxygen_ = 100.0f;
    stepSmooth_ = 0.0f;
    miningProgress_ = 0.0f;
    say("Вы очнулись на острове. Ломайте блоки — из них всё и строится.");
}

Vec3 Survivor::eyePosition() const {
    // stepSmooth_ — остаток сглаживания шага: камера идёт к своей настоящей высоте за
    // доли секунды, поэтому подъём на блок читается как шаг, а не как рывок.
    return Vec3{ pos_.x, pos_.y + (crouch_ ? EYE_CROUCH : EYE_OFFSET) - stepSmooth_, pos_.z };
}

Vec3 Survivor::lookDirection() const {
    return Vec3{ -sinf(yaw_) * cosf(pitch_), sinf(pitch_), -cosf(yaw_) * cosf(pitch_) };
}

bool Survivor::collides(Vec3 p) const {
    float height = crouch_ ? CROUCH_HEIGHT : HEIGHT;
    int minX = (int)floorf(p.x - HALF_WIDTH), maxX = (int)floorf(p.x + HALF_WIDTH);
    int minZ = (int)floorf(p.z - HALF_WIDTH), maxZ = (int)floorf(p.z + HALF_WIDTH);
    int minY = (int)floorf(p.y), maxY = (int)floorf(p.y + height - 0.02f);
    for(int y = minY; y <= maxY; ++y)
        for(int x = minX; x <= maxX; ++x)
            for(int z = minZ; z <= maxZ; ++z)
                if(voxels_.isSolidAt(x, y, z)) return true;
    return false;
}

void Survivor::update(const SurvivorInput& in, float dt){
    if(dt <= 0.0f) return;
    yaw_ = in.yaw;
    pitch_ = in.pitch;
    updateMovement(in, dt);
    updateMetabolism(dt);
    updateInteraction(in, dt);
    messageAge_ += dt;
}

void Survivor::updateMovement(const SurvivorInput& in, float dt){
    // Присед отменяется, если над головой блок: иначе игрок встал бы внутрь потолка.
    bool wantCrouch = in.crouch;
    if(!wantCrouch && crouch_){
        Vec3 test = pos_;
        bool wasCrouch = crouch_;
        const_cast<Survivor*>(this)->crouch_ = false;
        if(collides(test)) const_cast<Survivor*>(this)->crouch_ = wasCrouch;
    } else {
        crouch_ = wantCrouch;
    }

    // В воде: голова ниже уровня воды — плывём.
    Block feet = voxels_.blockAt((int)floorf(pos_.x), (int)floorf(pos_.y), (int)floorf(pos_.z));
    Block head = voxels_.blockAt((int)floorf(pos_.x), (int)floorf(pos_.y + 1.5f), (int)floorf(pos_.z));
    inWater_ = (feet == Block::Water || head == Block::Water);

    sprinting_ = in.sprint && !crouch_ && stamina_ > 1.0f &&
                 (fabsf(in.moveX) + fabsf(in.moveY)) > 0.1f && !inWater_;

    float speed = inWater_ ? SPEED_SWIM : (crouch_ ? SPEED_CROUCH : (sprinting_ ? SPEED_SPRINT : SPEED_WALK));
    if(hunger_ < 20.0f || thirst_ < 20.0f) speed *= 0.75f;

    float sinY = sinf(yaw_), cosY = cosf(yaw_);
    Vec3 forward{ -sinY, 0.0f, -cosY };
    Vec3 right{ cosY, 0.0f, -sinY };
    Vec3 wish{ forward.x * in.moveY + right.x * in.moveX, 0.0f, forward.z * in.moveY + right.z * in.moveX };
    float wishLen = sqrtf(wish.x*wish.x + wish.z*wish.z);
    if(wishLen > 1.0f){ wish.x /= wishLen; wish.z /= wishLen; wishLen = 1.0f; }
    currentSpeed_ = wishLen * speed;

    // Движение по осям ПО ОЧЕРЕДИ. Двигать сразу по диагонали нельзя: упёршись в угол,
    // игрок залипал бы, вместо того чтобы скользить вдоль стены.
    Vec3 next = pos_;
    float dx = wish.x * speed * dt;
    float dz = wish.z * speed * dt;

    Vec3 tryX = next; tryX.x += dx;
    if(!collides(tryX)) next = tryX;
    else {
        // Автоматический шаг на блок вверх: на телефоне заставлять жать «прыжок» перед
        // каждой ступенькой — верный способ бросить игру.
        Vec3 stepUp = tryX; stepUp.y += STEP_HEIGHT;
        if(onGround_ && !collides(stepUp)){ next = stepUp; stepSmooth_ += STEP_HEIGHT; }
    }
    Vec3 tryZ = next; tryZ.z += dz;
    if(!collides(tryZ)) next = tryZ;
    else {
        Vec3 stepUp = tryZ; stepUp.y += STEP_HEIGHT;
        if(onGround_ && !collides(stepUp)){ next = stepUp; stepSmooth_ += STEP_HEIGHT; }
    }
    pos_.x = next.x; pos_.y = next.y; pos_.z = next.z;

    // Спуск по ступенькам: если под ногами обрыв ровно в один блок, не отпускаем игрока
    // в свободное падение, а «прилипаем» к нижней ступени. Иначе спуск по лестнице из
    // блоков превращается в череду коротких падений с подпрыгиванием камеры.
    if(onGround_ && velY_ <= 0.0f && !inWater_){
        Vec3 below = pos_; below.y -= 0.06f;
        if(!collides(below)){
            Vec3 step = pos_; step.y -= 1.0f;
            if(collides(step)){
                float target = floorf(pos_.y) - 1.0f + 1.0f;   // верх нижней ступени
                float drop = pos_.y - target;
                if(drop > 0.0f && drop <= 1.05f){
                    pos_.y = target;
                    stepSmooth_ -= drop;
                }
            }
        }
    }

    // Сглаживание догоняет истинную высоту примерно за 0.12 с.
    float decay = clampf(dt * 9.0f, 0.0f, 1.0f);
    stepSmooth_ -= stepSmooth_ * decay;
    stepSmooth_ = clampf(stepSmooth_, -1.2f, 1.2f);

    // Границы карты.
    float size = voxels_.world().config().size;
    pos_.x = clampf(pos_.x, 1.0f, size - 1.0f);
    pos_.z = clampf(pos_.z, 1.0f, size - 1.0f);

    if(inWater_){
        // В воде барахтаемся: тонем медленно, прыжок работает как гребок вверх.
        velY_ += (in.jump ? 12.0f : -3.0f) * dt;
        velY_ = clampf(velY_, -2.5f, 3.5f);
        stamina_ = clampf(stamina_ - dt * 3.0f, 0.0f, 100.0f);
        fallStartY_ = pos_.y;
    } else {
        if(in.jump && onGround_ && stamina_ > 6.0f){
            velY_ = JUMP_SPEED;
            onGround_ = false;
            fallStartY_ = pos_.y;
            stamina_ = clampf(stamina_ - 6.0f, 0.0f, 100.0f);
        }
        velY_ -= GRAVITY * dt;
        if(velY_ < -60.0f) velY_ = -60.0f;
    }

    Vec3 tryY = pos_;
    tryY.y += velY_ * dt;
    if(!collides(tryY)){
        pos_.y = tryY.y;
        if(velY_ < 0.0f) onGround_ = false;
    } else {
        if(velY_ < 0.0f){
            // Приземление: доводим до верхней грани блока и считаем урон падения.
            pos_.y = floorf(pos_.y) ;
            if(!onGround_){
                float fallen = fallStartY_ - pos_.y;
                if(fallen > 4.0f && !inWater_){
                    float damage = (fallen - 4.0f) * 12.0f;
                    health_ = clampf(health_ - damage, 0.0f, 100.0f);
                    char buf[96];
                    snprintf(buf, sizeof(buf), "Падение с %.0f блоков: -%.0f HP", (double)fallen, (double)damage);
                    say(buf);
                }
            }
            onGround_ = true;
            fallStartY_ = pos_.y;
        }
        velY_ = 0.0f;
    }
    if(!onGround_ && velY_ > 0.0f) fallStartY_ = pos_.y;

    if(sprinting_) stamina_ = clampf(stamina_ - STAMINA_DRAIN * dt, 0.0f, 100.0f);
    else           stamina_ = clampf(stamina_ + STAMINA_REGEN * dt, 0.0f, 100.0f);
}

void Survivor::updateMetabolism(float dt){
    // ---- Дыхание. Голова под водой — воздух кончается примерно за 45 секунд, дальше
    // игрок захлёбывается. На поверхности вдох восстанавливается быстро: наказывать за
    // ныряние дольше, чем длится само ныряние, незачем.
    int eyeX = (int)floorf(pos_.x), eyeZ = (int)floorf(pos_.z);
    int eyeY = (int)floorf(pos_.y + (crouch_ ? EYE_CROUCH : EYE_OFFSET));
    headUnderwater_ = (voxels_.blockAt(eyeX, eyeY, eyeZ) == Block::Water);
    if(headUnderwater_){
        oxygen_ = clampf(oxygen_ - (100.0f / 45.0f) * dt, 0.0f, 100.0f);
        if(oxygen_ <= 0.0f){
            health_ = clampf(health_ - 12.0f * dt, 0.0f, 100.0f);
            if(health_ <= 0.0f) say("Вы утонули");
        }
    } else {
        if(oxygen_ < 100.0f) oxygen_ = clampf(oxygen_ + 30.0f * dt, 0.0f, 100.0f);
    }

    const BiomeInfo& bi = biomeInfo(voxels_.world().biomeAt(pos_.x, pos_.z));
    float effort = 1.0f + (sprinting_ ? 0.8f : 0.0f);
    hunger_ = clampf(hunger_ - HUNGER_PER_SEC * effort * dt, 0.0f, 100.0f);
    thirst_ = clampf(thirst_ - THIRST_PER_SEC * effort * bi.thirstRate * dt, 0.0f, 100.0f);

    float ambient = bi.ambientTemp + env_.temperatureModifier();
    if(inWater_) ambient -= 8.0f;
    float target = 36.6f + (ambient - 20.0f) * 0.10f;
    bodyTemp_ += (target - bodyTemp_) * clampf(dt * 0.05f, 0.0f, 1.0f);

    if(ambientRadiation_ > 0.0f) radiation_ = clampf(radiation_ + ambientRadiation_ * dt, 0.0f, 100.0f);
    else                         radiation_ = clampf(radiation_ - dt * 0.35f, 0.0f, 100.0f);

    float damage = 0.0f;
    if(hunger_ <= 0.0f) damage += 0.6f;
    if(thirst_ <= 0.0f) damage += 1.0f;
    if(bodyTemp_ < 34.0f) damage += (34.0f - bodyTemp_) * 0.9f;
    if(bodyTemp_ > 39.0f) damage += (bodyTemp_ - 39.0f) * 0.9f;
    if(radiation_ > 25.0f) damage += (radiation_ - 25.0f) * 0.05f;
    if(damage > 0.0f) health_ = clampf(health_ - damage * dt, 0.0f, 100.0f);

    // Регенерация по ТЗ: 1 HP/с при сытости и жажде выше 80.
    if(damage <= 0.0f && hunger_ > 80.0f && thirst_ > 80.0f)
        health_ = clampf(health_ + 1.0f * dt, 0.0f, 100.0f);
}

void Survivor::updateInteraction(const SurvivorInput& in, float dt){
    if(placeCooldown_ > 0.0f) placeCooldown_ -= dt;

    target_ = voxels_.raycast(eyePosition(), lookDirection(), REACH);

    // ---- Добыча
    if(in.attack && target_.hit){
        if(target_.x != miningX_ || target_.y != miningY_ || target_.z != miningZ_){
            // Перевели прицел на другой блок — прогресс начинается заново.
            miningX_ = target_.x; miningY_ = target_.y; miningZ_ = target_.z;
            miningProgress_ = 0.0f;
        }
        const BlockInfo& info = blockInfo(target_.block);
        float hardness = info.hardness > 0.01f ? info.hardness : 0.2f;
        // Инструментов пока нет (этап 3): голыми руками камень идёт втрое дольше дерева.
        miningProgress_ += dt / hardness;
        stamina_ = clampf(stamina_ - dt * 2.5f, 0.0f, 100.0f);

        if(miningProgress_ >= 1.0f){
            miningProgress_ = 0.0f;
            Block broken = target_.block;
            voxels_.setBlock(target_.x, target_.y, target_.z, Block::Air);
            if(onBlockChanged) onBlockChanged(target_.x, target_.y, target_.z);

            ItemType drop = itemFromBlock(broken);
            if(drop != ItemType::None){
                int left = inventory_.add(drop, blockInfo(broken).dropCount);
                char buf[128];
                if(left > 0) snprintf(buf, sizeof(buf), "Инвентарь полон: %s не влез", itemDef(drop).nameRu);
                else         snprintf(buf, sizeof(buf), "+1 %s", itemDef(drop).nameRu);
                say(buf);
            }
            // С листвы иногда падают ягоды — еда на первое время.
            if(broken == Block::Leaves && ((target_.x * 7 + target_.z * 13 + target_.y) % 5) == 0)
                inventory_.add(ItemType::Berry, 1);
        }
    } else {
        miningProgress_ = 0.0f;
    }

    // ---- Строительство
    if(in.place && placeCooldown_ <= 0.0f && target_.hit){
        ItemStack& stack = inventory_.selectedStack();
        if(!stack.empty()){
            Block toPlace = itemDef(stack.type).placeable;
            if(toPlace != Block::Air){
                int px = target_.prevX, py = target_.prevY, pz = target_.prevZ;
                // Нельзя ставить блок внутрь себя — иначе игрок замуровывается на месте.
                Vec3 test = pos_;
                voxels_.setBlock(px, py, pz, toPlace);
                bool blocksPlayer = collides(test);
                if(blocksPlayer){
                    voxels_.setBlock(px, py, pz, Block::Air);
                    say("Здесь стоите вы");
                } else {
                    inventory_.consumeSelected();
                    if(onBlockChanged) onBlockChanged(px, py, pz);
                    placeCooldown_ = PLACE_COOLDOWN;
                }
            } else {
                say(std::string(itemDef(stack.type).nameRu) + " нельзя поставить блоком");
            }
        }
    }

    // ---- Действие: съесть выбранное или напиться, если стоишь в воде
    if(in.action){
        ItemStack& stack = inventory_.selectedStack();
        const ItemDef& def = itemDef(stack.type);
        if(!stack.empty() && (def.food > 0 || def.water > 0)){
            hunger_ = clampf(hunger_ + (float)def.food, 0.0f, 100.0f);
            thirst_ = clampf(thirst_ + (float)def.water, 0.0f, 100.0f);
            inventory_.consumeSelected();
            char buf[128];
            snprintf(buf, sizeof(buf), "Съедено: %s (+%d сытость)", def.nameRu, def.food);
            say(buf);
        } else if(inWater_ || voxels_.blockAt((int)floorf(pos_.x), (int)floorf(pos_.y), (int)floorf(pos_.z)) == Block::Water){
            if(thirst_ >= 99.0f){ say("Пить больше не хочется"); }
            else {
                thirst_ = clampf(thirst_ + 25.0f, 0.0f, 100.0f);
                // Вода сырая: примерно каждый четвёртый глоток стоит здоровья.
                if(((int)(thirst_ * 7.0f) % 4) == 0){
                    health_ = clampf(health_ - 3.0f, 0.0f, 100.0f);
                    say("Вы напились грязной воды: +25 жажда, -3 HP");
                } else {
                    say("Вы напились: +25 жажда");
                }
            }
        }
    }
}

void Survivor::say(const std::string& text){
    message_ = text;
    messageAge_ = 0.0f;
    LOG_DEBUG("%s", text.c_str());
}
