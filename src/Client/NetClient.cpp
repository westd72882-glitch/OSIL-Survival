#include "NetClient.h"
#include "../Core/Json.h"
#include "../Core/Log.h"
#include "../Net/Http.h"

#include <chrono>

namespace {
long long nowMs(){
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

NetClient::~NetClient(){ leave(); }

NetClient::Info NetClient::query(const std::string& address){
    Info info;
    net::Url url;
    if(!net::parseUrl(address, url)) return info;
    std::string body;
    int status = 0;
    long long t0 = nowMs();
    if(!net::request(url, "GET", "/", "", body, status, 3000) || status != 200) return info;
    info.ping = (int)(nowMs() - t0);

    JsonValue root;
    if(!jsonParse(body.c_str(), body.size(), root)) return info;
    info.ok = true;
    info.name = root["name"].asString(address);
    info.map = root["map"].asString("Survival Island");
    info.players = root["players"].asInt();
    info.max = root["max"].asInt(100);
    return info;
}

bool NetClient::join(const std::string& address, const std::string& playerName){
    leave();

    net::Url url;
    if(!net::parseUrl(address, url)){
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "неверный адрес сервера";
        return false;
    }
    if(url.secure){
        std::lock_guard<std::mutex> lock(mutex_);
        // Честно говорим, в чём дело: TLS в клиенте не собран, а https без него не
        // открыть. Адрес того же сервера по http работает.
        status_ = "https пока не поддержан — укажите адрес по http";
        return false;
    }

    std::string body, reply;
    int status = 0;
    body = "{\"name\":\"" + net::jsonEscape(playerName) + "\"}";
    if(!net::request(url, "POST", "/join", body, reply, status, 5000)){
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "сервер не отвечает";
        return false;
    }
    if(status != 200){
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = (status == 503) ? "сервер полон" : "сервер отказал во входе";
        return false;
    }
    JsonValue root;
    if(!jsonParse(reply.c_str(), reply.size(), root) || root["id"].asInt() <= 0){
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "сервер ответил непонятным";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        address_ = address;
        name_ = playerName;
        serverName_ = root["name"].asString(address);
        since_ = (long long)root["head"].asDouble(0.0);
        outgoing_.clear();
        incoming_.clear();
        others_.clear();
        status_ = "подключено";
    }
    id_.store(root["id"].asInt());
    seed_ = (unsigned long long)root["seed"].asDouble(0.0);
    serverTime_.store(root["time"].asFloat(8.0f));
    connected_.store(true);
    running_.store(true);
    worker_ = std::thread([this]{ loop(); });
    LOG_INFO("сеть: вошли на сервер %s как %s (id %d)",
             serverName_.c_str(), playerName.c_str(), id_.load());
    return true;
}

void NetClient::leave(){
    if(running_.exchange(false)){
        if(worker_.joinable()) worker_.join();
    }
    if(connected_.exchange(false)){
        // Уходим по-человечески: сервер сразу уберёт нас из списка, а не через таймаут.
        net::Url url;
        std::string address;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            address = address_;
        }
        if(net::parseUrl(address, url)){
            std::string reply;
            int status = 0;
            net::request(url, "POST", "/leave",
                         "{\"id\":" + std::to_string(id_.load()) + "}", reply, status, 1500);
        }
    }
    id_.store(0);
    std::lock_guard<std::mutex> lock(mutex_);
    others_.clear();
    status_ = "не подключено";
}

std::string NetClient::statusText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::string NetClient::serverName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return serverName_;
}

void NetClient::setLocalState(const net::PlayerState& st){
    std::lock_guard<std::mutex> lock(mutex_);
    local_ = st;
    local_.id = id_.load();
    local_.name = name_;
}

void NetClient::pushEdit(int x, int y, int z, int block){
    if(!connected_.load()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    // Очередь ограничена: если сеть встала, лучше потерять хвост правок, чем память.
    if(outgoing_.size() > 4000) return;
    net::Edit e; e.x = x; e.y = y; e.z = z; e.block = block;
    outgoing_.push_back(e);
}

std::vector<net::PlayerState> NetClient::players() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return others_;
}

std::vector<net::Edit> NetClient::takeEdits(){
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<net::Edit> out;
    out.swap(incoming_);
    return out;
}

void NetClient::loop(){
    net::Url url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!net::parseUrl(address_, url)) return;
    }
    int failures = 0;
    while(running_.load()){
        net::PlayerState me;
        std::vector<net::Edit> send;
        long long since = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            me = local_;
            since = since_;
            // За один обмен отправляем не больше двух сотен правок: остальное уедет
            // следующим, а пакет останется небольшим.
            size_t take = outgoing_.size() > 200 ? 200 : outgoing_.size();
            send.assign(outgoing_.begin(), outgoing_.begin() + (long)take);
            outgoing_.erase(outgoing_.begin(), outgoing_.begin() + (long)take);
        }

        std::string body = net::encodeSyncRequest(me, send, since);
        std::string reply;
        int status = 0;
        long long t0 = nowMs();
        bool ok = net::request(url, "POST", "/sync", body, reply, status, 4000);
        ping_.store((int)(nowMs() - t0));

        if(ok && status == 200){
            failures = 0;
            std::vector<net::PlayerState> players;
            std::vector<net::Edit> edits;
            long long head = since;
            float time = serverTime_.load();
            if(net::decodeSyncResponse(reply, players, edits, head, time)){
                std::lock_guard<std::mutex> lock(mutex_);
                others_ = players;
                since_ = head;
                for(const net::Edit& e : edits) incoming_.push_back(e);
                status_ = "подключено";
            }
            serverTime_.store(time);
        } else if(status == 409){
            // Сервер нас забыл (таймаут) — переподключаемся тем же именем.
            std::string addr, nick;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                addr = address_; nick = name_;
                status_ = "переподключение...";
            }
            std::string join = "{\"name\":\"" + net::jsonEscape(nick) + "\"}";
            std::string reply2;
            int st2 = 0;
            if(net::request(url, "POST", "/join", join, reply2, st2, 4000) && st2 == 200){
                JsonValue root;
                if(jsonParse(reply2.c_str(), reply2.size(), root) && root["id"].asInt() > 0){
                    id_.store(root["id"].asInt());
                    std::lock_guard<std::mutex> lock(mutex_);
                    since_ = (long long)root["head"].asDouble(0.0);
                }
            }
        } else {
            ++failures;
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = (failures > 3) ? "связь потеряна" : "связь неустойчива";
        }

        // Десять обменов в секунду: движение чужих игроков сглаживается на клиенте, а
        // чаще — это лишний трафик на мобильном интернете.
        for(int i = 0; i < 10 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
