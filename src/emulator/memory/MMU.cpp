/**
 * MMU.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Memory bus read/write routing + OAM DMA engine.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "MMU.hpp"
#include "../cartridge/Cartridge.hpp"
#include "../ppu/PPU.hpp"
#include "../apu/APU.hpp"
#include "../timer/Timer.hpp"
#include "../joypad/Joypad.hpp"
#include <cstdio>
#include "../core/Types.hpp"

namespace GB {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

MMU::MMU(Cartridge& cart, PPU& ppu, APU& apu, Timer& timer, Joypad& joypad)
    : m_cart(cart), m_ppu(ppu), m_apu(apu), m_timer(timer), m_joypad(joypad) {}

void MMU::init()  { reset(); }
void MMU::reset() {
    m_wram.fill(0);
    m_hram.fill(0);
    m_ie           = 0;
    m_dmaActive    = false;
    m_dmaProgress  = 0;
    m_dmaCycles    = 0;
    m_dmaRegister  = 0xFF;
    m_dmaPending   = false;
    m_dmaPendingSrc = 0;
    m_dmaStartDelay = 0;
    m_serialData    = 0;
    m_serialControl = 0;
    m_serialBuffer.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Read dispatch
// ─────────────────────────────────────────────────────────────────────────────

u8 MMU::readDirect(u16 addr) const {

    // ── ROM (bank 0 and switchable bank N) ────────────────────────────────────
    if (addr <= ROM_BANKN_END) {
        return m_cart.read(addr);
    }

    // ── VRAM ──────────────────────────────────────────────────────────────────
    if (addr <= VRAM_END) {
        return m_ppu.readVRAM(addr);
    }

    // ── External (cartridge) RAM ──────────────────────────────────────────────
    if (addr <= ERAM_END) {
        return m_cart.read(addr);
    }

    // ── Work RAM + Echo ───────────────────────────────────────────────────────
    if (addr <= WRAM_END) {
        return m_wram[addr - WRAM_START];
    }
    if (addr <= ECHO_END) {
        // Echo mirrors WRAM 0xC000–0xDDFF
        return m_wram[addr - ECHO_START];
    }

    // ── OAM ───────────────────────────────────────────────────────────────────
    if (addr <= OAM_END) {
        // OAM is inaccessible during modes 2 & 3 (hardware returns 0xFF)
        return m_ppu.readOAM(addr);
    }

    // ── Unusable region ───────────────────────────────────────────────────────
    if (addr <= UNUSABLE_END) {
        return 0xFF;
    }

    // ── I/O Registers ─────────────────────────────────────────────────────────
    if (addr <= IO_END) {
        if (addr == REG_JOYP)              return m_joypad.readJOYP(0);
        if (addr == REG_SB)                return m_serialData;
        if (addr == REG_SC)                return m_serialControl;
        if (addr == REG_DMA)               return m_dmaRegister;
        if (addr == REG_DIV || addr == REG_TIMA ||
            addr == REG_TMA  || addr == REG_TAC)    return m_timer.readReg(addr);
        if (addr == REG_IF)                return m_timer.readIF(); // IF is managed by timer/bus
        if (addr >= REG_NR10 && addr <= 0xFF3F)    return m_apu.readReg(addr);
        if (addr >= REG_LCDC && addr <= REG_WX)    return m_ppu.readReg(addr);
        return 0xFF; // Unmapped I/O reads open-bus
    }

    // ── High RAM ──────────────────────────────────────────────────────────────
    if (addr <= HRAM_END) {
        return m_hram[addr - HRAM_START];
    }

    // ── Interrupt Enable ──────────────────────────────────────────────────────
    if (addr == IE_REGISTER) {
        return m_ie;
    }

    return 0xFF;
}

u8 MMU::read(u16 addr) const {
    if (m_dmaActive) {
        // The timing ROMs rely on bytes immediately before OAM remaining visible
        // while the actual OAM window is blocked during DMA.
        if (addr >= OAM_START && addr <= OAM_END) {
            return 0xFF;
        }
    }

    return readDirect(addr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Write dispatch
// ─────────────────────────────────────────────────────────────────────────────

void MMU::write(u16 addr, u8 value) {

    if (m_dmaActive) {
        // Writes to the OAM window are ignored while DMA is active.
        if (addr >= OAM_START && addr <= OAM_END) {
            return;
        }
    }

    // ── ROM (MBC register writes) ──────────────────────────────────────────────
    if (addr <= ROM_BANKN_END) {
        m_cart.write(addr, value); return;
    }

    // ── VRAM ──────────────────────────────────────────────────────────────────
    if (addr <= VRAM_END) {
        m_ppu.writeVRAM(addr, value); return;
    }

    // ── External RAM ──────────────────────────────────────────────────────────
    if (addr <= ERAM_END) {
        m_cart.write(addr, value); return;
    }

    // ── Work RAM ──────────────────────────────────────────────────────────────
    if (addr <= WRAM_END) {
        m_wram[addr - WRAM_START] = value; return;
    }
    if (addr <= ECHO_END) {
        m_wram[addr - ECHO_START] = value; return;
    }

    // ── OAM ───────────────────────────────────────────────────────────────────
    if (addr <= OAM_END) {
        m_ppu.writeOAM(addr, value); return;
    }

    // ── Unusable ──────────────────────────────────────────────────────────────
    if (addr <= UNUSABLE_END) { return; }

    // ── I/O Registers ─────────────────────────────────────────────────────────
    if (addr <= IO_END) {
        if (addr == REG_JOYP)              { m_joypad.writeJOYP(value); return; }
        if (addr == REG_SB) { m_serialData = value; return; }
        if (addr == REG_SC) {
            m_serialControl = value;
            // Instant serial transfer: bit7=request, bit0=internal clock.
            // Immediately complete so test ROMs don't hang waiting for the IRQ.
            if ((value & 0x81u) == 0x81u) {
                m_serialBuffer += static_cast<char>(m_serialData);
                m_serialControl &= ~0x80u; // clear transfer-in-progress bit
                m_timer.raiseIF(Interrupt::Serial);
            }
            return;
        }
        if (addr == REG_DIV || addr == REG_TIMA ||
            addr == REG_TMA  || addr == REG_TAC)   { m_timer.writeReg(addr, value); return; }
        if (addr == REG_IF)                { m_timer.writeIF(value); return; }
        if (addr >= REG_NR10 && addr <= 0xFF3F)   { m_apu.writeReg(addr, value); return; }
        if (addr == REG_DMA)               { startDMA(value); return; }
        if (addr >= REG_LCDC && addr <= REG_WX)   { m_ppu.writeReg(addr, value); return; }
        return;
    }

    // ── High RAM ──────────────────────────────────────────────────────────────
    if (addr <= HRAM_END) {
        m_hram[addr - HRAM_START] = value; return;
    }

    // ── Interrupt Enable ──────────────────────────────────────────────────────
    if (addr == IE_REGISTER) {
        m_ie = value; return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OAM DMA
// ─────────────────────────────────────────────────────────────────────────────

void MMU::startDMA(u8 page) {
    // Source address = page * 0x100 (e.g. 0xA0 -> 0xA000)
    // New DMA begins after a 1 M-cycle delay; if one is already running,
    // it continues until the new transfer takes over at that point.
    m_dmaRegister   = page;
    m_dmaPending    = true;
    m_dmaPendingSrc = static_cast<u16>(page) << 8u;
    m_dmaStartDelay = 8;
}

void MMU::tickDMA(u32 cycles) {
    auto runActiveDma = [this](u32 stepCycles) {
        if (!m_dmaActive || stepCycles == 0) return;

        m_dmaCycles += stepCycles;
        while (m_dmaCycles >= 4 && m_dmaProgress < 160) {
            u8 byte = readDirect(static_cast<u16>(m_dmaSrc + m_dmaProgress));
            m_ppu.writeOAM(static_cast<u16>(OAM_START + m_dmaProgress), byte);
            ++m_dmaProgress;
            m_dmaCycles -= 4;
        }

        if (m_dmaProgress >= 160) {
            m_dmaActive = false;
        }
    };

    u32 remaining = cycles;
    while (remaining > 0) {
        if (m_dmaPending && m_dmaStartDelay > 0) {
            const u32 step = (remaining < m_dmaStartDelay) ? remaining : m_dmaStartDelay;
            runActiveDma(step);
            remaining -= step;
            m_dmaStartDelay -= step;

            if (m_dmaPending && m_dmaStartDelay == 0) {
                m_dmaSrc      = m_dmaPendingSrc;
                m_dmaProgress = 0;
                m_dmaCycles   = 0;
                m_dmaActive   = true;
                m_dmaPending  = false;
            }
            continue;
        }

        runActiveDma(remaining);
        break;
    }
}

} // namespace GB
