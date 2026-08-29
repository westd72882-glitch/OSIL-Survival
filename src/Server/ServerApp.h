#pragma once
// ==================== ВЫДЕЛЕННЫЙ СЕРВЕР ====================
// Этап 1: сервер поднимает мир и крутит авторитарную симуляцию с фиксированным тиком.
// Сети и игроков ещё нет (это 2-й этап), но каркас уже тот самый, в который они лягут:
//   - конфигурация читается один раз и дальше не перечитывается;
//   - мир детерминирован и целиком лежит в памяти;
//   - симуляция идёт шагами tickrate, независимо от нагрузки;
//   - управление — через реестр консольных команд (общий со будущим RCON).
#include "ConsoleHost.h"
#include "../Core/Config.h"
#include "../World/Environment.h"
#include "../World/Monuments.h"
#include "../World/Resources.h"
#include "../World/World.h"

#include <memory>
#include <string>

struct ServerSettings {
    std::string hostname = "OSIL Survival [WIP]";
    std::string description = "Хардкорное выживание";
    int   port = 28015;
    int   maxPlayers = 100;
    int   tickRate = 30;             // шагов симуляции в секунду
    int   saveIntervalSeconds = 300; // как часто сохранять мир (этап 2 — SQLite)
    std::string savePath = "world.db";
    std::string logFile = "";
    bool  raidHoursEnabled = false;  // рейд-часы из ТЗ (окно, когда можно взрывать)
    int   raidStartHour = 18;
    int   raidEndHour = 6;

    static ServerSettings fromConfig(const Config& cfg);
    std::string describe() const;
};

class ServerApp {
public:
    // Возвращает код возврата процесса (0 — штатное завершение).
    int run(int argc, char** argv);
    // Просьба завершиться (из обработчика сигнала или команды quit).
    static void requestShutdown();

private:
    void registerCommands();
    void tick(float dt);
    void logStatus();

    Config config_;
    ServerSettings settings_;
    WorldConfig worldConfig_;
    std::unique_ptr<World> world_;
    std::unique_ptr<ResourceMap> resources_;
    std::unique_ptr<MonumentMap> monuments_;
    std::unique_ptr<Environment> env_;
    ConsoleHost console_;

    double secondsSinceStatus_ = 0.0;
    uint64_t tickCounter_ = 0;
};
