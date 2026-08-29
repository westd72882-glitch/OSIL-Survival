#include "ServerApp.h"
#include "../Core/Log.h"
#include "../Core/Text.h"
#include "../Core/Time.h"

#include <atomic>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {
std::atomic<bool> g_shutdown{false};

void handleSignal(int sig){
    // В обработчике сигнала можно только выставить флаг: логировать и трогать мир отсюда
    // нельзя — это не async-signal-safe и легко ловит взаимоблокировку на мьютексе лога.
    (void)sig;
    g_shutdown.store(true);
}

std::string fmt(const char* f, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
;

std::string fmt(const char* f, ...){
    char buf[1024];
    va_list args;
    va_start(args, f);
    vsnprintf(buf, sizeof(buf), f, args);
    va_end(args);
    return std::string(buf);
}
} // namespace

ServerSettings ServerSettings::fromConfig(const Config& cfg){
    ServerSettings s;
    s.hostname    = cfg.getString("server.hostname", s.hostname);
    s.description = cfg.getString("server.description", s.description);
    s.port        = cfg.getInt("server.port", s.port);
    s.maxPlayers  = cfg.getInt("server.maxplayers", s.maxPlayers);
    s.tickRate    = cfg.getInt("server.tickrate", s.tickRate);
    s.saveIntervalSeconds = cfg.getInt("server.saveinterval", s.saveIntervalSeconds);
    s.savePath    = cfg.getString("server.savepath", s.savePath);
    s.logFile     = cfg.getString("server.logfile", s.logFile);
    s.raidHoursEnabled = cfg.getBool("raid.enabled", s.raidHoursEnabled);
    s.raidStartHour    = cfg.getInt("raid.starthour", s.raidStartHour);
    s.raidEndHour      = cfg.getInt("raid.endhour", s.raidEndHour);

    if(s.maxPlayers < 1) s.maxPlayers = 1;
    if(s.maxPlayers > 500) s.maxPlayers = 500;
    // Тикрейт ниже 10 делает попадания «желейными», выше 60 — упирается в процессор
    // при 100 игроках и почти ничего не даёт: стрельба всё равно проверяется
    // лагокомпенсацией по истории позиций (2-й этап).
    if(s.tickRate < 10) s.tickRate = 10;
    if(s.tickRate > 60) s.tickRate = 60;
    return s;
}

std::string ServerSettings::describe() const {
    return fmt("\"%s\", порт %d, до %d игроков, тик %d Гц, сохранение каждые %d с в %s",
               hostname.c_str(), port, maxPlayers, tickRate, saveIntervalSeconds, savePath.c_str());
}

void ServerApp::requestShutdown(){ g_shutdown.store(true); }

void ServerApp::registerCommands(){
    console_.registerCommand({"help", "help", "список команд",
        [this](const std::vector<std::string>&) -> std::string {
            std::string out = "доступные команды:";
            for(const auto& kv : console_.commands())
                out += "\n  " + kv.second.usage + std::string(" — ") + kv.second.help;
            return out;
        }});

    console_.registerCommand({"status", "status", "состояние сервера",
        [this](const std::vector<std::string>&) -> std::string {
            return fmt("%s | %s | погода: %s (%.0f%%), ветер %.1f м/с | тиков %llu | игроков 0/%d",
                       settings_.hostname.c_str(), env_->timeString(),
                       weatherName(env_->weather()), (double)(env_->weatherIntensity() * 100.0f),
                       (double)env_->windSpeed(), (unsigned long long)tickCounter_,
                       settings_.maxPlayers);
        }});

    console_.registerCommand({"time", "time [часы]", "показать или задать время суток",
        [this](const std::vector<std::string>& args) -> std::string {
            if(args.empty())
                return fmt("%s (%s, освещённость %.0f%%)", env_->timeString(),
                           env_->isNight() ? "ночь" : "день", (double)(env_->lightLevel() * 100.0f));
            env_->setTimeOfDay((float)atof(args[0].c_str()));
            return fmt("время: %s", env_->timeString());
        }});

    console_.registerCommand({"weather", "weather [clear|cloudy|rain|fog|snow|storm] [0..1]",
        "показать или задать погоду",
        [this](const std::vector<std::string>& args) -> std::string {
            if(args.empty())
                return fmt("погода: %s, сила %.0f%%, ветер %.1f м/с, видимость %.0f%%",
                           weatherName(env_->weather()), (double)(env_->weatherIntensity() * 100.0f),
                           (double)env_->windSpeed(), (double)(env_->visibilityFactor() * 100.0f));
            Weather w = Weather::Clear;
            bool found = false;
            for(int i = 0; i < (int)Weather::COUNT; ++i){
                if(args[0] == weatherId((Weather)i)){ w = (Weather)i; found = true; break; }
            }
            if(!found) return "неизвестная погода: " + args[0];
            float intensity = args.size() > 1 ? (float)atof(args[1].c_str()) : 0.8f;
            env_->forceWeather(w, intensity);
            return fmt("погода: %s", weatherName(w));
        }});

    console_.registerCommand({"world", "world", "параметры мира",
        [this](const std::vector<std::string>&) -> std::string {
            const float* f = world_->biomeFractions();
            return fmt("%s\nбиомы: океан %.1f%%, берег %.1f%%, равнина %.1f%%, лес %.1f%%, "
                       "пустыня %.1f%%, болото %.1f%%, снег %.1f%%",
                       worldConfig_.describe().c_str(),
                       (double)(f[(int)Biome::Ocean] * 100.f), (double)(f[(int)Biome::Beach] * 100.f),
                       (double)(f[(int)Biome::Grassland] * 100.f), (double)(f[(int)Biome::Forest] * 100.f),
                       (double)(f[(int)Biome::Desert] * 100.f), (double)(f[(int)Biome::Swamp] * 100.f),
                       (double)(f[(int)Biome::Snow] * 100.f));
        }});

    console_.registerCommand({"monuments", "monuments", "список монументов",
        [this](const std::vector<std::string>&) -> std::string {
            std::string out = fmt("монументов: %zu", monuments_->monuments().size());
            for(const Monument& m : monuments_->monuments())
                out += fmt("\n  %s (%5.0f, %5.0f) радиус %3.0f м, радиация %.1f, лут %d",
                           padRightUtf8(m.name, 20).c_str(), (double)m.pos.x, (double)m.pos.z,
                           (double)m.radius, (double)m.radiation, m.lootTier);
            return out;
        }});

    console_.registerCommand({"probe", "probe <x> <z>", "что находится в точке карты",
        [this](const std::vector<std::string>& args) -> std::string {
            if(args.size() < 2) return "нужно: probe <x> <z>";
            float x = (float)atof(args[0].c_str());
            float z = (float)atof(args[1].c_str());
            WorldSample s = world_->sampleAt(x, z);
            size_t nearby = resources_->query(x, z, 30.0f).size();
            return fmt("(%.0f, %.0f): высота %.1f м, уклон %.1f°, биом %s, влажность %.2f, "
                       "температура %.0f°C, радиация %.1f, объектов в 30 м: %zu%s",
                       (double)x, (double)z, (double)s.height, (double)s.slopeDegrees,
                       biomeName(s.biome), (double)s.moisture01,
                       (double)(s.ambientTempC + env_->temperatureModifier()),
                       (double)monuments_->radiationAt(x, z), nearby,
                       world_->isBuildable(x, z) ? ", строить можно" : ", строить нельзя");
        }});

    console_.registerCommand({"spawn", "spawn", "подобрать точку возрождения",
        [this](const std::vector<std::string>&) -> std::string {
            Rng rng(splitMix64(worldConfig_.seed ^ (uint64_t)nowMillis()));
            for(int i = 0; i < 24; ++i){
                Vec3 p = world_->findSpawnPoint(rng);
                if(!monuments_->isSafeSpawn(p.x, p.z)) continue;
                return fmt("точка возрождения: (%.0f, %.1f, %.0f), биом %s",
                           (double)p.x, (double)p.y, (double)p.z,
                           biomeName(world_->biomeAt(p.x, p.z)));
            }
            return "не удалось подобрать безопасную точку (карта перегружена монументами)";
        }});

    console_.registerCommand({"config", "config [ключ]", "показать конфигурацию",
        [this](const std::vector<std::string>& args) -> std::string {
            if(!args.empty()) return args[0] + " = " + config_.getString(args[0], "(не задано)");
            std::string out = "конфигурация:";
            for(const std::string& k : config_.keys()) out += "\n  " + k + " = " + config_.getString(k);
            return out;
        }});

    // ---- Администрирование. Реестр заведён уже сейчас, чтобы 2-й этап только подставил
    // реальные действия: команды, их имена и формат ответа менять не придётся.
    auto notYet = [](const char* what){
        return [what](const std::vector<std::string>&) -> std::string {
            return std::string(what) + ": появится вместе с сетевым слоем (этап 2)";
        };
    };
    console_.registerCommand({"players", "players", "список игроков", notYet("список игроков")});
    console_.registerCommand({"kick", "kick <игрок> [причина]", "выгнать игрока", notYet("кик")});
    console_.registerCommand({"ban",  "ban <игрок> [срок] [причина]", "забанить игрока", notYet("бан")});
    console_.registerCommand({"unban","unban <игрок>", "снять бан", notYet("разбан")});
    console_.registerCommand({"save", "save", "сохранить мир", notYet("сохранение")});

    console_.registerCommand({"quit", "quit", "остановить сервер",
        [](const std::vector<std::string>&) -> std::string {
            ServerApp::requestShutdown();
            return "останавливаюсь...";
        }});
    console_.registerCommand({"stop", "stop", "то же, что quit",
        [](const std::vector<std::string>&) -> std::string {
            ServerApp::requestShutdown();
            return "останавливаюсь...";
        }});
}

void ServerApp::tick(float dt){
    // Порядок шага симуляции (расширяется на следующих этапах):
    //   1. приём пакетов и ввода игроков        (этап 2)
    //   2. движение и физика                    (этап 2)
    //   3. метаболизм, крафт, печи, ловушки     (этап 3-4)
    //   4. NPC и мировые события                (этап 5)
    //   5. окружение (время суток и погода)     — уже здесь
    //   6. рассылка снапшотов                   (этап 2)
    env_->tick(dt);
    ++tickCounter_;
}

void ServerApp::logStatus(){
    LOG_INFO("[статус] %s | погода: %s | тиков: %llu | игроков 0/%d",
             env_->timeString(), weatherName(env_->weather()),
             (unsigned long long)tickCounter_, settings_.maxPlayers);
}

int ServerApp::run(int argc, char** argv){
    // 1. Конфигурация: файл, затем аргументы командной строки (они сильнее).
    std::string configPath = "config/server.cfg";
    for(int i = 1; i < argc - 1; ++i){
        std::string a = argv[i];
        if(a == "--config" || a == "-c") configPath = argv[i + 1];
    }
    config_.loadFile(configPath);
    config_.applyArgs(argc, argv);

    logSetLevel(logLevelFromString(config_.getString("server.loglevel", "info")));
    settings_ = ServerSettings::fromConfig(config_);
    if(!settings_.logFile.empty()) logSetFile(settings_.logFile);

    LOG_INFO("=== OSIL Survival — выделенный сервер (этап 1: мир и симуляция) ===");
    LOG_INFO("сервер: %s", settings_.describe().c_str());

    // 2. Мир.
    worldConfig_ = WorldConfig::fromConfig(config_);
    world_.reset(new World(worldConfig_));
    world_->generate();
    resources_.reset(new ResourceMap(*world_));
    resources_->generate();
    monuments_.reset(new MonumentMap(*world_));
    monuments_->generate();
    env_.reset(new Environment(worldConfig_));
    LOG_INFO("мир готов; сид %llu — сохраните его, чтобы повторить карту",
             (unsigned long long)worldConfig_.seed);

    // 3. Управление.
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    registerCommands();
    console_.startStdinThread();
    LOG_INFO("введите help для списка команд; Ctrl+C — остановка");

    // 4. Главный цикл: фиксированный тик, между тиками — сон, чтобы не жечь процессор.
    TickClock clock(settings_.tickRate);
    while(!g_shutdown.load()){
        console_.drain();

        int steps = clock.advance();
        for(int i = 0; i < steps; ++i) tick(clock.tickDelta());

        if(steps > 0){
            secondsSinceStatus_ += steps * clock.tickDelta();
            if(secondsSinceStatus_ >= 60.0){
                secondsSinceStatus_ = 0.0;
                logStatus();
            }
        }

        int64_t sleepMs = clock.millisUntilNextTick();
        sleepMillis(sleepMs > 0 ? (sleepMs > 5 ? 5 : sleepMs) : 1);
    }

    LOG_INFO("остановка сервера: тиков %llu, пропущено %llu",
             (unsigned long long)tickCounter_, (unsigned long long)clock.droppedTicks());
    console_.stopStdinThread();
    logShutdown();
    return 0;
}
