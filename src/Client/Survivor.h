#pragma once
// ==================== ИГРОК В КУБИЧЕСКОМ МИРЕ ====================
// Здесь всё, что происходит с персонажем: движение с коллизией по блокам, метаболизм
// (здоровье/голод/жажда/выносливость по ТЗ), добыча блоков и строительство.
//
// Коллизия честно блочная: игрок — коробка 0.6 x 1.8 x 0.6, и она двигается по осям
// по очереди с упором в твёрдые блоки. Это принципиально важнее, чем кажется: именно
// такая проверка позволяет копать под собой, строить лестницы вверх и не проваливаться
// сквозь только что поставленный блок.
//
// Класс не знает ни про ввод с экрана, ни про отрисовку: на вход подаются намерения,
// наружу отдаётся состояние. На 2-м этапе тот же расчёт переедет на сервер.
#include "Inventory.h"
#include "../Core/Math.h"
#include "../World/Environment.h"
#include "../World/VoxelWorld.h"

#include <functional>
#include <string>

struct SurvivorInput {
    float moveX = 0, moveY = 0;
    float yaw = 0, pitch = 0;
    bool sprint = false;
    bool crouch = false;
    bool jump = false;      // одноразовое
    bool attack = false;    // удерживается: копать
    bool place = false;     // одноразовое: поставить блок
    bool action = false;    // одноразовое: съесть/напиться
};

class Survivor {
public:
    Survivor(VoxelWorld& voxels, const Environment& env, Inventory& inventory);

    void spawn(Vec3 position);
    void update(const SurvivorInput& in, float dt);

    // Мир изменился (сломали/поставили блок) — рендеру надо пересобрать чанк.
    std::function<void(int x, int y, int z)> onBlockChanged;

    // ---- Состояние
    Vec3 position() const { return pos_; }
    Vec3 eyePosition() const;
    Vec3 lookDirection() const;
    bool onGround() const { return onGround_; }
    bool inWater() const { return inWater_; }
    bool isCrouching() const { return crouch_; }
    bool isSprinting() const { return sprinting_; }
    float speed() const { return currentSpeed_; }

    float health() const { return health_; }
    float hunger() const { return hunger_; }
    float thirst() const { return thirst_; }
    float stamina() const { return stamina_; }
    float radiation() const { return radiation_; }
    float bodyTemp() const { return bodyTemp_; }
    bool isDead() const { return health_ <= 0.0f; }
    void setAmbientRadiation(float radPerSec){ ambientRadiation_ = radPerSec; }

    // ---- Взаимодействие с блоками
    const RayHit& target() const { return target_; }
    float miningProgress() const { return miningProgress_; }   // 0..1

    const std::string& lastMessage() const { return message_; }
    float messageAge() const { return messageAge_; }

private:
    void updateMovement(const SurvivorInput& in, float dt);
    void updateMetabolism(float dt);
    void updateInteraction(const SurvivorInput& in, float dt);
    // Пересекается ли коробка игрока в позиции p с твёрдыми блоками.
    bool collides(Vec3 p) const;
    void say(const std::string& text);

    VoxelWorld& voxels_;
    const Environment& env_;
    Inventory& inventory_;

    Vec3 pos_{};              // точка под ногами (центр коробки по горизонтали)
    float velY_ = 0.0f;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    bool onGround_ = true, inWater_ = false, crouch_ = false, sprinting_ = false;
    float currentSpeed_ = 0.0f;
    float fallStartY_ = 0.0f;

    float health_ = 100.0f, hunger_ = 100.0f, thirst_ = 100.0f, stamina_ = 100.0f;
    float radiation_ = 0.0f, ambientRadiation_ = 0.0f;
    float bodyTemp_ = 36.6f;

    RayHit target_;
    float miningProgress_ = 0.0f;
    int miningX_ = 0, miningY_ = 0, miningZ_ = 0;
    float placeCooldown_ = 0.0f;

    std::string message_;
    float messageAge_ = 999.0f;
};
