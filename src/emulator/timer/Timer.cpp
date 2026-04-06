/**
 * Timer.cpp
 */
#include "Timer.hpp"

namespace GB {

constexpr int Timer::TIMA_CYCLES[4];

Timer::Timer(std::function<void(Interrupt)> irqCallback)
    : m_irqCallback(std::move(irqCallback)) {}

void Timer::init()  { reset(); }
void Timer::reset() {
    m_div  = 0xAB; m_tima = 0; m_tma = 0; m_tac = 0xF8;
    m_if   = 0xE1;
    m_divCycles = 0; m_timaCycles = 0;
    m_overflowState = OverflowState::None;
    m_overflowCycles = 0;
}

void Timer::incrementTima() {
    if (m_overflowState != OverflowState::None) return;
    if (m_tima == 0xFF) {
        m_tima = 0x00;
        m_overflowState = OverflowState::Overflowed;
        m_overflowCycles = 0;
    } else {
        ++m_tima;
    }
}

u32 Timer::tick(u32 cycles) {
    const auto stateAtStart = m_overflowState;

    if (stateAtStart != OverflowState::None) {
        m_overflowCycles += static_cast<int>(cycles);
        while (m_overflowCycles >= 4 && m_overflowState != OverflowState::None) {
            m_overflowCycles -= 4;

            if (m_overflowState == OverflowState::Overflowed) {
                m_overflowState = OverflowState::Reloading;
                m_tima = m_tma;
                m_irqCallback(Interrupt::Timer);
            } else {
                m_overflowState = OverflowState::None;
                m_overflowCycles = 0;
            }
        }
    }

    // ── DIV register: increments every 256 T-cycles ───────────────────────────
    m_divCycles += static_cast<int>(cycles);
    while (m_divCycles >= 256) {
        m_divCycles -= 256;
        ++m_div; // DIV is 8-bit, wraps naturally
    }

    // ── TIMA counter: only when timer is enabled (TAC bit 2) ─────────────────
    if (m_tac & 0x04u) {
        m_timaCycles += static_cast<int>(cycles);
        int threshold = TIMA_CYCLES[m_tac & 0x03u];

        while (m_timaCycles >= threshold) {
            m_timaCycles -= threshold;
            incrementTima();
        }
    }
    return cycles;
}

u8 Timer::readReg(u16 address) const {
    switch (address) {
        case REG_DIV:  return m_div;
        case REG_TIMA: return m_tima;
        case REG_TMA:  return m_tma;
        case REG_TAC:  return m_tac | 0xF8u; // Upper 5 bits always 1
        default:       return 0xFF;
    }
}

void Timer::writeReg(u16 address, u8 value) {
    auto timerInputHigh = [](u8 tac, u32 counter) -> bool {
        if ((tac & 0x04u) == 0) return false;
        static constexpr int BIT_POS[4] = { 9, 3, 5, 7 };
        const int shift = BIT_POS[tac & 0x03u];
        return ((counter >> shift) & 1u) != 0;
    };

    switch (address) {
        case REG_DIV:
            // Any write to DIV resets the internal 16-bit counter to 0.
            // This also resets the TIMA clock phase since both share the
            // same underlying counter on hardware.
            {
                const u32 counter = (static_cast<u32>(m_div) << 8u)
                                  + static_cast<u32>(m_divCycles);
                if (timerInputHigh(m_tac, counter)) {
                    incrementTima();
                }
            }
            m_div = 0; m_divCycles = 0; m_timaCycles = 0;
            break;
        case REG_TIMA:
            if (m_overflowState == OverflowState::Overflowed) {
                m_overflowState = OverflowState::None;
                m_overflowCycles = 0;
                m_tima = value;
                break;
            }
            if (m_overflowState == OverflowState::Reloading) {
                break;
            }
            m_tima = value;
            break;
        case REG_TMA:
            m_tma = value;
            if (m_overflowState == OverflowState::Reloading) {
                m_tima = m_tma;
            }
            break;
        case REG_TAC:
        {
            const u32 counter = (static_cast<u32>(m_div) << 8u)
                              + static_cast<u32>(m_divCycles);
            const bool oldHigh = timerInputHigh(m_tac, counter);
            m_tac = value & 0x07u;
            const bool newHigh = timerInputHigh(m_tac, counter);
            // Changing TAC can cause a falling edge on the timer input.
            if (oldHigh && !newHigh) {
                incrementTima();
            }
            break;
        }
    }
}

} // namespace GB
