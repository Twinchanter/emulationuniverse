/**
 * IJoypad.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Input abstraction layer.  The concrete Joypad class reads from a real
 * SDL2/OS event queue, while tests can inject synthetic button events via
 * this interface without any dependency on windowing code.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../Types.hpp"

namespace GB {

/// Logical Game Boy buttons.  Names match the physical hardware labels.
enum class Button : u8 {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    Select = 6,
    Start  = 7
};

class IJoypad {
public:
    virtual ~IJoypad() = default;

    /// Mark a button as currently held down.
    virtual void press(Button btn)   = 0;

    /// Mark a button as released.
    virtual void release(Button btn) = 0;

    /**
     * Read the JOYP register value (0xFF00) given the current selection bits.
     * The CPU or MMU calls this when the game reads 0xFF00.
     * @p select  The upper nibble written by the game (bits 4 & 5).
     * @return    The combined 6-bit register byte.
     */
    virtual u8 readJOYP(u8 select) const = 0;

    /// Notify the joypad that the selection byte has been written (0xFF00 write).
    virtual void writeJOYP(u8 value) = 0;
};

} // namespace GB
