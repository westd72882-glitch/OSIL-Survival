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
// Время одного замаха голыми руками. С топором вдвое быстрее.
const float SWING_TIME = 0.55f;
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
    swingCooldown_ = 0.0f;
    swingPeriod_ = 0.0f;
    pendingHit_ = false;
    dead_ = false;
    respawnLeft_ = 0.0f;
    regenTimer_ = 0.0f;
    lastHealth_ = health_;
    say("Вы очнулись на острове. Возьмите топор и рубите деревья.");
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
    // Мёртвый не ходит, не бьёт и не ест: от него остаётся только тело, которое
    // дотягивает гравитацией до земли. Поэтому ввод обнуляется целиком, а не
    // выборочно — иначе труп продолжал бежать в ту сторону, куда был наклонён джойстик.
    SurvivorInput act = in;
    if(dead_){
        act.moveX = act.moveY = 0.0f;
        act.sprint = act.crouch = act.jump = false;
        act.attack = act.place = act.action = false;
    }
    updateMovement(act, dt);
    updateMetabolism(dt);
    updateInteraction(act, dt);

    // Страховка на случай урона в обход applyDamage: ноль здоровья — это смерть.
    if(!dead_ && health_ <= 0.0f){
        dead_ = true;
        health_ = 0.0f;
        respawnLeft_ = 5.0f;
        say("Вы погибли");
    }

    // Индикатор урона и отсчёт возрождения. Урон ловим по падению здоровья, а не по
    // каждому источнику: источников много (падение, холод, утопление, голод), а
    // мигнуть экраном надо одинаково.
    damageAge_ += dt;
    if(health_ < lastHealth_ - 0.01f) damageAge_ = 0.0f;
    lastHealth_ = health_;
    if(isDead()){
        if(respawnLeft_ <= 0.0f) respawnLeft_ = 5.0f;
        respawnLeft_ -= dt;
        if(respawnLeft_ <= 0.0f){
            respawnLeft_ = 0.0f;
            if(onRespawn) onRespawn();
        }
    } else {
        respawnLeft_ = 0.0f;
    }
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
                    char buf[96];
                    snprintf(buf, sizeof(buf), "Падение с %.0f блоков: -%.0f HP", (double)fallen, (double)damage);
                    say(buf);
                    applyDamage(damage, "Вы разбились при падении");
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
        if(oxygen_ <= 0.0f) applyDamage(12.0f * dt, "Вы утонули");
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
    if(damage > 0.0f) applyDamage(damage * dt, "Вы не выжили");

    // Регенерация редкая и только «в тишине»: за каждую полную минуту без единого
    // урона возвращается 1 HP. Непрерывное лечение делало падения безобидными —
    // здоровье успевало отрасти раньше, чем игрок понимал, что упал.
    if(!dead_){
        regenTimer_ += dt;
        while(regenTimer_ >= 60.0f){
            regenTimer_ -= 60.0f;
            if(health_ > 0.0f && health_ < 100.0f && hunger_ > 0.0f && thirst_ > 0.0f)
                health_ = clampf(health_ + 1.0f, 0.0f, 100.0f);
        }
    }
}

// Единственная точка списания здоровья. Смерть фиксируется ЗДЕСЬ же, в тот же момент,
// когда здоровье дошло до нуля: если проверять её отдельным проходом позже, любая
// прибавка в том же кадре (регенерация) отменяет смерть, и падение со скалы не убивает.
void Survivor::applyDamage(float amount, const char* cause){
    if(amount <= 0.0f || dead_) return;
    health_ = clampf(health_ - amount, 0.0f, 100.0f);
    damageAge_ = 0.0f;
    regenTimer_ = 0.0f;
    if(health_ <= 0.0f){
        health_ = 0.0f;
        dead_ = true;
        respawnLeft_ = 5.0f;
        say(cause ? cause : "Вы погибли");
    }
}

// Топор в руках ускоряет добычу. Лежит он в инвентаре с самого начала, брать в руки —
// значит выбрать его в поясе.
bool Survivor::hasAxe() const {
    const ItemStack& sel = inventory_.selectedStack();
    return !sel.empty() && sel.type == ItemType::Axe;
}

// Один удар по объекту: ресурс в инвентарь, счётчик ударов вниз, кончились — объект
// уходит (дерево валится целиком, жила и бочка исчезают).
void Survivor::hitTarget(Block block, int x, int y, int z){
    // Сколько всего ресурса в объекте и сколько уходит за удар. С дерева 50 древесины,
    // с любой жилы 30 — цифры из ТЗ, а число ударов из них и выводится.
    ItemType drop = ItemType::None;
    int perHit = 1;
    int total  = 30;
    switch(block){
        case Block::Wood:      drop = ItemType::Wood;      perHit = 5; total = 50; break;
        case Block::Stone:     drop = ItemType::Stone;     perHit = 3; total = 30; break;
        case Block::OreMetal:  drop = ItemType::OreMetal;  perHit = 3; total = 30; break;
        case Block::OreSulfur: drop = ItemType::OreSulfur; perHit = 3; total = 30; break;
        case Block::Barrel:    drop = ItemType::Scrap;     perHit = 4; total = 12; break;
        default: return;
    }
    int hitsTotal = (total + perHit - 1) / perHit;

    // Счётчик ударов привязан к объекту, а не к блоку: бьём в одну точку — вырабатываем
    // всё дерево целиком, а не отдельный кубик ствола.
    if(x != hitX_ || y != hitY_ || z != hitZ_ || hitBlock_ != block){
        hitX_ = x; hitY_ = y; hitZ_ = z; hitBlock_ = block;
        hitsLeft_ = hitsTotal;
    }

    int left = inventory_.add(drop, perHit);
    char buf[128];
    if(left > 0) snprintf(buf, sizeof(buf), "Инвентарь полон: %s не влез", itemDef(drop).nameRu);
    else         snprintf(buf, sizeof(buf), "+%d %s", perHit - left, itemDef(drop).nameRu);
    say(buf);

    if(--hitsLeft_ <= 0){
        hitsLeft_ = 0;
        hitBlock_ = Block::Air;
        if(onNodeBroken) onNodeBroken(block, x, y, z);
        voxels_.fellCluster(x, y, z);
    }
}

void Survivor::updateInteraction(const SurvivorInput& in, float dt){
    target_ = voxels_.raycast(eyePosition(), lookDirection(), REACH);

    // Замах — дискретное действие: одно нажатие даёт один удар, и бить можно всегда,
    // а не только по добываемому объекту. Так же будет работать удар по другому
    // игроку, когда появится сетевая игра. Зажатая кнопка ничего не «копает».
    if(swingCooldown_ > 0.0f){
        swingCooldown_ -= dt;
        if(swingCooldown_ < 0.0f) swingCooldown_ = 0.0f;
    }

    // Ломать рельеф нельзя вообще: земля, песок, снег, трава и дорога — не ресурс.
    // Добывается только то, что стоит НА земле: дерево, жила, бочка. Каждый удар даёт
    // ресурс, а когда объект выработан — дерево падает, жила и бочка исчезают.
    // Добывать можно ТОЛЬКО топором: факел, руки и прочее бьют, но ресурса не дают.
    bool canHit = target_.hit && hasAxe() &&
                  (isHarvestable(target_.block) || isBuildBlock(target_.block));

    if(in.attack && swingCooldown_ <= 0.0f){
        // Топором машут быстрее, чем факелом или кулаком.
        swingPeriod_ = hasAxe() ? SWING_TIME * 0.72f : SWING_TIME;
        swingCooldown_ = swingPeriod_;
        stamina_ = clampf(stamina_ - 1.6f, 0.0f, 100.0f);
        pendingHit_ = false;
        if(canHit){
            pendingHit_ = true;
            pendX_ = target_.x; pendY_ = target_.y; pendZ_ = target_.z;
        } else if(target_.hit && isHarvestable(target_.block)){
            // Ударить-то можно чем угодно, но добыть — только топором. Молчать здесь
            // нельзя: игрок будет долго бить факелом и не поймёт, почему пусто.
            say("Нужен топор: этим ресурс не добыть");
        }
    }

    // Ресурс капает в середине замаха — в момент, когда инструмент дошёл до цели.
    // Начислять его в момент нажатия неправильно: дерево прибавлялось раньше, чем
    // топор до него долетал.
    if(pendingHit_ && swingCooldown_ <= swingPeriod_ * 0.55f){
        pendingHit_ = false;
        Block b = voxels_.blockAt(pendX_, pendY_, pendZ_);
        if(isHarvestable(b))      hitTarget(b, pendX_, pendY_, pendZ_);
        else if(isBuildBlock(b) && onHitBuild) onHitBuild(b, pendX_, pendY_, pendZ_);
    }

    // ---- Кнопка взаимодействия: поставить объект, залутать ящик, открыть печь,
    // шкаф или ящик-хранилище.
    if(in.action && actionCooldown_ <= 0.0f){
        actionCooldown_ = 0.4f;
        if(target_.hit && target_.block == Block::Crate){
            lootCrate(target_.x, target_.y, target_.z);
        } else if(target_.hit && target_.block == Block::Furnace){
            if(onOpenFurnace) onOpenFurnace();
        } else if(target_.hit && (target_.block == Block::Cupboard || target_.block == Block::Box)){
            if(onOpenObject) onOpenObject(target_.block, target_.x, target_.y, target_.z);
        } else {
            const ItemStack& sel = inventory_.selectedStack();
            // Печь, шкаф и ящик ставятся одинаково: в клетку перед тем блоком, куда
            // смотрит игрок. Раньше правило было прописано только под печь.
            Block put = sel.empty() ? Block::Air : itemDef(sel.type).placeable;
            bool object = (put == Block::Furnace || put == Block::Cupboard || put == Block::Box);
            if(object && target_.hit){
                int px = target_.prevX, py = target_.prevY, pz = target_.prevZ;
                if(voxels_.blockAt(px, py, pz) == Block::Air){
                    voxels_.setBlock(px, py, pz, put);
                    inventory_.consumeSelected();
                    if(onObjectPlaced) onObjectPlaced(put, px, py, pz);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s поставлен", blockName(put));
                    say(buf);
                }
            }
        }
    }
    if(actionCooldown_ > 0.0f) actionCooldown_ -= dt;
}

// Плавка: серная руда превращается в серу, железная — в металл; топливо — дрова.
void Survivor::smeltInFurnace(){
    if(inventory_.countOf(ItemType::Wood) < 1){
        say("Нужны дрова, чтобы топить печь");
        return;
    }
    ItemType ore = ItemType::None, result = ItemType::None;
    if(inventory_.countOf(ItemType::OreSulfur) >= 2){ ore = ItemType::OreSulfur; result = ItemType::Sulfur; }
    else if(inventory_.countOf(ItemType::OreMetal) >= 2){ ore = ItemType::OreMetal; result = ItemType::MetalFrag; }
    if(ore == ItemType::None){
        say("Нечего плавить: нужна руда");
        return;
    }
    inventory_.remove(ore, 2);
    inventory_.remove(ItemType::Wood, 1);
    inventory_.add(result, 1);
    char buf[96];
    snprintf(buf, sizeof(buf), "Выплавлено: %s", itemDef(result).nameRu);
    say(buf);
}

// Ящик на заправке: отдаёт лут и исчезает. Через полчаса вернётся сам — блок ящика
// поставил мир, а восстановление декора уже есть.
void Survivor::lootCrate(int x, int y, int z){
    uint64_t h = hashCoords(x * 31 + y, z * 17 + y, 0x1007ULL, 0xC5A7EULL);
    struct Drop { ItemType type; int min, max; };
    const Drop table[] = {
        { ItemType::Scrap,     4, 14 },
        { ItemType::MetalFrag, 2,  8 },
        { ItemType::Sulfur,    2,  8 },
        { ItemType::Wood,      5, 20 },
        { ItemType::Cloth,     2,  9 },
    };
    // Две-три позиции из таблицы: пустой ящик разочаровывает, полный обесценивает лут.
    int rolls = 2 + (int)(h % 2);
    char buf[128];
    for(int i = 0; i < rolls; ++i){
        uint64_t r = h >> (8 * (i + 1));
        const Drop& d = table[r % (sizeof(table)/sizeof(table[0]))];
        int count = d.min + (int)((r >> 8) % (uint64_t)(d.max - d.min + 1));
        inventory_.add(d.type, count);
        snprintf(buf, sizeof(buf), "+%d %s", count, itemDef(d.type).nameRu);
        say(buf);
    }
    voxels_.setBlock(x, y, z, Block::Air);
}

void Survivor::say(const std::string& text){
    message_ = text;
    messageAge_ = 0.0f;
    LOG_DEBUG("%s", text.c_str());
}
