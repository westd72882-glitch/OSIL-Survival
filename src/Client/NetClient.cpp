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

namespace {
// Спрашиваем у сервера карточку по «/». Это же и проверка, что адрес живой.
bool askStatus(const net::Url& url, NetClient::Info& info){
    std::string body;
    int status = 0;
    long long t0 = nowMs();
    if(!net::request(url, "GET", url.path.empty() ? "/" : url.path, "", body, status, 4000))
        return false;
    if(status != 200) return false;
    JsonValue root;
    if(!jsonParse(body.c_str(), body.size(), root)) return false;
    if(root["protocol"].isNull() && root["name"].isNull()) return false;
    info.ok = true;
    info.ping = (int)(nowMs() - t0);
    info.name = root["name"].asString("сервер");
    info.map = root["map"].asString("Survival Island");
    info.players = root["players"].asInt();
    info.max = root["max"].asInt(100);
    return true;
}
} // namespace

namespace {
// Похож ли адрес на голый IP («192.168.1.50»). Такой адрес — это чей-то компьютер или
// VPS, а не облако, и лезть к нему по https незачем.
bool looksLikeIp(const std::string& host){
    int digits = 0, dots = 0;
    for(char c : host){
        if(c >= '0' && c <= '9') ++digits;
        else if(c == '.') ++dots;
        else return false;
    }
    return dots == 3 && digits >= 4;
}
} // namespace

bool NetClient::resolve(const std::string& address, net::Url& out, Info& info){
    net::Url url;
    if(!net::parseUrl(address, url)) return false;

    bool explicitScheme = (address.rfind("http://", 0) == 0) || (address.rfind("https://", 0) == 0);
    std::string hostPart = address;
    size_t schemePos = hostPart.find("://");
    if(schemePos != std::string::npos) hostPart = hostPart.substr(schemePos + 3);
    size_t slash = hostPart.find('/');
    if(slash != std::string::npos) hostPart = hostPart.substr(0, slash);
    bool explicitPort = hostPart.find(':') != std::string::npos;

    // Схема или порт указаны руками — игрок знает, куда идёт, и подбирать нечего.
    if(explicitScheme || explicitPort){
        if(url.secure && !net::secureSupported()) return false;
        if(askStatus(url, info)){ out = url; return true; }
        return false;
    }

    // Голое имя. Доменное имя почти всегда означает облако (тот же render.com), где
    // наружу торчит только https, а голый IP — чей-то сервер на игровом порту. С этого
    // и начинаем, чтобы не ждать таймаута на заведомо закрытом порту.
    net::Url plain = url;      // http на игровом порту 28015
    net::Url secure = url;
    secure.secure = true;
    secure.port = 443;

    bool cloudFirst = !looksLikeIp(url.host) && url.host.find('.') != std::string::npos;
    if(cloudFirst && net::secureSupported()){
        if(askStatus(secure, info)){ out = secure; return true; }
        info = Info{};
    }
    if(askStatus(plain, info)){ out = plain; return true; }
    if(!cloudFirst && net::secureSupported()){
        info = Info{};
        if(askStatus(secure, info)){ out = secure; return true; }
    }
    return false;
}

NetClient::Info NetClient::query(const std::string& address){
    Info info;
    net::Url url;
    resolve(address, url, info);
    return info;
}

bool NetClient::join(const std::string& address, const std::string& playerName){
    leave();

    // Сначала находим рабочий адрес: http на игровом порту или https, если сервер
    // живёт в облаке. Заодно узнаём имя сервера и сколько там мест.
    net::Url url;
    Info info;
    if(!resolve(address, url, info)){
        std::lock_guard<std::mutex> lock(mutex_);
        net::Url parsed;
        if(!net::parseUrl(address, parsed))            status_ = "неверный адрес сервера";
        else if(parsed.secure && !net::secureSupported()) status_ = "эта сборка не умеет https";
        else                                            status_ = "сервер не отвечает";
        return false;
    }

    std::string body = "{\"name\":\"" + net::jsonEscape(playerName) + "\"}";
    std::string reply;
    int status = 0;
    if(!net::request(url, "POST", "/join", body, reply, status, 6000)){
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
        url_ = url;
        address_ = address;
        name_ = playerName;
        serverName_ = root["name"].asString(info.name.empty() ? address : info.name);
        since_ = (long long)root["head"].asDouble(0.0);
        outgoing_.clear();
        incoming_.clear();
        others_.clear();
        status_ = "подключено";
    }
    id_.store(root["id"].asInt());
    maxPlayers_.store(root["max"].asInt(info.max ? info.max : 100));
    seed_ = (unsigned long long)root["seed"].asDouble(0.0);
    serverTime_.store(root["time"].asFloat(8.0f));
    connected_.store(true);
    running_.store(true);
    worker_ = std::thread([this]{ loop(); });
    LOG_INFO("сеть: вошли на сервер %s (%s) как %s (id %d)",
             serverName_.c_str(), url.secure ? "https" : "http",
             playerName.c_str(), id_.load());
    return true;
}

void NetClient::leave(){
    if(running_.exchange(false)){
        if(worker_.joinable()) worker_.join();
    }
    if(connected_.exchange(false)){
        // Уходим по-человечески: сервер сразу уберёт нас из списка, а не через таймаут.
        net::Url url;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            url = url_;
        }
        if(!url.host.empty()){
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

int NetClient::onlineCount() const {
    if(!connected_.load()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)others_.size() + 1;
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
        url = url_;
    }
    if(url.host.empty()) return;
    int failures = 0;
    while(running_.load()){
        net::PlayerState me;
        std::vector<net::Edit> send;
        long long since = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            me = local_;
            // Идентификатор берём из живого значения, а не из последнего кадра игры:
            // первые обмены уходят раньше, чем игровой цикл успевает положить своё
            // состояние, и сервер отвечал на них «нужен повторный вход» — из-за этого
            // при каждом входе плодилась пачка призраков в списке игроков.
            me.id = id_.load();
            me.name = name_;
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
            std::string nick;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                nick = name_;
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
