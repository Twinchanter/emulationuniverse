/**
 * Cartridge.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "Cartridge.hpp"
#include "mbc/MBC0.hpp"
#include "mbc/MBC1.hpp"
#include "mbc/MBC2.hpp"
#include "mbc/MBC3.hpp"
#include "mbc/MBC5.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace GB {

// ─────────────────────────────────────────────────────────────────────────────
// IMemory – delegate to MBC
// ─────────────────────────────────────────────────────────────────────────────

u8 Cartridge::read(u16 address) const {
    if (!m_loaded || !m_mbc) return 0xFF;
    return m_mbc->read(m_rom, m_ram, address);
}

void Cartridge::write(u16 address, u8 value) {
    if (!m_loaded || !m_mbc) return;
    m_mbc->write(m_ram, address, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// ROM loading
// ─────────────────────────────────────────────────────────────────────────────

bool Cartridge::load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open ROM: " + filePath);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    m_rom.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(m_rom.data()), size)) {
        throw std::runtime_error("Failed to read ROM: " + filePath);
    }

    // Minimum size check: ROM header ends at 0x014F
    if (m_rom.size() < 0x0150) {
        throw std::runtime_error("ROM too small to contain a valid header");
    }

    parseHeader();
    createMBC();
    allocateRAM();
    m_mbc->reset();
    m_loaded = true;

    std::printf("[Cartridge] Loaded: %s  Type=0x%02X  ROM=%zuKB  RAM=%zuKB\n",
                m_header.title.c_str(),
                static_cast<u8>(m_header.cartType),
                m_rom.size() / 1024,
                m_ram.size() / 1024);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header parsing
// ─────────────────────────────────────────────────────────────────────────────

void Cartridge::parseHeader() {
    // Title: 0x0134–0x0143 (may be shorter with CGB flag in 0x0143)
    char titleBuf[17] = {};
    std::memcpy(titleBuf, &m_rom[0x0134], 16);
    m_header.title = std::string(titleBuf);

    m_header.cartType         = static_cast<CartridgeType>(m_rom[0x0147]);
    m_header.romSizeCode      = m_rom[0x0148];
    m_header.ramSizeCode      = m_rom[0x0149];
    m_header.destinationCode  = m_rom[0x014A];
    m_header.headerChecksum   = m_rom[0x014D];
    m_header.globalChecksum   = static_cast<u16>((m_rom[0x014E] << 8u) | m_rom[0x014F]);

    // Battery-backed types
    switch (m_header.cartType) {
        case CartridgeType::MBC1_RAM_BATT:
        case CartridgeType::MBC2_BATT:
        case CartridgeType::ROM_RAM_BATT:
        case CartridgeType::MBC3_TIMER_BATT:
        case CartridgeType::MBC3_TIMER_RAM_BATT:
        case CartridgeType::MBC3_RAM_BATT:
        case CartridgeType::MBC5_RAM_BATT:
            m_hasBattery = true;
            break;
        default:
            m_hasBattery = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MBC creation factory
// ─────────────────────────────────────────────────────────────────────────────

void Cartridge::createMBC() {
    switch (m_header.cartType) {
        case CartridgeType::ROM_ONLY:
        case CartridgeType::ROM_RAM:
        case CartridgeType::ROM_RAM_BATT:
            m_mbc = std::make_unique<MBC0>();
            break;
        case CartridgeType::MBC1:
        case CartridgeType::MBC1_RAM:
        case CartridgeType::MBC1_RAM_BATT:
            m_mbc = std::make_unique<MBC1>();
            break;
        case CartridgeType::MBC3:
        case CartridgeType::MBC3_RAM:
        case CartridgeType::MBC3_RAM_BATT:
        case CartridgeType::MBC3_TIMER_BATT:
        case CartridgeType::MBC3_TIMER_RAM_BATT:
            m_mbc = std::make_unique<MBC3>();
            break;
        case CartridgeType::MBC2:
        case CartridgeType::MBC2_BATT:
            m_mbc = std::make_unique<MBC2>();
            break;
        case CartridgeType::MBC5:
        case CartridgeType::MBC5_RAM:
        case CartridgeType::MBC5_RAM_BATT:
            m_mbc = std::make_unique<MBC5>();
            break;
        default:
            m_mbc = std::make_unique<MBC0>();
            std::fprintf(stderr, "[Cartridge] Warning: Unknown cart type 0x%02X, using ROM-only\n",
                         static_cast<u8>(m_header.cartType));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// External RAM sizing
// ─────────────────────────────────────────────────────────────────────────────

void Cartridge::allocateRAM() {
    static const size_t RAM_SIZES[] = {
        0,       // 0x00 – No RAM
        0,       // 0x01 – Unused
        8192,    // 0x02 –  8 KB ( 1 bank)
        32768,   // 0x03 – 32 KB ( 4 banks)
        131072,  // 0x04 – 128 KB (16 banks)
        65536    // 0x05 –  64 KB ( 8 banks)
    };
    size_t idx = m_header.ramSizeCode;
    size_t sz  = (idx < 6) ? RAM_SIZES[idx] : 0;
    m_ram.assign(sz, 0xFF); // Initialise to 0xFF (unwritten flash behaviour)
}

// ─────────────────────────────────────────────────────────────────────────────
// Battery-backed save persistence
// ─────────────────────────────────────────────────────────────────────────────

void Cartridge::saveSRAM(const std::string& savePath) const {
    if (!m_hasBattery || m_ram.empty()) return;
    std::ofstream out(savePath, std::ios::binary);
    if (out) out.write(reinterpret_cast<const char*>(m_ram.data()),
                       static_cast<std::streamsize>(m_ram.size()));
}

void Cartridge::loadSRAM(const std::string& savePath) {
    if (!m_hasBattery || m_ram.empty()) return;
    std::ifstream in(savePath, std::ios::binary);
    if (in) in.read(reinterpret_cast<char*>(m_ram.data()),
                    static_cast<std::streamsize>(m_ram.size()));
}

} // namespace GB
