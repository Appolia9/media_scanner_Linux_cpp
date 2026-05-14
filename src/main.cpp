#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <csignal>
#include <cstring>
#include <future>
#include <condition_variable>

// HTTP-сервер
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace fs = std::filesystem;

// Глобальное состояние
std::mutex              g_mutex;
std::string             g_json_cache;       // последний сформированный JSON
std::atomic<bool>       g_running{true};
std::condition_variable g_cv;               // для ожидания завершения

// Расширения медиафайлов
const std::set<std::string> AUDIO_EXT = {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".wma", ".m4a", ".opus"
};

const std::set<std::string> VIDEO_EXT = {
    ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".mpg", ".mpeg", ".m4v"
};

const std::set<std::string> IMAGE_EXT = {
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".tif", ".webp", ".svg", ".ico"
};


static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Экранирование спецсимволов в JSON-строке
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);  // резервируем с запасом
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '/':  out += "\\/";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Формирование JSON из трёх списков файлов
static std::string build_json(
    const std::vector<std::string>& audio,
    const std::vector<std::string>& video,
    const std::vector<std::string>& images)
{
    auto arr = [](const std::vector<std::string>& v) -> std::string {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ", ";
            s += "\"" + json_escape(v[i]) + "\"";
        }
        s += "]";
        return s;
    };
    
    std::ostringstream oss;
    oss << "{\n"
        << "  \"audio\": "  << arr(audio)  << ",\n"
        << "  \"video\": "  << arr(video)  << ",\n"
        << "  \"images\": " << arr(images) << "\n"
        << "}";
    return oss.str();
}

// Сканирование каталога
static void scan_directory(const fs::path& dir, const std::string& output_file, bool use_http) {
    std::vector<std::string> audio, video, images;
    std::error_code ec;
    
    // Пропуск ошибок доступа
    fs::recursive_directory_iterator it(dir, 
        fs::directory_options::skip_permission_denied, 
        ec);
    
    if (ec) {
        std::cerr << "[ERROR] Не удалось открыть каталог " << dir << ": " << ec.message() << "\n";
        return;
    }
    
    size_t total_files = 0;
    size_t media_files = 0;
    
    for (const auto& entry : it) {
        ++total_files;
        
        if (!g_running) return;
        
        if (!entry.is_regular_file(ec)) {
            if (ec) ec.clear();
            continue;
        }
        
        std::string ext = to_lower(entry.path().extension().string());
        std::string name = entry.path().filename().string();
        
        if (AUDIO_EXT.count(ext)) {
            audio.push_back(name);
            ++media_files;
        } else if (VIDEO_EXT.count(ext)) {
            video.push_back(name);
            ++media_files;
        } else if (IMAGE_EXT.count(ext)) {
            images.push_back(name);
            ++media_files;
        }
    }
    
    if (ec) {
        std::cerr << "[WARN] Ошибка при обходе каталога: " << ec.message() << "\n";
    }
    
    std::sort(audio.begin(),  audio.end());
    std::sort(video.begin(),  video.end());
    std::sort(images.begin(), images.end());
    
    std::string json = build_json(audio, video, images);
    
    if (use_http) {
        // Режим HTTP: обновляем кэш
        std::lock_guard<std::mutex> lk(g_mutex);
        g_json_cache = json;
        std::cout << "[INFO] JSON обновлён в памяти ("
                  << audio.size()  << " audio, "
                  << video.size()  << " video, "
                  << images.size() << " images, "
                  << "всего проверено " << total_files << " файлов)\n";
    } else {
        // Режим файла: записываем .media_files
        std::ofstream ofs(output_file);
        if (!ofs) {
            std::cerr << "[ERROR] Не удалось открыть файл: " << output_file << "\n";
            return;
        }
        ofs << json << "\n";
        std::cout << "[INFO] Записан " << output_file << " ("
                  << audio.size()  << " audio, "
                  << video.size()  << " video, "
                  << images.size() << " images, "
                  << "всего проверено " << total_files << " файлов)\n";
    }
}

// HTTP-сервер
static std::string read_http_request(int client_fd) {
    std::string result;
    char buf[4096];
    bool headers_end = false;
    size_t total_read = 0;
    
    // Устанавливаем таймаут на чтение
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (!headers_end && total_read < 65536) {  // ограничиваем размер запроса
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (n == 0) break;  // соединение закрыто
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // таймаут
            break;  // ошибка
        }
        
        buf[n] = '\0';
        result += buf;
        total_read += n;
        
        // Проверяем, достигли ли конца заголовков
        if (result.find("\r\n\r\n") != std::string::npos) {
            headers_end = true;
        }
    }
    
    return result;
}

static void http_server(uint16_t port, std::promise<void>&& ready_promise) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[ERROR] socket(): " << strerror(errno) << "\n";
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[ERROR] bind(): " << strerror(errno) << "\n";
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 128) < 0) {  
        std::cerr << "[ERROR] listen(): " << strerror(errno) << "\n";
        close(server_fd);
        return;
    }
    
    ready_promise.set_value();
    std::cout << "[HTTP] Сервер запущен на http://localhost:" << port << "/media_files\n";
    
    while (g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd, &fds);
        timeval tv{1, 0};   // 1 секунда
        
        int ready = select(server_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ready < 0 && errno != EINTR) {
            if (g_running) {
                std::cerr << "[ERROR] select(): " << strerror(errno) << "\n";
            }
            continue;
        }
        if (ready <= 0) continue;
        
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                std::cerr << "[ERROR] accept(): " << strerror(errno) << "\n";
            }
            continue;
        }
        
        // Читаем HTTP-запрос
        std::string request = read_http_request(client_fd);
        
        bool valid = false;
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;
        
        if (method == "GET" && (path == "/media_files" || path == "/media_files/")) {
            valid = true;
        }
        
        std::string body, response;
        
        if (valid) {
            std::lock_guard<std::mutex> lk(g_mutex);
            body = g_json_cache.empty() ? "{}" : g_json_cache;
            
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/json; charset=utf-8\r\n"
                << "Content-Length: " << body.size() << "\r\n"
                << "Connection: close\r\n"
                << "Access-Control-Allow-Origin: *\r\n"  // для веб-интерфейсов
                << "\r\n"
                << body;
            response = oss.str();
        } else {
            body = "{\"error\": \"Not Found\", \"endpoint\": \"/media_files\"}";
            std::ostringstream oss;
            oss << "HTTP/1.1 404 Not Found\r\n"
                << "Content-Type: application/json; charset=utf-8\r\n"
                << "Content-Length: " << body.size() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << body;
            response = oss.str();
        }
        
        size_t total_sent = 0;
        while (total_sent < response.size()) {
            ssize_t n = send(client_fd, response.c_str() + total_sent, 
                           response.size() - total_sent, 0);
            if (n <= 0) break;
            total_sent += n;
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    std::cout << "[HTTP] Сервер остановлен\n";
}

// Сигналы
static void signal_handler(int sig) {
    std::cout << "\n[INFO] Получен сигнал " << sig << ", завершение работы...\n";
    g_running = false;
    g_cv.notify_all();  // пробуждаем ожидающие потоки
}

// Помощь
static void print_usage(const char* prog) {
    std::cout <<
        "Использование:\n"
        "  " << prog << " [ОПЦИИ]\n\n"
        "Опции:\n"
        "  -d <путь>      Каталог для сканирования (по умолчанию: $HOME)\n"
        "  -i <секунды>   Интервал сканирования в секундах (по умолчанию: 60)\n"
        "  -o <файл>      Выходной файл (по умолчанию: $HOME/.media_files)\n"
        "  --http         Не писать файл; раздавать JSON по HTTP\n"
        "  -p <порт>      Порт HTTP-сервера (по умолчанию: 1234, требует --http)\n"
        "  -h             Показать эту справку\n\n";
}


int main(int argc, char* argv[]) {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    std::string scan_dir   = home;
    std::string output     = std::string(home) + "/.media_files";
    int         interval   = 60;
    bool        use_http   = false;
    uint16_t    http_port  = 1234;
    
    // Разбор аргументов
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            scan_dir = argv[++i];
        } else if (arg == "-i" && i + 1 < argc) {
            try {
                interval = std::stoi(argv[++i]);
                if (interval <= 0) throw std::invalid_argument("non-positive");
            } catch (...) {
                std::cerr << "[ERROR] Неверный интервал: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--http") {
            use_http = true;
        } else if (arg == "-p" && i + 1 < argc) {
            try {
                int port = std::stoi(argv[++i]);
                if (port < 1 || port > 65535) throw std::out_of_range("port");
                http_port = static_cast<uint16_t>(port);
            } catch (...) {
                std::cerr << "[ERROR] Неверный порт: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "[ERROR] Неизвестный аргумент: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::error_code ec;
    if (!fs::is_directory(scan_dir, ec)) {
        std::cerr << "[ERROR] Каталог не найден: " << scan_dir 
                  << " (" << ec.message() << ")\n";
        return 1;
    }
    
    // Сигналы завершения
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);  // игнорируем SIGPIPE для HTTP
    
    std::cout << "[INFO] Сканируемый каталог : " << scan_dir << "\n";
    std::cout << "[INFO] Интервал            : " << interval << " сек.\n";
    if (use_http) {
        std::cout << "[INFO] Режим               : HTTP-сервер\n";
        std::cout << "[INFO] Порт                : " << http_port << "\n";
    } else {
        std::cout << "[INFO] Режим               : запись в файл\n";
        std::cout << "[INFO] Выходной файл       : " << output << "\n";
    }
    std::cout << "Нажмите Ctrl+C для завершения\n\n";
    
    // Запускаем HTTP-сервер в отдельном потоке (если нужно)
    std::thread http_thread;
    if (use_http) {
        std::promise<void> ready_promise;
        std::future<void> ready_future = ready_promise.get_future();
        http_thread = std::thread(http_server, http_port, std::move(ready_promise));
        
        if (ready_future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            std::cerr << "[ERROR] Таймаут при запуске HTTP-сервера\n";
            g_running = false;
        }
    }
    
    // Основной цикл сканирования
    int scan_count = 0;
    while (g_running) {
        ++scan_count;
        std::cout << "[INFO] Сканирование #" << scan_count << "...\n";
        scan_directory(scan_dir, output, use_http);
        
        // Ждём interval секунд с возможностью прерваться
        for (int i = 0; i < interval && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    std::cout << "\n[INFO] Завершение работы...\n";
    if (http_thread.joinable()) {
        http_thread.join();
    }
    
    return 0;
}