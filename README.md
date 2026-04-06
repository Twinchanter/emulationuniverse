# EmulationUniverse — Game Boy Emulator Core

Foundation emulator for the all-in-one multi-system project.  
Built in **C++17** with strict OOP: every hardware subsystem is a concrete class behind an abstract interface, making it trivial to swap in GBA, SNES, or custom ROM-hack system components without touching the front-end.

---

## Architecture

```
GameBoy (orchestrator)
├── Cartridge       — ROM loader, MBC factory, SRAM persistence
│   ├── MBC0        — ROM-only (Tetris, Dr. Mario)
│   ├── MBC1        — Pokémon Red/Blue/Yellow, Link's Awakening
│   └── MBC3 + RTC  — Pokémon Gold/Silver/Crystal ← ROM hack target
├── MMU             — Full 16-bit address bus dispatcher + OAM DMA
├── CPU             — Sharp LR35902: 512-opcode dispatch table (256 standard + 256 CB)
├── PPU             — Scanline renderer: BG / Window / Sprite layers, DMG palette
├── APU             — 4-channel synthesis: Square×2, Wave, Noise; frame sequencer
├── Timer           — DIV/TIMA/TMA/TAC hardware timer; owns the IF register
└── Joypad          — Active-low JOYP register; fires Joypad IRQ on press

Interfaces (core/interfaces/)
├── IComponent      — init / reset / tick lifecycle contract
├── IMemory         — read / write bus contract
├── ICPU            — step / handleInterrupts / requestInterrupt
├── IDisplay        — present(FrameBuffer) / isOpen / setTitle
└── IJoypad         — press / release / readJOYP / writeJOYP
```

All interrupt routing flows through `Timer::raiseIF()` → CPU `handleInterrupts()` so every subsystem remains decoupled from the CPU class.

---

## Building

### Prerequisites
| Tool | Minimum version |
|------|----------------|
| CMake | 3.20 |
| C++ compiler | GCC 10 / Clang 12 / MSVC 2019 (C++17 mode) |

### Headless (no display)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/emulationuniverse path/to/rom.gb [path/to/save.sav]
```

### Debugger mode (interactive CLI)
```bash
./build/emulationuniverse path/to/rom.gb [path/to/save.sav] --debugger
```

### Debugger script mode (repeatable sessions)
```bash
./build/emulationuniverse path/to/rom.gb [path/to/save.sav] --debug-script debug-scripts/boot_trace.dbg
```

### Debugger log output
```bash
./build/emulationuniverse path/to/rom.gb --debug-script debug-scripts/trace_with_include.dbg --debug-log logs/debug-session.log
```

Script behavior:
- Runs debugger commands from file in order
- Supports comments with `#`
- Supports script composition via `include <relative-or-absolute-path>`
- If script ends without `quit`, interactive debugger prompt continues
- `--debug-script` automatically enables debugger mode
- `--debug-log` mirrors debugger output to both console and log file

Included sample scripts:
- `debug-scripts/boot_trace.dbg`
- `debug-scripts/break_vblank.dbg`
- `debug-scripts/common/baseline.dbg`
- `debug-scripts/trace_with_include.dbg`

Debugger commands:
- `help`
- `regs`
- `step [n]`
- `run [n]`
- `break <hexAddr>`
- `delbreak <hexAddr>`
- `mem <hexAddr> [len]`
- `write <hexAddr> <hexByte>`
- `quit`

### Save-state CLI
```bash
./build/emulationuniverse path/to/rom.gb [path/to/save.sav] --load-state slot0.state --save-state slot1.state
```

Save-state format details:
- Versioned binary header (`EMUNIV2`)
- FNV-1a checksum over payload
- Full restore validation (register file + full 64KB memory map)

### With SDL2 display + audio
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_SDL2=ON
cmake --build build --parallel
./build/emulationuniverse path/to/rom.gb
```

### With tests
```bash
cmake -B build -DENABLE_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### ROM suite harness (Blargg + Mooneye)
The harness executable is built at:
- `build/tests/Release/gbemu_system_tests.exe` (Windows)

Environment variables:
- `EMU_TEST_ROMS_ROOT`: root directory containing ROM packs
- `EMU_TEST_MANIFEST`: optional manifest path (defaults to `tests/rom_manifest.csv`)

Manifest format (`tests/rom_manifest.csv`):
- `suite,mode,path,priority,max_frames`
- Example: `blargg,blargg,blargg/cpu_instrs/cpu_instrs.gb,critical,1600`
- `priority` values: `critical`, `high`, `medium`, `low`
- `max_frames` controls timeout budget per case (use higher values for slower or timing-sensitive ROMs)

Result log is written to:
- `tests/logs/rom_suite_results.csv`

Dashboard and summary generation:
- Script: `tests/Generate-RomSuiteDashboard.ps1`
- Dashboard output: `tests/logs/rom_suite_dashboard.md`
- Machine-readable summary: `tests/logs/rom_suite_summary.json`

Trend tracking:
- Script: `tests/Update-RomSuiteTrend.ps1`
- History store: `tests/logs/rom_suite_history.json`
- Trend report: `tests/logs/rom_suite_trend.md`
- `Run ROM Suite + Dashboard` updates trend automatically after each run

ROM asset readiness precheck:
- Script: `tests/Validate-RomSuiteAssets.ps1`
- Readiness report: `tests/logs/rom_assets_readiness.md`
- Readiness JSON: `tests/logs/rom_assets_readiness.json`
- This precheck fails fast when required manifest ROM files are missing.

ROM asset import helper:
- Script: `tests/Import-RomSuiteAssets.ps1`
- Import report: `tests/logs/rom_assets_import.md`
- Import JSON: `tests/logs/rom_assets_import.json`
- VS Code task: `Import ROM Suite Assets`
- Required env var for task: `EMU_TEST_ASSET_SOURCE` (path to your local ROM test-pack root)
- The importer maps files into `roms/test` according to `tests/rom_manifest.csv`.

Recommended failing-test-first loop:
1. Set `EMU_TEST_ASSET_SOURCE` and run VS Code task `Import ROM Suite Assets`
2. Run VS Code task `Validate ROM Suite Assets` (or run `Run ROM Suite + Dashboard`, which performs this precheck automatically)
3. Open `tests/logs/rom_assets_readiness.md` and confirm missing ROM files are cleared by priority
4. Run VS Code task `Run ROM Suite + Dashboard`
5. Open `tests/logs/rom_suite_dashboard.md` and start with `critical` failures first
6. Pick the top failing case in priority order and fix emulator behavior
7. Open `tests/logs/rom_suite_trend.md` to confirm pass/fail movement vs previous run
8. Repeat until the dashboard failure list is empty

### CPU micro-tests (no external ROM assets required)
- Binary: `build/tests/Release/gbemu_cpu_micro_tests.exe`
- Raw result CSV: `tests/logs/cpu_micro_results.csv`
- Dashboard script: `tests/Generate-CpuMicroDashboard.ps1`
- Dashboard output: `tests/logs/cpu_micro_dashboard.md`
- Summary JSON: `tests/logs/cpu_micro_summary.json`
- Trend script: `tests/Update-CpuMicroTrend.ps1`
- Trend outputs: `tests/logs/cpu_micro_history.json`, `tests/logs/cpu_micro_trend.md`
- VS Code tasks:
	- `Run CPU Micro Tests`
	- `Run CPU Micro + Dashboard`
- Recommended loop while ROM packs are unavailable:
	1. Run `Run CPU Micro + Dashboard`
	2. Fix highest-priority failing micro-test first (`critical`, then `high`, then `medium`)
	3. Check `tests/logs/cpu_micro_trend.md` for regression/progress movement

---

## Supported Cartridge Types

| Type | Games | Status |
|------|-------|--------|
| ROM-only (MBC0) | Tetris, Dr. Mario | ✅ |
| MBC1 | Pokémon Red/Blue/Yellow | ✅ |
| MBC3 + RTC | Pokémon Gold/Silver/Crystal | ✅ |
| MBC5 | Gen II+ remakes | ⚠️ MBC1 fallback |
| MBC2 | Kirby's Block Ball | 🔜 Planned |

---

## Extending to a Multi-System (ROM Hack Target)

The architecture is designed for the eventual **all-in-one open-world system**:

1. **New CPU**: Implement `ICPU` for an ARM7TDMI (GBA) or 65816 (SNES).  
	The `GameBoy` orchestrator only holds an `ICPU*`, so the loop doesn't change.

2. **Cross-system data bridge**: The `IMemory` interface allows a custom MMU to map a second ROM's data space into the same address bus — the foundation for the backwards-compatibility layer.

3. **MBC extension**: Add `MBCX.hpp` for a custom bank controller that maps both DMG and GBA ROM banks simultaneously.

4. **Connector API**: `CPU::registers()`, `PPU::frameBuffer()`, and `Cartridge::header()` are all public so external systems (analytics tools, network sync, ROM patchers) can consume diagnostics without depending on the UI.

---

## File Map

```
src/emulator/
├── core/
│   ├── Types.hpp              — Common typedefs, memory map constants, helpers
│   └── interfaces/
│       ├── IComponent.hpp     — Lifecycle: init / reset / tick
│       ├── IMemory.hpp        — Memory bus: read / write / readWord / writeWord
│       ├── ICPU.hpp           — CPU: step / handleInterrupts / requestInterrupt
│       ├── IDisplay.hpp       — Renderer: present / setTitle / isOpen
│       └── IJoypad.hpp        — Input: press / release / readJOYP / writeJOYP
├── cpu/
│   ├── Registers.hpp          — LR35902 register file + flag helpers
│   ├── CPU.hpp / CPU.cpp      — Fetch-decode-execute, interrupt service, ALU helpers
│   └── Opcodes.cpp            — All 512 opcode handler implementations
├── memory/
│   └── MMU.hpp / MMU.cpp      — Bus dispatcher, OAM DMA engine
├── cartridge/
│   ├── Cartridge.hpp/.cpp     — ROM loader, MBC factory, SRAM persistence
│   └── mbc/
│       ├── IMBC.hpp           — Bank controller interface
│       ├── MBC0.hpp           — ROM-only
│       ├── MBC1.hpp           — 2 MB ROM / 32 KB RAM
│       └── MBC3.hpp           — 2 MB ROM / 32 KB RAM + RTC
├── ppu/
│   └── PPU.hpp / PPU.cpp      — Scanline renderer, VRAM/OAM access, LCD registers
├── apu/
│   └── APU.hpp / APU.cpp      — 4-channel audio, frame sequencer, AudioCallback
├── timer/
│   └── Timer.hpp / Timer.cpp  — DIV/TIMA/TMA/TAC + IF register
├── joypad/
│   └── Joypad.hpp / Joypad.cpp— JOYP register, active-low button state
├── GameBoy.hpp / GameBoy.cpp  — Top-level orchestrator, runFrame loop
└── main.cpp                   — CLI entry point
```

---

## Roadmap

- [ ] MBC2 / MBC5 full implementation
- [ ] Game Boy Color (CGB) mode: double speed, colour palettes, VRAM bank 1
- [x] SDL2 display + audio frontend
- [x] Debugger: step, breakpoints, memory view
- [x] Save-state serialisation
- [ ] GBA CPU (ARM7TDMI) subsystem
- [ ] Cross-system open-world bridge layer (ROM hack engine)
- [ ] Network save-sync for multi-world data consistency
