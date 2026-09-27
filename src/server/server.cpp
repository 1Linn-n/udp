// UDP-сервер: принимает пакеты, проверяет CRC32, авторизует, обрабатывает
// MOVEMENT / SHOOT, возвращает состояние, ведёт лог (stdout + server.log).
// Кроссплатформа: Winsock2 (Windows) / Berkeley sockets (Linux).
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <sys/socket.h>
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

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../common/protocol.h"

using namespace netproto;

static std::ofstream g_log;
static uint16_t g_serverSeq = 0;

static std::string nowStr() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];
    std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&t));
    return buf;
}

static void logLine(const std::string& s) {
    std::cout << s << std::endl;
    if (g_log.is_open()) { g_log << s << std::endl; }
}

struct PlayerState {
    float x = 0, y = 0, z = 0;
    uint32_t moves = 0, shots = 0;
};

int main(int argc, char** argv) {
    uint16_t port = DEFAULT_PORT;
    if (argc >= 2) port = static_cast<uint16_t>(std::atoi(argv[1]));

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind(" << port << ") failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        return 1;
    }

    g_log.open("server.log", std::ios::app);

    // Заранее заданный список логин/пароль (доп. задание: авторизация)
    const std::map<std::string, std::string> users = {
        {"player1", "qwerty"},
        {"admin", "admin123"},
        {"guest", "guest"},
    };
    std::set<std::string> authed;          // "ip:port"
    std::map<std::string, PlayerState> players;

    logLine("[" + nowStr() + "] UDP server listening on 0.0.0.0:" + std::to_string(port));

    std::vector<uint8_t> buf(MAX_UDP);
    for (;;) {
        sockaddr_in cli{};
        socklen_t cliLen = sizeof(cli);
        int n = recvfrom(sock, reinterpret_cast<char*>(buf.data()),
                         static_cast<int>(buf.size()), 0,
                         reinterpret_cast<sockaddr*>(&cli), &cliLen);
        if (n == SOCKET_ERROR) {
            logLine("[" + nowStr() + "] recvfrom error: " + std::to_string(WSAGetLastError()));
            continue;
        }
        char ipStr[64] = {};
#ifdef _WIN32
        InetNtopA(AF_INET, &cli.sin_addr, ipStr, sizeof(ipStr));
#else
        inet_ntop(AF_INET, &cli.sin_addr, ipStr, sizeof(ipStr));
#endif
        std::string endpoint = std::string(ipStr) + ":" + std::to_string(ntohs(cli.sin_port));

        bool badCrc = false;
        Packet pkt;
        try {
            pkt = decodePacket(buf.data(), static_cast<std::size_t>(n), badCrc);
        } catch (const std::exception& e) {
            logLine("[" + nowStr() + "] " + endpoint + " MALFORMED (" +
                    std::to_string(n) + " bytes): " + e.what());
            continue;
        }

        // Базовый лог всех полученных команд (требование)
        std::string hdr = "[" + nowStr() + "] " + endpoint +
            " <- " + typeName(pkt.header.type) +
            " seq=" + std::to_string(pkt.header.seq) +
            " size=" + std::to_string(pkt.header.payloadSize) +
            " crc=" + (badCrc ? "FAIL" : "OK");

        if (badCrc) {
            logLine(hdr + " -- dropped (checksum mismatch)");
            auto resp = encodePacket(PacketType::PACKET_ERROR, g_serverSeq++,
                makeTextPayload(pkt.header.seq, 1, "checksum mismatch"));
            sendto(sock, reinterpret_cast<const char*>(resp.data()),
                   static_cast<int>(resp.size()), 0,
                   reinterpret_cast<sockaddr*>(&cli), cliLen);
            continue;
        }

        std::vector<uint8_t> respPayload;
        PacketType respType = PacketType::PACKET_ERROR;

        try {
            switch (pkt.header.type) {
                case PacketType::LOGIN: {
                    std::string login, pass;
                    parseLoginPayload(pkt.payload, login, pass);
                    logLine(hdr + " login='" + login + "'");
                    auto it = users.find(login);
                    if (it != users.end() && it->second == pass) {
                        authed.insert(endpoint);
                        players.try_emplace(endpoint);
                        respType = PacketType::AUTH_OK;
                        respPayload = makeTextPayload(pkt.header.seq, 0, "welcome " + login);
                        logLine("[" + nowStr() + "] " + endpoint + " AUTH OK (" + login + ")");
                    } else {
                        respType = PacketType::AUTH_FAIL;
                        respPayload = makeTextPayload(pkt.header.seq, 1, "invalid login/password");
                        logLine("[" + nowStr() + "] " + endpoint + " AUTH FAIL (" + login + ")");
                    }
                    break;
                }
                case PacketType::MOVEMENT: {
                    float x, y, z;
                    parseMovementPayload(pkt.payload, x, y, z);
                    logLine(hdr + " x=" + std::to_string(x) +
                            " y=" + std::to_string(y) + " z=" + std::to_string(z));
                    if (!authed.count(endpoint)) {
                        respPayload = makeTextPayload(pkt.header.seq, 2, "auth required: send LOGIN first");
                        logLine("[" + nowStr() + "] " + endpoint + " MOVEMENT rejected (no auth)");
                    } else {
                        PlayerState& st = players[endpoint];
                        st.x = x; st.y = y; st.z = z; st.moves++;
                        respType = PacketType::STATE_UPDATE;
                        respPayload = makeStatePayload(pkt.header.seq, st.x, st.y, st.z);
                        logLine("[" + nowStr() + "] " + endpoint + " -> STATE_UPDATE seq=" +
                                std::to_string(pkt.header.seq) + " pos=(" +
                                std::to_string(st.x) + "," + std::to_string(st.y) + "," +
                                std::to_string(st.z) + ")");
                    }
                    break;
                }
                case PacketType::SHOOT: {
                    uint8_t weapon = parseShootPayload(pkt.payload);
                    logLine(hdr + " weaponId=" + std::to_string(weapon));
                    if (!authed.count(endpoint)) {
                        respPayload = makeTextPayload(pkt.header.seq, 2, "auth required: send LOGIN first");
                        logLine("[" + nowStr() + "] " + endpoint + " SHOOT rejected (no auth)");
                    } else {
                        players[endpoint].shots++;
                        uint8_t hit = (weapon % 2 == 0) ? 1 : 0; // детерминированная имитация
                        respType = PacketType::SHOT_RESULT;
                        respPayload = makeShotResultPayload(pkt.header.seq, weapon, hit);
                        logLine("[" + nowStr() + "] " + endpoint + " -> SHOT_RESULT weapon=" +
                                std::to_string(weapon) + " hit=" + std::to_string(hit));
                    }
                    break;
                }
                default: {
                    logLine(hdr + " -- unknown type, ignored");
                    respPayload = makeTextPayload(pkt.header.seq, 3, "unknown packet type");
                    break;
                }
            }
        } catch (const std::exception& e) {
            logLine(hdr + " -- payload error: " + e.what());
            respType = PacketType::PACKET_ERROR;
            respPayload = makeTextPayload(pkt.header.seq, 4, std::string("payload error: ") + e.what());
        }

        auto resp = encodePacket(respType, g_serverSeq++, respPayload);
        sendto(sock, reinterpret_cast<const char*>(resp.data()),
               static_cast<int>(resp.size()), 0,
               reinterpret_cast<sockaddr*>(&cli), cliLen);
    }

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
