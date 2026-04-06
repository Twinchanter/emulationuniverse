/**
 * Joypad.cpp
 */
#include "Joypad.hpp"

namespace GB {

Joypad::Joypad(std::function<void(Interrupt)> irqCallback)
    : m_irqCallback(std::move(irqCallback)) {}

void Joypad::reset() {
    m_actionBits    = 0x0F; // All released (active-low: 1=unpressed)
    m_directionBits = 0x0F;
    m_selectByte    = 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Button state management
// ─────────────────────────────────────────────────────────────────────────────

void Joypad::press(Button btn) {
    u8 bit = static_cast<u8>(btn);
    if (bit <= 3) {
        // Direction buttons: Right=0, Left=1, Up=2, Down=3
        u8 mask = static_cast<u8>(1u << bit);
        bool wasPrevReleased = (m_directionBits & mask) != 0;
        m_directionBits &= ~mask; // Clear bit = pressed (active-low)
        if (wasPrevReleased) m_irqCallback(Interrupt::Joypad);
    } else {
        // Action buttons: A=4, B=5, Select=6, Start=7
        u8 mask = static_cast<u8>(1u << (bit - 4u));
        bool wasPrevReleased = (m_actionBits & mask) != 0;
        m_actionBits &= ~mask;
        if (wasPrevReleased) m_irqCallback(Interrupt::Joypad);
    }
}

void Joypad::release(Button btn) {
    u8 bit = static_cast<u8>(btn);
    if (bit <= 3) {
        m_directionBits |= static_cast<u8>(1u << bit);
    } else {
        m_actionBits |= static_cast<u8>(1u << (bit - 4u));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JOYP register
// ─────────────────────────────────────────────────────────────────────────────

u8 Joypad::readJOYP(u8 /*select*/) const {
    // Use the last-written select byte to determine which group to expose
    u8 result = 0xCFu; // Bits 7-6 always set; bits 5-4 from select register

    bool selectAction    = !(m_selectByte & 0x20u); // Bit 5 = 0 → action buttons
    bool selectDirection = !(m_selectByte & 0x10u); // Bit 4 = 0 → directions

    result |= (m_selectByte & 0x30u); // Preserve bits 5-4

    u8 buttons = 0x0Fu; // Default: no buttons pressed
    if (selectAction)    buttons &= m_actionBits;
    if (selectDirection) buttons &= m_directionBits;
    result |= (buttons & 0x0Fu);

    return result;
}

void Joypad::writeJOYP(u8 value) {
    // Only bits 5 and 4 are writable (the select bits)
    m_selectByte = (m_selectByte & 0xCFu) | (value & 0x30u);
}

} // namespace GB
