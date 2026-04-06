/**
 * Opcodes.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Complete implementation of all 512 Sharp LR35902 opcode handlers.
 *
 * Cycle counts:
 *   Reported in T-cycles (1 M-cycle = 4 T-cycles).
 *   Branch instructions return the *taken* cycle count (the not-taken count is
 *   always 8 T-cycles for conditional jumps and calls).
 *
 * Register encoding used in the 0x40-0xBF range:
 *   bits 5-3 = destination (or bit index for BIT/SET/RES)
 *   bits 2-0 = source register
 *   0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "CPU.hpp"

namespace GB {

// ── Convenience macro: decode src register index from current opcode ──────────
#define SRC_IDX  (m_currentOpcode & 0x07u)
#define DST_IDX  ((m_currentOpcode >> 3u) & 0x07u)
#define BIT_IDX  ((m_currentOpcode >> 3u) & 0x07u)

// Read the operand addressed by the 3-bit register index.
// If the index is 6 the operand is the byte at (HL).
#define READ_REG(idx) \
    ((idx) == 6 ? read8Timed(m_reg.HL()) : regRef(m_reg, (idx)))

// Write the operand addressed by the 3-bit register index.
#define WRITE_REG(idx, val) \
    do { if ((idx) == 6) { write8Timed(m_reg.HL(), (val)); } \
         else { regRef(m_reg, (idx)) = (val); } } while (0)

namespace {
/// Forward declaration of regRef (defined in CPU.cpp anonymous namespace).
/// Re-declare here as static to give file-local access without header exposure.
static u8& regRef(RegisterFile& r, u8 idx) {
    switch (idx & 0x07u) {
        case 0: return r.B; case 1: return r.C;
        case 2: return r.D; case 3: return r.E;
        case 4: return r.H; case 5: return r.L;
        case 7: return r.A;
        default: return r.A; // Should never hit (6 handled by caller)
    }
}
} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// Row 0x00  –  misc / 16-bit loads / rotates
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_NOP()  { return 4; }                            // 0x00

u32 CPU::op_LD_BC_d16() {                                   // 0x01
    m_reg.setBC(fetch16()); return 12;
}
u32 CPU::op_LD_mBC_A() {                                    // 0x02
    write8Timed(m_reg.BC(), m_reg.A); return 8;
}
u32 CPU::op_INC_BC() { m_reg.setBC(m_reg.BC() + 1); return 8; } // 0x03
u32 CPU::op_INC_B()  { m_reg.B = alu_inc(m_reg.B); return 4;  } // 0x04
u32 CPU::op_DEC_B()  { m_reg.B = alu_dec(m_reg.B); return 4;  } // 0x05
u32 CPU::op_LD_B_d8(){ m_reg.B = fetch8(); return 8;           } // 0x06

u32 CPU::op_RLCA() {                                        // 0x07
    bool msb = (m_reg.A & 0x80u) != 0;
    m_reg.A  = static_cast<u8>((m_reg.A << 1u) | (msb ? 1u : 0u));
    m_reg.setFlags(false, false, false, msb);
    return 4;
}
u32 CPU::op_LD_ma16_SP() {                                  // 0x08
    u16 addr = fetch16();
    write16Timed(addr, m_reg.SP);
    return 20;
}
u32 CPU::op_ADD_HL_BC() { m_reg.setHL(alu_addHL(m_reg.BC())); return 8; } // 0x09
u32 CPU::op_LD_A_mBC()  { m_reg.A = read8Timed(m_reg.BC()); return 8;  } // 0x0A
u32 CPU::op_DEC_BC()    { m_reg.setBC(m_reg.BC() - 1); return 8;        } // 0x0B
u32 CPU::op_INC_C()     { m_reg.C = alu_inc(m_reg.C); return 4;         } // 0x0C
u32 CPU::op_DEC_C()     { m_reg.C = alu_dec(m_reg.C); return 4;         } // 0x0D
u32 CPU::op_LD_C_d8()   { m_reg.C = fetch8(); return 8;                  } // 0x0E

u32 CPU::op_RRCA() {                                        // 0x0F
    bool lsb = (m_reg.A & 0x01u) != 0;
    m_reg.A  = static_cast<u8>((m_reg.A >> 1u) | (lsb ? 0x80u : 0u));
    m_reg.setFlags(false, false, false, lsb);
    return 4;
}

// ═════════════════════════════════════════════════════════════════════════════
// Row 0x10
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_STOP() {                                        // 0x10
    fetch8(); // consume the padding 0x00 byte
    m_stopped = true;
    return 4;
}
u32 CPU::op_LD_DE_d16()   { m_reg.setDE(fetch16()); return 12;         } // 0x11
u32 CPU::op_LD_mDE_A()    { write8Timed(m_reg.DE(), m_reg.A); return 8;} // 0x12
u32 CPU::op_INC_DE()      { m_reg.setDE(m_reg.DE() + 1); return 8;    } // 0x13
u32 CPU::op_INC_D()       { m_reg.D = alu_inc(m_reg.D); return 4;     } // 0x14
u32 CPU::op_DEC_D()       { m_reg.D = alu_dec(m_reg.D); return 4;     } // 0x15
u32 CPU::op_LD_D_d8()     { m_reg.D = fetch8(); return 8;              } // 0x16

u32 CPU::op_RLA() {                                         // 0x17
    bool oldC = m_reg.flagC();
    bool msb  = (m_reg.A & 0x80u) != 0;
    m_reg.A   = static_cast<u8>((m_reg.A << 1u) | (oldC ? 1u : 0u));
    m_reg.setFlags(false, false, false, msb);
    return 4;
}
u32 CPU::op_JR_r8() {                                       // 0x18
    s8 offset = static_cast<s8>(fetch8());
    m_reg.PC  = static_cast<u16>(m_reg.PC + offset);
    return 12;
}
u32 CPU::op_ADD_HL_DE() { m_reg.setHL(alu_addHL(m_reg.DE())); return 8; } // 0x19
u32 CPU::op_LD_A_mDE()  { m_reg.A = read8Timed(m_reg.DE()); return 8;  } // 0x1A
u32 CPU::op_DEC_DE()    { m_reg.setDE(m_reg.DE() - 1); return 8;        } // 0x1B
u32 CPU::op_INC_E()     { m_reg.E = alu_inc(m_reg.E); return 4;         } // 0x1C
u32 CPU::op_DEC_E()     { m_reg.E = alu_dec(m_reg.E); return 4;         } // 0x1D
u32 CPU::op_LD_E_d8()   { m_reg.E = fetch8(); return 8;                  } // 0x1E

u32 CPU::op_RRA() {                                         // 0x1F
    bool oldC = m_reg.flagC();
    bool lsb  = (m_reg.A & 0x01u) != 0;
    m_reg.A   = static_cast<u8>((m_reg.A >> 1u) | (oldC ? 0x80u : 0u));
    m_reg.setFlags(false, false, false, lsb);
    return 4;
}

// ═════════════════════════════════════════════════════════════════════════════
// Row 0x20
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_JR_NZ_r8() {                                    // 0x20
    s8 offset = static_cast<s8>(fetch8());
    if (!m_reg.flagZ()) { m_reg.PC = static_cast<u16>(m_reg.PC + offset); return 12; }
    return 8;
}
u32 CPU::op_LD_HL_d16()  { m_reg.setHL(fetch16()); return 12;            } // 0x21
u32 CPU::op_LDI_mHL_A()  {                                               // 0x22
    write8Timed(m_reg.HL(), m_reg.A); m_reg.setHL(m_reg.HL() + 1); return 8;
}
u32 CPU::op_INC_HL()     { m_reg.setHL(m_reg.HL() + 1); return 8;       } // 0x23
u32 CPU::op_INC_H()      { m_reg.H = alu_inc(m_reg.H); return 4;        } // 0x24
u32 CPU::op_DEC_H()      { m_reg.H = alu_dec(m_reg.H); return 4;        } // 0x25
u32 CPU::op_LD_H_d8()    { m_reg.H = fetch8(); return 8;                 } // 0x26

u32 CPU::op_DAA() {                                         // 0x27
    // Decimal Adjust Accumulator: corrects A after BCD arithmetic
    u8  a = m_reg.A;
    bool n = m_reg.flagN(), h = m_reg.flagH(), c = m_reg.flagC();
    if (!n) {
        if (c || a > 0x99) { a += 0x60; m_reg.setC(true); }
        if (h || (a & 0x0F) > 0x09) { a += 0x06; }
    } else {
        if (c) a -= 0x60;
        if (h) a -= 0x06;
    }
    m_reg.A = a;
    m_reg.setZ(a == 0);
    m_reg.setH(false);
    return 4;
}
u32 CPU::op_JR_Z_r8() {                                     // 0x28
    s8 offset = static_cast<s8>(fetch8());
    if (m_reg.flagZ()) { m_reg.PC = static_cast<u16>(m_reg.PC + offset); return 12; }
    return 8;
}
u32 CPU::op_ADD_HL_HL() { m_reg.setHL(alu_addHL(m_reg.HL())); return 8; } // 0x29
u32 CPU::op_LDI_A_mHL() {                                               // 0x2A
    m_reg.A = read8Timed(m_reg.HL()); m_reg.setHL(m_reg.HL() + 1); return 8;
}
u32 CPU::op_DEC_HL()     { m_reg.setHL(m_reg.HL() - 1); return 8;       } // 0x2B
u32 CPU::op_INC_L()      { m_reg.L = alu_inc(m_reg.L); return 4;        } // 0x2C
u32 CPU::op_DEC_L()      { m_reg.L = alu_dec(m_reg.L); return 4;        } // 0x2D
u32 CPU::op_LD_L_d8()    { m_reg.L = fetch8(); return 8;                 } // 0x2E

u32 CPU::op_CPL() {                                         // 0x2F
    m_reg.A = ~m_reg.A;
    m_reg.setN(true); m_reg.setH(true);
    return 4;
}

// ═════════════════════════════════════════════════════════════════════════════
// Row 0x30
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_JR_NC_r8() {                                    // 0x30
    s8 offset = static_cast<s8>(fetch8());
    if (!m_reg.flagC()) { m_reg.PC = static_cast<u16>(m_reg.PC + offset); return 12; }
    return 8;
}
u32 CPU::op_LD_SP_d16()  { m_reg.SP = fetch16(); return 12;              } // 0x31
u32 CPU::op_LDD_mHL_A()  {                                               // 0x32
    write8Timed(m_reg.HL(), m_reg.A); m_reg.setHL(m_reg.HL() - 1); return 8;
}
u32 CPU::op_INC_SP()     { ++m_reg.SP; return 8;                         } // 0x33
u32 CPU::op_INC_mHL()    {                                               // 0x34
    u8 v = read8Timed(m_reg.HL()); write8Timed(m_reg.HL(), alu_inc(v)); return 12;
}
u32 CPU::op_DEC_mHL()    {                                               // 0x35
    u8 v = read8Timed(m_reg.HL()); write8Timed(m_reg.HL(), alu_dec(v)); return 12;
}
u32 CPU::op_LD_mHL_d8()  { write8Timed(m_reg.HL(), fetch8()); return 12; } // 0x36

u32 CPU::op_SCF() {                                         // 0x37
    m_reg.setN(false); m_reg.setH(false); m_reg.setC(true); return 4;
}
u32 CPU::op_JR_C_r8() {                                     // 0x38
    s8 offset = static_cast<s8>(fetch8());
    if (m_reg.flagC()) { m_reg.PC = static_cast<u16>(m_reg.PC + offset); return 12; }
    return 8;
}
u32 CPU::op_ADD_HL_SP() { m_reg.setHL(alu_addHL(m_reg.SP)); return 8;   } // 0x39
u32 CPU::op_LDD_A_mHL() {                                               // 0x3A
    m_reg.A = read8Timed(m_reg.HL()); m_reg.setHL(m_reg.HL() - 1); return 8;
}
u32 CPU::op_DEC_SP()     { --m_reg.SP; return 8;                         } // 0x3B
u32 CPU::op_INC_A()      { m_reg.A = alu_inc(m_reg.A); return 4;        } // 0x3C
u32 CPU::op_DEC_A()      { m_reg.A = alu_dec(m_reg.A); return 4;        } // 0x3D
u32 CPU::op_LD_A_d8()    { m_reg.A = fetch8(); return 8;                 } // 0x3E

u32 CPU::op_CCF() {                                         // 0x3F
    m_reg.setN(false); m_reg.setH(false); m_reg.setC(!m_reg.flagC()); return 4;
}

// ═════════════════════════════════════════════════════════════════════════════
// Rows 0x40–0x7F:  LD r, r  (table-driven generic handler)
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_LD_r_r() {                                      // 0x40–0x7F
    u8 src = READ_REG(SRC_IDX);
    WRITE_REG(DST_IDX, src);
    // 8 cycles if either operand is (HL), else 4
    return (SRC_IDX == 6 || DST_IDX == 6) ? 8u : 4u;
}

u32 CPU::op_HALT() {                                        // 0x76
    m_halted = true; return 4;
}

// ═════════════════════════════════════════════════════════════════════════════
// Rows 0x80–0xBF: 8-bit ALU – register operand
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_ADD_A_r() { alu_add(READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }
u32 CPU::op_ADC_A_r() { alu_add(READ_REG(SRC_IDX), true);   return (SRC_IDX==6)?8:4; }
u32 CPU::op_SUB_r()   { alu_sub(READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }
u32 CPU::op_SBC_A_r() { alu_sub(READ_REG(SRC_IDX), true);   return (SRC_IDX==6)?8:4; }
u32 CPU::op_AND_r()   { alu_and(READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }
u32 CPU::op_XOR_r()   { alu_xor(READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }
u32 CPU::op_OR_r()    { alu_or (READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }
u32 CPU::op_CP_r()    { alu_cp (READ_REG(SRC_IDX));         return (SRC_IDX==6)?8:4; }

// ═════════════════════════════════════════════════════════════════════════════
// Row 0xC0: Returns, Pops, Jumps, Calls, Pushes, ALU immediate, RSTs
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_RET_NZ() {                                      // 0xC0
    if (!m_reg.flagZ()) { syncHardware(4); m_reg.PC = pop16(); syncHardware(4); return 20; } return 8;
}
u32 CPU::op_POP_BC()  { m_reg.setBC(pop16()); return 12; }   // 0xC1

u32 CPU::op_JP_NZ_a16() {                                   // 0xC2
    u16 addr = fetch16();
    if (!m_reg.flagZ()) { syncHardware(4); m_reg.PC = addr; return 16; } return 12;
}
u32 CPU::op_JP_a16() { u16 addr = fetch16(); syncHardware(4); m_reg.PC = addr; return 16; }    // 0xC3

u32 CPU::op_CALL_NZ_a16() {                                 // 0xC4
    u16 addr = fetch16();
    if (!m_reg.flagZ()) { syncHardware(4); push16(m_reg.PC); m_reg.PC = addr; return 24; } return 12;
}
u32 CPU::op_PUSH_BC()  { syncHardware(4); push16(m_reg.BC()); return 16;       } // 0xC5
u32 CPU::op_ADD_A_d8() { alu_add(fetch8()); return 8;          } // 0xC6
u32 CPU::op_RST_00()   { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0000; return 16; } // 0xC7

u32 CPU::op_RET_Z() {                                       // 0xC8
    if (m_reg.flagZ()) { syncHardware(4); m_reg.PC = pop16(); syncHardware(4); return 20; } return 8;
}
u32 CPU::op_RET()  { m_reg.PC = pop16(); syncHardware(4); return 16;           } // 0xC9

u32 CPU::op_JP_Z_a16() {                                    // 0xCA
    u16 addr = fetch16();
    if (m_reg.flagZ()) { syncHardware(4); m_reg.PC = addr; return 16; } return 12;
}
u32 CPU::op_PREFIX_CB() {                                   // 0xCB
    m_currentOpcode = fetch8();
    OpHandler cb = m_cbTable[m_currentOpcode];
    // CB handlers already return full instruction timing (including prefix fetch).
    return (this->*cb)();
}
u32 CPU::op_CALL_Z_a16() {                                  // 0xCC
    u16 addr = fetch16();
    if (m_reg.flagZ()) { syncHardware(4); push16(m_reg.PC); m_reg.PC = addr; return 24; } return 12;
}
u32 CPU::op_CALL_a16() { u16 a = fetch16(); syncHardware(4); push16(m_reg.PC); m_reg.PC = a; return 24; } // 0xCD
u32 CPU::op_ADC_A_d8() { alu_add(fetch8(), true); return 8;   } // 0xCE
u32 CPU::op_RST_08()   { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0008; return 16; } // 0xCF

// ═════════════════════════════════════════════════════════════════════════════
// Row 0xD0
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_RET_NC() {
    if (!m_reg.flagC()) { syncHardware(4); m_reg.PC = pop16(); syncHardware(4); return 20; } return 8;
}
u32 CPU::op_POP_DE()  { m_reg.setDE(pop16()); return 12;     } // 0xD1
u32 CPU::op_JP_NC_a16() {
    u16 addr = fetch16();
    if (!m_reg.flagC()) { syncHardware(4); m_reg.PC = addr; return 16; } return 12;
}
u32 CPU::op_CALL_NC_a16() {
    u16 addr = fetch16();
    if (!m_reg.flagC()) { syncHardware(4); push16(m_reg.PC); m_reg.PC = addr; return 24; } return 12;
}
u32 CPU::op_PUSH_DE()  { syncHardware(4); push16(m_reg.DE()); return 16;       } // 0xD5
u32 CPU::op_SUB_d8()   { alu_sub(fetch8()); return 8;          } // 0xD6
u32 CPU::op_RST_10()   { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0010; return 16; } // 0xD7

u32 CPU::op_RET_C() {
    if (m_reg.flagC()) { syncHardware(4); m_reg.PC = pop16(); syncHardware(4); return 20; } return 8;
}
u32 CPU::op_RETI() {                                        // 0xD9
    m_reg.PC = pop16(); syncHardware(4); m_IME = true; return 16;
}
u32 CPU::op_JP_C_a16() {
    u16 addr = fetch16();
    if (m_reg.flagC()) { syncHardware(4); m_reg.PC = addr; return 16; } return 12;
}
u32 CPU::op_CALL_C_a16() {
    u16 addr = fetch16();
    if (m_reg.flagC()) { syncHardware(4); push16(m_reg.PC); m_reg.PC = addr; return 24; } return 12;
}
u32 CPU::op_SBC_A_d8() { alu_sub(fetch8(), true); return 8;   } // 0xDE
u32 CPU::op_RST_18()   { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0018; return 16; } // 0xDF

// ═════════════════════════════════════════════════════════════════════════════
// Row 0xE0
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_LDH_ma8_A() {                                   // 0xE0
    write8Timed(0xFF00u | fetch8(), m_reg.A); return 12;
}
u32 CPU::op_POP_HL()   { m_reg.setHL(pop16()); return 12;    } // 0xE1
u32 CPU::op_LDH_mC_A() {                                    // 0xE2
    write8Timed(static_cast<u16>(0xFF00u | m_reg.C), m_reg.A); return 8;
}
u32 CPU::op_PUSH_HL()  { syncHardware(4); push16(m_reg.HL()); return 16;      } // 0xE5
u32 CPU::op_AND_d8()   { alu_and(fetch8()); return 8;         } // 0xE6
u32 CPU::op_RST_20()   { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0020; return 16; } // 0xE7

u32 CPU::op_ADD_SP_r8() {                                   // 0xE8
    s8 offset = static_cast<s8>(m_bus.read(m_reg.PC++));
    syncHardware(4);
    syncHardware(8);
    m_reg.SP  = alu_addSP(offset);
    return 16;
}
u32 CPU::op_JP_HL() { m_reg.PC = m_reg.HL(); return 4; }    // 0xE9
u32 CPU::op_LD_ma16_A() {                                   // 0xEA
    write8Timed(fetch16(), m_reg.A); return 16;
}
u32 CPU::op_XOR_d8() { alu_xor(fetch8()); return 8;          } // 0xEE
u32 CPU::op_RST_28() { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0028; return 16; } // 0xEF

// ═════════════════════════════════════════════════════════════════════════════
// Row 0xF0
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::op_LDH_A_ma8() {                                   // 0xF0
    m_reg.A = read8Timed(0xFF00u | fetch8()); return 12;
}
u32 CPU::op_POP_AF()   { m_reg.setAF(pop16()); return 12;   } // 0xF1

u32 CPU::op_LDH_A_mC() {                                    // 0xF2
    m_reg.A = read8Timed(static_cast<u16>(0xFF00u | m_reg.C)); return 8;
}
u32 CPU::op_DI()  { m_IME = false; m_IMEPending = false; return 4; } // 0xF3
u32 CPU::op_PUSH_AF() { syncHardware(4); push16(m_reg.AF()); return 16;       } // 0xF5
u32 CPU::op_OR_d8()   { alu_or(fetch8()); return 8;           } // 0xF6
u32 CPU::op_RST_30()  { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0030; return 16; } // 0xF7

u32 CPU::op_LD_HL_SP_r8() {                                 // 0xF8
    s8 offset = static_cast<s8>(m_bus.read(m_reg.PC++));
    syncHardware(4);
    syncHardware(4);
    m_reg.setHL(alu_addSP(offset));
    return 12;
}
u32 CPU::op_LD_SP_HL() { m_reg.SP = m_reg.HL(); return 8;   } // 0xF9
u32 CPU::op_LD_A_ma16() { m_reg.A = read8Timed(fetch16()); return 16; } // 0xFA

u32 CPU::op_EI() {                                          // 0xFB
    // IME is enabled *after* the following instruction (not immediately)
    m_IMEPending = true; return 4;
}
u32 CPU::op_CP_d8()  { alu_cp(fetch8()); return 8;           } // 0xFE
u32 CPU::op_RST_38() { syncHardware(4); push16(m_reg.PC); m_reg.PC = 0x0038; return 16; } // 0xFF

// ═════════════════════════════════════════════════════════════════════════════
// CB-prefixed instruction handlers
// The m_currentOpcode at this point is the CB sub-opcode (fetched in PREFIX_CB)
// ═════════════════════════════════════════════════════════════════════════════

u32 CPU::cb_RLC_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_rlc(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_RRC_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_rrc(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_RL_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_rl(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_RR_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_rr(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_SLA_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_sla(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_SRA_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_sra(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_SWAP_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_swap(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_SRL_r() {
    u8 idx = SRC_IDX;
    u8 v = READ_REG(idx); v = cb_srl(v); WRITE_REG(idx, v);
    return (idx == 6) ? 16u : 8u;
}

u32 CPU::cb_BIT_b_r() {
    // BIT b, r – test bit without modifying the register
    u8 bit = BIT_IDX;
    u8 idx = SRC_IDX;
    u8 v   = READ_REG(idx);
    m_reg.setZ(!testBit(v, bit));
    m_reg.setN(false);
    m_reg.setH(true);
    return (idx == 6) ? 12u : 8u;
}
u32 CPU::cb_RES_b_r() {
    u8 bit = BIT_IDX;
    u8 idx = SRC_IDX;
    WRITE_REG(idx, clearBit(READ_REG(idx), bit));
    return (idx == 6) ? 16u : 8u;
}
u32 CPU::cb_SET_b_r() {
    u8 bit = BIT_IDX;
    u8 idx = SRC_IDX;
    WRITE_REG(idx, setBit(READ_REG(idx), bit));
    return (idx == 6) ? 16u : 8u;
}

#undef SRC_IDX
#undef DST_IDX
#undef BIT_IDX
#undef READ_REG
#undef WRITE_REG

} // namespace GB
