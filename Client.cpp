#include <arpa/inet.h>      // htonl / htons / ntohl / ntohs / inet_pton
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket / connect / send / recv
#include <unistd.h>         // close

#include <atomic>
#include <cerrno>           // errno / EINTR
#include <cstdint>
#include <cstring>          // memcpy
#include <iostream>
#include <string>
#include <thread>

// 组帧：[4字节总长度(大端)][2字节cmd(大端)][body]
static std::string packFrame(uint16_t cmd, const std::string& body) {
    uint32_t total = 6 + static_cast<uint32_t>(body.size());
    uint32_t lenNet = htonl(total);
    uint16_t cmdNet = htons(cmd);
    std::string frame;
    frame.append(reinterpret_cast<const char*>(&lenNet), 4);
    frame.append(reinterpret_cast<const char*>(&cmdNet), 2);
    frame.append(body);
    return frame;
}

// 必须读满 n 字节：TCP 不保证一次 recv 拿完一帧（半包）
static bool recvN(int fd, char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd, buf + got, n - got, 0);
        if (r > 0) {
            got += static_cast<size_t>(r);
        } else if (r == 0) {
            return false;                // 对端关闭
        } else if (errno != EINTR) {
            return false;                // 真实错误
        }
    }
    return true;
}

// 发完整个缓冲区，处理部分写
static bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent,
                           data.size() - sent, MSG_NOSIGNAL);
        if (n >= 0) {
            sent += static_cast<size_t>(n);
        } else if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

// 接收线程：循环按 帧头6字节 -> body 的顺序读取
static void recvLoop(int fd, std::atomic<bool>& running) {
    while (running) {
        char header[6];
        if (!recvN(fd, header, 6)) break;
        uint32_t total;
        uint16_t cmd;
        std::memcpy(&total, header, 4);
        std::memcpy(&cmd, header + 4, 2);
        total = ntohl(total);
        cmd = ntohs(cmd);
        if (total < 6) break;

        std::string body;
        body.resize(total - 6);
        if (total > 6 && !recvN(fd, body.data(), body.size())) break;

        std::cout << "\n[收到] cmd=" << cmd << " body=" << body
                  << "\n> " << std::flush;
    }
    running = false;
    std::cout << "\n[连接已断开]\n" << std::flush;
}

int main() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    std::atomic<bool> running{true};
    std::thread reader(recvLoop, fd, std::ref(running));
    reader.detach();

    std::cout << "用法: login <名字> / chat <内容> / echo <内容> / quit\n";
    std::string line;
    while (running) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "quit") {
            break;
        } else if (line.rfind("login ", 0) == 0) {
            sendAll(fd, packFrame(2, line.substr(6)));
        } else if (line.rfind("chat ", 0) == 0) {
            sendAll(fd, packFrame(3, line.substr(5)));
        } else if (line.rfind("echo ", 0) == 0) {
            sendAll(fd, packFrame(1, line.substr(5)));
        } else {
            std::cout << "无法识别\n";
        }
    }
    running = false;
    ::close(fd);
    return 0;
}
