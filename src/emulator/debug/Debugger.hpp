#pragma once

#include "../GameBoy.hpp"

#include <unordered_set>
#include <vector>

namespace GB {

class Debugger {
public:
    explicit Debugger(GameBoy& gb) : m_gb(gb) {}

    void addBreakpoint(u16 address) { m_breakpoints.insert(address); }
    void removeBreakpoint(u16 address) { m_breakpoints.erase(address); }
    void clearBreakpoints() { m_breakpoints.clear(); }
    bool hasBreakpoint(u16 address) const { return m_breakpoints.count(address) > 0; }

    // Run until current PC hits a breakpoint or maxInstructions elapse.
    bool runUntilBreakpoint(u64 maxInstructions = 1'000'000);

    u16 programCounter() const;
    RegisterFile registers() const;

    u8 readMemory(u16 address) const;
    void writeMemory(u16 address, u8 value);

private:
    GameBoy& m_gb;
    std::unordered_set<u16> m_breakpoints;
};

} // namespace GB
