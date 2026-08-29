#include "TestHarness.h"
#include "../src/Core/Log.h"

#include <cstring>
#include <exception>
#include <stdexcept>

namespace {
struct TestCase { const char* name; TestFn fn; };
// Функция-обёртка вместо глобального вектора: порядок инициализации глобальных объектов
// между единицами трансляции не определён, и регистрация из статических конструкторов
// в разных .cpp могла бы попасть в ещё не построенный вектор.
std::vector<TestCase>& registry(){
    static std::vector<TestCase> tests;
    return tests;
}
} // namespace

void registerTest(const char* name, TestFn fn){ registry().push_back({name, fn}); }

void testFail(const char* file, int line, const std::string& message){
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%d: %s", file, line, message.c_str());
    throw std::runtime_error(buf);
}

int runAllTests(const char* filter){
    // Тесты сами по себе шумные не должны быть: логи мира глушим до предупреждений.
    logSetLevel(LogLevel::WARN);

    int passed = 0, failed = 0, skipped = 0;
    for(const TestCase& t : registry()){
        if(filter && *filter && !strstr(t.name, filter)){ ++skipped; continue; }
        try {
            t.fn();
            printf("  [ ok ] %s\n", t.name);
            ++passed;
        } catch(const std::exception& e){
            printf("  [ПРОВАЛ] %s\n         %s\n", t.name, e.what());
            ++failed;
        } catch(...){
            printf("  [ПРОВАЛ] %s\n         неизвестное исключение\n", t.name);
            ++failed;
        }
    }
    printf("\nитого: успешно %d, провалено %d, пропущено %d\n", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv){
    const char* filter = argc > 1 ? argv[1] : "";
    printf("=== тесты OSIL Survival ===\n");
    return runAllTests(filter);
}
