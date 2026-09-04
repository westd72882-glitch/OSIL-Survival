#pragma once
// ==================== СЕТЕВОЙ КЛИЕНТ ====================
// Держит связь с игровым сервером в отдельном потоке: раз в 100 мс отправляет своё
// состояние и накопленные правки мира, забирает чужие. Игровой цикл с сетью не
// разговаривает напрямую — он кладёт своё состояние и забирает снимок чужого, поэтому
// никакая задержка сети не роняет кадры.
#include "../Net/Http.h"
#include "../Net/Protocol.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class NetClient {
public:
    ~NetClient();

    // Подключение: адрес вида «host», «host:порт» или «http://host:порт».
    // Возвращает false, если сервер не ответил (текст причины — в statusText()).
    bool join(const std::string& address, const std::string& playerName);
    void leave();

    bool connected() const { return connected_.load(); }
    int  playerId() const { return id_.load(); }
    unsigned long long seed() const { return seed_; }
    std::string statusText() const;
    std::string serverName() const;
    int  maxPlayers() const { return maxPlayers_.load(); }
    // Сколько народу на сервере сейчас: остальные плюс мы сами.
    int  onlineCount() const;

    // ---- Обмен с игровым циклом
    void setLocalState(const net::PlayerState& st);
    // Правка мира от НАС: её надо разослать остальным.
    void pushEdit(int x, int y, int z, int block);
    // Снимок остальных игроков (копия — её можно спокойно читать в отрисовке).
    std::vector<net::PlayerState> players() const;
    // Правки, пришедшие от других: забираются один раз и применяются к миру.
    std::vector<net::Edit> takeEdits();
    float serverTimeOfDay() const { return serverTime_.load(); }
    // Сколько миллисекунд занял последний обмен — это и есть пинг в списке серверов.
    int  pingMs() const { return ping_.load(); }

    // Разовый опрос сервера для списка в меню: имя, игроки и пинг.
    struct Info {
        bool ok = false;
        std::string name, map;
        int players = 0, max = 0, ping = 0;
    };
    static Info query(const std::string& address);
    // Подбирает рабочий адрес: «host» без схемы сначала пробуется как игровой сервер на
    // http:28015, а если там тишина — как облачный на https. Так один и тот же список
    // серверов работает и со своей машиной, и с хостингом.
    static bool resolve(const std::string& address, net::Url& out, Info& info);

private:
    void loop();

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int>  id_{0};
    std::atomic<int>  ping_{0};
    std::atomic<float> serverTime_{8.0f};
    std::atomic<int> maxPlayers_{100};
    unsigned long long seed_ = 0;

    // Разобранный адрес: у него уже выбрана схема (http или https) и порт, поэтому
    // сетевой поток не гадает заново на каждом обмене.
    net::Url url_;
    mutable std::mutex mutex_;
    std::string address_, name_, status_, serverName_;
    net::PlayerState local_;
    std::vector<net::Edit> outgoing_;   // наши правки, ждущие отправки
    std::vector<net::Edit> incoming_;   // чужие правки, ждущие применения
    std::vector<net::PlayerState> others_;
    long long since_ = 0;
};
