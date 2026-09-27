// UDP-клиент: LOGIN -> циклическая отправка MOVEMENT / SHOOT раз в секунду
// (период настраивается), вывод ответов сервера.
// Использование:
//   client [server_ip] [port] [login] [password] [count] [interval_ms]
// По умолчанию: 127.0.0.1 54000 player1 qwerty 10 1000
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
using SOCKET = int;
inline int closesocket(int s) { return ::close(s); }
inline int WSAGetLastError() { return errno; }
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../common/protocol.h"

using namespace netproto;

// Отправить пакет и дождаться ответа (timeout_ms). Возвращает false при таймауте.
static bool roundTrip(SOCKET sock, const sockaddr_in& srv,
                      PacketType type, uint16_t seq,
                      const std::vector<uint8_t>& payload,
                      Packet& answer, bool& badCrc, int timeout_ms) {
    auto bytes = encodePacket(type, seq, payload);
    if (sendto(sock, reinterpret_cast<const char*>(bytes.data()),
               static_cast<int>(bytes.size()), 0,
               reinterpret_cast<const sockaddr*>(&srv), sizeof(srv)) == SOCKET_ERROR) {
        std::cerr << "sendto failed: " << WSAGetLastError() << "\n";
        return false;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(static_cast<int>(sock + 1), &rfds, nullptr, nullptr, &tv);
    if (r <= 0) return false; // timeout / error

    std::vector<uint8_t> buf(MAX_UDP);
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(sock, reinterpret_cast<char*>(buf.data()),
                     static_cast<int>(buf.size()), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n == SOCKET_ERROR) return false;
    try {
        answer = decodePacket(buf.data(), static_cast<std::size_t>(n), badCrc);
    } catch (const std::exception& e) {
        std::cout << "  [malformed reply: " << e.what() << "]\n";
        return false;
    }
    return true;
}

static void printAnswer(const Packet& a) {
    std::cout << "  <- " << typeName(a.header.type)
              << " seq=" << a.header.seq
              << " size=" << a.header.payloadSize;
    try {
        switch (a.header.type) {
            case PacketType::STATE_UPDATE: {
                uint16_t ack; float x, y, z;
                parseStatePayload(a.payload, ack, x, y, z);
                std::cout << " ack=" << ack << " new_pos=("
                          << x << "," << y << "," << z << ")";
                break;
            }
            case PacketType::SHOT_RESULT: {
                uint16_t ack; uint8_t w, hit;
                parseShotResultPayload(a.payload, ack, w, hit);
                std::cout << " ack=" << ack << " weapon=" << (int)w
                          << " hit=" << (int)hit;
                break;
            }
            case PacketType::AUTH_OK:
            case PacketType::AUTH_FAIL:
            case PacketType::PACKET_ERROR: {
                uint16_t ack; uint8_t code; std::string msg;
                parseTextPayload(a.payload, ack, code, msg);
                std::cout << " ack=" << ack << " code=" << (int)code
                          << " msg='" << msg << "'";
                break;
            }
            default:
                std::cout << " (raw " << a.payload.size() << " bytes)";
        }
    } catch (const std::exception& e) {
        std::cout << " [payload parse error: " << e.what() << "]";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
    std::string ip = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : DEFAULT_PORT;
    std::string login = argc > 3 ? argv[3] : "player1";
    std::string password = argc > 4 ? argv[4] : "qwerty";
    int count = argc > 5 ? std::atoi(argv[5]) : 10;
    int intervalMs = argc > 6 ? std::atoi(argv[6]) : 1000;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &srv.sin_addr) != 1) {
        std::cerr << "bad server ip: " << ip << "\n";
        return 1;
    }

    std::cout << "UDP client -> " << ip << ":" << port
              << " login=" << login << " count=" << count
              << " interval=" << intervalMs << "ms\n";

    uint16_t seq = 0;
    Packet ans;
    bool badCrc = false;

    // 1) Авторизация (доп. задание)
    std::cout << "[" << seq << "] -> LOGIN\n";
    if (roundTrip(sock, srv, PacketType::LOGIN, seq,
                  makeLoginPayload(login, password), ans, badCrc, 2000)) {
        if (badCrc) std::cout << "  [warning: reply CRC FAIL]\n";
        printAnswer(ans);
        if (ans.header.type == PacketType::AUTH_FAIL) {
            std::cerr << "Auth failed, exiting.\n";
            closesocket(sock);
            return 2;
        }
    } else {
        std::cout << "  [no reply to LOGIN]\n";
    }
    seq++;

    // 2) Игровые команды с заданной периодичностью
    float x = 0.0f, y = 0.0f, z = 0.0f;
    for (int i = 0; i < count; ++i) {
        bool doMove = (i % 2 == 0);
        PacketType t;
        std::vector<uint8_t> pl;
        if (doMove) {
            x += 1.0f; y += 0.5f;
            t = PacketType::MOVEMENT;
            pl = makeMovementPayload(x, y, z);
            std::cout << "[" << seq << "] -> MOVEMENT x=" << x << " y=" << y << " z=" << z << "\n";
        } else {
            uint8_t weapon = static_cast<uint8_t>((i / 2) % 4);
            t = PacketType::SHOOT;
            pl = makeShootPayload(weapon);
            std::cout << "[" << seq << "] -> SHOOT weaponId=" << (int)weapon << "\n";
        }
        if (roundTrip(sock, srv, t, seq, pl, ans, badCrc, 2000)) {
            if (badCrc) std::cout << "  [warning: reply CRC FAIL]\n";
            printAnswer(ans);
        } else {
            std::cout << "  [no reply / timeout]\n";
        }
        seq++;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
