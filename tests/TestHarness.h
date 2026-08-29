#pragma once
// ==================== МИНИ-ФРЕЙМВОРК ТЕСТОВ ====================
// Внешний фреймворк (gtest/Catch2) сюда не тянем по той же причине, что и в остальном
// проекте: сборка обязана подниматься на голой системе одной командой cmake без сети.
// Нужного здесь ровно три вещи — регистрация теста, проверка и понятный отчёт.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

typedef void (*TestFn)();
void registerTest(const char* name, TestFn fn);
int runAllTests(const char* filter);
// Проверка провалилась: бросаем исключение, чтобы остальные тесты всё равно прошли.
void testFail(const char* file, int line, const std::string& message);

struct TestRegistrar { TestRegistrar(const char* name, TestFn fn){ registerTest(name, fn); } };

#define TEST(name)                                              \
    static void name();                                         \
    static TestRegistrar registrar_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                            \
    do { if(!(cond)) testFail(__FILE__, __LINE__, "не выполнено: " #cond); } while(0)

#define CHECK_MSG(cond, msg)                                                   \
    do { if(!(cond)) testFail(__FILE__, __LINE__, std::string("не выполнено: " #cond " — ") + (msg)); } while(0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        double va = (double)(a), vb = (double)(b);                             \
        if(std::fabs(va - vb) > (double)(eps)){                                \
            char buf[256];                                                     \
            snprintf(buf, sizeof(buf), "ожидалось %g ≈ %g (допуск %g)", va, vb, (double)(eps)); \
            testFail(__FILE__, __LINE__, buf);                                 \
        }                                                                      \
    } while(0)

#define CHECK_RANGE(v, lo, hi)                                                 \
    do {                                                                       \
        double vv = (double)(v);                                               \
        if(vv < (double)(lo) || vv > (double)(hi)){                            \
            char buf[256];                                                     \
            snprintf(buf, sizeof(buf), #v " = %g вне диапазона [%g, %g]", vv, (double)(lo), (double)(hi)); \
            testFail(__FILE__, __LINE__, buf);                                 \
        }                                                                      \
    } while(0)
