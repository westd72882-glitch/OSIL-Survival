#pragma once
// ==================== ПАРАМЕТРЫ МИРА ====================
// Один источник правды для генерации: и выделенный сервер, и утилита предпросмотра
// (osil_mapgen), и клиент на 5-м этапе строят мир из ЭТОЙ структуры. Любое поле,
// влияющее на геометрию, обязано попадать в сид-зависимый расчёт — иначе клиент и
// сервер разойдутся в высотах, и игроки будут «проваливаться» сквозь землю.
#include <cstdint>
#include <string>

class Config;

struct WorldConfig {
    uint64_t seed = 0;

    // ---- Размеры. Карта квадратная, координаты идут от 0 до size.
    float size = 1000.0f;
    // Шаг сетки высот в метрах. 4 м — компромисс: 1000x1000 отсчётов (4 МБ float),
    // сервер держит всю карту в памяти, а высота между узлами берётся билинейно.
    float heightGridStep = 4.0f;

    // ---- Вертикаль (метры над уровнем моря).
    float maxHeight   = 120.0f; // высшая точка гор
    float waterLevel  = 18.0f;  // уровень океана: всё ниже — вода
    float beachBand   = 4.0f;   // полоса пляжа над водой

    // ---- Форма материка: карта — остров, по краям гарантированно океан.
    // Без этого база у самой границы карты упиралась бы в невидимую стену, а не в берег.
    float islandFalloffStart = 0.56f; // доля полурадиуса, где начинается спад к морю
    float islandFalloffPower = 2.4f;

    // ---- Частоты слоёв (1/метры).
    float continentFrequency = 1.0f / 2600.0f; // общая форма суши
    float hillFrequency      = 1.0f / 420.0f;  // холмы
    float mountainFrequency  = 1.0f / 900.0f;  // хребты (ridged)
    float warpFrequency      = 1.0f / 700.0f;  // искажение координат
    float warpStrength       = 180.0f;         // метры
    float moistureFrequency  = 1.0f / 1500.0f;
    float temperatureFrequency = 1.0f / 1900.0f;
    float riverFrequency     = 1.0f / 1400.0f;

    // ---- Реки: русла вырезаются по «гребню» отдельного шума.
    bool  riversEnabled = true;
    float riverWidth = 0.020f;  // полуширина в единицах шума (не в метрах)
    float riverDepth = 9.0f;    // на сколько метров прорезается русло

    // ---- Ресурсы: шаг сетки рассева и общий множитель плотности.
    float resourceCellSize = 12.0f;
    float resourceDensity  = 1.0f;

    // Служебный признак: частоты уже приведены к размеру карты (см. sanitize).
    bool frequenciesScaled = false;

    // ---- Монументы (радиационные зоны и лут-точки).
    int   monumentCount = 0;   // локации отключены до этапа 5
    float monumentMinSpacing = 380.0f;

    // ---- Суточный цикл: полные сутки за 60 минут реального времени (ТЗ).
    float dayLengthMinutes = 60.0f;
    float startTimeOfDay   = 8.0f; // часы игрового времени на старте свежего мира

    // Читает значения из server.cfg (префикс world.*), недостающие оставляет по умолчанию.
    static WorldConfig fromConfig(const Config& cfg);
    // Проверка на вменяемость значений: правит и пишет предупреждения в лог.
    void sanitize();

    // Число узлов сетки высот по стороне (size/шаг + 1).
    int gridSize() const;
    std::string describe() const;
};
