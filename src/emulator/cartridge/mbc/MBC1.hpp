/**
 * MBC1.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * MBC1 – up to 2 MB ROM / 32 KB RAM.
 * Used by: Pokémon Red/Blue/Yellow, Link's Awakening DX, and many others.
 *
 * Register map (write-to-ROM intercepts):
 *   0x0000–0x1FFF  RAM Enable (0x0A in lower nibble enables, any other disables)
 *   0x2000–0x3FFF  ROM bank lower 5 bits (0 treated as 1)
 *   0x4000–0x5FFF  ROM bank upper 2 bits OR RAM bank select
 *   0x6000–0x7FFF  Mode: 0 = ROM banking (default), 1 = RAM banking
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "IMBC.hpp"

namespace GB {

class MBC1 final : public IMBC {
public:
    u8 read(const std::vector<u8>& romData,
            const std::vector<u8>& ramData,
            u16 address) const override {

        const u32 romBanks = std::max<u32>(1u, static_cast<u32>(romData.size() / 0x4000u));

        if (address <= ROM_BANK0_END) {
            // In mode 1 the upper bits can also affect bank 0.
            u32 bank = (m_mode == 1) ? static_cast<u32>(m_upperBits << 5u) : 0u;
            bank %= romBanks;
            u32 offset = (bank << 14u) | address;
            return (offset < romData.size()) ? romData[offset] : 0xFF;
        }

        if (address <= ROM_BANKN_END) {
            // Assembled bank = (upper 2 bits << 5) | lower 5 bits, bank 0->1
            u8  bank   = ((m_upperBits << 5u) | m_romBank) & 0x7Fu;
            if (bank == 0) bank = 1; // Hardware quirk: cannot select bank 0 in upper window
            bank = static_cast<u8>(bank % romBanks);
            u32 offset = (static_cast<u32>(bank) << 14u) | (address - ROM_BANKN_START);
            return (offset < romData.size()) ? romData[offset] : 0xFF;
        }

        if (address >= ERAM_START && address <= ERAM_END) {
            if (!m_ramEnabled || ramData.empty()) return 0xFF;
            const u32 ramBanks = std::max<u32>(1u, static_cast<u32>(ramData.size() / 0x2000u));
            u8  bank   = (m_mode == 1) ? m_upperBits : 0u;
            bank = static_cast<u8>(bank % ramBanks);
            u32 offset = (static_cast<u32>(bank) << 13u) | (address - ERAM_START);
            return (offset < ramData.size()) ? ramData[offset] : 0xFF;
        }

        return 0xFF;
    }

    void write(std::vector<u8>& ramData, u16 address, u8 value) override {
        if (address <= 0x1FFF) {
            // RAM Enable: any value with 0x0A in lower nibble enables
            m_ramEnabled = ((value & 0x0Fu) == 0x0Au);
            return;
        }
        if (address <= 0x3FFF) {
            // ROM bank lower 5 bits (mask to 5 bits, 0 becomes 1)
            m_romBank = value & 0x1Fu;
            if (m_romBank == 0) m_romBank = 1;
            return;
        }
        if (address <= 0x5FFF) {
            // Upper 2 bits (dual-use: RAM bank or ROM upper bits)
            m_upperBits = value & 0x03u;
            return;
        }
        if (address <= 0x7FFF) {
            m_mode = value & 0x01u; // 0 = ROM banking mode, 1 = RAM banking mode
            return;
        }
        if (address >= ERAM_START && address <= ERAM_END) {
            if (!m_ramEnabled || ramData.empty()) return;
            const u32 ramBanks = std::max<u32>(1u, static_cast<u32>(ramData.size() / 0x2000u));
            u8  bank   = (m_mode == 1) ? m_upperBits : 0u;
            bank = static_cast<u8>(bank % ramBanks);
            u32 offset = (static_cast<u32>(bank) << 13u) | (address - ERAM_START);
            if (offset < ramData.size()) ramData[offset] = value;
        }
    }

    void reset() override {
        m_romBank    = 1;
        m_upperBits  = 0;
        m_mode       = 0;
        m_ramEnabled = false;
    }

private:
    u8   m_romBank    = 1;     // Lower 5 bits of ROM bank number
    u8   m_upperBits  = 0;     // Upper 2 bits (ROM or RAM)
    u8   m_mode       = 0;     // Banking mode (0=ROM, 1=RAM)
    bool m_ramEnabled = false;
};

} // namespace GB
