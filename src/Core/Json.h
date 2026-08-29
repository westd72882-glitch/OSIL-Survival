#pragma once
// ==================== МИНИМАЛЬНЫЙ JSON-ПАРСЕР ====================
// Нужен ровно для одного: разобрать JSON-заголовок внутри .glb (описание сцены glTF —
// узлы, меши, аксессоры, скины, анимации). Внешнюю библиотеку тянуть не хочется: в
// проекте нет менеджера зависимостей для C++, а всё остальное (SDL, curl, mbedTLS)
// собирается из исходников через FetchContent и уже заметно раздувает время сборки CI.
//
// Сознательные упрощения (glTF-заголовку их достаточно):
//   - парсим в дерево значений, без потоковой обработки;
//   - все числа — double (в glTF все индексы и координаты укладываются в double точно);
//   - escape-последовательности в строках поддержаны только базовые (\" \\ \/ \n \r \t);
//   - \uXXXX не разворачивается в UTF-8 (в glTF-заголовках встречается исчезающе редко,
//     и только в именах — на геометрию не влияет).

#include <string>
#include <vector>
#include <map>

enum class JsonType { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

struct JsonValue {
    JsonType type = JsonType::NUL;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;

    // ---- Удобные аксессоры: не бросают исключений, а возвращают значение по умолчанию.
    // Загрузчик моделей обязан переживать неполный/непривычный файл без падения, поэтому
    // весь доступ к полям идёт только через них.
    bool has(const std::string& key) const;
    const JsonValue& operator[](const std::string& key) const; // отсутствует -> NUL-значение
    const JsonValue& operator[](size_t index) const;           // выход за границы -> NUL-значение
    size_t size() const;                                        // для массивов; иначе 0

    int asInt(int def = 0) const;
    float asFloat(float def = 0.0f) const;
    double asDouble(double def = 0.0) const;
    bool asBool(bool def = false) const;
    std::string asString(const std::string& def = "") const;
    bool isNull() const { return type == JsonType::NUL; }
};

// Разбирает JSON из буфера. При синтаксической ошибке возвращает false, а out остаётся
// в неопределённом (но валидном) состоянии.
bool jsonParse(const char* data, size_t length, JsonValue& out);
