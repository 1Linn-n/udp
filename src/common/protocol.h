#pragma once
// Общий UDP-протокол игры: типы, заголовок, сериализация, CRC32.
// Все многобайтовые поля — в сетевом порядке байт (big-endian).
// Заголовок: type(u8) | seq(u16 BE) | payload_size(u16 BE) | crc32(u32 BE) = 9 байт.
// CRC32 (IEEE) считается по байтам [type, seqHi, seqLo, sizeHi, sizeLo] + payload.

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

namespace netproto {

inline constexpr std::size_t HEADER_SIZE = 9;
inline constexpr std::size_t MAX_UDP = 65507;
inline constexpr uint16_t DEFAULT_PORT = 54000;

// Типы пакетов: 1..3 клиент->сервер, 100..103,255 сервер->клиент
enum class PacketType : uint8_t {
    MOVEMENT     = 1,   // payload: float x,y,z (12 байт)
    SHOOT        = 2,   // payload: weaponId u8 (1 байт)
    LOGIN        = 3,   // payload: ulen u8 + login + plen u8 + password
    STATE_UPDATE = 100, // payload: ackSeq u16 + float x,y,z (14 байт)
    SHOT_RESULT  = 101, // payload: ackSeq u16 + weaponId u8 + hit u8 (4 байта)
    AUTH_OK      = 102, // payload: ackSeq u16 + code u8 + msgLen u8 + msg
    AUTH_FAIL    = 103, // то же, что AUTH_OK
    PACKET_ERROR = 255  // то же, что AUTH_OK (имя без конфликта с макросом ERROR из windows.h)
};

struct Header {
    PacketType type{};
    uint16_t seq = 0;
    uint16_t payloadSize = 0;
    uint32_t checksum = 0;
};

struct Packet {
    Header header{};
    std::vector<uint8_t> payload;
};

// --- CRC32 (IEEE 802.3, poly 0xEDB88320) ---
uint32_t crc32(const uint8_t* data, std::size_t len, uint32_t crc = 0xFFFFFFFFu);

// --- helpers BE ---
void putU16BE(std::vector<uint8_t>& out, uint16_t v);
void putU32BE(std::vector<uint8_t>& out, uint32_t v);
void putFloatBE(std::vector<uint8_t>& out, float f);
uint16_t getU16BE(const uint8_t* p);
uint32_t getU32BE(const uint8_t* p);
float getFloatBE(const uint8_t* p);

// --- packet level ---
// Собрать дейтаграмму (header + payload) с корректным CRC32
std::vector<uint8_t> encodePacket(PacketType type, uint16_t seq,
                                  const std::vector<uint8_t>& payload);
// Разобрать дейтаграмму; при badCrc=true контрольная сумма НЕ сошлась
// (бросает runtime_error при слишком коротком пакете / обрезке payload)
Packet decodePacket(const uint8_t* data, std::size_t len, bool& badCrc);

// --- payload builders ---
std::vector<uint8_t> makeMovementPayload(float x, float y, float z);
std::vector<uint8_t> makeShootPayload(uint8_t weaponId);
std::vector<uint8_t> makeLoginPayload(const std::string& login, const std::string& password);
std::vector<uint8_t> makeStatePayload(uint16_t ackSeq, float x, float y, float z);
std::vector<uint8_t> makeShotResultPayload(uint16_t ackSeq, uint8_t weaponId, uint8_t hit);
std::vector<uint8_t> makeTextPayload(uint16_t ackSeq, uint8_t code, const std::string& msg);

// --- payload parsers (бросают runtime_error при неверном размере) ---
void parseMovementPayload(const std::vector<uint8_t>& p, float& x, float& y, float& z);
uint8_t parseShootPayload(const std::vector<uint8_t>& p);
void parseLoginPayload(const std::vector<uint8_t>& p, std::string& login, std::string& password);
void parseStatePayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, float& x, float& y, float& z);
void parseShotResultPayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, uint8_t& weaponId, uint8_t& hit);
void parseTextPayload(const std::vector<uint8_t>& p, uint16_t& ackSeq, uint8_t& code, std::string& msg);

const char* typeName(PacketType t);

} // namespace netproto
