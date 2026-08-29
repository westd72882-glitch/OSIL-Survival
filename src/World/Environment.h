#pragma once
// ==================== ВРЕМЯ СУТОК И ПОГОДА ====================
// По ТЗ: полные сутки за 60 минут реального времени, смена погоды (дождь, туман,
// снегопад). Обе системы живут на сервере и влияют на игру, а не только на картинку:
// ночью хуже видно и холоднее (расход еды/тепла), дождь наполняет ёмкости водой и
// глушит звук шагов, снегопад в горах ускоряет переохлаждение.
//
// Погода детерминирована: время мира делится на «эпохи» фиксированной длины, и погода
// эпохи — функция от (сид, номер эпохи). Клиенту достаточно знать время мира, чтобы
// показать ту же самую погоду, — по сети гонять состояние погоды не нужно вовсе.
#include "WorldConfig.h"

#include <cstdint>

enum class Weather : uint8_t {
    Clear = 0,  // ясно
    Cloudy,     // облачно
    Rain,       // дождь
    Fog,        // туман
    Snow,       // снегопад (в горах и на холодном севере)
    Storm,      // гроза
    COUNT
};

const char* weatherName(Weather w);
const char* weatherId(Weather w);

class Environment {
public:
    explicit Environment(const WorldConfig& cfg);

    // Шаг симуляции (dt — секунды реального времени).
    void tick(float dt);

    // ---- Время
    double worldSeconds() const { return worldSeconds_; }     // всего прошло игрового времени
    float  timeOfDay() const;      // часы 0..24
    int    dayNumber() const;      // сутки с начала вайпа, с 1
    bool   isNight() const;        // солнце ниже горизонта
    float  sunAltitude() const;    // высота солнца, радианы (-pi/2..pi/2)
    float  lightLevel() const;     // 0 — глухая ночь, 1 — полдень; с учётом погоды
    // Перевод в человекочитаемое "День 3, 14:35".
    const char* timeString() const;

    // Установить время суток (админская команда `time 12`).
    void setTimeOfDay(float hours);

    // ---- Погода
    Weather weather() const { return weather_; }
    float weatherIntensity() const { return intensity_; }   // 0..1
    float windSpeed() const { return windSpeed_; }          // м/с
    float windDirection() const { return windDir_; }        // радианы
    // Поправка к температуре окружения от времени суток и погоды, °C.
    float temperatureModifier() const;
    // Множитель видимости для ИИ и рендера: 1 — ясный день, 0.2 — ночь в тумане.
    float visibilityFactor() const;
    // Идёт ли осадок, наполняющий ёмкости водой.
    bool isRaining() const { return weather_ == Weather::Rain || weather_ == Weather::Storm; }

    // Принудительная погода (админская команда `weather rain`); держится до конца эпохи.
    void forceWeather(Weather w, float intensity);

private:
    void updateWeather();
    Weather rollWeather(uint64_t epoch, float& outIntensity) const;

    WorldConfig cfg_;
    double worldSeconds_ = 0.0;
    double dayLengthSeconds_ = 3600.0;

    Weather weather_ = Weather::Clear;
    float intensity_ = 0.0f;
    uint64_t currentEpoch_ = (uint64_t)-1;
    bool forced_ = false;
    float windSpeed_ = 2.0f;
    float windDir_ = 0.0f;

    mutable char timeBuf_[32] = {0};
};
