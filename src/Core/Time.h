#pragma once
// ==================== ВРЕМЯ И ТИК СЕРВЕРА ====================
// Сервер авторитарен, значит вся симуляция обязана идти ФИКСИРОВАННЫМ шагом: иначе
// урон падения, регенерация и таймеры крафта поедут в зависимости от нагрузки машины,
// и один и тот же выстрел на разных серверах даст разный результат.
//
// TickClock — накопитель (accumulator): реальное время копится, симуляция делает целое
// число шагов по dt, остаток переносится на следующий кадр. Чтобы «спираль смерти»
// (сервер не успевает и начинает считать всё больше шагов) не убила сервер совсем,
// за один проход выполняется не больше maxCatchUpTicks шагов, а остальное отбрасывается
// с предупреждением в лог.
#include <cstdint>

// Монотонное время в миллисекундах с момента старта процесса (не зависит от часов ОС).
int64_t nowMillis();
// Уснуть на указанное число миллисекунд (для холостого хода тика сервера).
void sleepMillis(int64_t ms);

class TickClock {
public:
    explicit TickClock(int ticksPerSecond = 30, int maxCatchUpTicks = 5);

    // Вызывать раз за проход цикла. Возвращает, сколько шагов симуляции надо выполнить.
    int advance();

    float  tickDelta() const { return tickDelta_; }      // длительность шага, секунды
    int    ticksPerSecond() const { return tps_; }
    uint64_t totalTicks() const { return totalTicks_; }
    // Сколько миллисекунд можно спать, чтобы не крутить цикл вхолостую.
    int64_t millisUntilNextTick() const;
    // Сколько шагов было отброшено из-за перегрузки — метрика здоровья сервера.
    uint64_t droppedTicks() const { return droppedTicks_; }

private:
    int      tps_;
    int      maxCatchUp_;
    float    tickDelta_;
    int64_t  lastMillis_;
    double   accumulatorMs_ = 0.0;
    uint64_t totalTicks_ = 0;
    uint64_t droppedTicks_ = 0;
};
