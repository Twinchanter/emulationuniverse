/**
 * Registers.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Sharp LR35902 register file.
 *
 * The LR35902 has eight 8-bit registers organised into four 16-bit pairs:
 *   AF  –  Accumulator (A) + Flags (F)
 *   BC  –  General purpose (B high, C low)
 *   DE  –  General purpose (D high, E low)
 *   HL  –  Indirect address pointer (H high, L low)
 * Plus two 16-bit special-purpose registers:
 *   SP  –  Stack Pointer
 *   PC  –  Program Counter
 *
 * Flag register (F) layout: [Z N H C 0 0 0 0]
 *   Z – Zero flag      (bit 7): set when result is 0
 *   N – Subtract flag  (bit 6): set after subtraction
 *   H – Half-carry     (bit 5): carry from bit 3 into bit 4
 *   C – Carry flag     (bit 4): carry out of bit 7
 *   Bits 3-0 are always 0.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/Types.hpp"

namespace GB {

// Bit positions for each flag inside the F register
static constexpr u8 FLAG_Z_BIT = 7;  // Zero
static constexpr u8 FLAG_N_BIT = 6;  // Subtract
static constexpr u8 FLAG_H_BIT = 5;  // Half-carry
static constexpr u8 FLAG_C_BIT = 4;  // Carry

// Mask values (pre-shifted) for fast flag checks
static constexpr u8 FLAG_Z = 1u << FLAG_Z_BIT;
static constexpr u8 FLAG_N = 1u << FLAG_N_BIT;
static constexpr u8 FLAG_H = 1u << FLAG_H_BIT;
static constexpr u8 FLAG_C = 1u << FLAG_C_BIT;

/**
 * RegisterFile
 * The complete register state of the LR35902.
 * Pairs are stored as two 8-bit members so we can trivially serialise
 * state (for save-states or network sync) and avoid endian surprises.
 */
struct RegisterFile {
    // ── 8-bit registers ───────────────────────────────────────────────────────
    u8 A = 0x01; // Accumulator (DMG power-on value)
    u8 F = 0xB0; // Flags       (Z=1, N=0, H=1, C=1 on DMG boot)
    u8 B = 0x00;
    u8 C = 0x13;
    u8 D = 0x00;
    u8 E = 0xD8;
    u8 H = 0x01;
    u8 L = 0x4D;

    // ── 16-bit special-purpose ────────────────────────────────────────────────
    u16 SP = 0xFFFE; // Stack pointer after boot ROM exits
    u16 PC = 0x0100; // Execution begins at 0x0100 post-boot

    // ── 16-bit pair accessors ─────────────────────────────────────────────────

    inline u16 AF() const noexcept { return makeU16(F & 0xF0u, A); }
    inline void setAF(u16 v) noexcept { A = v >> 8u; F = v & 0xF0u; }

    inline u16 BC() const noexcept { return makeU16(C, B); }
    inline void setBC(u16 v) noexcept { B = v >> 8u; C = v & 0xFFu; }

    inline u16 DE() const noexcept { return makeU16(E, D); }
    inline void setDE(u16 v) noexcept { D = v >> 8u; E = v & 0xFFu; }

    inline u16 HL() const noexcept { return makeU16(L, H); }
    inline void setHL(u16 v) noexcept { H = v >> 8u; L = v & 0xFFu; }

    // ── Flag helpers (inline to avoid branch-heavy code in opcode handlers) ───

    inline bool flagZ() const noexcept { return (F & FLAG_Z) != 0; }
    inline bool flagN() const noexcept { return (F & FLAG_N) != 0; }
    inline bool flagH() const noexcept { return (F & FLAG_H) != 0; }
    inline bool flagC() const noexcept { return (F & FLAG_C) != 0; }

    inline void setZ(bool v) noexcept { v ? (F |= FLAG_Z) : (F &= ~FLAG_Z); F &= 0xF0u; }
    inline void setN(bool v) noexcept { v ? (F |= FLAG_N) : (F &= ~FLAG_N); F &= 0xF0u; }
    inline void setH(bool v) noexcept { v ? (F |= FLAG_H) : (F &= ~FLAG_H); F &= 0xF0u; }
    inline void setC(bool v) noexcept { v ? (F |= FLAG_C) : (F &= ~FLAG_C); F &= 0xF0u; }

    /**
     * Set all four flags at once.  Using a single call minimises stale writes
     * and is the idiomatic pattern inside ALU opcode handlers.
     */
    inline void setFlags(bool z, bool n, bool h, bool c) noexcept {
        F = (static_cast<u8>(z) << FLAG_Z_BIT) |
            (static_cast<u8>(n) << FLAG_N_BIT) |
            (static_cast<u8>(h) << FLAG_H_BIT) |
            (static_cast<u8>(c) << FLAG_C_BIT);
        // Lower nibble of F is always 0 per hardware spec
    }

    /// Reset to DMG power-on values (same as after the boot ROM completes).
    void reset() noexcept {
        A = 0x01; F = 0xB0;
        B = 0x00; C = 0x13;
        D = 0x00; E = 0xD8;
        H = 0x01; L = 0x4D;
        SP = 0xFFFE;
        PC = 0x0100;
    }
};

} // namespace GB
