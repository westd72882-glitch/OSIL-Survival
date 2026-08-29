#include "Config.h"
#include "Log.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if(a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string stripQuotes(const std::string& s){
    if(s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::string lower(std::string s){
    for(char& c : s) if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
} // namespace

bool Config::loadFile(const std::string& path){
    std::ifstream in(path);
    if(!in.is_open()){
        LOG_WARN("конфиг не найден: %s (используются значения по умолчанию)", path.c_str());
        return false;
    }

    std::string line;
    int lineNo = 0;
    while(std::getline(in, line)){
        ++lineNo;
        // Комментарий может стоять и в конце строки, но только если он не внутри кавычек:
        // пароли и названия серверов вполне могут содержать '#'.
        bool inQuotes = false;
        for(size_t i = 0; i < line.size(); ++i){
            if(line[i] == '"') inQuotes = !inQuotes;
            if(inQuotes) continue;
            if(line[i] == '#' || (line[i] == '/' && i + 1 < line.size() && line[i+1] == '/')){
                line = line.substr(0, i);
                break;
            }
        }

        line = trim(line);
        if(line.empty()) continue;

        // Допускаются оба вида: "key = value" и "key value".
        size_t sep = line.find('=');
        std::string key, value;
        if(sep != std::string::npos){
            key   = trim(line.substr(0, sep));
            value = trim(line.substr(sep + 1));
        } else {
            size_t sp = line.find_first_of(" \t");
            if(sp == std::string::npos){
                LOG_WARN("%s:%d — строка без значения: '%s'", path.c_str(), lineNo, line.c_str());
                continue;
            }
            key   = trim(line.substr(0, sp));
            value = trim(line.substr(sp + 1));
        }
        if(key.empty()) continue;
        values_[lower(key)] = stripQuotes(value);
    }
    LOG_INFO("конфиг загружен: %s (%d параметров)", path.c_str(), (int)values_.size());
    return true;
}

void Config::applyArgs(int argc, char** argv){
    for(int i = 1; i < argc; ++i){
        std::string arg = argv[i];
        if(arg.size() < 2) continue;

        std::string key, value;
        if(arg.rfind("--", 0) == 0){
            arg = arg.substr(2);
            size_t eq = arg.find('=');
            if(eq != std::string::npos){
                key = arg.substr(0, eq);
                value = arg.substr(eq + 1);
            } else if(i + 1 < argc){
                key = arg;
                value = argv[++i];
            } else {
                key = arg; value = "true"; // флаг без значения
            }
        } else if(arg[0] == '+' && i + 1 < argc){
            key = arg.substr(1);
            value = argv[++i];
        } else {
            continue;
        }
        if(key.empty()) continue;
        values_[lower(key)] = stripQuotes(value);
        LOG_DEBUG("аргумент командной строки: %s = %s", key.c_str(), value.c_str());
    }
}

bool Config::saveFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if(!out.is_open()) return false;
    out << "# OSIL Survival — снимок действующей конфигурации\n";
    for(const auto& kv : values_) out << kv.first << " = " << kv.second << "\n";
    return true;
}

bool Config::has(const std::string& key) const { return values_.count(lower(key)) > 0; }
void Config::set(const std::string& key, const std::string& value){ values_[lower(key)] = value; }

std::string Config::getString(const std::string& key, const std::string& def) const {
    auto it = values_.find(lower(key));
    return it == values_.end() ? def : it->second;
}

int Config::getInt(const std::string& key, int def) const {
    auto it = values_.find(lower(key));
    if(it == values_.end() || it->second.empty()) return def;
    char* end = nullptr;
    long v = strtol(it->second.c_str(), &end, 10);
    if(end == it->second.c_str()){
        LOG_WARN("параметр %s: '%s' — не целое число, взято %d", key.c_str(), it->second.c_str(), def);
        return def;
    }
    return (int)v;
}

float Config::getFloat(const std::string& key, float def) const {
    auto it = values_.find(lower(key));
    if(it == values_.end() || it->second.empty()) return def;
    char* end = nullptr;
    float v = strtof(it->second.c_str(), &end);
    if(end == it->second.c_str()){
        LOG_WARN("параметр %s: '%s' — не число, взято %f", key.c_str(), it->second.c_str(), (double)def);
        return def;
    }
    return v;
}

bool Config::getBool(const std::string& key, bool def) const {
    auto it = values_.find(lower(key));
    if(it == values_.end()) return def;
    std::string v = lower(trim(it->second));
    if(v == "1" || v == "true" || v == "yes" || v == "on")  return true;
    if(v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

std::vector<std::string> Config::keys() const {
    std::vector<std::string> out;
    out.reserve(values_.size());
    for(const auto& kv : values_) out.push_back(kv.first);
    return out;
}
