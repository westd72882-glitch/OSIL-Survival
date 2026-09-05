#include "NetServer.h"
#include "../Core/Json.h"
#include "../Core/Log.h"
#include "../Core/Sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
// Закрытый бета-тест: играть можно только под этими никами. Список нарочно в коде —
// заводить панель администратора ради четырёх тестеров незачем.
const char* const ALLOWED_NICKS[] = { "Tester0", "Tester5", "Tester6", "AdminTester" };

std::string randomSalt(){
    static bool seeded = false;
    if(!seeded){ srand((unsigned)time(nullptr) ^ (unsigned)clock()); seeded = true; }
    char buf[17];
    const char* hex = "0123456789abcdef";
    for(int i = 0; i < 16; ++i) buf[i] = hex[rand() % 16];
    buf[16] = '\0';
    return std::string(buf);
}
} // namespace

bool NetServer::nickAllowed(const std::string& nick) const {
    for(const char* n : ALLOWED_NICKS) if(nick == n) return true;
    return false;
}

void NetServer::loadAccounts(){
    accounts_.clear();
    FILE* f = fopen(accountsPath_.c_str(), "rb");
    if(!f) return;
    char nick[64], salt[64], hash[128];
    while(fscanf(f, "%63s %63s %127s", nick, salt, hash) == 3){
        Account a;
        a.nick = nick; a.salt = salt; a.hash = hash;
        accounts_[a.nick] = a;
    }
    fclose(f);
    LOG_INFO("аккаунты: загружено %d", (int)accounts_.size());
}

void NetServer::saveAccounts() const {
    FILE* f = fopen(accountsPath_.c_str(), "wb");
    if(!f){ LOG_ERROR("аккаунты: не удалось записать %s", accountsPath_.c_str()); return; }
    for(const auto& kv : accounts_)
        fprintf(f, "%s %s %s\n", kv.second.nick.c_str(), kv.second.salt.c_str(),
                kv.second.hash.c_str());
    fclose(f);
}

// Вход и регистрация. Пароль в файле не лежит: хранится соль и SHA-256 от «соль+пароль».
std::string NetServer::handleAuth(const std::string& route, const std::string& body,
                                  int& status){
    JsonValue root;
    std::string nick, password;
    if(jsonParse(body.c_str(), body.size(), root)){
        nick = root["nick"].asString("");
        password = root["password"].asString("");
    }
    if(nick.empty() || password.size() < 3){
        status = 400;
        return "{\"error\":\"нужны ник и пароль от трёх символов\"}";
    }
    if(!nickAllowed(nick)){
        status = 403;
        return "{\"error\":\"закрытый тест: этот ник не в списке\"}";
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(nick);
    if(route == "/auth/register"){
        if(it != accounts_.end()){
            status = 409;
            return "{\"error\":\"аккаунт уже создан, входите по паролю\"}";
        }
        Account a;
        a.nick = nick;
        a.salt = randomSalt();
        a.hash = sha256Hex(a.salt + password);
        accounts_[nick] = a;
        saveAccounts();
        LOG_INFO("аккаунты: зарегистрирован %s", nick.c_str());
        return "{\"ok\":1,\"nick\":\"" + net::jsonEscape(nick) + "\"}";
    }

    // Вход.
    if(it == accounts_.end()){
        status = 404;
        return "{\"error\":\"аккаунта нет — создайте его\"}";
    }
    if(sha256Hex(it->second.salt + password) != it->second.hash){
        status = 401;
        return "{\"error\":\"неверный пароль\"}";
    }
    return "{\"ok\":1,\"nick\":\"" + net::jsonEscape(nick) + "\"}";
}

bool NetServer::start(const Config& cfg){
    cfg_ = cfg;
    loadAccounts();
    bool ok = http_.start(cfg.port, [this](const std::string& m, const std::string& p,
                                           const std::string& b, int& st){
        return handle(m, p, b, st);
    });
    if(ok) LOG_INFO("сеть: сервер «%s» ждёт игроков на порту %d", cfg_.name.c_str(), cfg_.port);
    return ok;
}

void NetServer::stop(){ http_.stop(); }

int NetServer::playerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)players_.size();
}

void NetServer::update(float dt, float timeOfDay){
    std::lock_guard<std::mutex> lock(mutex_);
    timeOfDay_ = timeOfDay;
    for(auto it = players_.begin(); it != players_.end(); ){
        it->second.silence += dt;
        if(it->second.silence > cfg_.timeoutSeconds){
            LOG_INFO("сеть: игрок %s (%d) отключился по таймауту",
                     it->second.state.name.c_str(), it->first);
            it = players_.erase(it);
        } else ++it;
    }
    // Мешки погибших живут минуту. Клиенты гасят их у себя сами по таймеру, здесь мы
    // лишь перестаём отдавать протухшие тем, кто вошёл позже.
    for(auto it = bagAge_.begin(); it != bagAge_.end(); ){
        it->second += dt;
        if(it->second > BAG_LIFETIME){
            int bag = it->first;
            for(auto b = liveBags_.begin(); b != liveBags_.end(); ){
                if((int)(b->first >> 8) == bag) b = liveBags_.erase(b);
                else ++b;
            }
            it = bagAge_.erase(it);
        } else ++it;
    }
}

std::string NetServer::statusJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string s = "{";
    s += "\"name\":\"" + net::jsonEscape(cfg_.name) + "\"";
    s += ",\"map\":\"" + net::jsonEscape(cfg_.map) + "\"";
    s += ",\"players\":" + std::to_string(players_.size());
    s += ",\"max\":" + std::to_string(cfg_.maxPlayers);
    s += ",\"seed\":" + std::to_string(cfg_.seed);
    s += ",\"time\":" + std::to_string((int)timeOfDay_);
    s += ",\"protocol\":3";
    s += "}";
    return s;
}

std::string NetServer::handle(const std::string& method, const std::string& path,
                              const std::string& body, int& status){
    status = 200;
    // Путь может прийти с параметрами — они нам не нужны.
    std::string route = path.substr(0, path.find('?'));

    // Проверка живости хостинга и карточка сервера для списка в меню.
    if(route == "/" || route == "/status") return statusJson();

    // Аккаунты закрытого теста.
    if((route == "/auth/login" || route == "/auth/register") && method == "POST")
        return handleAuth(route, body, status);

    if(route == "/join" && method == "POST"){
        JsonValue root;
        std::string name = "выживший";
        if(jsonParse(body.c_str(), body.size(), root)) name = root["name"].asString(name);
        if(name.size() > 24) name.resize(24);

        std::lock_guard<std::mutex> lock(mutex_);
        if((int)players_.size() >= cfg_.maxPlayers){
            status = 503;
            return "{\"error\":\"сервер полон\"}";
        }
        // Один ник — один игрок в мире. Дубликаты отсекает именно сервер: клиенту тут
        // верить нельзя, а два «Tester5» в списке ломают и удары, и чужие метки.
        for(const auto& kv : players_){
            if(kv.second.state.name != name) continue;
            status = 409;
            return "{\"error\":\"этот ник уже в игре\"}";
        }
        int id = nextId_++;
        Slot slot;
        slot.state.id = id;
        slot.state.name = name;
        players_[id] = slot;
        LOG_INFO("сеть: вошёл %s (%d), всего игроков %d", name.c_str(), id, (int)players_.size());

        // Вошедшему отдаём состояние мира: лежащие предметы и уже сваленные деревья.
        // Без этого он видел бы лес, которого давно нет, и пустую поляну вместо чужого
        // выброшенного ящика.
        std::vector<net::Event> replay;
        replay.reserve(liveDrops_.size() + felledTrees_.size() + liveBags_.size());
        for(const auto& kv : liveDrops_) replay.push_back(kv.second);
        for(const auto& kv : liveBags_) replay.push_back(kv.second);
        for(net::Event e : felledTrees_){
            e.a = 1;                 // «тихо»: дерево уже упало, анимацию показывать не надо
            replay.push_back(e);
        }

        std::string s = "{\"id\":" + std::to_string(id);
        s += ",\"max\":" + std::to_string(cfg_.maxPlayers);
        s += ",\"seed\":" + std::to_string(cfg_.seed);
        s += ",\"head\":" + std::to_string(headSeq_);
        s += ",\"ehead\":" + std::to_string(eventSeq_);
        s += ",\"name\":\"" + net::jsonEscape(cfg_.name) + "\"";
        s += ",\"time\":" + std::to_string((int)timeOfDay_);
        s += ",\"replay\":" + net::encodeEvents(replay) + "}";
        return s;
    }

    if(route == "/leave" && method == "POST"){
        JsonValue root;
        if(jsonParse(body.c_str(), body.size(), root)){
            int id = root["id"].asInt();
            std::lock_guard<std::mutex> lock(mutex_);
            players_.erase(id);
        }
        return "{\"ok\":1}";
    }

    if(route == "/sync" && method == "POST"){
        net::PlayerState me;
        std::vector<net::Edit> incoming;
        std::vector<net::Event> incomingEvents;
        long long since = 0, esince = 0;
        if(!net::decodeSyncRequest(body, me, incoming, incomingEvents, since, esince)){
            status = 400;
            return "{\"error\":\"плохой запрос\"}";
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = players_.find(me.id);
        if(it == players_.end()){
            // Игрока выбросило по таймауту — пусть перезайдёт: id больше не наш.
            status = 409;
            return "{\"error\":\"нужен повторный вход\"}";
        }
        it->second.state = me;
        it->second.silence = 0.0f;

        // Правки от игрока ложатся в общий журнал под своими номерами.
        for(net::Edit e : incoming){
            e.seq = ++headSeq_;
            journal_.push_back(e);
        }
        if(journal_.size() > JOURNAL_LIMIT)
            journal_.erase(journal_.begin(), journal_.begin() + (long)(journal_.size() - JOURNAL_LIMIT));

        // События (дропы, упавшие деревья, взрывы, удары) идут своим журналом: они
        // короткоживущие, и держать их вместе с постройками незачем.
        for(net::Event e : incomingEvents){
            // Удар по игроку в журнал НЕ кладём: он копится жертве и уезжает ей полем
            // ответа. Пока он попадал ещё и в журнал, жертва получала урон дважды —
            // цифра над головой показывала 5, а снимало 10.
            if(e.type == (int)net::EventType::Hit){
                auto victim = players_.find(e.id);
                if(victim != players_.end()) victim->second.pendingDamage += e.b;
                continue;
            }
            e.seq = ++eventSeq_;
            e.owner = me.id;          // подписываем событие отправителем
            eventJournal_.push_back(e);
            // Заодно ведём состояние мира: что лежит на земле и что уже срублено.
            if(e.type == (int)net::EventType::Drop){
                liveDrops_[e.id] = e;
            } else if(e.type == (int)net::EventType::Pickup){
                liveDrops_.erase(e.id);
            } else if(e.type == (int)net::EventType::TreeFell){
                if(felledTrees_.size() < FELLED_LIMIT) felledTrees_.push_back(e);
            } else if(e.type == (int)net::EventType::BagDrop){
                liveBags_[((long long)e.id << 8) | (long long)(e.c & 0xFF)] = e;
                bagAge_[e.id] = 0.0f;
            } else if(e.type == (int)net::EventType::BagTake){
                liveBags_.erase(((long long)e.id << 8) | (long long)(e.c & 0xFF));
            }
        }
        if(eventJournal_.size() > EVENT_LIMIT)
            eventJournal_.erase(eventJournal_.begin(),
                                eventJournal_.begin() + (long)(eventJournal_.size() - EVENT_LIMIT));

        // В ответ — все, кроме самого игрока, и правки, которых у него ещё нет.
        std::vector<net::PlayerState> others;
        others.reserve(players_.size());
        for(const auto& kv : players_){
            if(kv.first == me.id) continue;
            others.push_back(kv.second.state);
        }
        // Отдаём порциями, чтобы телефон не захлебнулся. ВАЖНО: в ответе едет номер
        // последней ОТПРАВЛЕННОЙ записи, а не общий конец журнала — иначе клиент
        // считал бы, что получил всё, и хвост правок терялся молча.
        std::vector<net::Edit> fresh;
        long long sentHead = headSeq_;
        for(const net::Edit& e : journal_){
            if(e.seq <= since) continue;
            if(fresh.size() >= 400){ sentHead = fresh.back().seq; break; }
            fresh.push_back(e);
        }
        std::vector<net::Event> freshEvents;
        long long sentEventHead = eventSeq_;
        for(const net::Event& e : eventJournal_){
            if(e.seq <= esince) continue;
            if(freshEvents.size() >= 200){ sentEventHead = e.seq - 1; break; }
            // Свои же события обратно не возвращаем: отправитель их уже применил, и
            // повторно взрывать его собственной гранатой было бы нечестно. Номер при
            // этом всё равно считается пройденным — событие мы видели.
            if(e.owner == me.id) continue;
            freshEvents.push_back(e);
        }
        int damage = it->second.pendingDamage;
        it->second.pendingDamage = 0;
        return net::encodeSyncResponse(others, fresh, freshEvents, sentHead, sentEventHead,
                                       timeOfDay_, damage);
    }

    status = 404;
    return "{\"error\":\"нет такого адреса\"}";
}
