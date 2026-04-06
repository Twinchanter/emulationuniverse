/**
 * Joypad.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Game Boy joypad controller driver.
 *
 * The JOYP register (0xFF00) maps two 4-bit groups through a select scheme:
 *   Writing bit 5 = 0 selects the Direction buttons (Down, Up, Left, Right)
 *   Writing bit 4 = 0 selects the Action  buttons (Start, Select, B, A)
 *
 * The lower nibble in JOYP is active-low (0=pressed).
 *
 * Pressing any button also triggers the Joypad interrupt.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IJoypad.hpp"
#include "../core/interfaces/IComponent.hpp"
#include <functional>

namespace GB {

class Joypad final : public IJoypad, public IComponent {
public:
    explicit Joypad(std::function<void(Interrupt)> irqCallback);
    ~Joypad() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override { reset(); }
    void reset() override;
    u32  tick(u32 /*cycles*/) override { return 0; }

    // ── IJoypad ─────────────────────────────────────────────────────────────
    void press  (Button btn) override;
    void release(Button btn) override;
    u8   readJOYP (u8 select) const override;
    void writeJOYP(u8 value)  override;

private:
    // Internal button state bitvectors (bit position matches Button enum)
    u8 m_actionBits    = 0x0F; // Bits 3-0: Start, Select, B, A  (active-low; 0F = all released)
    u8 m_directionBits = 0x0F; // Bits 3-0: Down, Up, Left, Right

    u8 m_selectByte = 0xFF; // Last byte written to JOYP (selects group)

    std::function<void(Interrupt)> m_irqCallback;
};

} // namespace GB
