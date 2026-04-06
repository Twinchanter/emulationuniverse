/**
 * IMemory.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Abstract read/write contract for any addressable memory region.
 * Concrete implementations (MMU, VRAM, WRAM, Cartridge ROM/RAM) all satisfy
 * this interface, which lets the CPU and DMA units refer to memory without
 * depending on any specific mapping strategy.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../Types.hpp"

namespace GB {

class IMemory {
public:
    virtual ~IMemory() = default;

    /// Read a single byte from the given 16-bit address.
    virtual u8  read(u16 address) const = 0;

    /// Write a single byte to the given 16-bit address.
    virtual void write(u16 address, u8 value) = 0;

    // ── Convenience helpers (default to byte-level ops) ──────────────────────
    /// Read a 16-bit little-endian word starting at @p address.
    virtual u16 readWord(u16 address) const {
        return makeU16(read(address), read(address + 1u));
    }

    /// Write a 16-bit little-endian word starting at @p address.
    virtual void writeWord(u16 address, u16 value) {
        write(address,      static_cast<u8>(value & 0xFFu));
        write(address + 1u, static_cast<u8>(value >> 8u));
    }
};

} // namespace GB
