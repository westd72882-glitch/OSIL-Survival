#include "ConsoleHost.h"
#include "../Core/Log.h"

#include <iostream>

std::vector<std::string> splitArgs(const std::string& line){
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for(char c : line){
        if(c == '"'){ inQuotes = !inQuotes; continue; }
        if(!inQuotes && (c == ' ' || c == '\t')){
            if(!cur.empty()){ out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if(!cur.empty()) out.push_back(cur);
    return out;
}

void ConsoleHost::registerCommand(const ConsoleCommand& cmd){
    commands_[cmd.name] = cmd;
}

std::string ConsoleHost::execute(const std::string& line){
    std::vector<std::string> parts = splitArgs(line);
    if(parts.empty()) return "";

    std::string name = parts[0];
    for(char& c : name) if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

    auto it = commands_.find(name);
    if(it == commands_.end())
        return "неизвестная команда: " + name + " (введите help)";

    std::vector<std::string> args(parts.begin() + 1, parts.end());
    return it->second.handler(args);
}

void ConsoleHost::startStdinThread(){
    if(running_) return;
    running_ = true;
    thread_ = std::thread([this](){
        std::string line;
        while(running_ && std::getline(std::cin, line)){
            if(line.empty()) continue;
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push(line);
        }
        // stdin закрыт (сервер запущен под systemd без терминала) — просто выходим:
        // управление дальше идёт только по RCON и сигналам.
    });
}

void ConsoleHost::stopStdinThread(){
    running_ = false;
    // Поток висит на getline и разбудить его переносимо нельзя; отцепляем — процесс
    // всё равно завершается, и ОС закроет дескриптор.
    if(thread_.joinable()) thread_.detach();
}

void ConsoleHost::drain(){
    for(;;){
        std::string line;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(pending_.empty()) return;
            line = pending_.front();
            pending_.pop();
        }
        std::string reply = execute(line);
        if(!reply.empty()) LOG_INFO("%s", reply.c_str());
    }
}
