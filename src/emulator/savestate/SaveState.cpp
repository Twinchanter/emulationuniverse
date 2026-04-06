#include "SaveState.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace GB {

namespace {

struct SaveHeader {
    char magic[8] = {'E', 'M', 'U', 'N', 'I', 'V', '2', '\0'};
    u32 version = 2;
    u32 payloadSize = 0;
    u32 checksumFNV1a = 0;
};

struct SavePayload {
    RegisterFile regs{};
    std::array<u8, 0x10000> mem{};
};

constexpr u32 FNV_OFFSET = 2166136261u;
constexpr u32 FNV_PRIME  = 16777619u;

u32 fnv1a(const u8* data, size_t size) {
    u32 h = FNV_OFFSET;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<u32>(data[i]);
        h *= FNV_PRIME;
    }
    return h;
}

void setError(std::string* out, const std::string& msg) {
    if (out) {
        *out = msg;
    }
}

bool validateRestoredState(GameBoy& gb, const SavePayload& payload, std::string* errorOut) {
    const RegisterFile gotRegs = gb.cpu().registers();
    if (std::memcmp(&gotRegs, &payload.regs, sizeof(RegisterFile)) != 0) {
        setError(errorOut, "register validation failed after restore");
        return false;
    }

    // Validate every byte in the 16-bit address space after restore.
    for (u32 addr = 0; addr <= 0xFFFFu; ++addr) {
        u8 got = gb.readByte(static_cast<u16>(addr));
        if (got != payload.mem[addr]) {
            setError(errorOut, "memory validation failed after restore at address " + std::to_string(addr));
            return false;
        }
    }

    return true;
};

} // namespace

bool SaveState::saveToFile(const GameBoy& gb, const std::string& path, std::string* errorOut) {
    SavePayload payload{};
    payload.regs = gb.cpu().registers();

    for (u32 addr = 0; addr <= 0xFFFFu; ++addr) {
        payload.mem[addr] = gb.readByte(static_cast<u16>(addr));
    }

    SaveHeader header{};
    header.payloadSize = static_cast<u32>(sizeof(SavePayload));
    header.checksumFNV1a = fnv1a(reinterpret_cast<const u8*>(&payload), sizeof(SavePayload));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        setError(errorOut, "failed to open save-state path for writing");
        return false;
    }

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&payload), sizeof(payload));

    if (!out.good()) {
        setError(errorOut, "failed to write save-state bytes");
        return false;
    }
    return true;
}

bool SaveState::loadFromFile(GameBoy& gb, const std::string& path, std::string* errorOut) {
    SaveHeader header{};

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setError(errorOut, "failed to open save-state path for reading");
        return false;
    }

    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in.good()) {
        setError(errorOut, "failed to read save-state header");
        return false;
    }

    const char expectedMagic[8] = {'E', 'M', 'U', 'N', 'I', 'V', '2', '\0'};
    if (std::memcmp(header.magic, expectedMagic, sizeof(expectedMagic)) != 0) {
        setError(errorOut, "invalid save-state magic");
        return false;
    }

    if (header.version != 2) {
        setError(errorOut, "unsupported save-state version");
        return false;
    }

    if (header.payloadSize != sizeof(SavePayload)) {
        setError(errorOut, "save-state payload size mismatch");
        return false;
    }

    SavePayload payload{};
    in.read(reinterpret_cast<char*>(&payload), sizeof(payload));
    if (!in.good()) {
        setError(errorOut, "failed to read save-state payload");
        return false;
    }

    const u32 actualChecksum = fnv1a(reinterpret_cast<const u8*>(&payload), sizeof(payload));
    if (actualChecksum != header.checksumFNV1a) {
        setError(errorOut, "save-state checksum mismatch");
        return false;
    }

    gb.reset();
    for (u32 addr = 0; addr <= 0xFFFFu; ++addr) {
        gb.writeByte(static_cast<u16>(addr), payload.mem[addr]);
    }
    gb.setCPURegisters(payload.regs);

    return validateRestoredState(gb, payload, errorOut);
}

} // namespace GB
