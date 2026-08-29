#pragma once
// ==================== КОНСОЛЬ ВЫДЕЛЕННОГО СЕРВЕРА ====================
// Администратор управляет сервером командами в stdin (и, начиная со 2-го этапа, теми же
// командами по RCON — реестр общий). Чтение stdin блокирующее, поэтому оно вынесено в
// отдельный поток: главный поток обязан крутить тик симуляции строго по часам и не имеет
// права ждать ввода. Команды складываются в очередь и исполняются В ГЛАВНОМ ПОТОКЕ,
// между тиками, — так обработчику не нужны блокировки при доступе к миру и игрокам.
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct ConsoleCommand {
    std::string name;
    std::string usage;   // "time [часы]"
    std::string help;    // одна строка описания
    // Возвращает текст ответа (уйдёт в лог/в RCON-сессию).
    std::function<std::string(const std::vector<std::string>& args)> handler;
};

class ConsoleHost {
public:
    void registerCommand(const ConsoleCommand& cmd);
    // Разбирает строку и вызывает обработчик. Используется и для stdin, и для RCON.
    std::string execute(const std::string& line);

    void startStdinThread();   // фоновое чтение stdin
    void stopStdinThread();
    // Выполнить все накопленные команды (вызывается из главного цикла).
    void drain();

    const std::map<std::string, ConsoleCommand>& commands() const { return commands_; }

private:
    std::map<std::string, ConsoleCommand> commands_;
    std::queue<std::string> pending_;
    std::mutex mutex_;
    std::thread thread_;
    bool running_ = false;
};

// Разбор строки на слова с учётом кавычек: ban "Игрок с пробелом" 3d чит.
std::vector<std::string> splitArgs(const std::string& line);
