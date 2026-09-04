#pragma once
// ==================== ИГРОВОЙ СЕРВЕР ПО СЕТИ ====================
// Держит список живых игроков и журнал правок мира, отвечает на четыре запроса
// протокола (см. src/Net/Protocol.h) и умеет рассказать о себе по «/» — этим же
// адресом хостинг проверяет, что сервис жив.
//
// Состояние целиком в памяти: мир детерминирован и восстанавливается из сида, а
// постройки живут в журнале правок. Сохранение на диск — отдельная работа, и без него
// сервер честно теряет постройки при перезапуске (о чём написано в docs/network.md).
#include "../Net/Http.h"
#include "../Net/Protocol.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class NetServer {
public:
    struct Config {
        std::string name = "OSIL Survival";
        std::string map = "Survival Island";
        int         maxPlayers = 100;
        unsigned long long seed = 0;
        int         port = 28015;
        float       timeoutSeconds = 12.0f;   // без /sync дольше — игрок «отвалился»
    };

    bool start(const Config& cfg);
    void stop();
    // Зовётся из тика сервера: выкидывает отвалившихся и двигает время суток.
    void update(float dt, float timeOfDay);
    int  playerCount() const;

private:
    std::string handle(const std::string& method, const std::string& path,
                       const std::string& body, int& status);
    std::string statusJson() const;

    struct Slot {
        net::PlayerState state;
        float silence = 0.0f;    // сколько секунд от игрока ничего не приходило
    };

    mutable std::mutex mutex_;
    Config cfg_;
    net::Server http_;
    std::unordered_map<int, Slot> players_;
    std::vector<net::Edit> journal_;      // правки мира по порядку
    long long headSeq_ = 0;
    int nextId_ = 1;
    float timeOfDay_ = 8.0f;
    // Журнал не растёт бесконечно: старые правки уже разъехались по всем, кто в игре.
    static const size_t JOURNAL_LIMIT = 20000;
};
