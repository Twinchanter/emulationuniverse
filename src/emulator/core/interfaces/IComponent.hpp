/**
 * IComponent.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Base lifecycle interface that every hardware component must satisfy.
 * Enforces a uniform init / reset / tick contract so the GameBoy orchestrator
 * can treat all subsystems polymorphically without knowing their internals.
 *
 * Design rationale:
 *   - init()  : one-time setup (allocate look-up tables, map callbacks, etc.)
 *   - reset() : restore power-on state; called on soft-reset without re-init
 *   - tick(cycles) : advance component by the given number of T-cycles
 *                    (1 M-cycle = 4 T-cycles on the LR35902)
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../Types.hpp"

namespace GB {

class IComponent {
public:
    virtual ~IComponent() = default;

    /// One-time hardware initialisation (build LUTs, register callbacks).
    virtual void init()  = 0;

    /// Restore power-on state; safe to call multiple times.
    virtual void reset() = 0;

    /**
     * Advance the component by @p cycles T-cycles.
     * Returns the number of T-cycles actually consumed (may differ for
     * components that operate on M-cycle boundaries).
     */
    virtual u32 tick(u32 cycles) = 0;
};

} // namespace GB
