#include "Json.h"
#include <cstdlib>
#include <cstring>

// Общий "пустой" результат для промахов по ключу/индексу: аксессоры возвращают ссылку
// на него, чтобы вызывающий код мог спокойно писать j["a"]["b"][3].asInt() даже если
// половины пути в файле нет.
static const JsonValue g_nullValue;

bool JsonValue::has(const std::string& key) const {
    return type == JsonType::OBJECT && objectValue.find(key) != objectValue.end();
}
const JsonValue& JsonValue::operator[](const std::string& key) const {
    if(type != JsonType::OBJECT) return g_nullValue;
    auto it = objectValue.find(key);
    return (it == objectValue.end()) ? g_nullValue : it->second;
}
const JsonValue& JsonValue::operator[](size_t index) const {
    if(type != JsonType::ARRAY || index >= arrayValue.size()) return g_nullValue;
    return arrayValue[index];
}
size_t JsonValue::size() const {
    return (type == JsonType::ARRAY) ? arrayValue.size() : 0;
}
int JsonValue::asInt(int def) const {
    return (type == JsonType::NUMBER) ? (int)numberValue : def;
}
float JsonValue::asFloat(float def) const {
    return (type == JsonType::NUMBER) ? (float)numberValue : def;
}
double JsonValue::asDouble(double def) const {
    return (type == JsonType::NUMBER) ? numberValue : def;
}
bool JsonValue::asBool(bool def) const {
    return (type == JsonType::BOOL) ? boolValue : def;
}
std::string JsonValue::asString(const std::string& def) const {
    return (type == JsonType::STRING) ? stringValue : def;
}

// ==================== ПАРСЕР ====================
namespace {

struct Parser {
    const char* p;
    const char* end;

    bool eof() const { return p >= end; }
    void skipWhitespace(){
        while(p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }

    bool parseValue(JsonValue& out);

    bool parseString(std::string& out){
        if(eof() || *p != '"') return false;
        p++; // открывающая кавычка
        out.clear();
        while(p < end && *p != '"'){
            if(*p == '\\'){
                p++;
                if(eof()) return false;
                switch(*p){
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u':
                        // \uXXXX пропускаем целиком (см. оговорку в Json.h): в glTF такие
                        // последовательности встречаются только в именах узлов/анимаций и
                        // на разбор геометрии не влияют.
                        if(end - p < 5) return false;
                        p += 4;
                        out.push_back('?');
                        break;
                    default: return false;
                }
                p++;
            } else {
                out.push_back(*p++);
            }
        }
        if(eof()) return false; // строка не закрыта
        p++; // закрывающая кавычка
        return true;
    }

    bool parseNumber(JsonValue& out){
        const char* start = p;
        if(p < end && (*p == '-' || *p == '+')) p++;
        while(p < end && ((*p >= '0' && *p <= '9') || *p == '.' ||
                          *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
        if(p == start) return false;
        std::string tmp(start, p - start);
        out.type = JsonType::NUMBER;
        out.numberValue = strtod(tmp.c_str(), nullptr);
        return true;
    }

    bool parseArray(JsonValue& out){
        if(eof() || *p != '[') return false;
        p++;
        out.type = JsonType::ARRAY;
        skipWhitespace();
        if(!eof() && *p == ']'){ p++; return true; } // пустой массив
        while(true){
            JsonValue item;
            skipWhitespace();
            if(!parseValue(item)) return false;
            out.arrayValue.push_back(std::move(item));
            skipWhitespace();
            if(eof()) return false;
            if(*p == ','){ p++; continue; }
            if(*p == ']'){ p++; return true; }
            return false;
        }
    }

    bool parseObject(JsonValue& out){
        if(eof() || *p != '{') return false;
        p++;
        out.type = JsonType::OBJECT;
        skipWhitespace();
        if(!eof() && *p == '}'){ p++; return true; } // пустой объект
        while(true){
            skipWhitespace();
            std::string key;
            if(!parseString(key)) return false;
            skipWhitespace();
            if(eof() || *p != ':') return false;
            p++;
            JsonValue value;
            skipWhitespace();
            if(!parseValue(value)) return false;
            out.objectValue[key] = std::move(value);
            skipWhitespace();
            if(eof()) return false;
            if(*p == ','){ p++; continue; }
            if(*p == '}'){ p++; return true; }
            return false;
        }
    }
};

bool Parser::parseValue(JsonValue& out){
    skipWhitespace();
    if(eof()) return false;
    char c = *p;
    if(c == '{') return parseObject(out);
    if(c == '[') return parseArray(out);
    if(c == '"'){
        out.type = JsonType::STRING;
        return parseString(out.stringValue);
    }
    if(c == 't'){
        if(end - p < 4 || strncmp(p, "true", 4) != 0) return false;
        p += 4; out.type = JsonType::BOOL; out.boolValue = true; return true;
    }
    if(c == 'f'){
        if(end - p < 5 || strncmp(p, "false", 5) != 0) return false;
        p += 5; out.type = JsonType::BOOL; out.boolValue = false; return true;
    }
    if(c == 'n'){
        if(end - p < 4 || strncmp(p, "null", 4) != 0) return false;
        p += 4; out.type = JsonType::NUL; return true;
    }
    return parseNumber(out);
}

} // namespace

bool jsonParse(const char* data, size_t length, JsonValue& out){
    if(!data || length == 0) return false;
    Parser parser{ data, data + length };
    if(!parser.parseValue(out)) return false;
    parser.skipWhitespace();
    return true; // хвостовой мусор после корневого значения игнорируем
}
