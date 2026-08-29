#include "Environment.h"
#include "../Core/Log.h"
#include "../Core/Math.h"
#include "../Core/Random.h"

#include <cmath>
#include <cstdio>

namespace {
const uint64_t SALT_WEATHER = 0x4001;
// Длина погодной эпохи в игровых секундах. 1/8 суток ≈ 7.5 реальных минут при часовом
// цикле: достаточно долго, чтобы дождь успел «пожить», и достаточно коротко, чтобы
// погода не застревала на весь вечер.
const double WEATHER_EPOCH_FRACTION = 1.0 / 8.0;
} // namespace

const char* weatherName(Weather w){
    switch(w){
        case Weather::Clear:  return "Ясно";
        case Weather::Cloudy: return "Облачно";
        case Weather::Rain:   return "Дождь";
        case Weather::Fog:    return "Туман";
        case Weather::Snow:   return "Снегопад";
        case Weather::Storm:  return "Гроза";
        default: return "?";
    }
}

const char* weatherId(Weather w){
    switch(w){
        case Weather::Clear:  return "clear";
        case Weather::Cloudy: return "cloudy";
        case Weather::Rain:   return "rain";
        case Weather::Fog:    return "fog";
        case Weather::Snow:   return "snow";
        case Weather::Storm:  return "storm";
        default: return "clear";
    }
}

Environment::Environment(const WorldConfig& cfg) : cfg_(cfg) {
    dayLengthSeconds_ = (double)cfg_.dayLengthMinutes * 60.0;
    // Стартовое время суток задаётся конфигом: свежий сервер обычно поднимают утром,
    // чтобы первые игроки не появились в темноте без единого факела.
    worldSeconds_ = (double)cfg_.startTimeOfDay / 24.0 * dayLengthSeconds_;
    updateWeather();
}

void Environment::tick(float dt){
    worldSeconds_ += (double)dt;
    updateWeather();
    // Ветер плавно «дышит» вокруг силы, заданной погодой: резкие скачки выглядели бы
    // как телепорт для листвы и звука на клиенте.
    float target = 2.0f;
    switch(weather_){
        case Weather::Storm: target = 16.0f; break;
        case Weather::Rain:  target = 8.0f;  break;
        case Weather::Snow:  target = 6.0f;  break;
        case Weather::Fog:   target = 0.5f;  break;
        case Weather::Cloudy:target = 4.0f;  break;
        default:             target = 2.0f;  break;
    }
    windSpeed_ += (target * (0.6f + intensity_ * 0.6f) - windSpeed_) * clampf(dt * 0.15f, 0.0f, 1.0f);
    windDir_ += dt * 0.01f;
    if(windDir_ > 6.28318f) windDir_ -= 6.28318f;
}

float Environment::timeOfDay() const {
    double frac = fmod(worldSeconds_, dayLengthSeconds_) / dayLengthSeconds_;
    if(frac < 0.0) frac += 1.0;
    return (float)(frac * 24.0);
}

int Environment::dayNumber() const {
    return (int)(worldSeconds_ / dayLengthSeconds_) + 1;
}

float Environment::sunAltitude() const {
    // Простая модель: солнце встаёт в 6:00, садится в 18:00, максимум в полдень.
    float h = timeOfDay();
    float angle = (h - 6.0f) / 12.0f * 3.14159265f; // 0 на восходе, pi на закате
    return sinf(angle) * 1.4f - 0.35f;              // ночью уходит ниже горизонта
}

bool Environment::isNight() const { return sunAltitude() < 0.0f; }

float Environment::lightLevel() const {
    float sun = clampf(sunAltitude() / 1.05f, -0.35f, 1.0f);
    float base = clampf(sun, 0.0f, 1.0f);
    // Даже глухой ночью есть луна и звёзды: 0.06 — минимум, иначе игра становится
    // чёрным экраном, а не «страшной ночью».
    base = 0.06f + base * 0.94f;
    switch(weather_){
        case Weather::Storm:  base *= 0.45f; break;
        case Weather::Rain:   base *= 0.7f;  break;
        case Weather::Fog:    base *= 0.75f; break;
        case Weather::Snow:   base *= 0.8f;  break;
        case Weather::Cloudy: base *= 0.85f; break;
        default: break;
    }
    return clampf(base, 0.0f, 1.0f);
}

const char* Environment::timeString() const {
    float h = timeOfDay();
    int hours = (int)h;
    int minutes = (int)((h - (float)hours) * 60.0f);
    snprintf(timeBuf_, sizeof(timeBuf_), "День %d, %02d:%02d", dayNumber(), hours, minutes);
    return timeBuf_;
}

void Environment::setTimeOfDay(float hours){
    hours = clampf(hours, 0.0f, 23.99f);
    double dayStart = floor(worldSeconds_ / dayLengthSeconds_) * dayLengthSeconds_;
    worldSeconds_ = dayStart + (double)hours / 24.0 * dayLengthSeconds_;
    LOG_INFO("время мира изменено: %s", timeString());
}

Weather Environment::rollWeather(uint64_t epoch, float& outIntensity) const {
    Rng rng(splitMix64(cfg_.seed ^ SALT_WEATHER ^ (epoch * 0x9e3779b97f4a7c15ULL)));
    float roll = rng.nextFloat();
    Weather w;
    // Распределение: большую часть времени играбельная погода, тяжёлые условия — редко.
    if(roll < 0.42f)      w = Weather::Clear;
    else if(roll < 0.66f) w = Weather::Cloudy;
    else if(roll < 0.80f) w = Weather::Rain;
    else if(roll < 0.90f) w = Weather::Fog;
    else if(roll < 0.96f) w = Weather::Snow;
    else                  w = Weather::Storm;
    outIntensity = rng.nextRange(0.35f, 1.0f);
    return w;
}

void Environment::updateWeather(){
    uint64_t epoch = (uint64_t)(worldSeconds_ / (dayLengthSeconds_ * WEATHER_EPOCH_FRACTION));
    if(epoch == currentEpoch_) return;
    currentEpoch_ = epoch;
    if(forced_){
        // Принудительно выставленная админом погода живёт до конца эпохи, дальше
        // мир возвращается к своему детерминированному расписанию.
        forced_ = false;
        return;
    }
    Weather next = rollWeather(epoch, intensity_);
    if(next != weather_)
        LOG_INFO("погода сменилась: %s -> %s (сила %.0f%%)", weatherName(weather_),
                 weatherName(next), (double)(intensity_ * 100.0f));
    weather_ = next;
}

void Environment::forceWeather(Weather w, float intensity){
    weather_ = w;
    intensity_ = clampf(intensity, 0.0f, 1.0f);
    forced_ = true;
    LOG_INFO("погода задана вручную: %s (сила %.0f%%)", weatherName(w), (double)(intensity_ * 100.0f));
}

float Environment::temperatureModifier() const {
    // Ночь холоднее дня примерно на 12 °C; дождь и снег добавляют минус.
    float night = 1.0f - clampf((sunAltitude() + 0.35f) / 1.4f, 0.0f, 1.0f);
    float mod = -12.0f * night;
    switch(weather_){
        case Weather::Rain:  mod -= 5.0f * intensity_; break;
        case Weather::Storm: mod -= 8.0f * intensity_; break;
        case Weather::Snow:  mod -= 10.0f * intensity_; break;
        case Weather::Fog:   mod -= 2.0f * intensity_; break;
        default: break;
    }
    return mod;
}

float Environment::visibilityFactor() const {
    float v = lightLevel();
    switch(weather_){
        case Weather::Fog:   v *= 0.30f + 0.25f * (1.0f - intensity_); break;
        case Weather::Storm: v *= 0.45f; break;
        case Weather::Rain:  v *= 0.65f; break;
        case Weather::Snow:  v *= 0.55f; break;
        default: break;
    }
    return clampf(v, 0.05f, 1.0f);
}
