#pragma once
// ==================== МИР: ГЕНЕРАЦИЯ И ЗАПРОСЫ ====================
// Класс World — то, что на сервере является «землёй»: он строит карту высот из шума
// Перлина, хранит её в памяти целиком и отвечает на запросы, которые нужны симуляции
// каждый тик: какая тут высота, какой уклон (можно ли поставить фундамент), какой биом
// (что тут растёт и как быстро хочется пить), вода это или суша.
//
// Почему сетка, а не «считать шум на каждый запрос»: запрос высоты идёт на КАЖДОЕ
// движение каждого игрока и каждой пули — это тысячи вызовов в тик. Шум с 5 октавами
// и искажением координат стоит ~микросекунду, сетка с билинейной выборкой — десятки
// наносекунд. Память: карта 4000 м с шагом 4 м = 1001x1001 float ≈ 4 МБ на слой.
//
// Детерминизм: весь результат — функция только от WorldConfig (в первую очередь от сида).
// Никаких обращений к системному времени, потокам или порядку вызовов внутри генерации.
#include "WorldConfig.h"
#include "Biome.h"
#include "../Core/Math.h"
#include "../Core/Noise.h"
#include "../Core/Random.h"

#include <vector>

// Полная сводка по точке карты — то, что нужно и симуляции, и предпросмотру.
struct WorldSample {
    float height = 0.0f;        // метры над уровнем моря
    float slopeDegrees = 0.0f;  // уклон поверхности
    Vec3  normal{0, 1, 0};
    float moisture01 = 0.0f;
    float temperature01 = 0.0f;
    float zone01 = 0.5f;        // положение в зональной раскладке биомов (0 — запад, 1 — восток)
    float ambientTempC = 0.0f;  // «ощущаемая» температура биома с поправкой на высоту
    Biome biome = Biome::Ocean;
    bool  underwater = false;
};

class World {
public:
    explicit World(const WorldConfig& cfg);

    // Строит карты высот/влажности/температуры. Тяжёлая операция (секунды), вызывается
    // один раз при старте сервера или утилиты предпросмотра.
    void generate();
    bool isGenerated() const { return generated_; }

    const WorldConfig& config() const { return cfg_; }

    // ---- Запросы (все безопасны для координат вне карты: возвращают краевые значения).
    float heightAt(float x, float z) const;
    Vec3  normalAt(float x, float z) const;
    float slopeAt(float x, float z) const;     // градусы
    float moistureAt(float x, float z) const;  // 0..1
    float temperatureAt(float x, float z) const; // 0..1
    // Зональная раскладка биомов: 0 — пустынный запад, 1 — снежный восток, середина —
    // равнина. Считается от координаты с искажением шумом, поэтому граница зон живая,
    // но сами зоны остаются крупными и предсказуемыми.
    float zoneAt(float x, float z) const;
    Biome biomeAt(float x, float z) const;
    WorldSample sampleAt(float x, float z) const;

    bool inBounds(float x, float z) const;
    bool isWater(float x, float z) const;
    // Можно ли поставить сюда фундамент: суша, не слишком круто, не в воде.
    bool isBuildable(float x, float z, float maxSlopeDegrees = 40.0f) const;

    // Случайная точка возрождения «в чистом поле»: суша, пологий уклон, подальше от воды.
    // Возвращает точку на поверхности; y — высота земли.
    Vec3 findSpawnPoint(Rng& rng, int attempts = 512) const;

    // ---- Доступ к сеткам (нужен утилите предпросмотра и клиенту при построении меша).
    int gridSize() const { return gridSize_; }
    const std::vector<float>& heightGrid() const { return heights_; }

    // Статистика по биомам (доля клеток), считается после generate().
    const float* biomeFractions() const { return biomeFraction_; }

private:
    // «Сырая» высота из шума в конкретной точке — источник для сетки.
    float generateHeightRaw(float x, float z) const;
    float generateTemperatureRaw(float x, float z, float height) const;
    // Влажность считается по одной формуле и при построении сетки, и при запросе
    // «на лету» — иначе поля разъедутся, и биом до генерации отличался бы от биома после.
    float generateMoistureRaw(float x, float z, float height) const;
    // Расстояние до края карты в нормированной «скруглённо-квадратной» метрике:
    // 0 в центре, 1 на границе.
    float edgeDistance(float x, float z) const;
    // Маска острова: 1 в центре карты, 0 у краёв — гарантирует океан по периметру.
    float islandMask(float x, float z) const;
    // Глубина вреза русла реки в этой точке (0 — реки нет).
    float riverCarve(float x, float z) const;

    // Билинейная выборка из произвольной сетки размером gridSize_ x gridSize_.
    float sampleGrid(const std::vector<float>& grid, float x, float z) const;

    WorldConfig cfg_;
    int gridSize_ = 0;
    bool generated_ = false;

    PerlinNoise continentNoise_, hillNoise_, mountainNoise_, warpNoise_;
    PerlinNoise moistureNoise_, temperatureNoise_, riverNoise_;

    std::vector<float> heights_;      // метры
    std::vector<float> moisture_;     // 0..1
    std::vector<float> temperature_;  // 0..1
    float biomeFraction_[(int)Biome::COUNT] = {0};
};
