/**
 * MMU.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Memory Management Unit – the single address dispatcher for the Game Boy bus.
 *
 * Map (from Pan Docs):
 *   0x0000–0x3FFF  ROM Bank 0         (always mapped, from cartridge)
 *   0x4000–0x7FFF  ROM Bank N         (switchable via MBC)
 *   0x8000–0x9FFF  Video RAM (VRAM)
 *   0xA000–0xBFFF  External (cart) RAM (switchable via MBC)
 *   0xC000–0xDFFF  Work RAM (WRAM)
 *   0xE000–0xFDFF  Echo of WRAM
 *   0xFE00–0xFE9F  Object Attribute Memory (OAM)
 *   0xFEA0–0xFEFF  Not usable
 *   0xFF00–0xFF7F  I/O Registers
 *   0xFF80–0xFFFE  High RAM (HRAM / Zero Page)
 *   0xFFFF         Interrupt Enable register
 *
 * All subsystems that need bus access receive an IMemory& reference and never
 * hold a direct pointer to the MMU—isolating them from future bus changes.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IMemory.hpp"
#include "../core/interfaces/IComponent.hpp"
#include <array>
#include <memory>
#include <string>

namespace GB {

// Forward declarations: avoids a full circular include chain
class Cartridge;
class PPU;
class APU;
class Timer;
class Joypad;

class MMU final : public IMemory, public IComponent {
public:
    /**
     * Build the bus and wire all subsystems.
     * Every pointer must remain valid for the lifetime of the MMU; the GameBoy
     * orchestrator owns all objects and ensures correct destruction order.
     */
    MMU(Cartridge& cart, PPU& ppu, APU& apu, Timer& timer, Joypad& joypad);
    ~MMU() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override;
    void reset() override;
    u32  tick(u32 cycles) override { return cycles; } // MMU itself has no clocked state

    // ── IMemory ─────────────────────────────────────────────────────────────
    u8   read (u16 address) const override;
    void write(u16 address, u8 value) override;

    // ── OAM DMA ─────────────────────────────────────────────────────────────
    /// Called each CPU step to continue any running OAM DMA transfer.
    void tickDMA(u32 cycles);

    // ── Serial output capture (for blargg/test harness) ───────────────────────
    const std::string& serialOutput() const { return m_serialBuffer; }
    void               clearSerial()        { m_serialBuffer.clear(); }

private:
    // ── Subsystem references (non-owning) ────────────────────────────────────
    Cartridge& m_cart;
    PPU&       m_ppu;
    APU&       m_apu;
    Timer&     m_timer;
    Joypad&    m_joypad;

    // ── Internal RAM ─────────────────────────────────────────────────────────
    std::array<u8, 0x2000> m_wram{};   // 8 KB Work RAM  0xC000–0xDFFF
    std::array<u8, 0x007F> m_hram{};   // 127 B High RAM 0xFF80–0xFFFE
    u8                     m_ie = 0;   // 0xFFFF Interrupt Enable

    // ── I/O registers not owned by a dedicated subsystem ─────────────────────
    u8          m_serialData    = 0;  // 0xFF01 SB
    u8          m_serialControl = 0;  // 0xFF02 SC
    std::string m_serialBuffer;       // Bytes "sent" via SC=0x81 (test harness)

    // ── OAM DMA state ────────────────────────────────────────────────────────
    bool   m_dmaActive   = false;
    u16    m_dmaSrc      = 0;     // Source base address
    u8     m_dmaProgress = 0;     // Byte index into the 160-byte transfer
    u32    m_dmaCycles   = 0;     // Cycles accumulated this transfer
    u8     m_dmaRegister = 0xFF;  // Last value written to FF46
    bool   m_dmaPending  = false;
    u16    m_dmaPendingSrc = 0;
    u32    m_dmaStartDelay = 0;   // T-cycles until pending DMA takes over

    u8 readDirect(u16 addr) const;

    /// Trigger an OAM DMA transfer from the source page (0xXX * 0x100).
    void startDMA(u8 page);
};

} // namespace GB
