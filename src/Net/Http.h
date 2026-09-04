#pragma once
// ==================== HTTP: КЛИЕНТ И СЕРВЕР ====================
// Сеть игры сделана поверх обычного HTTP/1.1, а не поверх своего UDP-протокола, и это
// осознанный выбор:
//   * бесплатные хостинги (в том числе render.com, куда мы и выкладываем сервер) дают
//     наружу ровно один HTTP-порт и требуют health-check по «/». Свой UDP туда не
//     пролезет ни при каких настройках;
//   * HTTP отлаживается браузером и curl: список игроков видно с телефона, не запуская
//     игру;
//   * потери и переподключения обрабатывает TCP, а нам в выживании хватает 10 обменов
//     в секунду — это не шутер с хитсканом.
// Зависимостей никаких: голые сокеты BSD, ручной разбор заголовков. Тела запросов —
// JSON, который разбирается уже готовым парсером из Core/Json.h.
#include <functional>
#include <string>

namespace net {

// Разобранный адрес сервера. Игрок вводит его в меню как «host:port» или полный URL.
struct Url {
    std::string host;
    int         port = 80;
    std::string path = "/";
    bool        secure = false;   // https:// — клиент такое пока не умеет (нет TLS)
};

// Понимает «example.com», «example.com:28015», «http://example.com/path».
bool parseUrl(const std::string& raw, Url& out);

// Один запрос и один ответ. Блокирующий вызов с таймаутом — крутится в сетевом потоке
// клиента, к отрисовке отношения не имеет.
bool request(const Url& url, const char* method, const std::string& path,
             const std::string& body, std::string& outBody, int& outStatus,
             int timeoutMs = 4000);

// Простейший HTTP-сервер: на каждое соединение — свой поток, один запрос, ответ и
// закрытие. Ни keep-alive, ни chunked: игровой обмен короткий, а простота здесь
// важнее экономии на рукопожатии.
class Server {
public:
    // Возвращает тело ответа; статус кладёт в status (200 по умолчанию).
    using Handler = std::function<std::string(const std::string& method,
                                              const std::string& path,
                                              const std::string& body,
                                              int& status)>;
    ~Server();
    bool start(int port, Handler handler);
    void stop();
    bool running() const { return running_; }
    int  port() const { return port_; }

private:
    void acceptLoop();

    int  listenFd_ = -1;
    int  port_ = 0;
    bool running_ = false;
    Handler handler_;
    void* thread_ = nullptr;   // std::thread*, чтобы не тащить <thread> в заголовок
};

} // namespace net
