#include "UUID.h"

#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace DirtSim {

namespace {

std::mt19937_64& getRandomEngine()
{
    thread_local std::mt19937_64 engine{ std::random_device{}() };
    return engine;
}

uint8_t hexCharToNibble(char c)
{
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    throw std::invalid_argument("Invalid hex character in UUID string");
}

} // namespace

UUID::UUID() : bytes_{}
{}

UUID UUID::generate()
{
    UUID uuid;
    auto& engine = getRandomEngine();
    std::uniform_int_distribution<uint64_t> dist;

    // Fill with random bytes.
    uint64_t high = dist(engine);
    uint64_t low = dist(engine);

    for (int i = 0; i < 8; ++i) {
        uuid.bytes_[i] = static_cast<uint8_t>((high >> (56 - i * 8)) & 0xFF);
        uuid.bytes_[i + 8] = static_cast<uint8_t>((low >> (56 - i * 8)) & 0xFF);
    }

    // Version 4 (random) in byte 6.
    uuid.bytes_[6] = (uuid.bytes_[6] & 0x0F) | 0x40;

    // Variant (RFC 4122) in byte 8.
    uuid.bytes_[8] = (uuid.bytes_[8] & 0x3F) | 0x80;

    return uuid;
}

UUID UUID::nil()
{
    return UUID{};
}

UUID UUID::fromString(const std::string& str)
{
    if (str.length() != 36) {
        throw std::invalid_argument("UUID string must be 36 characters");
    }

    if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') {
        throw std::invalid_argument("UUID string must have dashes at positions 8, 13, 18, 23");
    }

    UUID uuid;
    int byteIndex = 0;

    for (size_t i = 0; i < str.length(); i += 2) {
        if (str[i] == '-') {
            --i;
            continue;
        }

        uint8_t high = hexCharToNibble(str[i]);
        uint8_t low = hexCharToNibble(str[i + 1]);
        uuid.bytes_[byteIndex++] = (high << 4) | low;
    }

    return uuid;
}

std::string UUID::toString() const
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(bytes_[i]);
    }

    return oss.str();
}

std::string UUID::toShortString() const
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (int i = 0; i < 4; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes_[i]);
    }

    return oss.str();
}

bool UUID::isNil() const
{
    for (uint8_t byte : bytes_) {
        if (byte != 0) return false;
    }
    return true;
}

bool UUID::operator==(const UUID& other) const
{
    return bytes_ == other.bytes_;
}

bool UUID::operator!=(const UUID& other) const
{
    return !(*this == other);
}

bool UUID::operator<(const UUID& other) const
{
    return bytes_ < other.bytes_;
}

} // namespace DirtSim

std::size_t std::hash<DirtSim::UUID>::operator()(const DirtSim::UUID& uuid) const noexcept
{
    const auto& bytes = uuid.bytes();
    std::size_t hash = 14695981039346656037ULL;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}
