/**
 * ICPU.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Abstract CPU contract.  The Game Boy uses the Sharp LR35902; adding GBA or
 * SNES support later means providing a new concrete class that satisfies this
 * interface without touching the orchestration layer.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../Types.hpp"
#include "IComponent.hpp"

namespace GB {

class ICPU : public IComponent {
public:
    virtual ~ICPU() = default;

    /**
     * Execute one instruction and return the number of T-cycles consumed.
     * tick() from IComponent advances the CPU by an externally specified cycle
     * budget; step() executes exactly one opcode (used by debuggers).
     */
    virtual u32 step() = 0;

    /// Service pending hardware interrupts.  Returns cycles consumed (0 or 20).
    virtual u32 handleInterrupts() = 0;

    /// True when the CPU is in HALT state waiting for an interrupt.
    virtual bool isHalted() const = 0;

    /// True when the CPU is in STOP state (display off, awaiting button press).
    virtual bool isStopped() const = 0;

    /// Request an interrupt by OR-ing the IF register (called by other hardware).
    virtual void requestInterrupt(Interrupt irq) = 0;

    // ── Debug / introspection ────────────────────────────────────────────────
    virtual u16 getPC() const = 0;
    virtual u16 getSP() const = 0;
    virtual u64 getCycleCount() const = 0;
};

} // namespace GB
