#include "Protocol.h"
#include "../Core/Json.h"

#include <cstdio>

namespace net {

std::string jsonEscape(const std::string& text){
    std::string out;
    out.reserve(text.size() + 8);
    for(char c : text){
        switch(c){
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                // Управляющие символы в имени игрока не нужны никому.
                if((unsigned char)c >= 0x20) out += c;
                break;
        }
    }
    return out;
}

namespace {
std::string num(float v){
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", (double)v);
    return buf;
}
} // namespace

std::string encodePlayer(const PlayerState& p){
    std::string s = "{";
    s += "\"id\":" + std::to_string(p.id);
    s += ",\"n\":\"" + jsonEscape(p.name) + "\"";
    s += ",\"x\":" + num(p.x) + ",\"y\":" + num(p.y) + ",\"z\":" + num(p.z);
    s += ",\"yw\":" + num(p.yaw) + ",\"pt\":" + num(p.pitch);
    s += ",\"sp\":" + num(p.speed) + ",\"sw\":" + num(p.swing);
    s += ",\"po\":" + std::to_string(p.pose);
    s += ",\"hd\":" + std::to_string(p.held);
    s += ",\"hp\":" + std::to_string(p.health);
    s += "}";
    return s;
}

namespace {
std::string encodeEdits(const std::vector<Edit>& edits){
    std::string s = "[";
    for(size_t i = 0; i < edits.size(); ++i){
        const Edit& e = edits[i];
        if(i) s += ",";
        s += "{\"x\":" + std::to_string(e.x) + ",\"y\":" + std::to_string(e.y) +
             ",\"z\":" + std::to_string(e.z) + ",\"b\":" + std::to_string(e.block);
        if(e.seq) s += ",\"s\":" + std::to_string(e.seq);
        s += "}";
    }
    s += "]";
    return s;
}

void decodeEdits(const JsonValue& arr, std::vector<Edit>& out){
    out.clear();
    for(size_t i = 0; i < arr.size(); ++i){
        const JsonValue& e = arr[i];
        Edit ed;
        ed.x = e["x"].asInt();
        ed.y = e["y"].asInt();
        ed.z = e["z"].asInt();
        ed.block = e["b"].asInt();
        ed.seq = (long long)e["s"].asDouble(0.0);
        out.push_back(ed);
    }
}

PlayerState decodePlayer(const JsonValue& v){
    PlayerState p;
    p.id = v["id"].asInt();
    p.name = v["n"].asString("игрок");
    p.x = v["x"].asFloat();
    p.y = v["y"].asFloat();
    p.z = v["z"].asFloat();
    p.yaw = v["yw"].asFloat();
    p.pitch = v["pt"].asFloat();
    p.speed = v["sp"].asFloat();
    p.swing = v["sw"].asFloat();
    p.pose = v["po"].asInt();
    p.held = v["hd"].asInt();
    p.health = v["hp"].asInt(100);
    return p;
}
} // namespace

std::string encodeSyncRequest(const PlayerState& me, const std::vector<Edit>& edits,
                              long long sinceSeq){
    std::string s = "{\"me\":" + encodePlayer(me);
    s += ",\"since\":" + std::to_string(sinceSeq);
    s += ",\"edits\":" + encodeEdits(edits);
    s += "}";
    return s;
}

std::string encodeSyncResponse(const std::vector<PlayerState>& players,
                               const std::vector<Edit>& edits, long long headSeq,
                               float timeOfDay){
    std::string s = "{\"players\":[";
    for(size_t i = 0; i < players.size(); ++i){
        if(i) s += ",";
        s += encodePlayer(players[i]);
    }
    s += "],\"edits\":" + encodeEdits(edits);
    s += ",\"head\":" + std::to_string(headSeq);
    s += ",\"time\":" + num(timeOfDay);
    s += "}";
    return s;
}

bool decodeSyncRequest(const std::string& json, PlayerState& out,
                       std::vector<Edit>& edits, long long& sinceSeq){
    JsonValue root;
    if(!jsonParse(json.c_str(), json.size(), root)) return false;
    out = decodePlayer(root["me"]);
    sinceSeq = (long long)root["since"].asDouble(0.0);
    decodeEdits(root["edits"], edits);
    return true;
}

bool decodeSyncResponse(const std::string& json, std::vector<PlayerState>& players,
                        std::vector<Edit>& edits, long long& headSeq, float& timeOfDay){
    JsonValue root;
    if(!jsonParse(json.c_str(), json.size(), root)) return false;
    players.clear();
    const JsonValue& arr = root["players"];
    for(size_t i = 0; i < arr.size(); ++i) players.push_back(decodePlayer(arr[i]));
    decodeEdits(root["edits"], edits);
    headSeq = (long long)root["head"].asDouble(0.0);
    timeOfDay = root["time"].asFloat(timeOfDay);
    return true;
}

} // namespace net
