#include "Http.h"
#include "../Core/Log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace net {
namespace {

// Читает из сокета, пока не кончится тело ответа или не истечёт терпение.
bool readAll(int fd, std::string& out){
    char buf[4096];
    for(;;){
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if(n > 0){ out.append(buf, (size_t)n); continue; }
        if(n == 0) return true;                       // соединение закрыто — всё пришло
        if(errno == EINTR) continue;
        return !out.empty();                          // таймаут: отдаём, что успели
    }
}

bool sendAll(int fd, const std::string& data){
    size_t sent = 0;
    while(sent < data.size()){
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if(n > 0){ sent += (size_t)n; continue; }
        if(n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

void setTimeout(int fd, int ms){
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// Тело ответа — всё, что после пустой строки. Content-Length не проверяем: сервер
// закрывает соединение сам, и это и есть признак конца.
std::string httpBody(const std::string& raw){
    size_t p = raw.find("\r\n\r\n");
    if(p == std::string::npos){
        p = raw.find("\n\n");
        if(p == std::string::npos) return "";
        return raw.substr(p + 2);
    }
    return raw.substr(p + 4);
}

int httpStatus(const std::string& raw){
    // «HTTP/1.1 200 OK»
    size_t sp = raw.find(' ');
    if(sp == std::string::npos) return 0;
    return atoi(raw.c_str() + sp + 1);
}

} // namespace

bool parseUrl(const std::string& raw, Url& out){
    std::string s = raw;
    // Пробелы по краям игрок вводит регулярно — это не повод не подключаться.
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '/')) s.pop_back();
    if(s.empty()) return false;

    out = Url{};
    if(s.rfind("https://", 0) == 0){ out.secure = true; out.port = 443; s = s.substr(8); }
    else if(s.rfind("http://", 0) == 0){ s = s.substr(7); }

    size_t slash = s.find('/');
    if(slash != std::string::npos){
        out.path = s.substr(slash);
        s = s.substr(0, slash);
    }
    size_t colon = s.find(':');
    if(colon != std::string::npos){
        out.host = s.substr(0, colon);
        out.port = atoi(s.c_str() + colon + 1);
    } else {
        out.host = s;
        if(!out.secure) out.port = 28015;   // порт сервера игры по умолчанию
    }
    if(out.host.empty() || out.port <= 0 || out.port > 65535) return false;
    if(out.path.empty()) out.path = "/";
    return true;
}

bool request(const Url& url, const char* method, const std::string& path,
             const std::string& body, std::string& outBody, int& outStatus,
             int timeoutMs){
    outBody.clear();
    outStatus = 0;
    if(url.secure) return false;   // TLS клиент не умеет: см. docs/network.md

    char portText[16];
    snprintf(portText, sizeof(portText), "%d", url.port);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if(getaddrinfo(url.host.c_str(), portText, &hints, &res) != 0 || !res) return false;

    int fd = -1;
    for(addrinfo* a = res; a; a = a->ai_next){
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if(fd < 0) continue;
        setTimeout(fd, timeoutMs);
        if(connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if(fd < 0) return false;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    std::string req;
    req.reserve(body.size() + 256);
    req += method; req += ' '; req += path; req += " HTTP/1.1\r\n";
    req += "Host: " + url.host + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Connection: close\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    req += body;

    bool ok = sendAll(fd, req);
    std::string raw;
    if(ok) ok = readAll(fd, raw);
    close(fd);
    if(!ok || raw.empty()) return false;

    outStatus = httpStatus(raw);
    outBody = httpBody(raw);
    return outStatus > 0;
}

// ==================== СЕРВЕРНАЯ ЧАСТЬ ====================

Server::~Server(){ stop(); }

bool Server::start(int port, Handler handler){
    stop();
    handler_ = std::move(handler);
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) return false;
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // ноль-адрес обязателен для хостингов
    addr.sin_port = htons((uint16_t)port);
    if(bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) != 0){
        LOG_ERROR("сеть: не удалось занять порт %d (%s)", port, strerror(errno));
        close(listenFd_); listenFd_ = -1;
        return false;
    }
    if(listen(listenFd_, 64) != 0){
        close(listenFd_); listenFd_ = -1;
        return false;
    }
    port_ = port;
    running_ = true;
    thread_ = new std::thread([this]{ acceptLoop(); });
    LOG_INFO("сеть: слушаем порт %d", port);
    return true;
}

void Server::stop(){
    if(!running_ && !thread_) return;
    running_ = false;
    if(listenFd_ >= 0){ shutdown(listenFd_, SHUT_RDWR); close(listenFd_); listenFd_ = -1; }
    if(thread_){
        std::thread* t = (std::thread*)thread_;
        if(t->joinable()) t->join();
        delete t;
        thread_ = nullptr;
    }
}

void Server::acceptLoop(){
    while(running_){
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int fd = accept(listenFd_, (sockaddr*)&peer, &len);
        if(fd < 0){
            if(!running_) break;
            if(errno == EINTR) continue;
            continue;
        }
        // Каждое соединение живёт в своём потоке и умирает вместе с ответом. Обмен
        // короткий (пара килобайт), поэтому пул потоков тут был бы сложнее, чем нужно.
        std::thread([this, fd]{
            setTimeout(fd, 5000);
            std::string raw;
            char buf[4096];
            size_t headerEnd = std::string::npos;
            size_t contentLength = 0;
            bool haveHeader = false;
            for(;;){
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if(n <= 0) break;
                raw.append(buf, (size_t)n);
                if(!haveHeader){
                    headerEnd = raw.find("\r\n\r\n");
                    if(headerEnd != std::string::npos){
                        haveHeader = true;
                        // Content-Length ищем без учёта регистра: клиенты пишут по-разному.
                        std::string head = raw.substr(0, headerEnd);
                        for(char& c : head) c = (char)tolower((unsigned char)c);
                        size_t p = head.find("content-length:");
                        if(p != std::string::npos) contentLength = (size_t)atoi(head.c_str() + p + 15);
                    }
                }
                if(haveHeader && raw.size() >= headerEnd + 4 + contentLength) break;
            }
            if(!raw.empty()){
                std::string method, path;
                size_t sp1 = raw.find(' ');
                size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : raw.find(' ', sp1 + 1);
                if(sp1 != std::string::npos && sp2 != std::string::npos){
                    method = raw.substr(0, sp1);
                    path = raw.substr(sp1 + 1, sp2 - sp1 - 1);
                }
                std::string body;
                if(headerEnd != std::string::npos) body = raw.substr(headerEnd + 4);
                int status = 200;
                std::string reply = handler_ ? handler_(method, path, body, status) : std::string("{}");
                std::string out = "HTTP/1.1 " + std::to_string(status) +
                                  (status == 200 ? " OK\r\n" : " ERR\r\n");
                out += "Content-Type: application/json; charset=utf-8\r\n";
                out += "Access-Control-Allow-Origin: *\r\n";
                out += "Connection: close\r\n";
                out += "Content-Length: " + std::to_string(reply.size()) + "\r\n\r\n";
                out += reply;
                sendAll(fd, out);
            }
            close(fd);
        }).detach();
    }
}

} // namespace net
