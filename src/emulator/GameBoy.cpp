/**
 * GameBoy.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "GameBoy.hpp"
#include <stdexcept>

namespace GB {

// ─────────────────────────────────────────────────────────────────────────────
// Construction – wire all subsystems
// ─────────────────────────────────────────────────────────────────────────────

GameBoy::GameBoy(IDisplay* display, AudioCallback audioOut)
    : m_timer([this](Interrupt irq) {
          // Timer fires interrupt request into the CPU via the IF register
          m_timer.raiseIF(irq);
      })
    , m_joypad([this](Interrupt irq) {
          m_timer.raiseIF(irq);
      })
    , m_apu(std::move(audioOut))
{
    // Build PPU with IRQ callback that updates IF
    m_ppu = std::make_unique<PPU>(display, [this](Interrupt irq) {
        m_timer.raiseIF(irq);
    });

    // Build MMU (needs all subsystems already constructed)
    m_mmu = std::make_unique<MMU>(m_cart, *m_ppu, m_apu, m_timer, m_joypad);

    // Build CPU with injected memory bus and per-cycle hardware sync.
    m_cpu = std::make_unique<CPU>(*m_mmu, [this](u32 cycles) {
        m_ppu->tick(cycles);
        m_timer.tick(cycles);
        m_apu.tick(cycles);
        m_mmu->tickDMA(cycles);
    });

    // One-time initialisation of all subsystems
    m_cart.init();
    m_timer.init();
    m_joypad.init();
    m_apu.init();
    m_ppu->init();
    m_mmu->init();
    m_cpu->init();
}

// ─────────────────────────────────────────────────────────────────────────────
// ROM loading
// ─────────────────────────────────────────────────────────────────────────────

bool GameBoy::loadROM(const std::string& romPath, const std::string& savePath) {
    bool ok = m_cart.load(romPath);
    if (!ok) return false;

    if (!savePath.empty()) {
        m_cart.loadSRAM(savePath);
    }
    reset();
    m_running = true;
    return true;
}

void GameBoy::saveSRAM(const std::string& savePath) const {
    m_cart.saveSRAM(savePath);
}

// ─────────────────────────────────────────────────────────────────────────────
// System reset
// ─────────────────────────────────────────────────────────────────────────────

void GameBoy::reset() {
    m_cart.reset();
    m_timer.reset();
    m_joypad.reset();
    m_apu.reset();
    m_ppu->reset();
    m_mmu->reset();
    m_cpu->reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main emulation loop
// ─────────────────────────────────────────────────────────────────────────────

u32 GameBoy::runFrame() {
    // Target: ~70224 T-cycles per frame (4.194304 MHz / 59.73 fps)
    constexpr u32 FRAME_CYCLES = 70224;

    u32 elapsed = 0;
    while (elapsed < FRAME_CYCLES) {
        u32 cpuCycles = m_cpu->step();

        // Route ANY pending interrupts from IF+IE into the CPU
        // (CPU::step() already calls handleInterrupts at the top; we also
        //  propagate the IF register written by Timer/PPU/Joypad here)
        {
            u8 ifVal = m_timer.readIF();
            u8 ieVal = m_mmu->read(IE_REGISTER);
            if (ifVal & ieVal & 0x1Fu) {
                // Something is pending; CPU will handle it on next step()
                // For HALT wake, we just need the flags to be set (done above).
            }
        }

        elapsed += cpuCycles;
    }
    return elapsed;
}

u32 GameBoy::stepInstruction() {
    u32 cpuCycles = m_cpu->step();
    return cpuCycles;
}

u8 GameBoy::readByte(u16 address) const {
    return m_mmu->read(address);
}

void GameBoy::writeByte(u16 address, u8 value) {
    m_mmu->write(address, value);
}

void GameBoy::setCPURegisters(const RegisterFile& regs) {
    m_cpu->registers() = regs;
}

const std::string& GameBoy::serialOutput() const {
    return m_mmu->serialOutput();
}

void GameBoy::clearSerial() {
    m_mmu->clearSerial();
}

// ─────────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────────

void GameBoy::pressButton  (Button btn) { m_joypad.press(btn);   }
void GameBoy::releaseButton(Button btn) { m_joypad.release(btn); }

} // namespace GB
