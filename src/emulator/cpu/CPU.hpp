/**
 * CPU.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Sharp LR35902 CPU – declaration.
 *
 * Architecture notes (for future porting to GBA ARM7TDMI / SNES 65816):
 *   • All opcode handlers are stored in a 256-entry function-pointer table
 *     (m_opcodeTable) and a second table for CB-prefixed instructions
 *     (m_cbTable).  Adding a new ISA means providing new tables; the fetch-
 *     decode-execute loop itself does not change.
 *   • IMemory is injected; the CPU does not own memory, which lets tests
 *     substitute a lightweight mock bus.
 *   • Cycle counts are returned by each handler and accumulated into
 *     m_totalCycles so other subsystems can synchronise without polling.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/ICPU.hpp"
#include "../core/interfaces/IMemory.hpp"
#include "Registers.hpp"
#include <array>
#include <functional>

namespace GB {

// Forward declaration (CPU needs to fire interrupts back into the bus)
class MMU;

class CPU final : public ICPU {
public:
    /**
     * @param bus  The system memory bus.  Must outlive this CPU instance.
     *             Injected so tests can use a stub without an MMU.
     */
    explicit CPU(IMemory& bus, std::function<void(u32)> syncCallback = {});
    ~CPU() override = default;

    // ── IComponent ───────────────────────────────────────────────────────────
    void init()  override;
    void reset() override;

    /**
     * Advance the CPU by up to @p cycles T-cycles, executing as many complete
     * instructions as fit.  Returns the actual cycles consumed (≥ requested).
     */
    u32 tick(u32 cycles) override;

    // ── ICPU ─────────────────────────────────────────────────────────────────
    u32  step()             override;
    u32  handleInterrupts() override;
    bool isHalted()   const override { return m_halted;  }
    bool isStopped()  const override { return m_stopped; }
    void requestInterrupt(Interrupt irq) override;

    u16 getPC() const override { return m_reg.PC; }
    u16 getSP() const override { return m_reg.SP; }
    u64 getCycleCount() const override { return m_totalCycles; }

    // ── Direct register access (for debugger / save-states) ──────────────────
    const RegisterFile& registers() const { return m_reg; }
    RegisterFile&       registers()       { return m_reg; }

private:
    // ── Internal memory helpers ───────────────────────────────────────────────
    /// Fetch and increment PC (one byte).
    inline u8 fetch8() {
        const u8 value = m_bus.read(m_reg.PC++);
        syncHardware(4);
        return value;
    }
    /// Fetch two bytes (LE word) and advance PC by 2.
    inline u16 fetch16() {
        u16 lo = fetch8();
        u16 hi = fetch8();
        return static_cast<u16>((hi << 8u) | lo);
    }
    /// Push 16-bit value onto stack, decrement SP by 2.
    inline void push16(u16 value) {
        m_reg.SP = static_cast<u16>(m_reg.SP - 1u);
        m_bus.write(m_reg.SP, static_cast<u8>(value >> 8u));
        syncHardware(4);
        m_reg.SP = static_cast<u16>(m_reg.SP - 1u);
        m_bus.write(m_reg.SP, static_cast<u8>(value & 0xFFu));
        syncHardware(4);
    }
    /// Pop 16-bit value from stack, increment SP by 2.
    inline u16 pop16() {
        const u8 lo = m_bus.read(m_reg.SP);
        syncHardware(4);
        m_reg.SP = static_cast<u16>(m_reg.SP + 1u);
        const u8 hi = m_bus.read(m_reg.SP);
        syncHardware(4);
        m_reg.SP = static_cast<u16>(m_reg.SP + 1u);
        return makeU16(lo, hi);
    }

    inline u8 read8Timed(u16 address) {
        const u8 value = m_bus.read(address);
        syncHardware(4);
        return value;
    }

    inline void write8Timed(u16 address, u8 value) {
        m_bus.write(address, value);
        syncHardware(4);
    }

    inline void write16Timed(u16 address, u16 value) {
        write8Timed(address, static_cast<u8>(value & 0xFFu));
        write8Timed(static_cast<u16>(address + 1u), static_cast<u8>(value >> 8u));
    }

    inline void syncHardware(u32 cycles) {
        if (cycles == 0) return;
        m_syncedCycles += cycles;
        if (m_syncCallback) {
            m_syncCallback(cycles);
        }
    }

    // ── Opcode dispatch tables ────────────────────────────────────────────────
    // Each entry is a pointer to a member function returning u32 (cycles used).
    using OpHandler = u32(CPU::*)();
    std::array<OpHandler, 256> m_opcodeTable{};
    std::array<OpHandler, 256> m_cbTable{};

    /// Populate both dispatch tables.  Called once from init().
    void buildOpcodeTables();
    void buildCBTables();

    // ── ALU helpers (shared by multiple opcodes) ──────────────────────────────
    void alu_add(u8 value, bool withCarry = false);
    void alu_sub(u8 value, bool withCarry = false);
    void alu_and(u8 value);
    void alu_xor(u8 value);
    void alu_or (u8 value);
    void alu_cp (u8 value);
    u8   alu_inc(u8 value);
    u8   alu_dec(u8 value);
    u16  alu_addHL(u16 value);
    u16  alu_addSP(s8 offset);

    // CB rotate/shift helpers (operate on a byte, set flags)
    u8 cb_rlc (u8 v);
    u8 cb_rrc (u8 v);
    u8 cb_rl  (u8 v);
    u8 cb_rr  (u8 v);
    u8 cb_sla (u8 v);
    u8 cb_sra (u8 v);
    u8 cb_swap(u8 v);
    u8 cb_srl (u8 v);

    // ── All opcode handler methods ────────────────────────────────────────────
    // Prefixed with op_ (standard) and cb_ (CB-prefix) for clarity.
    // Full implementations are in CPU.cpp / Opcodes.cpp.

    // Row 0x00
    u32 op_NOP();     // 0x00
    u32 op_LD_BC_d16();
    u32 op_LD_mBC_A();
    u32 op_INC_BC();
    u32 op_INC_B();
    u32 op_DEC_B();
    u32 op_LD_B_d8();
    u32 op_RLCA();
    u32 op_LD_ma16_SP();
    u32 op_ADD_HL_BC();
    u32 op_LD_A_mBC();
    u32 op_DEC_BC();
    u32 op_INC_C();
    u32 op_DEC_C();
    u32 op_LD_C_d8();
    u32 op_RRCA();

    // Row 0x10
    u32 op_STOP();
    u32 op_LD_DE_d16();
    u32 op_LD_mDE_A();
    u32 op_INC_DE();
    u32 op_INC_D();
    u32 op_DEC_D();
    u32 op_LD_D_d8();
    u32 op_RLA();
    u32 op_JR_r8();
    u32 op_ADD_HL_DE();
    u32 op_LD_A_mDE();
    u32 op_DEC_DE();
    u32 op_INC_E();
    u32 op_DEC_E();
    u32 op_LD_E_d8();
    u32 op_RRA();

    // Row 0x20
    u32 op_JR_NZ_r8();
    u32 op_LD_HL_d16();
    u32 op_LDI_mHL_A();
    u32 op_INC_HL();
    u32 op_INC_H();
    u32 op_DEC_H();
    u32 op_LD_H_d8();
    u32 op_DAA();
    u32 op_JR_Z_r8();
    u32 op_ADD_HL_HL();
    u32 op_LDI_A_mHL();
    u32 op_DEC_HL();
    u32 op_INC_L();
    u32 op_DEC_L();
    u32 op_LD_L_d8();
    u32 op_CPL();

    // Row 0x30
    u32 op_JR_NC_r8();
    u32 op_LD_SP_d16();
    u32 op_LDD_mHL_A();
    u32 op_INC_SP();
    u32 op_INC_mHL();
    u32 op_DEC_mHL();
    u32 op_LD_mHL_d8();
    u32 op_SCF();
    u32 op_JR_C_r8();
    u32 op_ADD_HL_SP();
    u32 op_LDD_A_mHL();
    u32 op_DEC_SP();
    u32 op_INC_A();
    u32 op_DEC_A();
    u32 op_LD_A_d8();
    u32 op_CCF();

    // Rows 0x40–0x7F: LD r, r  (64 ops; generated via table in buildOpcodeTables)
    u32 op_LD_r_r();   // generic handler—actual src/dst encoded in opcode

    // 0x76: HALT (special case inside the LD block)
    u32 op_HALT();

    // Rows 0x80–0xBF: ALU operations on registers
    u32 op_ADD_A_r();
    u32 op_ADC_A_r();
    u32 op_SUB_r();
    u32 op_SBC_A_r();
    u32 op_AND_r();
    u32 op_XOR_r();
    u32 op_OR_r();
    u32 op_CP_r();

    // Row 0xC0
    u32 op_RET_NZ();
    u32 op_POP_BC();
    u32 op_JP_NZ_a16();
    u32 op_JP_a16();
    u32 op_CALL_NZ_a16();
    u32 op_PUSH_BC();
    u32 op_ADD_A_d8();
    u32 op_RST_00();
    u32 op_RET_Z();
    u32 op_RET();
    u32 op_JP_Z_a16();
    u32 op_PREFIX_CB();
    u32 op_CALL_Z_a16();
    u32 op_CALL_a16();
    u32 op_ADC_A_d8();
    u32 op_RST_08();

    // Row 0xD0
    u32 op_RET_NC();
    u32 op_POP_DE();
    u32 op_JP_NC_a16();
    u32 op_CALL_NC_a16();
    u32 op_PUSH_DE();
    u32 op_SUB_d8();
    u32 op_RST_10();
    u32 op_RET_C();
    u32 op_RETI();
    u32 op_JP_C_a16();
    u32 op_CALL_C_a16();
    u32 op_SBC_A_d8();
    u32 op_RST_18();

    // Row 0xE0
    u32 op_LDH_ma8_A();
    u32 op_POP_HL();
    u32 op_LDH_mC_A();
    u32 op_PUSH_HL();
    u32 op_AND_d8();
    u32 op_RST_20();
    u32 op_ADD_SP_r8();
    u32 op_JP_HL();
    u32 op_LD_ma16_A();
    u32 op_XOR_d8();
    u32 op_RST_28();

    // Row 0xF0
    u32 op_LDH_A_ma8();
    u32 op_POP_AF();
    u32 op_LDH_A_mC();
    u32 op_DI();
    u32 op_PUSH_AF();
    u32 op_OR_d8();
    u32 op_RST_30();
    u32 op_LD_HL_SP_r8();
    u32 op_LD_SP_HL();
    u32 op_LD_A_ma16();
    u32 op_EI();
    u32 op_CP_d8();
    u32 op_RST_38();

    // Illegal / undefined opcode (should never execute; logs and NOPs)
    u32 op_ILLEGAL();

    // ── CB-prefixed handlers (0xCB 0x00–0xFF) ────────────────────────────────
    // Pattern: each CB op family covers 8 registers (B C D E H L [HL] A)
    u32 cb_RLC_r();
    u32 cb_RRC_r();
    u32 cb_RL_r();
    u32 cb_RR_r();
    u32 cb_SLA_r();
    u32 cb_SRA_r();
    u32 cb_SWAP_r();
    u32 cb_SRL_r();
    u32 cb_BIT_b_r();
    u32 cb_RES_b_r();
    u32 cb_SET_b_r();

    // ── State ─────────────────────────────────────────────────────────────────
    IMemory&                m_bus;           // Injected memory bus
    std::function<void(u32)> m_syncCallback;
    RegisterFile            m_reg;           // All CPU registers
    bool                    m_halted  = false;
    bool                    m_stopped = false;
    bool                    m_IME     = false; // Interrupt Master Enable
    bool                    m_IMEPending = false; // EI schedules IME after one instruction
    u64                     m_totalCycles = 0;
    u32                     m_syncedCycles = 0;
    u8                      m_currentOpcode = 0; // Cached opcode for handler context
};

} // namespace GB
