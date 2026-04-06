/**
 * GameBoy.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Top-level orchestrator – the "system board".
 *
 * Owns and wires all hardware subsystems:
 *   Cartridge → MMU → CPU
 *                └─ PPU
 *                └─ APU
 *                └─ Timer
 *                └─ Joypad
 *
 * The main emulation loop calls runFrame() once per video frame.
 * All interrupt routing flows through the CPU's requestInterrupt() method;
 * subsystems hold a callback lambda that calls it so they remain decoupled
 * from the CPU class itself.
 *
 * Extensibility note:
 *   When adding a second system (e.g. Game Boy Color, GBA), create a new
 *   orchestrator class (GameBoyColor, GBA) that composes different concrete
 *   subsystems satisfying the same interfaces.  The CPU, PPU, and APU
 *   interfaces allow drop-in replacement without touching the renderer or
 *   the front-end.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "core/interfaces/IDisplay.hpp"
#include "cpu/CPU.hpp"
#include "memory/MMU.hpp"
#include "ppu/PPU.hpp"
#include "apu/APU.hpp"
#include "timer/Timer.hpp"
#include "joypad/Joypad.hpp"
#include "cartridge/Cartridge.hpp"

#include <string>
#include <memory>

namespace GB {

class GameBoy {
public:
    /**
     * @param display   Optional renderer.  Pass nullptr for headless/test mode.
     * @param audioOut  Optional audio callback.  Pass nullptr to mute.
     */
    explicit GameBoy(IDisplay* display = nullptr,
                     AudioCallback audioOut = nullptr);
    ~GameBoy() = default;

    // Non-copyable, non-movable (hardware has identity)
    GameBoy(const GameBoy&) = delete;
    GameBoy& operator=(const GameBoy&) = delete;

    // ── ROM management ───────────────────────────────────────────────────────
    /// Load a ROM and optional save file.  Resets the system after loading.
    bool loadROM(const std::string& romPath,
                 const std::string& savePath = "");

    /// Persist battery-backed SRAM to disk.
    void saveSRAM(const std::string& savePath) const;

    // ── System lifecycle ─────────────────────────────────────────────────────
    void reset();

    /**
     * Run the emulator for exactly one video frame (70224 T-cycles).
     * Returns the actual T-cycles executed (may be slightly over due to
     * the last instruction boundary).
     */
    u32 runFrame();

    // ── Input ────────────────────────────────────────────────────────────────
    void pressButton  (Button btn);
    void releaseButton(Button btn);

    // ── Introspection (for debugger / test harness) ───────────────────────────
    const CPU&       cpu()       const { return *m_cpu;  }
    CPU&             cpuMutable()      { return *m_cpu;  }
    const PPU&       ppu()       const { return *m_ppu;  }
    const Cartridge& cartridge() const { return m_cart;  }
    bool             isRunning() const { return m_running;}

    // ── Serial port output (accumulated by instant-serial simulation) ─────────
    const std::string& serialOutput() const;
    void               clearSerial();

    // ── Debug: step a single CPU instruction ─────────────────────────────────
    u32 stepInstruction();

    // ── Debug/SaveState memory and register access ───────────────────────────
    u8  readByte(u16 address) const;
    void writeByte(u16 address, u8 value);
    void setCPURegisters(const RegisterFile& regs);

private:
    // ── Subsystem ownership order matters for init/destruction ────────────────
    // Cart has no dependencies; MMU depends on everything else.
    Cartridge          m_cart;
    Timer              m_timer;
    Joypad             m_joypad;
    APU                m_apu;

    // PPU and CPU are heap-allocated (need display/callback plumbing before ctor)
    std::unique_ptr<PPU> m_ppu;
    std::unique_ptr<MMU> m_mmu;
    std::unique_ptr<CPU> m_cpu;

    bool m_running = false;
};

} // namespace GB
