#include "WorldConfig.h"
#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Core/Random.h"
#include "../Core/Math.h"
#include "../Core/Time.h"

#include <cmath>
#include <cstdio>
#include <ctime>

WorldConfig WorldConfig::fromConfig(const Config& cfg){
    WorldConfig w;
    std::string seedText = cfg.getString("world.seed", "0");
    w.seed = seedFromString(seedText.c_str());
    if(w.seed == 0){
        // Сид 0 означает «выбрать случайно и записать в лог», чтобы карту можно было
        // повторить: администратор просто копирует напечатанное число в конфиг.
        w.seed = splitMix64((uint64_t)time(nullptr) ^ (uint64_t)nowMillis());
        LOG_INFO("world.seed не задан — выбран случайный сид: %llu", (unsigned long long)w.seed);
    }

    w.size              = cfg.getFloat("world.size", w.size);
    w.heightGridStep    = cfg.getFloat("world.gridstep", w.heightGridStep);
    w.maxHeight         = cfg.getFloat("world.maxheight", w.maxHeight);
    w.waterLevel        = cfg.getFloat("world.waterlevel", w.waterLevel);
    w.riversEnabled     = cfg.getBool ("world.rivers", w.riversEnabled);
    w.resourceDensity   = cfg.getFloat("world.resourcedensity", w.resourceDensity);
    w.monumentCount     = cfg.getInt  ("world.monuments", w.monumentCount);
    w.dayLengthMinutes  = cfg.getFloat("world.daylength", w.dayLengthMinutes);
    w.startTimeOfDay    = cfg.getFloat("world.starttime", w.startTimeOfDay);
    w.sanitize();
    return w;
}

void WorldConfig::sanitize(){
    // Частоты шума заданы в 1/метрах и подобраны под карту 4000 м. На карте меньшего
    // размера их надо пересчитать, иначе весь остров попадает в один «холм» шума и
    // рельеф вырождается в пологий купол без гор и без равнин.
    if(size > 100.0f && fabsf(size - 4000.0f) > 1.0f && !frequenciesScaled){
        float k = 4000.0f / size;
        continentFrequency  *= k;
        hillFrequency       *= k;
        mountainFrequency   *= k;
        warpFrequency       *= k;
        moistureFrequency   *= k;
        temperatureFrequency*= k;
        riverFrequency      *= k;
        warpStrength        /= k;   // искажение задано в метрах — уменьшается вместе с картой
        riverWidth          *= 1.0f;
        frequenciesScaled = true;
    }

    // Слишком маленькая карта ломает расстановку монументов, слишком большая —
    // съедает память под сетку высот (квадратичный рост).
    if(size < 1000.0f){ LOG_WARN("world.size=%.0f мало, поднято до 1000", (double)size); size = 1000.0f; }
    if(size > 8000.0f){ LOG_WARN("world.size=%.0f велико, срезано до 8000", (double)size); size = 8000.0f; }
    if(heightGridStep < 1.0f) heightGridStep = 1.0f;
    if(heightGridStep > 16.0f) heightGridStep = 16.0f;
    if(maxHeight < 40.0f) maxHeight = 40.0f;
    waterLevel = clampf(waterLevel, 0.0f, maxHeight * 0.5f);
    if(resourceDensity < 0.05f) resourceDensity = 0.05f;
    if(resourceDensity > 5.0f)  resourceDensity = 5.0f;
    if(monumentCount < 0) monumentCount = 0;
    if(monumentCount > 40) monumentCount = 40;
    if(dayLengthMinutes < 1.0f) dayLengthMinutes = 1.0f;
    startTimeOfDay = clampf(startTimeOfDay, 0.0f, 23.99f);
}

int WorldConfig::gridSize() const {
    return (int)(size / heightGridStep) + 1;
}

std::string WorldConfig::describe() const {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "сид=%llu размер=%.0fx%.0f м, сетка=%dx%d (шаг %.1f м), вода=%.1f м, "
             "макс. высота=%.1f м, реки=%s, монументов=%d, сутки=%.0f мин",
             (unsigned long long)seed, (double)size, (double)size,
             gridSize(), gridSize(), (double)heightGridStep,
             (double)waterLevel, (double)maxHeight,
             riversEnabled ? "да" : "нет", monumentCount, (double)dayLengthMinutes);
    return std::string(buf);
}
