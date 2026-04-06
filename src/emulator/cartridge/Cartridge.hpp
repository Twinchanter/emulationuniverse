/**
 * Cartridge.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * ROM cartridge loader and ownership container.
 *
 * Responsibilities:
 *   - Parse the 0x0100–0x014F ROM header
 *   - Instantiate the correct IMBC subclass
 *   - Maintain the ROM and external RAM byte vectors
 *   - Provide save-file persistence for battery-backed RAM
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IMemory.hpp"
#include "../core/interfaces/IComponent.hpp"
#include "mbc/IMBC.hpp"
#include <string>
#include <memory>
#include <vector>

namespace GB {

/// Decoded ROM header (0x0104–0x014F)
struct ROMHeader {
    std::string title;        // 0x0134–0x0143 (16 bytes, NUL-trimmed)
    CartridgeType cartType;   // 0x0147
    u8  romSizeCode;          // 0x0148
    u8  ramSizeCode;          // 0x0149
    u8  destinationCode;      // 0x014A
    u8  headerChecksum;       // 0x014D
    u16 globalChecksum;       // 0x014E–0x014F
};

class Cartridge final : public IMemory, public IComponent {
public:
    Cartridge() = default;
    ~Cartridge() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override { /* nothing – load() performs initialisation */ }
    void reset() override { if (m_mbc) m_mbc->reset(); }
    u32  tick(u32 cycles) override { return cycles; }

    // ── IMemory ─────────────────────────────────────────────────────────────
    u8   read (u16 address) const override;
    void write(u16 address, u8 value) override;

    // ── ROM loading ─────────────────────────────────────────────────────────

    /**
     * Load and parse a ROM file from disk.
     * @throws std::runtime_error on file/format errors.
     */
    bool load(const std::string& filePath);

    /// True once a ROM has been successfully loaded.
    bool isLoaded() const { return m_loaded; }

    /// Parsed ROM header (valid only after load()).
    const ROMHeader& header() const { return m_header; }

    // ── Battery-backed save support ─────────────────────────────────────────
    void saveSRAM(const std::string& savePath) const;
    void loadSRAM(const std::string& savePath);

private:
    std::vector<u8>       m_rom{};       // Full ROM byte vector
    std::vector<u8>       m_ram{};       // External RAM (battery-backed when present)
    std::unique_ptr<IMBC> m_mbc;         // Concrete MBC selected by header
    ROMHeader             m_header{};
    bool                  m_loaded = false;
    bool                  m_hasBattery = false;

    /// Parse header from the loaded ROM bytes.
    void parseHeader();

    /// Construct the appropriate IMBC subclass for this cartridge type.
    void createMBC();

    /// Allocate the external RAM buffer based on the RAM size code.
    void allocateRAM();
};

} // namespace GB
