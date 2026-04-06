/**
 * MBC2.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * MBC2 – up to 256 KB ROM / 512 × 4-bit internal RAM.
 *
 * Register map (write-to-ROM intercepts, address bit 8 is the discriminator):
 *   0x0000–0x3FFF  A8=0: RAM enable (0x0A in lower nibble → enable; else disable)
 *                  A8=1: ROM bank select (lower 4 bits, 0 treated as 1)
 *
 * RAM notes:
 *   - 512 half-bytes of internal RAM, mapped to 0xA000–0xA1FF.
 *   - Upper nibble of each byte always reads as 0xF (undefined on hardware).
 *   - The 0xA200–0xBFFF window mirrors 0xA000–0xA1FF (512-byte wrap).
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "IMBC.hpp"
#include <array>

namespace GB {

class MBC2 final : public IMBC {
public:
    static constexpr u16 INTERNAL_RAM_SIZE = 512;

    u8 read(const std::vector<u8>& romData,
            const std::vector<u8>& /*ramData*/,
            u16 address) const override {

        const u32 romBanks = std::max<u32>(1u, static_cast<u32>(romData.size() / 0x4000u));

        if (address <= 0x3FFF) {
            return (address < romData.size()) ? romData[address] : 0xFF;
        }

        if (address <= 0x7FFF) {
            u8 bank = (m_romBank == 0) ? 1 : (m_romBank & 0x0Fu);
            bank = static_cast<u8>(bank % romBanks);
            u32 off = (static_cast<u32>(bank) << 14u) | (address - 0x4000u);
            return (off < romData.size()) ? romData[off] : 0xFF;
        }

        if (address >= 0xA000u && address <= 0xBFFFu) {
            if (!m_ramEnabled) return 0xFF;
            u16 off = (address - 0xA000u) & 0x01FFu; // 512-byte mirror
            return m_internalRam[off] | 0xF0u; // upper nibble always 1
        }

        return 0xFF;
    }

    void write(std::vector<u8>& /*ramData*/, u16 address, u8 value) override {
        if (address <= 0x3FFFu) {
            // Bit 8 of address selects register
            if (!((address >> 8u) & 1u)) {
                // A8=0: RAM enable
                m_ramEnabled = ((value & 0x0Fu) == 0x0Au);
            } else {
                // A8=1: ROM bank select
                m_romBank = value & 0x0Fu;
                if (m_romBank == 0) m_romBank = 1;
            }
            return;
        }

        if (address >= 0xA000u && address <= 0xBFFFu) {
            if (!m_ramEnabled) return;
            u16 off = (address - 0xA000u) & 0x01FFu;
            m_internalRam[off] = value & 0x0Fu; // only lower nibble stored
        }
    }

    void reset() override {
        m_romBank    = 1;
        m_ramEnabled = false;
        m_internalRam.fill(0xFFu);
    }

private:
    u8   m_romBank    = 1;
    bool m_ramEnabled = false;
    std::array<u8, INTERNAL_RAM_SIZE> m_internalRam{};
};

} // namespace GB
