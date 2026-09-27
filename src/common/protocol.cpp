// Реализация протокола: CRC32, BE-сериализация, encode/decode.
#include "protocol.h"
#include <cstring>

namespace netproto {

uint32_t crc32(const uint8_t* data, std::size_t len, uint32_t crc) {
    // Табличный CRC32 IEEE; таблица строится лениво один раз.
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    for (std::size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void putU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void putU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void putFloatBE(std::vector<uint8_t>& out, float f) {
    static_assert(sizeof(float) == 4, "need 32-bit float");
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits)); // host bits
    putU32BE(out, bits);                  // записываем старшим байтом вперёд
}

uint16_t getU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t getU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

float getFloatBE(const uint8_t* p) {
    uint32_t bits = getU32BE(p); // BE -> host bits (memcpy сохраняет биты IEEE754)
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

std::vector<uint8_t> encodePacket(PacketType type, uint16_t seq,
                                  const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> buf;
    buf.reserve(HEADER_SIZE + payload.size());
    buf.push_back(static_cast<uint8_t>(type));
    putU16BE(buf, seq);
    putU16BE(buf, static_cast<uint16_t>(payload.size()));
    // CRC32 по [type,seqHi,seqLo,sizeHi,sizeLo] + payload
    uint32_t crc;
    // Считаем по склеенному буферу (прозрачно и без ошибок цепочки):
    {
        std::vector<uint8_t> tmp;
        tmp.reserve(5 + payload.size());
        tmp.push_back(static_cast<uint8_t>(type));
        tmp.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
        tmp.push_back(static_cast<uint8_t>(seq & 0xFF));
        uint16_t sz = static_cast<uint16_t>(payload.size());
        tmp.push_back(static_cast<uint8_t>((sz >> 8) & 0xFF));
        tmp.push_back(static_cast<uint8_t>(sz & 0xFF));
        tmp.insert(tmp.end(), payload.begin(), payload.end());
        crc = crc32(tmp.data(), tmp.size());
    }
    putU32BE(buf, crc);
    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

Packet decodePacket(const uint8_t* data, std::size_t len, bool& badCrc) {
    if (len < HEADER_SIZE)
        throw std::runtime_error("packet too short (< header)");
    Packet pkt;
    pkt.header.type = static_cast<PacketType>(data[0]);
    pkt.header.seq = getU16BE(data + 1);
    pkt.header.payloadSize = getU16BE(data + 3);
    pkt.header.checksum = getU32BE(data + 5);
    if (len < HEADER_SIZE + pkt.header.payloadSize)
        throw std::runtime_error("packet truncated (payload shorter than declared)");
    pkt.payload.assign(data + HEADER_SIZE, data + HEADER_SIZE + pkt.header.payloadSize);
    // Проверка CRC32
    std::vector<uint8_t> tmp;
    tmp.reserve(5 + pkt.payload.size());
    tmp.insert(tmp.end(), data, data + 5); // type+seq+size как пришли
    tmp.insert(tmp.end(), pkt.payload.begin(), pkt.payload.end());
    uint32_t calc = crc32(tmp.data(), tmp.size());
    badCrc = (calc != pkt.header.checksum);
    return pkt;
}

// ---------- builders ----------
std::vector<uint8_t> makeMovementPayload(float x, float y, float z) {
    std::vector<uint8_t> p;
    p.reserve(12);
    putFloatBE(p, x);
    putFloatBE(p, y);
    putFloatBE(p, z);
    return p;
}

std::vector<uint8_t> makeShootPayload(uint8_t weaponId) {
    return std::vector<uint8_t>{weaponId};
}

std::vector<uint8_t> makeLoginPayload(const std::string& login, const std::string& password) {
    if (login.size() > 255 || password.size() > 255)
        throw std::runtime_error("login/password too long (max 255)");
    std::vector<uint8_t> p;
    p.reserve(2 + login.size() + password.size());
    p.push_back(static_cast<uint8_t>(login.size()));
    p.insert(p.end(), login.begin(), login.end());
    p.push_back(static_cast<uint8_t>(password.size()));
    p.insert(p.end(), password.begin(), password.end());
    return p;
}

std::vector<uint8_t> makeStatePayload(uint16_t ackSeq, float x, float y, float z) {
    std::vector<uint8_t> p;
    p.reserve(14);
    putU16BE(p, ackSeq);
    putFloatBE(p, x);
    putFloatBE(p, y);
    putFloatBE(p, z);
    return p;
}

std::vector<uint8_t> makeShotResultPayload(uint16_t ackSeq, uint8_t weaponId, uint8_t hit) {
    std::vector<uint8_t> p;
    p.reserve(4);
    putU16BE(p, ackSeq);
    p.push_back(weaponId);
    p.push_back(hit);
    return p;
}

std::vector<uint8_t> makeTextPayload(uint16_t ackSeq, uint8_t code, const std::string& msg) {
    if (msg.size() > 255)
        throw std::runtime_error("message too long (max 255)");
    std::vector<uint8_t> p;
    p.reserve(4 + msg.size());
    putU16BE(p, ackSeq);
    p.push_back(code);
    p.push_back(static_cast<uint8_t>(msg.size()));
    p.insert(p.end(), msg.begin(), msg.end());
    return p;
}

// ---------- parsers ----------
void parseMovementPayload(const std::vector<uint8_t>& p, float& x, float& y, float& z) {
    if (p.size() != 12) throw std::runtime_error("MOVEMENT payload must be 12 bytes");
    x = getFloatBE(p.data());
    y = getFloatBE(p.data() + 4);
    z = getFloatBE(p.data() + 8);
}

uint8_t parseShootPayload(const std::vector<uint8_t>& p) {
    if (p.size() != 1) throw std::runtime_error("SHOOT payload must be 1 byte");
    return p[0];
}

void parseLoginPayload(const std::vector<uint8_t>& p, std::string& login, std::string& password) {
    if (p.size() < 2) throw std::runtime_error("LOGIN payload too short");
    std::size_t ulen = p[0];
    if (p.size() < 1 + ulen + 1) throw std::runtime_error("LOGIN payload truncated (login)");
    login.assign(reinterpret_cast<const char*>(p.data() + 1), ulen);
    std::size_t plen = p[1 + ulen];
    if (p.size() != 1 + ulen + 1 + plen)
        throw std::runtime_error("LOGIN payload size mismatch (password)");
    password.assign(reinterpret_cast<const char*>(p.data() + 1 + ulen + 1), plen);
}

void parseStatePayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, float& x, float& y, float& z) {
    if (p.size() != 14) throw std::runtime_error("STATE payload must be 14 bytes");
    ackSeq = getU16BE(p.data());
    x = getFloatBE(p.data() + 2);
    y = getFloatBE(p.data() + 6);
    z = getFloatBE(p.data() + 10);
}

void parseShotResultPayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, uint8_t& weaponId, uint8_t& hit) {
    if (p.size() != 4) throw std::runtime_error("SHOT_RESULT payload must be 4 bytes");
    ackSeq = getU16BE(p.data());
    weaponId = p[2];
    hit = p[3];
}

void parseTextPayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, uint8_t& code, std::string& msg) {
    if (p.size() < 4) throw std::runtime_error("TEXT payload too short");
    ackSeq = getU16BE(p.data());
    code = p[2];
    std::size_t mlen = p[3];
    if (p.size() != 4 + mlen) throw std::runtime_error("TEXT payload size mismatch");
    msg.assign(reinterpret_cast<const char*>(p.data() + 4), mlen);
}

const char* typeName(PacketType t) {
    switch (t) {
        case PacketType::MOVEMENT: return "MOVEMENT";
        case PacketType::SHOOT: return "SHOOT";
        case PacketType::LOGIN: return "LOGIN";
        case PacketType::STATE_UPDATE: return "STATE_UPDATE";
        case PacketType::SHOT_RESULT: return "SHOT_RESULT";
        case PacketType::AUTH_OK: return "AUTH_OK";
        case PacketType::AUTH_FAIL: return "AUTH_FAIL";
        case PacketType::PACKET_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace netproto
