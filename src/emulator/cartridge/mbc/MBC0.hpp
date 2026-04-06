/**
 * MBC0.hpp / No MBC (ROM-only cartridge)
 * ─────────────────────────────────────────────────────────────────────────────
 * Cartridge type 0x00: Up to 32 KB ROM, no banking, no external RAM.
 * Used by Tetris, Dr. Mario, and other early titles.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "IMBC.hpp"

namespace GB {

class MBC0 final : public IMBC {
public:
    u8 read(const std::vector<u8>& romData,
            const std::vector<u8>& /*ramData*/,
            u16 address) const override {
        // ROM spans 0x0000–0x7FFF; clamp to avoid out-of-bounds on undersized ROMs
        if (address < romData.size()) {
            return romData[address];
        }
        return 0xFF; // Open bus
    }

    void write(std::vector<u8>& /*ramData*/, u16 /*address*/, u8 /*value*/) override {
        // ROM-only: writes are silently ignored
    }

    void reset() override {}
};

} // namespace GB
