// ==================== ТЕСТЫ ЯДРА ====================
// Главное, что здесь проверяется, — ДЕТЕРМИНИЗМ. Если генератор случайных чисел или шум
// начнут выдавать разные значения при одинаковом сиде, мир на сервере и на клиенте
// разъедется, и это проявится не сразу, а как «невидимые деревья» и провалы под землю.
#include "TestHarness.h"
#include "../src/Core/Config.h"
#include "../src/Core/Math.h"
#include "../src/Core/Noise.h"
#include "../src/Core/Random.h"
#include "../src/Core/Sha256.h"
#include "../src/Core/Time.h"

#include <cstdio>
#include <fstream>
#include <set>

TEST(rng_повторяем_при_том_же_сиде){
    Rng a(12345), b(12345);
    for(int i = 0; i < 1000; ++i) CHECK(a.nextU32() == b.nextU32());
}

TEST(rng_разные_сиды_дают_разные_потоки){
    Rng a(1), b(2);
    int same = 0;
    for(int i = 0; i < 100; ++i) if(a.nextU32() == b.nextU32()) ++same;
    CHECK(same < 5);
}

TEST(rng_диапазоны_соблюдаются){
    Rng rng(777);
    for(int i = 0; i < 10000; ++i){
        CHECK_RANGE(rng.nextFloat(), 0.0, 1.0);
        CHECK_RANGE(rng.nextRange(-5.0f, 5.0f), -5.0, 5.0);
        int v = rng.nextInt(3, 7);
        CHECK(v >= 3 && v <= 7);
        CHECK(rng.nextBelow(10) < 10);
    }
}

TEST(rng_chance_даёт_ожидаемую_частоту){
    Rng rng(99);
    int hits = 0;
    const int n = 20000;
    for(int i = 0; i < n; ++i) if(rng.chance(0.25f)) ++hits;
    // 25% от 20000 = 5000; допуск 3% абсолютных — статистический разброс много меньше.
    CHECK_NEAR((double)hits / n, 0.25, 0.03);
}

TEST(rngForCell_не_зависит_от_порядка_обхода){
    // Ключевое свойство рассева ресурсов: клетку можно посчитать в любой момент.
    uint32_t direct = rngForCell(17, 42, 555, 0x2001).nextU32();
    Rng warmup(1); for(int i = 0; i < 100; ++i) warmup.nextU32();
    uint32_t afterOthers = rngForCell(17, 42, 555, 0x2001).nextU32();
    CHECK(direct == afterOthers);
    // Разные соли — независимые слои.
    CHECK(rngForCell(17, 42, 555, 0x2001).nextU32() != rngForCell(17, 42, 555, 0x2002).nextU32());
}

TEST(seedFromString_числа_как_есть_слова_хешируются){
    CHECK(seedFromString("12345") == 12345ULL);
    CHECK(seedFromString("osil") == seedFromString("osil"));
    CHECK(seedFromString("osil") != seedFromString("osil2"));
}

TEST(шум_повторяем_и_в_диапазоне){
    PerlinNoise a(4242), b(4242);
    NoiseParams p;
    for(int i = 0; i < 500; ++i){
        float x = (float)i * 3.7f, y = (float)i * -1.9f;
        float va = a.fbm(x, y, p), vb = b.fbm(x, y, p);
        CHECK(va == vb);
        CHECK_RANGE(va, -1.05, 1.05);
        CHECK_RANGE(a.ridged(x, y, p), -0.01, 1.01);
        CHECK_RANGE(a.billow(x, y, p), -0.01, 1.01);
    }
}

TEST(шум_непрерывен){
    // Соседние точки не должны отличаться скачком: иначе на карте появятся обрывы,
    // сквозь которые проваливается физика.
    PerlinNoise n(7);
    NoiseParams p; p.frequency = 1.0f / 100.0f;
    for(int i = 0; i < 200; ++i){
        float x = (float)i * 5.0f;
        float a = n.fbm(x, 0.0f, p);
        float b = n.fbm(x + 0.5f, 0.0f, p);
        CHECK(std::fabs(a - b) < 0.1f);
    }
}

TEST(шум_разные_сиды_дают_разные_поля){
    PerlinNoise a(1), b(2);
    NoiseParams p;
    int different = 0;
    for(int i = 0; i < 100; ++i){
        float x = (float)i * 13.0f;
        if(std::fabs(a.fbm(x, x, p) - b.fbm(x, x, p)) > 1e-4f) ++different;
    }
    CHECK(different > 90);
}

TEST(математика_зажимы_и_интерполяции){
    CHECK_NEAR(clampf(5.0f, 0.0f, 1.0f), 1.0, 1e-6);
    CHECK_NEAR(clampf(-5.0f, 0.0f, 1.0f), 0.0, 1e-6);
    CHECK_NEAR(lerpf(0.0f, 10.0f, 0.25f), 2.5, 1e-6);
    CHECK_NEAR(invLerpf(10.0f, 20.0f, 15.0f), 0.5, 1e-6);
    CHECK_NEAR(smoothstepf(0.0f, 1.0f, 0.5f), 0.5, 1e-6);
    CHECK_NEAR(smoothstepf(0.0f, 1.0f, -1.0f), 0.0, 1e-6);
    CHECK_NEAR(v2dist(Vec2{0,0}, Vec2{3,4}), 5.0, 1e-5);
    CHECK_NEAR(horizontalDist(Vec3{0, 100, 0}, Vec3{3, -50, 4}), 5.0, 1e-5);
}

TEST(конфиг_разбирает_файл_и_аргументы){
    const char* path = "test_config_tmp.cfg";
    {
        std::ofstream out(path);
        out << "# комментарий\n";
        out << "server.hostname = \"Мой сервер # не комментарий\"\n";
        out << "server.maxplayers 150\n";
        out << "world.seed = 4242   // хвостовой комментарий\n";
        out << "server.pvp = yes\n";
    }
    Config cfg;
    CHECK(cfg.loadFile(path));
    CHECK(cfg.getString("server.hostname") == "Мой сервер # не комментарий");
    CHECK(cfg.getInt("server.maxplayers", 0) == 150);
    CHECK(cfg.getInt("world.seed", 0) == 4242);
    CHECK(cfg.getBool("server.pvp", false) == true);
    CHECK(cfg.getInt("нет.такого", 7) == 7);

    // Аргументы командной строки сильнее файла.
    const char* argv[] = { "osil_server", "+world.seed", "999", "--server.maxplayers=64" };
    cfg.applyArgs(4, (char**)argv);
    CHECK(cfg.getInt("world.seed", 0) == 999);
    CHECK(cfg.getInt("server.maxplayers", 0) == 64);
    remove(path);
}

TEST(тик_идёт_фиксированным_шагом){
    TickClock clock(30);
    CHECK_NEAR(clock.tickDelta(), 1.0 / 30.0, 1e-6);
    sleepMillis(120);
    int steps = clock.advance();
    // За 120 мс при 30 Гц должно набежать 3-4 шага (планировщик ОС даёт разброс).
    CHECK(steps >= 2 && steps <= 5);
    CHECK(clock.totalTicks() == (uint64_t)steps);
}

TEST(sha256_совпадает_с_эталоном){
    // Контрольные значения из спецификации FIPS 180-4 — если реализация где-то
    // ошиблась, пароли аккаунтов будут хешироваться «во что-то своё», и вход перестанет
    // работать после любой правки.
    CHECK(sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
