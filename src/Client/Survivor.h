#pragma once
// ==================== ИГРОК: ДВИЖЕНИЕ, СОСТОЯНИЕ, ДОБЫЧА ====================
// Заготовка систем 3-го этапа, доведённая до играбельного минимума: по карте можно
// ходить, бегать, приседать, прыгать, падать с уроном, плавать, есть-пить и добывать
// ресурсы. Числа взяты из ТЗ (здоровье/голод/жажда/выносливость по 100, регенерация
// 1 HP/с при сытости и питье выше 80).
//
// Здесь же — единственное место, где живёт «правда» о персонаже на клиенте. На 2-м
// этапе этот же расчёт переедет на сервер, а клиент оставит себе предсказание: структура
// намеренно не завязана ни на ввод, ни на рендер — на вход подаются готовые намерения.
#include "../Core/Math.h"
#include "../World/Environment.h"
#include "../World/Resources.h"
#include "../World/World.h"

#include <string>

// Намерения игрока за кадр — то, что на 2-м этапе будет уезжать на сервер пакетом ввода.
struct SurvivorInput {
    float moveX = 0, moveY = 0;   // -1..1 в системе координат игрока
    float yaw = 0, pitch = 0;     // куда смотрит (радианы)
    bool sprint = false;
    bool crouch = false;
    bool jump = false;            // одноразовое нажатие
    bool attack = false;          // удерживается
    bool action = false;          // одноразовое: подобрать/использовать
};

// Что игрок насобирал. Полноценный инвентарь на 30 слотов — 3-й этап; пока счётчики.
struct Gathered {
    int wood = 0, stone = 0, metalOre = 0, sulfurOre = 0;
    int cloth = 0, food = 0;
    int total() const { return wood + stone + metalOre + sulfurOre + cloth + food; }
};

class Survivor {
public:
    Survivor(const World& world, ResourceMap& resources, const Environment& env);

    void spawn(Vec3 position);
    void update(const SurvivorInput& in, float dt);

    // ---- Состояние
    Vec3 position() const { return pos_; }
    Vec3 eyePosition() const;
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
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

    const Gathered& gathered() const { return loot_; }
    // Прогресс добычи текущей цели 0..1 и её название — для полоски на экране.
    float gatherProgress() const { return gatherProgress_; }
    const std::string& targetName() const { return targetName_; }

    // Напиться из водоёма. Вода из рек и озёр грязная: утоляет жажду, но иногда
    // отнимает немного здоровья (полноценное отравление и фляги — 3-й этап).
    void drinkWater();

    // Радиация задаётся снаружи (её считает карта монументов).
    void setAmbientRadiation(float radPerSec){ ambientRadiation_ = radPerSec; }

    // Сообщение о последнем событии (собрал ресурс, попил, упал) — строка для HUD.
    const std::string& lastMessage() const { return message_; }
    float messageAge() const { return messageAge_; }

private:
    void updateMovement(const SurvivorInput& in, float dt);
    void updateMetabolism(float dt);
    void updateGathering(const SurvivorInput& in, float dt);
    void say(const std::string& text);

    const World& world_;
    ResourceMap& resources_;
    const Environment& env_;

    Vec3 pos_{};
    float velY_ = 0.0f;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    bool onGround_ = true, inWater_ = false, crouch_ = false, sprinting_ = false;
    float currentSpeed_ = 0.0f;
    float fallStartY_ = 0.0f;   // с какой высоты начали падать — для урона при приземлении

    float health_ = 100.0f, hunger_ = 100.0f, thirst_ = 100.0f, stamina_ = 100.0f;
    float radiation_ = 0.0f, ambientRadiation_ = 0.0f;
    float bodyTemp_ = 36.6f;

    Gathered loot_;
    const ResourceNode* target_ = nullptr;
    float gatherProgress_ = 0.0f;
    float gatherCooldown_ = 0.0f;
    std::string targetName_;

    std::string message_;
    float messageAge_ = 999.0f;
};
