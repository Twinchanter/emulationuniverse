/**
 * CPU.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Sharp LR35902 CPU – core lifecycle, fetch/decode/execute loop, interrupts,
 * and ALU helpers.  Individual opcode implementations live in Opcodes.cpp.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "CPU.hpp"
#include <stdexcept>
#include <cstdio>

namespace GB {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

CPU::CPU(IMemory& bus, std::function<void(u32)> syncCallback)
    : m_bus(bus)
    , m_syncCallback(std::move(syncCallback)) {}

// ─────────────────────────────────────────────────────────────────────────────
// IComponent
// ─────────────────────────────────────────────────────────────────────────────

void CPU::init() {
    buildOpcodeTables();
    buildCBTables();
    reset();
}

void CPU::reset() {
    m_reg.reset();
    m_halted      = false;
    m_stopped     = false;
    m_IME         = false;
    m_IMEPending  = false;
    m_totalCycles = 0;
}

u32 CPU::tick(u32 cycles) {
    u32 elapsed = 0;
    while (elapsed < cycles) {
        elapsed += step();
    }
    return elapsed;
}

// ─────────────────────────────────────────────────────────────────────────────
// ICPU – step (fetch/decode/execute one instruction)
// ─────────────────────────────────────────────────────────────────────────────

u32 CPU::step() {
    m_syncedCycles = 0;

    // ── 1. Service pending interrupts before fetching ─────────────────────────
    u32 irqCycles = handleInterrupts();
    if (irqCycles > 0) {
        if (m_syncedCycles < irqCycles) {
            syncHardware(irqCycles - m_syncedCycles);
        }
        m_totalCycles += irqCycles;
        m_syncedCycles = 0;
        return irqCycles;
    }

    // ── 2. HALT: spin until an interrupt wakes us ─────────────────────────────
    if (m_halted) {
        // Consume 4 T-cycles per NOP while halted
        syncHardware(4);
        m_totalCycles += 4;
        m_syncedCycles = 0;
        return 4;
    }

    // ── 3. Deferred EI – IME activates the cycle after the EI instruction ─────
    if (m_IMEPending) {
        m_IME        = true;
        m_IMEPending = false;
    }

    // ── 4. Fetch opcode byte ──────────────────────────────────────────────────
    m_currentOpcode = fetch8();

    // ── 5. Dispatch through table ─────────────────────────────────────────────
    OpHandler handler = m_opcodeTable[m_currentOpcode];
    u32 cycles = (this->*handler)();

    if (m_syncedCycles < cycles) {
        syncHardware(cycles - m_syncedCycles);
    }
    m_syncedCycles = 0;

    m_totalCycles += cycles;
    return cycles;
}

// ─────────────────────────────────────────────────────────────────────────────
// Interrupt handling
// ─────────────────────────────────────────────────────────────────────────────

void CPU::requestInterrupt(Interrupt irq) {
    // OR the corresponding bit into the IF register (0xFF0F)
    u8 ifReg = m_bus.read(REG_IF);
    m_bus.write(REG_IF, ifReg | static_cast<u8>(irq));
}

u32 CPU::handleInterrupts() {
    u8 ifReg = m_bus.read(REG_IF);
    u8 ieReg = m_bus.read(IE_REGISTER);
    u8 pending = ifReg & ieReg & 0x1Fu; // Only bottom 5 bits are valid

    // Wake from HALT on any pending interrupt even if IME is off
    if (pending && m_halted) {
        m_halted = false;
    }

    if (!m_IME || !pending) {
        return 0; // No interrupt to service
    }

    // ── Find highest-priority pending interrupt (bit 0 = highest) ────────────
    for (u8 bit = 0; bit < 5; ++bit) {
        if (pending & (1u << bit)) {
            // Disable IME (interrupts are non-reentrant by default)
            m_IME = false;

            // Interrupt dispatch is not atomic: IE can be modified by the two
            // stack writes while pushing PC, which can cancel or retarget the
            // dispatch before the final vector is chosen.
            const u8 pcHigh = static_cast<u8>(m_reg.PC >> 8u);
            const u8 pcLow  = static_cast<u8>(m_reg.PC & 0xFFu);

            m_reg.SP = static_cast<u16>(m_reg.SP - 1u);
            m_bus.write(m_reg.SP, pcHigh);
            syncHardware(4);

            ifReg = m_bus.read(REG_IF);
            ieReg = m_bus.read(IE_REGISTER);
            pending = ifReg & ieReg & 0x1Fu;

            int selectedBit = -1;
            for (u8 candidate = 0; candidate < 5; ++candidate) {
                if (pending & (1u << candidate)) {
                    selectedBit = candidate;
                    break;
                }
            }

            m_reg.SP = static_cast<u16>(m_reg.SP - 1u);
            m_bus.write(m_reg.SP, pcLow);
            syncHardware(4);

            if (selectedBit < 0) {
                // The dispatch was canceled by the upper-byte push touching IE.
                m_reg.PC = 0x0000u;
                return 20;
            }

            // Clear only the interrupt that actually survives dispatch.
            m_bus.write(REG_IF, static_cast<u8>(ifReg & ~(1u << selectedBit)));

            static constexpr u16 VECTORS[5] = {
                0x0040, // VBlank
                0x0048, // LCD STAT
                0x0050, // Timer
                0x0058, // Serial
                0x0060  // Joypad
            };
            m_reg.PC = VECTORS[selectedBit];

            // ISR dispatch takes 5 M-cycles (20 T-cycles)
            return 20;
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// ALU helpers – keep these out of Opcodes.cpp for reuse across instruction sets
// ─────────────────────────────────────────────────────────────────────────────

void CPU::alu_add(u8 value, bool withCarry) {
    u8 carry = (withCarry && m_reg.flagC()) ? 1u : 0u;
    u16 result = m_reg.A + value + carry;
    m_reg.setFlags(
        (result & 0xFFu) == 0,                    // Z
        false,                                     // N
        ((m_reg.A ^ value ^ result) & 0x10u) != 0, // H
        result > 0xFF                              // C
    );
    m_reg.A = static_cast<u8>(result);
}

void CPU::alu_sub(u8 value, bool withCarry) {
    u8 carry = (withCarry && m_reg.flagC()) ? 1u : 0u;
    u16 result = m_reg.A - value - carry;
    m_reg.setFlags(
        (result & 0xFFu) == 0,
        true,
        ((m_reg.A ^ value ^ result) & 0x10u) != 0,
        m_reg.A < (value + carry)
    );
    m_reg.A = static_cast<u8>(result);
}

void CPU::alu_and(u8 value) {
    m_reg.A &= value;
    m_reg.setFlags(m_reg.A == 0, false, true, false);
}

void CPU::alu_xor(u8 value) {
    m_reg.A ^= value;
    m_reg.setFlags(m_reg.A == 0, false, false, false);
}

void CPU::alu_or(u8 value) {
    m_reg.A |= value;
    m_reg.setFlags(m_reg.A == 0, false, false, false);
}

void CPU::alu_cp(u8 value) {
    // Like SUB but result is discarded (only flags matter)
    u8 savedA = m_reg.A;
    alu_sub(value);
    m_reg.A = savedA;
}

u8 CPU::alu_inc(u8 value) {
    u8 result = value + 1u;
    // Carry flag is NOT affected by INC
    m_reg.setZ(result == 0);
    m_reg.setN(false);
    m_reg.setH((value & 0x0Fu) == 0x0Fu); // Lower nibble overflow
    return result;
}

u8 CPU::alu_dec(u8 value) {
    u8 result = value - 1u;
    m_reg.setZ(result == 0);
    m_reg.setN(true);
    m_reg.setH((value & 0x0Fu) == 0x00u); // Borrow from lower nibble
    return result;
}

u16 CPU::alu_addHL(u16 value) {
    u32 result = m_reg.HL() + value;
    m_reg.setN(false);
    m_reg.setH((m_reg.HL() ^ value ^ result) & 0x1000u);
    m_reg.setC(result > 0xFFFF);
    return static_cast<u16>(result);
}

u16 CPU::alu_addSP(s8 offset) {
    u16 sp = m_reg.SP;
    const u16 offU16 = static_cast<u16>(static_cast<s16>(offset));
    const u8  offU8  = static_cast<u8>(offset);
    u16 result = static_cast<u16>(sp + offU16);
    // For ADD SP,e8 (and LD HL,SP+e8), H/C are from low-byte unsigned add.
    m_reg.setFlags(
        false,
        false,
        ((sp & 0x000Fu) + (offU8 & 0x0Fu)) > 0x0Fu,
        ((sp & 0x00FFu) + offU8) > 0x00FFu
    );
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CB rotate / shift helpers
// ─────────────────────────────────────────────────────────────────────────────

u8 CPU::cb_rlc(u8 v) {
    bool msb = (v & 0x80u) != 0;
    v = static_cast<u8>((v << 1u) | (msb ? 1u : 0u));
    m_reg.setFlags(v == 0, false, false, msb);
    return v;
}

u8 CPU::cb_rrc(u8 v) {
    bool lsb = (v & 0x01u) != 0;
    v = static_cast<u8>((v >> 1u) | (lsb ? 0x80u : 0u));
    m_reg.setFlags(v == 0, false, false, lsb);
    return v;
}

u8 CPU::cb_rl(u8 v) {
    bool oldCarry = m_reg.flagC();
    bool msb = (v & 0x80u) != 0;
    v = static_cast<u8>((v << 1u) | (oldCarry ? 1u : 0u));
    m_reg.setFlags(v == 0, false, false, msb);
    return v;
}

u8 CPU::cb_rr(u8 v) {
    bool oldCarry = m_reg.flagC();
    bool lsb = (v & 0x01u) != 0;
    v = static_cast<u8>((v >> 1u) | (oldCarry ? 0x80u : 0u));
    m_reg.setFlags(v == 0, false, false, lsb);
    return v;
}

u8 CPU::cb_sla(u8 v) {
    bool msb = (v & 0x80u) != 0;
    v = static_cast<u8>(v << 1u);
    m_reg.setFlags(v == 0, false, false, msb);
    return v;
}

u8 CPU::cb_sra(u8 v) {
    bool lsb = (v & 0x01u) != 0;
    v = static_cast<u8>((v >> 1u) | (v & 0x80u)); // Arithmetic: preserve MSB
    m_reg.setFlags(v == 0, false, false, lsb);
    return v;
}

u8 CPU::cb_swap(u8 v) {
    v = static_cast<u8>((v >> 4u) | (v << 4u));
    m_reg.setFlags(v == 0, false, false, false);
    return v;
}

u8 CPU::cb_srl(u8 v) {
    bool lsb = (v & 0x01u) != 0;
    v >>= 1u; // Logical: MSB becomes 0
    m_reg.setFlags(v == 0, false, false, lsb);
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Register index helpers used by the table-driven LD / ALU / CB handlers.
//
// The LR35902 encodes an operand register in 3 bits:
//   0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
// ─────────────────────────────────────────────────────────────────────────────
namespace {

/// Return a reference to the 8-bit register selected by the 3-bit index.
/// index 6 is the (HL) memory operand — callers must special-case it.
u8& regRef(RegisterFile& r, u8 idx) {
    switch (idx & 0x07u) {
        case 0: return r.B;
        case 1: return r.C;
        case 2: return r.D;
        case 3: return r.E;
        case 4: return r.H;
        case 5: return r.L;
        case 7: return r.A;
        default: return r.A; // 6 = (HL) – must be handled by caller
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Opcode table construction
// ─────────────────────────────────────────────────────────────────────────────

void CPU::buildOpcodeTables() {
    // Default everything to the ILLEGAL handler (guards unimplemented ops)
    m_opcodeTable.fill(&CPU::op_ILLEGAL);

    // Row 0x00
    m_opcodeTable[0x00] = &CPU::op_NOP;
    m_opcodeTable[0x01] = &CPU::op_LD_BC_d16;
    m_opcodeTable[0x02] = &CPU::op_LD_mBC_A;
    m_opcodeTable[0x03] = &CPU::op_INC_BC;
    m_opcodeTable[0x04] = &CPU::op_INC_B;
    m_opcodeTable[0x05] = &CPU::op_DEC_B;
    m_opcodeTable[0x06] = &CPU::op_LD_B_d8;
    m_opcodeTable[0x07] = &CPU::op_RLCA;
    m_opcodeTable[0x08] = &CPU::op_LD_ma16_SP;
    m_opcodeTable[0x09] = &CPU::op_ADD_HL_BC;
    m_opcodeTable[0x0A] = &CPU::op_LD_A_mBC;
    m_opcodeTable[0x0B] = &CPU::op_DEC_BC;
    m_opcodeTable[0x0C] = &CPU::op_INC_C;
    m_opcodeTable[0x0D] = &CPU::op_DEC_C;
    m_opcodeTable[0x0E] = &CPU::op_LD_C_d8;
    m_opcodeTable[0x0F] = &CPU::op_RRCA;

    // Row 0x10
    m_opcodeTable[0x10] = &CPU::op_STOP;
    m_opcodeTable[0x11] = &CPU::op_LD_DE_d16;
    m_opcodeTable[0x12] = &CPU::op_LD_mDE_A;
    m_opcodeTable[0x13] = &CPU::op_INC_DE;
    m_opcodeTable[0x14] = &CPU::op_INC_D;
    m_opcodeTable[0x15] = &CPU::op_DEC_D;
    m_opcodeTable[0x16] = &CPU::op_LD_D_d8;
    m_opcodeTable[0x17] = &CPU::op_RLA;
    m_opcodeTable[0x18] = &CPU::op_JR_r8;
    m_opcodeTable[0x19] = &CPU::op_ADD_HL_DE;
    m_opcodeTable[0x1A] = &CPU::op_LD_A_mDE;
    m_opcodeTable[0x1B] = &CPU::op_DEC_DE;
    m_opcodeTable[0x1C] = &CPU::op_INC_E;
    m_opcodeTable[0x1D] = &CPU::op_DEC_E;
    m_opcodeTable[0x1E] = &CPU::op_LD_E_d8;
    m_opcodeTable[0x1F] = &CPU::op_RRA;

    // Row 0x20
    m_opcodeTable[0x20] = &CPU::op_JR_NZ_r8;
    m_opcodeTable[0x21] = &CPU::op_LD_HL_d16;
    m_opcodeTable[0x22] = &CPU::op_LDI_mHL_A;
    m_opcodeTable[0x23] = &CPU::op_INC_HL;
    m_opcodeTable[0x24] = &CPU::op_INC_H;
    m_opcodeTable[0x25] = &CPU::op_DEC_H;
    m_opcodeTable[0x26] = &CPU::op_LD_H_d8;
    m_opcodeTable[0x27] = &CPU::op_DAA;
    m_opcodeTable[0x28] = &CPU::op_JR_Z_r8;
    m_opcodeTable[0x29] = &CPU::op_ADD_HL_HL;
    m_opcodeTable[0x2A] = &CPU::op_LDI_A_mHL;
    m_opcodeTable[0x2B] = &CPU::op_DEC_HL;
    m_opcodeTable[0x2C] = &CPU::op_INC_L;
    m_opcodeTable[0x2D] = &CPU::op_DEC_L;
    m_opcodeTable[0x2E] = &CPU::op_LD_L_d8;
    m_opcodeTable[0x2F] = &CPU::op_CPL;

    // Row 0x30
    m_opcodeTable[0x30] = &CPU::op_JR_NC_r8;
    m_opcodeTable[0x31] = &CPU::op_LD_SP_d16;
    m_opcodeTable[0x32] = &CPU::op_LDD_mHL_A;
    m_opcodeTable[0x33] = &CPU::op_INC_SP;
    m_opcodeTable[0x34] = &CPU::op_INC_mHL;
    m_opcodeTable[0x35] = &CPU::op_DEC_mHL;
    m_opcodeTable[0x36] = &CPU::op_LD_mHL_d8;
    m_opcodeTable[0x37] = &CPU::op_SCF;
    m_opcodeTable[0x38] = &CPU::op_JR_C_r8;
    m_opcodeTable[0x39] = &CPU::op_ADD_HL_SP;
    m_opcodeTable[0x3A] = &CPU::op_LDD_A_mHL;
    m_opcodeTable[0x3B] = &CPU::op_DEC_SP;
    m_opcodeTable[0x3C] = &CPU::op_INC_A;
    m_opcodeTable[0x3D] = &CPU::op_DEC_A;
    m_opcodeTable[0x3E] = &CPU::op_LD_A_d8;
    m_opcodeTable[0x3F] = &CPU::op_CCF;

    // Rows 0x40–0x7F: LD r, r  (using the same generic handler)
    for (u8 op = 0x40; op <= 0x7F; ++op) {
        if (op == 0x76) {
            m_opcodeTable[op] = &CPU::op_HALT;
        } else {
            m_opcodeTable[op] = &CPU::op_LD_r_r;
        }
    }

    // Row 0x80–0xBF: 8-bit ALU on registers
    for (u8 op = 0x80; op <= 0x87; ++op) m_opcodeTable[op] = &CPU::op_ADD_A_r;
    for (u8 op = 0x88; op <= 0x8F; ++op) m_opcodeTable[op] = &CPU::op_ADC_A_r;
    for (u8 op = 0x90; op <= 0x97; ++op) m_opcodeTable[op] = &CPU::op_SUB_r;
    for (u8 op = 0x98; op <= 0x9F; ++op) m_opcodeTable[op] = &CPU::op_SBC_A_r;
    for (u8 op = 0xA0; op <= 0xA7; ++op) m_opcodeTable[op] = &CPU::op_AND_r;
    for (u8 op = 0xA8; op <= 0xAF; ++op) m_opcodeTable[op] = &CPU::op_XOR_r;
    for (u8 op = 0xB0; op <= 0xB7; ++op) m_opcodeTable[op] = &CPU::op_OR_r;
    for (u8 op = 0xB8; op <= 0xBF; ++op) m_opcodeTable[op] = &CPU::op_CP_r;

    // Row 0xC0
    m_opcodeTable[0xC0] = &CPU::op_RET_NZ;
    m_opcodeTable[0xC1] = &CPU::op_POP_BC;
    m_opcodeTable[0xC2] = &CPU::op_JP_NZ_a16;
    m_opcodeTable[0xC3] = &CPU::op_JP_a16;
    m_opcodeTable[0xC4] = &CPU::op_CALL_NZ_a16;
    m_opcodeTable[0xC5] = &CPU::op_PUSH_BC;
    m_opcodeTable[0xC6] = &CPU::op_ADD_A_d8;
    m_opcodeTable[0xC7] = &CPU::op_RST_00;
    m_opcodeTable[0xC8] = &CPU::op_RET_Z;
    m_opcodeTable[0xC9] = &CPU::op_RET;
    m_opcodeTable[0xCA] = &CPU::op_JP_Z_a16;
    m_opcodeTable[0xCB] = &CPU::op_PREFIX_CB;
    m_opcodeTable[0xCC] = &CPU::op_CALL_Z_a16;
    m_opcodeTable[0xCD] = &CPU::op_CALL_a16;
    m_opcodeTable[0xCE] = &CPU::op_ADC_A_d8;
    m_opcodeTable[0xCF] = &CPU::op_RST_08;

    // Row 0xD0
    m_opcodeTable[0xD0] = &CPU::op_RET_NC;
    m_opcodeTable[0xD1] = &CPU::op_POP_DE;
    m_opcodeTable[0xD2] = &CPU::op_JP_NC_a16;
    // 0xD3 undefined
    m_opcodeTable[0xD4] = &CPU::op_CALL_NC_a16;
    m_opcodeTable[0xD5] = &CPU::op_PUSH_DE;
    m_opcodeTable[0xD6] = &CPU::op_SUB_d8;
    m_opcodeTable[0xD7] = &CPU::op_RST_10;
    m_opcodeTable[0xD8] = &CPU::op_RET_C;
    m_opcodeTable[0xD9] = &CPU::op_RETI;
    m_opcodeTable[0xDA] = &CPU::op_JP_C_a16;
    // 0xDB undefined
    m_opcodeTable[0xDC] = &CPU::op_CALL_C_a16;
    // 0xDD undefined
    m_opcodeTable[0xDE] = &CPU::op_SBC_A_d8;
    m_opcodeTable[0xDF] = &CPU::op_RST_18;

    // Row 0xE0
    m_opcodeTable[0xE0] = &CPU::op_LDH_ma8_A;
    m_opcodeTable[0xE1] = &CPU::op_POP_HL;
    m_opcodeTable[0xE2] = &CPU::op_LDH_mC_A;
    // 0xE3 0xE4 undefined
    m_opcodeTable[0xE5] = &CPU::op_PUSH_HL;
    m_opcodeTable[0xE6] = &CPU::op_AND_d8;
    m_opcodeTable[0xE7] = &CPU::op_RST_20;
    m_opcodeTable[0xE8] = &CPU::op_ADD_SP_r8;
    m_opcodeTable[0xE9] = &CPU::op_JP_HL;
    m_opcodeTable[0xEA] = &CPU::op_LD_ma16_A;
    // 0xEB 0xEC 0xED undefined
    m_opcodeTable[0xEE] = &CPU::op_XOR_d8;
    m_opcodeTable[0xEF] = &CPU::op_RST_28;

    // Row 0xF0
    m_opcodeTable[0xF0] = &CPU::op_LDH_A_ma8;
    m_opcodeTable[0xF1] = &CPU::op_POP_AF;
    m_opcodeTable[0xF2] = &CPU::op_LDH_A_mC;
    m_opcodeTable[0xF3] = &CPU::op_DI;
    // 0xF4 undefined
    m_opcodeTable[0xF5] = &CPU::op_PUSH_AF;
    m_opcodeTable[0xF6] = &CPU::op_OR_d8;
    m_opcodeTable[0xF7] = &CPU::op_RST_30;
    m_opcodeTable[0xF8] = &CPU::op_LD_HL_SP_r8;
    m_opcodeTable[0xF9] = &CPU::op_LD_SP_HL;
    m_opcodeTable[0xFA] = &CPU::op_LD_A_ma16;
    m_opcodeTable[0xFB] = &CPU::op_EI;
    // 0xFC 0xFD undefined
    m_opcodeTable[0xFE] = &CPU::op_CP_d8;
    m_opcodeTable[0xFF] = &CPU::op_RST_38;
}

void CPU::buildCBTables() {
    m_cbTable.fill(&CPU::op_ILLEGAL);

    // 0x00–0x07: RLC r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x00 + i] = &CPU::cb_RLC_r;
    // 0x08–0x0F: RRC r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x08 + i] = &CPU::cb_RRC_r;
    // 0x10–0x17: RL r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x10 + i] = &CPU::cb_RL_r;
    // 0x18–0x1F: RR r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x18 + i] = &CPU::cb_RR_r;
    // 0x20–0x27: SLA r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x20 + i] = &CPU::cb_SLA_r;
    // 0x28–0x2F: SRA r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x28 + i] = &CPU::cb_SRA_r;
    // 0x30–0x37: SWAP r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x30 + i] = &CPU::cb_SWAP_r;
    // 0x38–0x3F: SRL r
    for (u8 i = 0; i < 8; ++i) m_cbTable[0x38 + i] = &CPU::cb_SRL_r;
    // 0x40–0x7F: BIT b, r
    for (u8 i = 0; i < 64; ++i) m_cbTable[0x40 + i] = &CPU::cb_BIT_b_r;
    // 0x80–0xBF: RES b, r
    for (u8 i = 0; i < 64; ++i) m_cbTable[0x80 + i] = &CPU::cb_RES_b_r;
    // 0xC0–0xFF: SET b, r
    for (u8 i = 0; i < 64; ++i) m_cbTable[0xC0 + i] = &CPU::cb_SET_b_r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Illegal / undefined opcode
// ─────────────────────────────────────────────────────────────────────────────

u32 CPU::op_ILLEGAL() {
    // Log and treat as NOP; real hardware behaviour is undefined
    std::fprintf(stderr, "[CPU] Illegal opcode 0x%02X at PC=0x%04X\n",
                 m_currentOpcode, m_reg.PC - 1u);
    return 4;
}

} // namespace GB
