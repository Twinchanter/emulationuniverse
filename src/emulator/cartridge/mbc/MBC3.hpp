/**
 * MBC3.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * MBC3 – up to 2 MB ROM / 32 KB RAM + Real-Time Clock (RTC).
 * Used by: Pokémon Gold/Silver/Crystal – the key cartridge for our ROM hack.
 *
 * Register map:
 *   0x0000–0x1FFF  RAM/RTC Enable
 *   0x2000–0x3FFF  ROM bank select (7 bits, 0 -> 1)
 *   0x4000–0x5FFF  RAM bank (0x00–0x03) or RTC register (0x08–0x0C)
 *   0x6000–0x7FFF  Latch clock data (write 0x00 then 0x01)
 *
 * RTC registers:
 *   0x08 = Seconds (0–59)
 *   0x09 = Minutes (0–59)
 *   0x0A = Hours   (0–23)
 *   0x0B = Day counter lower 8 bits
 *   0x0C = Day counter upper bit | halt flag | day carry flag
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "IMBC.hpp"
#include <ctime>

namespace GB {

class MBC3 final : public IMBC {
public:
    u8 read(const std::vector<u8>& romData,
            const std::vector<u8>& ramData,
            u16 address) const override {

        if (address <= ROM_BANK0_END) {
            return (address < romData.size()) ? romData[address] : 0xFF;
        }

        if (address <= ROM_BANKN_END) {
            u32 offset = (static_cast<u32>(m_romBank) << 14u) | (address - ROM_BANKN_START);
            return (offset < romData.size()) ? romData[offset] : 0xFF;
        }

        if (address >= ERAM_START && address <= ERAM_END) {
            if (!m_ramEnabled) return 0xFF;
            if (m_ramBank <= 0x03) {
                // Normal RAM access
                u32 offset = (static_cast<u32>(m_ramBank) << 13u) | (address - ERAM_START);
                return (offset < ramData.size()) ? ramData[offset] : 0xFF;
            } else {
                // RTC register access
                return readRTC(m_ramBank);
            }
        }
        return 0xFF;
    }

    void write(std::vector<u8>& ramData, u16 address, u8 value) override {
        if (address <= 0x1FFF) {
            m_ramEnabled = ((value & 0x0Fu) == 0x0Au);
            return;
        }
        if (address <= 0x3FFF) {
            m_romBank = value & 0x7Fu;
            if (m_romBank == 0) m_romBank = 1;
            return;
        }
        if (address <= 0x5FFF) {
            m_ramBank = value; // 0x00–0x03 = RAM; 0x08–0x0C = RTC
            return;
        }
        if (address <= 0x7FFF) {
            // Latch: write 0 then 1
            if (value == 0x00) m_latchReady = true;
            if (value == 0x01 && m_latchReady) {
                latchTime();
                m_latchReady = false;
            }
            return;
        }
        if (address >= ERAM_START && address <= ERAM_END) {
            if (!m_ramEnabled) return;
            if (m_ramBank <= 0x03) {
                u32 offset = (static_cast<u32>(m_ramBank) << 13u) | (address - ERAM_START);
                if (offset < ramData.size()) ramData[offset] = value;
            } else {
                writeRTC(m_ramBank, value);
            }
        }
    }

    void reset() override {
        m_romBank    = 1;
        m_ramBank    = 0;
        m_ramEnabled = false;
        m_latchReady = false;
        m_rtcHalt    = false;
        latchTime();
    }

private:
    u8   m_romBank    = 1;
    u8   m_ramBank    = 0;
    bool m_ramEnabled = false;
    bool m_latchReady = false;
    bool m_rtcHalt    = false;

    // Latched RTC values (captured on latch write)
    u8   m_rtcSeconds = 0;
    u8   m_rtcMinutes = 0;
    u8   m_rtcHours   = 0;
    u8   m_rtcDayLo   = 0;
    u8   m_rtcDayHi   = 0;

    void latchTime() {
        // Snapshot current system time into latched registers
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
    #if defined(_MSC_VER)
        localtime_s(&tmv, &t);
        std::tm* tm = &tmv;
    #else
        std::tm* tm = std::localtime(&t);
    #endif
        if (tm) {
            m_rtcSeconds = static_cast<u8>(tm->tm_sec % 60);
            m_rtcMinutes = static_cast<u8>(tm->tm_min % 60);
            m_rtcHours   = static_cast<u8>(tm->tm_hour % 24);
            // Day counter: days since some epoch; use tm_yday as approximation
            u16 day = static_cast<u16>(tm->tm_yday);
            m_rtcDayLo = static_cast<u8>(day & 0xFFu);
            m_rtcDayHi = static_cast<u8>((day >> 8u) & 0x01u);
        }
    }

    u8 readRTC(u8 reg) const {
        switch (reg) {
            case 0x08: return m_rtcSeconds;
            case 0x09: return m_rtcMinutes;
            case 0x0A: return m_rtcHours;
            case 0x0B: return m_rtcDayLo;
            case 0x0C: return m_rtcDayHi | (m_rtcHalt ? 0x40u : 0u);
            default:   return 0xFF;
        }
    }

    void writeRTC(u8 reg, u8 value) {
        switch (reg) {
            case 0x08: m_rtcSeconds = value & 0x3Fu; break;
            case 0x09: m_rtcMinutes = value & 0x3Fu; break;
            case 0x0A: m_rtcHours   = value & 0x1Fu; break;
            case 0x0B: m_rtcDayLo   = value;         break;
            case 0x0C:
                m_rtcDayHi = value & 0x01u;
                m_rtcHalt  = (value & 0x40u) != 0;
                break;
        }
    }
};

} // namespace GB
