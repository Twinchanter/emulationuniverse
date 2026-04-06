/**
 * Timer.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Game Boy hardware timer.
 *
 * Registers:
 *   0xFF04  DIV   – Divider. Increments at 16384 Hz (every 256 T-cycles).
 *                   Any write resets it to 0.
 *   0xFF05  TIMA  – Timer counter. Increments at a rate selected by TAC.
 *                   On overflow resets to TMA and fires a Timer IRQ.
 *   0xFF06  TMA   – Timer Modulo.  TIMA reloads from here on overflow.
 *   0xFF07  TAC   – Timer Control.
 *                   Bit 2: Timer Stop (0=stop, 1=run).
 *                   Bits 1-0: Clock select.
 *                     00 = 4096 Hz (every 1024 T-cycles)
 *                     01 = 262144 Hz (every 16 T-cycles)
 *                     10 = 65536 Hz (every 64 T-cycles)
 *                     11 = 16384 Hz (every 256 T-cycles)
 *
 * The Timer also acts as the Interrupt Flag (IF) guardian – it holds the
 * single IF register (0xFF0F) for the whole bus.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IComponent.hpp"
#include <functional>

namespace GB {

class Timer final : public IComponent {
public:
    /**
     * @param irqCallback  Called on TIMA overflow to request the Timer interrupt.
     */
    explicit Timer(std::function<void(Interrupt)> irqCallback);
    ~Timer() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override;
    void reset() override;
    u32  tick(u32 cycles) override;

    // ── Register access (MMU delegates here for DIV/TIMA/TMA/TAC) ────────────
    u8   readReg (u16 address) const;
    void writeReg(u16 address, u8 value);

    // ── Interrupt Flag (IF) – 0xFF0F ──────────────────────────────────────────
    // The IF register is logically part of the interrupt system but lives on the
    // bus; the Timer owns it as a convenient single-responsibility holder.
    u8   readIF()        const { return m_if | 0xE0u; } // Upper 3 bits always 1
    void writeIF(u8 val)       { m_if = val & 0x1Fu;  }
    void raiseIF(Interrupt irq){ m_if |= static_cast<u8>(irq); }

private:
    enum class OverflowState {
        None,
        Overflowed,
        Reloading,
    };

    u8  m_div  = 0xAB; // Power-on value after boot ROM
    u8  m_tima = 0;
    u8  m_tma  = 0;
    u8  m_tac  = 0xF8; // Timer disabled at power-on

    u8  m_if   = 0xE1; // Interrupt flags (power-on after boot ROM)

    int m_divCycles  = 0; // Cycles since last DIV increment
    int m_timaCycles = 0; // Cycles since last TIMA increment

    // Hardware overflow pipeline:
    // 1. TIMA overflows to 0x00 and stays cancelable for one M-cycle.
    // 2. On the next M-cycle, TIMA reloads from TMA and raises IF.
    OverflowState m_overflowState = OverflowState::None;
    int           m_overflowCycles = 0;

    // Lookup: TAC clock select → T-cycles per TIMA tick
    static constexpr int TIMA_CYCLES[4] = { 1024, 16, 64, 256 };

    void incrementTima();

    std::function<void(Interrupt)> m_irqCallback;
};

} // namespace GB
