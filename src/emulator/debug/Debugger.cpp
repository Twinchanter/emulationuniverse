#include "Debugger.hpp"

namespace GB {

bool Debugger::runUntilBreakpoint(u64 maxInstructions) {
    for (u64 i = 0; i < maxInstructions; ++i) {
        if (hasBreakpoint(programCounter())) {
            return true;
        }
        m_gb.stepInstruction();
    }
    return hasBreakpoint(programCounter());
}

u16 Debugger::programCounter() const {
    return m_gb.cpu().getPC();
}

RegisterFile Debugger::registers() const {
    return m_gb.cpu().registers();
}

u8 Debugger::readMemory(u16 address) const {
    return m_gb.readByte(address);
}

void Debugger::writeMemory(u16 address, u8 value) {
    m_gb.writeByte(address, value);
}

} // namespace GB
