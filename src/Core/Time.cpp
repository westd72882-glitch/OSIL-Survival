#include "Time.h"
#include "Log.h"

#include <chrono>
#include <thread>

namespace {
const std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();
}

int64_t nowMillis(){
    auto d = std::chrono::steady_clock::now() - g_start;
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

void sleepMillis(int64_t ms){
    if(ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TickClock::TickClock(int ticksPerSecond, int maxCatchUpTicks)
    : tps_(ticksPerSecond < 1 ? 1 : ticksPerSecond),
      maxCatchUp_(maxCatchUpTicks < 1 ? 1 : maxCatchUpTicks) {
    tickDelta_ = 1.0f / (float)tps_;
    lastMillis_ = nowMillis();
}

int TickClock::advance(){
    int64_t now = nowMillis();
    double elapsed = (double)(now - lastMillis_);
    lastMillis_ = now;
    // Защита от скачка времени (заморозка процесса, миграция контейнера): такой
    // «провал» не отыгрываем, иначе за один кадр пройдут минуты симуляции.
    if(elapsed > 1000.0) elapsed = 1000.0;
    accumulatorMs_ += elapsed;

    double stepMs = 1000.0 / (double)tps_;
    int steps = 0;
    while(accumulatorMs_ >= stepMs && steps < maxCatchUp_){
        accumulatorMs_ -= stepMs;
        ++steps;
    }
    if(accumulatorMs_ >= stepMs){
        // Не догнали: считаем отброшенные шаги и сбрасываем накопитель.
        uint64_t dropped = (uint64_t)(accumulatorMs_ / stepMs);
        droppedTicks_ += dropped;
        accumulatorMs_ = 0.0;
        LOG_WARN("сервер не успевает: отброшено шагов %llu (всего %llu)",
                 (unsigned long long)dropped, (unsigned long long)droppedTicks_);
    }
    totalTicks_ += (uint64_t)steps;
    return steps;
}

int64_t TickClock::millisUntilNextTick() const {
    double stepMs = 1000.0 / (double)tps_;
    double left = stepMs - accumulatorMs_;
    if(left < 0.0) return 0;
    return (int64_t)left;
}
