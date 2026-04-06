/**
 * MBC5.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * MBC5 – up to 8 MB ROM (512 × 16 KB banks) / 128 KB RAM (16 × 8 KB banks).
 *
 * Register map:
 *   0x0000–0x1FFF  RAM enable (0x0A in lower nibble → enable; else disable)
 *   0x2000–0x2FFF  ROM bank, lower 8 bits
 *   0x3000–0x3FFF  ROM bank, bit 8 (upper bit)
 *   0x4000–0x5FFF  RAM bank select (0–0x0F)
 *
 * Differences from MBC1:
 *   - ROM bank 0 CAN be selected in the 0x4000 window (no forced-to-1 quirk).
 *   - No ROM/RAM banking mode toggle.
 *   - 9-bit ROM bank register allows 512 banks (8 MB).
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "IMBC.hpp"

namespace GB {

class MBC5 final : public IMBC {
public:
    u8 read(const std::vector<u8>& romData,
            const std::vector<u8>& ramData,
            u16 address) const override {

        const u32 romBanks = std::max<u32>(1u, static_cast<u32>(romData.size() / 0x4000u));

        if (address <= 0x3FFFu) {
            return (address < romData.size()) ? romData[address] : 0xFF;
        }

        if (address <= 0x7FFFu) {
            // 9-bit bank number: no forced-to-1 (bank 0 allowed)
            u32 bank = static_cast<u32>(m_romBank) % romBanks;
            u32 off = (bank << 14u) | (address - 0x4000u);
            return (off < romData.size()) ? romData[off] : 0xFF;
        }

        if (address >= 0xA000u && address <= 0xBFFFu) {
            if (!m_ramEnabled || ramData.empty()) return 0xFF;
            const u32 ramBanks = std::max<u32>(1u, static_cast<u32>(ramData.size() / 0x2000u));
            u32 bank = static_cast<u32>(m_ramBank) % ramBanks;
            u32 off = (bank << 13u) | (address - 0xA000u);
            return (off < ramData.size()) ? ramData[off] : 0xFF;
        }

        return 0xFF;
    }

    void write(std::vector<u8>& ramData, u16 address, u8 value) override {
        if (address <= 0x1FFFu) {
            m_ramEnabled = ((value & 0x0Fu) == 0x0Au);
            return;
        }
        if (address <= 0x2FFFu) {
            // Lower 8 bits of ROM bank
            m_romBank = (m_romBank & 0x100u) | static_cast<u16>(value);
            return;
        }
        if (address <= 0x3FFFu) {
            // Bit 8 of ROM bank (only bit 0 of value used)
            m_romBank = (m_romBank & 0x0FFu) | (static_cast<u16>(value & 0x01u) << 8u);
            return;
        }
        if (address <= 0x5FFFu) {
            m_ramBank = value & 0x0Fu;
            return;
        }
        if (address >= 0xA000u && address <= 0xBFFFu) {
            if (!m_ramEnabled || ramData.empty()) return;
            const u32 ramBanks = std::max<u32>(1u, static_cast<u32>(ramData.size() / 0x2000u));
            u32 bank = static_cast<u32>(m_ramBank) % ramBanks;
            u32 off = (bank << 13u) | (address - 0xA000u);
            if (off < ramData.size()) ramData[off] = value;
        }
    }

    void reset() override {
        m_romBank    = 1;
        m_ramBank    = 0;
        m_ramEnabled = false;
    }

private:
    u16  m_romBank    = 1;     // 9-bit bank, MBC5 allows bank 0 in upper window
    u8   m_ramBank    = 0;
    bool m_ramEnabled = false;
};

} // namespace GB
