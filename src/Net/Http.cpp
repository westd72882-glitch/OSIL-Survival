#include "Http.h"
#include "../Core/Log.h"

// https в игре нужен ради облачных хостингов: они выпускают наружу только TLS-порт
// (render.com в том числе). Транспорта два, и оба системные — своей криптографии в
// игре нет и быть не должно:
//   * Android — Java HttpsURLConnection через JNI: он есть в любой прошивке, тянет за
//     собой системные корневые сертификаты и ничего не весит в APK;
//   * настольная сборка — OpenSSL, если он нашёлся при сборке (нужен для отладки).
#if defined(__ANDROID__)
  #include <jni.h>
  #include <SDL2/SDL_system.h>
#elif defined(OSIL_HAVE_OPENSSL)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

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

// Разворачивает тело, посланное кусками (Transfer-Encoding: chunked). Наш сервер так
// не отвечает, но прокси хостингов — сплошь и рядом, и без этого в JSON приезжают
// шестнадцатеричные длины кусков вместо данных.
std::string decodeChunked(const std::string& body){
    std::string out;
    size_t pos = 0;
    while(pos < body.size()){
        size_t eol = body.find("\r\n", pos);
        if(eol == std::string::npos) break;
        size_t size = (size_t)strtoul(body.substr(pos, eol - pos).c_str(), nullptr, 16);
        if(size == 0) break;                       // нулевой кусок — конец тела
        pos = eol + 2;
        if(pos + size > body.size()) size = body.size() - pos;
        out.append(body, pos, size);
        pos += size + 2;                           // за куском идёт свой перевод строки
    }
    return out;
}

// Тело ответа — всё, что после пустой строки. Content-Length не проверяем: сервер
// закрывает соединение сам, и это и есть признак конца.
std::string httpBody(const std::string& raw){
    size_t p = raw.find("\r\n\r\n");
    size_t skip = 4;
    if(p == std::string::npos){
        p = raw.find("\n\n");
        skip = 2;
        if(p == std::string::npos) return "";
    }
    std::string body = raw.substr(p + skip);
    std::string head = raw.substr(0, p);
    for(char& c : head) c = (char)tolower((unsigned char)c);
    if(head.find("transfer-encoding: chunked") != std::string::npos)
        return decodeChunked(body);
    return body;
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

namespace {

#if defined(__ANDROID__)
// Ссылки на Java-класс берутся один раз из главного потока: из рабочего потока
// FindClass ищет класс системным загрузчиком и классы приложения не находит.
jclass    g_httpsClass = nullptr;
jmethodID g_httpsMethod = nullptr;
#endif

// Собирает «http(s)://host:port/path» — в таком виде адрес нужен и Java, и логам.
std::string fullUrl(const Url& url, const std::string& path){
    std::string s = url.secure ? "https://" : "http://";
    s += url.host;
    bool defaultPort = (url.secure && url.port == 443) || (!url.secure && url.port == 80);
    if(!defaultPort) s += ":" + std::to_string(url.port);
    s += path.empty() ? "/" : path;
    return s;
}

#if defined(OSIL_HAVE_OPENSSL) && !defined(__ANDROID__)
bool requestOpenSSL(const Url& url, const char* method, const std::string& path,
                    const std::string& body, std::string& outBody, int& outStatus,
                    int timeoutMs){
    static bool inited = false;
    if(!inited){ SSL_library_init(); SSL_load_error_strings(); inited = true; }

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
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if(fd < 0) return false;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if(!ctx){ close(fd); return false; }
    // Сертификаты проверяем по системному хранилищу: без этого «зашифровано» ещё не
    // значит «тот сервер».
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, url.host.c_str());   // SNI: без него хостинг не поймёт, кто нужен
    bool ok = (SSL_connect(ssl) == 1);

    if(ok){
        std::string req;
        req += method; req += ' '; req += path; req += " HTTP/1.1\r\n";
        req += "Host: " + url.host + "\r\n";
        req += "Content-Type: application/json\r\n";
        req += "User-Agent: OSILSurvival/1.0\r\n";
        req += "Connection: close\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        req += body;
        size_t sent = 0;
        while(ok && sent < req.size()){
            int n = SSL_write(ssl, req.data() + sent, (int)(req.size() - sent));
            if(n <= 0){ ok = false; break; }
            sent += (size_t)n;
        }
    }

    std::string raw;
    if(ok){
        char buf[4096];
        for(;;){
            int n = SSL_read(ssl, buf, sizeof(buf));
            if(n > 0){ raw.append(buf, (size_t)n); continue; }
            break;
        }
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);
    if(raw.empty()) return false;
    outStatus = httpStatus(raw);
    outBody = httpBody(raw);
    return outStatus > 0;
}
#endif

#if defined(__ANDROID__)
bool requestAndroidTls(const Url& url, const char* method, const std::string& path,
                       const std::string& body, std::string& outBody, int& outStatus,
                       int timeoutMs){
    if(!g_httpsClass || !g_httpsMethod) return false;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if(!env) return false;

    std::string url_ = fullUrl(url, path);
    jstring jurl    = env->NewStringUTF(url_.c_str());
    jstring jmethod = env->NewStringUTF(method);
    jstring jbody   = env->NewStringUTF(body.c_str());
    jobject result  = env->CallStaticObjectMethod(g_httpsClass, g_httpsMethod,
                                                  jurl, jmethod, jbody, (jint)timeoutMs);
    if(env->ExceptionCheck()){ env->ExceptionClear(); result = nullptr; }
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jmethod);
    env->DeleteLocalRef(jbody);
    if(!result) return false;

    const char* text = env->GetStringUTFChars((jstring)result, nullptr);
    std::string reply = text ? text : "";
    if(text) env->ReleaseStringUTFChars((jstring)result, text);
    env->DeleteLocalRef(result);

    // Java отдаёт «код\nтело» — так проще, чем возвращать пару значений через JNI.
    size_t nl = reply.find('\n');
    if(nl == std::string::npos) return false;
    outStatus = atoi(reply.substr(0, nl).c_str());
    outBody = reply.substr(nl + 1);
    return outStatus > 0;
}
#endif

// Один обмен без учёта перенаправлений.
bool requestOnce(const Url& url, const char* method, const std::string& path,
                 const std::string& body, std::string& outBody, int& outStatus,
                 std::string& outLocation, int timeoutMs){
    outBody.clear();
    outStatus = 0;
    outLocation.clear();

    if(url.secure){
#if defined(__ANDROID__)
        // Java сам ходит по перенаправлениям, заголовок нам не нужен.
        return requestAndroidTls(url, method, path, body, outBody, outStatus, timeoutMs);
#elif defined(OSIL_HAVE_OPENSSL)
        return requestOpenSSL(url, method, path, body, outBody, outStatus, timeoutMs);
#else
        return false;
#endif
    }

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
    req += "User-Agent: OSILSurvival/1.0\r\n";   // без него часть прокси отвечает 403
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
    // Хостинги (тот же render.com) отвечают на http перенаправлением на https —
    // достаём адрес, чтобы пойти по нему.
    std::string head = raw.substr(0, raw.find("\r\n\r\n"));
    std::string lower = head;
    for(char& c : lower) c = (char)tolower((unsigned char)c);
    size_t p = lower.find("location:");
    if(p != std::string::npos){
        size_t start = p + 9;
        while(start < head.size() && (head[start] == ' ' || head[start] == '\t')) ++start;
        size_t end = head.find('\r', start);
        if(end == std::string::npos) end = head.find('\n', start);
        if(end != std::string::npos) outLocation = head.substr(start, end - start);
    }
    return outStatus > 0;
}

} // namespace

bool secureSupported(){
#if defined(__ANDROID__)
    return g_httpsClass != nullptr && g_httpsMethod != nullptr;
#elif defined(OSIL_HAVE_OPENSSL)
    return true;
#else
    return false;
#endif
}

void initSecureTransport(){
#if defined(__ANDROID__)
    if(g_httpsClass) return;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if(!env){ LOG_ERROR("сеть: нет JNIEnv — https будет недоступен"); return; }
    jclass local = env->FindClass("com/osil/survival/Https");
    if(!local || env->ExceptionCheck()){
        env->ExceptionClear();
        LOG_ERROR("сеть: класс Https не найден — https будет недоступен");
        return;
    }
    g_httpsClass = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    g_httpsMethod = env->GetStaticMethodID(
        g_httpsClass, "request",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
    if(!g_httpsMethod){
        env->ExceptionClear();
        LOG_ERROR("сеть: метод Https.request не найден");
    } else {
        LOG_INFO("сеть: https через системный транспорт Android готов");
    }
#endif
}

bool request(const Url& url, const char* method, const std::string& path,
             const std::string& body, std::string& outBody, int& outStatus,
             int timeoutMs){
    Url current = url;
    std::string currentPath = path;
    // Перенаправлений может быть два: http -> https и дальше на «настоящий» адрес.
    for(int hop = 0; hop < 3; ++hop){
        std::string location;
        bool ok = requestOnce(current, method, currentPath, body, outBody, outStatus,
                              location, timeoutMs);
        if(!ok) return false;
        bool redirect = (outStatus == 301 || outStatus == 302 || outStatus == 307 ||
                         outStatus == 308);
        if(!redirect || location.empty()) return true;

        Url next;
        if(location[0] == '/'){          // относительный адрес: хост тот же
            next = current;
            currentPath = location;
        } else {
            if(!parseUrl(location, next)) return true;
            // parseUrl подставляет игровой порт, когда его нет; для http/https нужны
            // обычные 80 и 443, иначе после перенаправления мы постучимся не туда.
            size_t schemeEnd = location.find("://");
            std::string rest = (schemeEnd == std::string::npos) ? location
                                                                : location.substr(schemeEnd + 3);
            if(rest.find(':') == std::string::npos || rest.find(':') > rest.find('/'))
                next.port = next.secure ? 443 : 80;
            currentPath = next.path;
            next.path = "/";
        }
        if(next.secure && !secureSupported()) return true;   // дальше идти нечем
        current = next;
    }
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
