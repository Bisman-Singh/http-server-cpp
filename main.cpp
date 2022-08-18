#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>

static volatile sig_atomic_t running = 1;

void signal_handler(int) { running = 0; }

std::string get_content_type(const std::string& path) {
    static const std::unordered_map<std::string, std::string> types = {
        {".html", "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".txt",  "text/plain"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".ico",  "image/x-icon"},
    };

    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        auto it = types.find(path.substr(dot));
        if (it != types.end()) return it->second;
    }
    return "application/octet-stream";
}

std::string build_response(int status, const std::string& status_text,
                           const std::string& content_type, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Prevent directory traversal via ".." in the request path
std::string sanitize_path(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '.' && i + 1 < raw.size() && raw[i + 1] == '.') {
            continue;
        }
        result += raw[i];
    }
    return result;
}

std::string parse_request_path(const std::string& request) {
    auto first_line_end = request.find("\r\n");
    if (first_line_end == std::string::npos) return "/";

    std::string first_line = request.substr(0, first_line_end);
    auto space1 = first_line.find(' ');
    auto space2 = first_line.find(' ', space1 + 1);
    if (space1 == std::string::npos || space2 == std::string::npos) return "/";

    std::string method = first_line.substr(0, space1);
    if (method != "GET") return "";

    return first_line.substr(space1 + 1, space2 - space1 - 1);
}

void handle_client(int client_fd, const std::string& www_root) {
    std::vector<char> buffer(8192);
    ssize_t n = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[static_cast<size_t>(n)] = '\0';
    std::string request(buffer.data());

    std::string path = parse_request_path(request);

    if (path.empty()) {
        std::string resp = build_response(405, "Method Not Allowed", "text/plain", "405 Method Not Allowed");
        send(client_fd, resp.c_str(), resp.size(), 0);
        close(client_fd);
        return;
    }

    path = sanitize_path(path);
    if (path == "/") path = "/index.html";

    std::string file_path = www_root + path;

    if (file_exists(file_path)) {
        std::string body = read_file(file_path);
        std::string content_type = get_content_type(file_path);
        std::string resp = build_response(200, "OK", content_type, body);
        send(client_fd, resp.c_str(), resp.size(), 0);
    } else {
        std::string body = "<html><body><h1>404 Not Found</h1><p>The requested file was not found.</p></body></html>";
        std::string resp = build_response(404, "Not Found", "text/html", body);
        send(client_fd, resp.c_str(), resp.size(), 0);
    }

    close(client_fd);
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
        if (port < 1 || port > 65535) {
            std::cerr << "Invalid port number: " << argv[1] << "\n";
            return 1;
        }
    }

    std::string www_root = "www";

    std::signal(SIGINT, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "Failed to listen\n";
        close(server_fd);
        return 1;
    }

    std::cout << "HTTP server listening on port " << port << "\n";
    std::cout << "Serving files from ./" << www_root << "/\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (!running) break;
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_client(client_fd, www_root);
            _exit(0);
        }
        close(client_fd);
    }

    close(server_fd);
    std::cout << "\nServer stopped.\n";
    return 0;
}
