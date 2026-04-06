/**
 * IMBC.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Abstract Memory Bank Controller interface.
 *
 * MBCs intercept writes to the ROM address space (0x0000–0x7FFF) and to the
 * external RAM range (0xA000–0xBFFF) to control bank switching and RAM enable.
 * The Cartridge class owns the concrete MBC and delegates to it.
 *
 * Adding a new MBC (e.g. MBC5 for Gen-2 Pokémon) means:
 *   1. Create MBC5 : public IMBC
 *   2. Implement the three pure virtuals
 *   3. Register in Cartridge::createMBC()
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../../core/Types.hpp"
#include <vector>

namespace GB {

class IMBC {
public:
    virtual ~IMBC() = default;

    /**
     * Read a byte from the cartridge address space.
     * @p romData  Full ROM byte vector (read-only view).
     * @p ramData  Mutable external RAM vector.
     * @p address  16-bit CPU address (0x0000–0x7FFF or 0xA000–0xBFFF).
     */
    virtual u8 read(const std::vector<u8>& romData,
                    const std::vector<u8>& ramData,
                    u16 address) const = 0;

    /**
     * Write a byte to the cartridge address space.
     * Writes to ROM addresses are register writes intercepted by the MBC.
     * Writes to ERAM addresses go to external RAM when enabled.
     */
    virtual void write(std::vector<u8>& ramData,
                       u16 address, u8 value) = 0;

    /// Reset bank state to power-on defaults.
    virtual void reset() = 0;
};

} // namespace GB
