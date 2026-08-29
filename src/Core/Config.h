#pragma once
// ==================== КОНФИГУРАЦИЯ ====================
// Плоское хранилище "ключ = значение" для server.cfg. Формат намеренно примитивный —
// как в конфигах выделенных серверов: одна настройка на строку, комментарии через # или //,
// секции обозначаются префиксом ключа через точку (world.seed, server.maxplayers).
//
// Приоритет источников (позже — сильнее):
//   1. значения по умолчанию в коде (getInt(key, def));
//   2. файл конфигурации;
//   3. аргументы командной строки (+world.seed 12345 или --world.seed=12345).
// Так админ может разово переопределить любой параметр при запуске, не трогая файл.
#include <string>
#include <map>
#include <vector>

class Config {
public:
    bool loadFile(const std::string& path);      // false, если файла нет (не ошибка — берутся умолчания)
    void applyArgs(int argc, char** argv);       // +key value, --key=value, --key value
    bool saveFile(const std::string& path) const; // выгрузка текущих значений (для отладки)

    bool has(const std::string& key) const;
    void set(const std::string& key, const std::string& value);

    std::string getString(const std::string& key, const std::string& def = "") const;
    int         getInt(const std::string& key, int def = 0) const;
    float       getFloat(const std::string& key, float def = 0.0f) const;
    bool        getBool(const std::string& key, bool def = false) const; // true/yes/on/1

    // Все ключи по возрастанию — для команды `config` в консоли сервера.
    std::vector<std::string> keys() const;

private:
    std::map<std::string, std::string> values_;
};
