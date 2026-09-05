#include "NetServer.h"
#include "../Core/Json.h"
#include "../Core/Log.h"

#include <algorithm>

bool NetServer::start(const Config& cfg){
    cfg_ = cfg;
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
    s += ",\"protocol\":1";
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
        replay.reserve(liveDrops_.size() + felledTrees_.size());
        for(const auto& kv : liveDrops_) replay.push_back(kv.second);
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
        return net::encodeSyncResponse(others, fresh, freshEvents, sentHead, sentEventHead, timeOfDay_);
    }

    status = 404;
    return "{\"error\":\"нет такого адреса\"}";
}
