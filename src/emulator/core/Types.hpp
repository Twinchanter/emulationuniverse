/**
 * Types.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Common primitive type aliases and enumerations shared across every subsystem
 * in the emulator.  Centralising these here means that when we port to a new
 * target architecture (e.g. GBA, SNES) we only need to audit one file.
 *
 * Naming convention:
 *   u8  / s8   – unsigned / signed  8-bit
 *   u16 / s16  – unsigned / signed 16-bit
 *   u32 / s32  – unsigned / signed 32-bit
 *   u64        – unsigned 64-bit (cycle counter, etc.)
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>
#include <vector>
#include <functional>
#include <stdexcept>

namespace GB {

// ── Primitive aliases ────────────────────────────────────────────────────────
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

// ── Game Boy hardware constants ───────────────────────────────────────────────
static constexpr u32 CPU_CLOCK_HZ       = 4'194'304;  // 4.194304 MHz
static constexpr u32 FRAME_RATE         = 60;
static constexpr u32 CYCLES_PER_FRAME   = CPU_CLOCK_HZ / FRAME_RATE;  // ~69905

// Screen dimensions (pixels)
static constexpr int SCREEN_WIDTH  = 160;
static constexpr int SCREEN_HEIGHT = 144;

// Memory map boundaries
static constexpr u16 ROM_BANK0_START  = 0x0000;
static constexpr u16 ROM_BANK0_END    = 0x3FFF;
static constexpr u16 ROM_BANKN_START  = 0x4000;
static constexpr u16 ROM_BANKN_END    = 0x7FFF;
static constexpr u16 VRAM_START       = 0x8000;
static constexpr u16 VRAM_END         = 0x9FFF;
static constexpr u16 ERAM_START       = 0xA000;  // External (cartridge) RAM
static constexpr u16 ERAM_END         = 0xBFFF;
static constexpr u16 WRAM_START       = 0xC000;  // Work RAM
static constexpr u16 WRAM_END         = 0xDFFF;
static constexpr u16 ECHO_START       = 0xE000;  // Mirror of WRAM C000-DDFF
static constexpr u16 ECHO_END         = 0xFDFF;
static constexpr u16 OAM_START        = 0xFE00;  // Object Attribute Memory
static constexpr u16 OAM_END          = 0xFE9F;
static constexpr u16 UNUSABLE_START   = 0xFEA0;
static constexpr u16 UNUSABLE_END     = 0xFEFF;
static constexpr u16 IO_START         = 0xFF00;  // I/O Registers
static constexpr u16 IO_END           = 0xFF7F;
static constexpr u16 HRAM_START       = 0xFF80;  // High RAM / Zero Page
static constexpr u16 HRAM_END         = 0xFFFE;
static constexpr u16 IE_REGISTER      = 0xFFFF;  // Interrupt Enable

// I/O Register addresses
static constexpr u16 REG_JOYP  = 0xFF00;  // Joypad
static constexpr u16 REG_SB    = 0xFF01;  // Serial data
static constexpr u16 REG_SC    = 0xFF02;  // Serial control
static constexpr u16 REG_DIV   = 0xFF04;  // Divider
static constexpr u16 REG_TIMA  = 0xFF05;  // Timer counter
static constexpr u16 REG_TMA   = 0xFF06;  // Timer modulo
static constexpr u16 REG_TAC   = 0xFF07;  // Timer control
static constexpr u16 REG_IF    = 0xFF0F;  // Interrupt Flag
static constexpr u16 REG_NR10  = 0xFF10;  // APU channel 1 sweep
static constexpr u16 REG_LCDC  = 0xFF40;  // LCD Control
static constexpr u16 REG_STAT  = 0xFF41;  // LCD Status
static constexpr u16 REG_SCY   = 0xFF42;  // BG Scroll Y
static constexpr u16 REG_SCX   = 0xFF43;  // BG Scroll X
static constexpr u16 REG_LY    = 0xFF44;  // Current scanline
static constexpr u16 REG_LYC   = 0xFF45;  // LY Compare
static constexpr u16 REG_DMA   = 0xFF46;  // OAM DMA
static constexpr u16 REG_BGP   = 0xFF47;  // BG Palette
static constexpr u16 REG_OBP0  = 0xFF48;  // Object Palette 0
static constexpr u16 REG_OBP1  = 0xFF49;  // Object Palette 1
static constexpr u16 REG_WY    = 0xFF4A;  // Window Y
static constexpr u16 REG_WX    = 0xFF4B;  // Window X

// ── Interrupt bit flags (used in IF and IE registers) ─────────────────────────
enum class Interrupt : u8 {
    VBlank  = 0x01,  // Bit 0 – vertical blank
    LCDStat = 0x02,  // Bit 1 – LCD STAT
    Timer   = 0x04,  // Bit 2 – timer overflow
    Serial  = 0x08,  // Bit 3 – serial transfer complete
    Joypad  = 0x10   // Bit 4 – joypad button press
};

// ── PPU mode states ───────────────────────────────────────────────────────────
enum class PPUMode : u8 {
    HBlank   = 0,  // Between scanlines
    VBlank   = 1,  // After last visible scanline
    OAMScan  = 2,  // Searching OAM for sprites on next line
    Drawing  = 3   // Transferring pixel data to LCD
};

// ── Cartridge types (from ROM header byte 0x0147) ─────────────────────────────
enum class CartridgeType : u8 {
    ROM_ONLY      = 0x00,
    MBC1          = 0x01,
    MBC1_RAM      = 0x02,
    MBC1_RAM_BATT = 0x03,
    MBC2          = 0x05,
    MBC2_BATT     = 0x06,
    ROM_RAM       = 0x08,
    ROM_RAM_BATT  = 0x09,
    MBC3_TIMER_BATT       = 0x0F,
    MBC3_TIMER_RAM_BATT   = 0x10,
    MBC3          = 0x11,
    MBC3_RAM      = 0x12,
    MBC3_RAM_BATT = 0x13,
    MBC5          = 0x19,
    MBC5_RAM      = 0x1A,
    MBC5_RAM_BATT = 0x1B
};

// ── 32-bit RGBA pixel (used by the frame buffer passed to the renderer) ───────
struct Pixel {
    u8 r, g, b, a;
};

// Frame buffer: 160×144 pixels
using FrameBuffer = std::array<Pixel, SCREEN_WIDTH * SCREEN_HEIGHT>;

// Helper: compose a 16-bit value from two bytes (little-endian order)
inline constexpr u16 makeU16(u8 lo, u8 hi) noexcept {
    return static_cast<u16>((hi << 8u) | lo);
}

// Helper: test a specific bit in a byte
inline constexpr bool testBit(u8 value, u8 bit) noexcept {
    return (value >> bit) & 0x01u;
}

// Helper: set a specific bit
inline constexpr u8 setBit(u8 value, u8 bit) noexcept {
    return value | static_cast<u8>(1u << bit);
}

// Helper: clear a specific bit
inline constexpr u8 clearBit(u8 value, u8 bit) noexcept {
    return value & static_cast<u8>(~(1u << bit));
}

} // namespace GB
