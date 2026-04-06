/**
 * APU.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "APU.hpp"
#include <cstring>

namespace GB {

// ── Square wave duty patterns ─────────────────────────────────────────────────
static constexpr bool DUTY_TABLE[4][8] = {
    { false,false,false,false,false,false,false,true  }, // 12.5%
    { true, false,false,false,false,false,false,true  }, // 25%
    { true, false,false,false,true, true, true, false }, // 50%
    { false,true, true, true, true, true, true, false }  // 75%
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

APU::APU(AudioCallback audioOut) : m_audioOut(std::move(audioOut)) {}

void APU::init()  { reset(); }
void APU::reset() {
    m_nr50 = 0x77; m_nr51 = 0xF3; m_nr52 = 0xF1;
    m_ch1 = {}; m_ch2 = {}; m_ch3 = {}; m_ch4 = {};
    m_frameSeqTimer = 0; m_frameSeqStep = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

u32 APU::tick(u32 cycles) {
    if (!(m_nr52 & 0x80u)) return cycles; // APU disabled

    for (u32 i = 0; i < cycles; i += 4) {
        // Advance frame sequencer (512 Hz = every 8192 T-cycles ÷ 4 = 2048 M-cycles)
        m_frameSeqTimer += 4;
        if (m_frameSeqTimer >= 8192) {
            m_frameSeqTimer -= 8192;
            tickFrameSequencer();
        }

        // Advance channel frequency timers (simplified: one sample per 4 TCycles)
        --m_ch1.freqTimer;
        if (m_ch1.freqTimer <= 0) {
            u16 freq = (static_cast<u16>(m_ch1.nr4 & 0x07u) << 8u) | m_ch1.nr3;
            m_ch1.freqTimer = (2048 - freq) * 4;
            m_ch1.dutyStep  = (m_ch1.dutyStep + 1) & 7;
        }

        --m_ch2.freqTimer;
        if (m_ch2.freqTimer <= 0) {
            u16 freq = (static_cast<u16>(m_ch2.nr4 & 0x07u) << 8u) | m_ch2.nr3;
            m_ch2.freqTimer = (2048 - freq) * 4;
            m_ch2.dutyStep  = (m_ch2.dutyStep + 1) & 7;
        }
    }

    // Emit a mixed audio sample every 87 T-cycles (~48000 Hz)
    mixAndOutput();
    return cycles;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame sequencer
// ─────────────────────────────────────────────────────────────────────────────

void APU::tickFrameSequencer() {
    // Step:  0    1    2    3    4    5    6    7
    // Len:   CLK  -    CLK  -    CLK  -    CLK  -
    // Vol:   -    -    -    -    -    -    -    CLK
    // Sweep: -    -    CLK  -    -    -    CLK  -
    switch (m_frameSeqStep & 7u) {
        case 0: clockLengthCounters(); break;
        case 2: clockLengthCounters(); clockSweep(); break;
        case 4: clockLengthCounters(); break;
        case 6: clockLengthCounters(); clockSweep(); break;
        case 7: clockEnvelopes(); break;
    }
    ++m_frameSeqStep;
}

void APU::clockLengthCounters() {
    // If length counter reaches 0 while stop-on-expire is set, disable channel
    auto clockCh = [](bool& enabled, int& len, u8 ctrl) {
        if ((ctrl & 0x40u) && len > 0) {
            --len;
            if (len == 0) enabled = false;
        }
    };
    clockCh(m_ch1.enabled, m_ch1.lengthTimer, m_ch1.nr4);
    clockCh(m_ch2.enabled, m_ch2.lengthTimer, m_ch2.nr4);
    clockCh(m_ch3.enabled, m_ch3.lengthTimer, m_ch3.nr34);
    clockCh(m_ch4.enabled, m_ch4.lengthTimer, m_ch4.nr44);
}

void APU::clockEnvelopes() {
    auto clockEnv = [](bool enabled, int& timer, u8 nr2, u8& vol) {
        if (!enabled) return;
        if (timer <= 0) return;
        --timer;
        if (timer == 0) {
            u8 period = nr2 & 0x07u;
            if (period == 0) return;
            timer = period;
            bool addMode = (nr2 & 0x08u) != 0;
            if (addMode && vol < 15) ++vol;
            else if (!addMode && vol > 0) --vol;
        }
    };
    clockEnv(m_ch1.enabled, m_ch1.envTimer, m_ch1.nr2, m_ch1.envVolume);
    clockEnv(m_ch2.enabled, m_ch2.envTimer, m_ch2.nr2, m_ch2.envVolume);
    clockEnv(m_ch4.enabled, m_ch4.envTimer, m_ch4.nr42, m_ch4.envVolume);
}

void APU::clockSweep() {
    if (!m_ch1.enabled) return;
    u8   period = (m_ch1.nr0 >> 4u) & 0x07u;
    if (period == 0) return;

    --m_ch1.sweepTimer;
    if (m_ch1.sweepTimer <= 0) {
        m_ch1.sweepTimer = period;
        u8  shift    = m_ch1.nr0 & 0x07u;
        bool decrease= (m_ch1.nr0 & 0x08u) != 0;
        u16  newFreq  = m_ch1.sweepFreq >> shift;
        if (decrease) m_ch1.sweepFreq -= newFreq;
        else          m_ch1.sweepFreq += newFreq;

        if (m_ch1.sweepFreq > 2047) {
            m_ch1.enabled = false; // Overflow disables channel
        } else {
            m_ch1.nr3 = static_cast<u8>(m_ch1.sweepFreq & 0xFFu);
            m_ch1.nr4 = (m_ch1.nr4 & 0xF8u) | static_cast<u8>(m_ch1.sweepFreq >> 8u);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mix + output
// ─────────────────────────────────────────────────────────────────────────────

void APU::mixAndOutput() {
    if (!m_audioOut) return;

    // Each channel outputs a value in [-1, +1]; mix and normalise
    float s1 = m_ch1.sample();
    float s2 = m_ch2.sample();
    float s3 = m_ch3.sample();
    float s4 = m_ch4.sample();

    // Apply panning (NR51): bits 7-4 = left enable for CH4-CH1; bits 3-0 = right
    float left  = 0.f, right = 0.f;
    if (m_nr51 & 0x10u) left  += s1;
    if (m_nr51 & 0x01u) right += s1;
    if (m_nr51 & 0x20u) left  += s2;
    if (m_nr51 & 0x02u) right += s2;
    if (m_nr51 & 0x40u) left  += s3;
    if (m_nr51 & 0x04u) right += s3;
    if (m_nr51 & 0x80u) left  += s4;
    if (m_nr51 & 0x08u) right += s4;

    // Apply master volume (0–7 maps to ~0.0–1.0)
    float masterL = ((m_nr50 >> 4u) & 0x07u) / 7.f;
    float masterR = ( m_nr50        & 0x07u) / 7.f;
    left  = (left  / 4.f) * masterL;
    right = (right / 4.f) * masterR;

    m_audioOut(left, right);
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel sample functions
// ─────────────────────────────────────────────────────────────────────────────

float APU::SquareChannel::sample() const {
    if (!enabled || envVolume == 0) return 0.f;
    u8 duty = (nr1 >> 6u) & 0x03u;
    return DUTY_TABLE[duty][dutyStep] ? (envVolume / 15.f) : -(envVolume / 15.f);
}

float APU::WaveChannel::sample() const {
    if (!enabled) return 0.f;
    u8 raw = waveTable[sampleIdx / 2];
    u8 nibble = (sampleIdx & 1) ? (raw & 0x0Fu) : (raw >> 4u);
    u8 shift = ((nr32 >> 5u) & 3u);
    if (shift == 0) return 0.f; // Muted
    return (nibble >> (shift - 1u)) / 15.f * 2.f - 1.f;
}

float APU::NoiseChannel::sample() const {
    if (!enabled || envVolume == 0) return 0.f;
    return (lfsr & 1u) ? (envVolume / 15.f) : -(envVolume / 15.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel trigger helpers
// ─────────────────────────────────────────────────────────────────────────────

void APU::SquareChannel::trigger() {
    enabled    = true;
    envVolume  = (nr2 >> 4u) & 0x0Fu;
    envTimer   = nr2 & 0x07u;
    u8 lenLoad = 64 - (nr1 & 0x3Fu);
    if (lengthTimer == 0) lengthTimer = lenLoad;
    u16 freq   = (static_cast<u16>(nr4 & 0x07u) << 8u) | nr3;
    freqTimer  = (2048 - freq) * 4;
    sweepFreq  = freq;
    sweepTimer = (nr0 >> 4u) & 0x07u;
}

void APU::WaveChannel::trigger() {
    enabled    = true;
    sampleIdx  = 0;
    u16 freq   = (static_cast<u16>(nr34 & 0x07u) << 8u) | nr33;
    freqTimer  = (2048 - freq) * 2;
    if (lengthTimer == 0) lengthTimer = 256 - nr31;
}

void APU::NoiseChannel::trigger() {
    enabled    = true;
    envVolume  = (nr42 >> 4u) & 0x0Fu;
    envTimer   = nr42 & 0x07u;
    lfsr       = 0x7FFF;
    if (lengthTimer == 0) lengthTimer = 64 - (nr41 & 0x3Fu);
}

// ─────────────────────────────────────────────────────────────────────────────
// I/O register access
// ─────────────────────────────────────────────────────────────────────────────

u8 APU::readReg(u16 addr) const {
    if (addr == 0xFF10) return m_ch1.nr0 | 0x80u;
    if (addr == 0xFF11) return m_ch1.nr1 | 0x3Fu;
    if (addr == 0xFF12) return m_ch1.nr2;
    if (addr == 0xFF13) return 0xFF; // Write-only
    if (addr == 0xFF14) return m_ch1.nr4 | 0xBFu;

    if (addr == 0xFF16) return m_ch2.nr1 | 0x3Fu;
    if (addr == 0xFF17) return m_ch2.nr2;
    if (addr == 0xFF18) return 0xFF;
    if (addr == 0xFF19) return m_ch2.nr4 | 0xBFu;

    if (addr == 0xFF1A) return m_ch3.nr30 | 0x7Fu;
    if (addr == 0xFF1B) return 0xFF;
    if (addr == 0xFF1C) return m_ch3.nr32 | 0x9Fu;
    if (addr == 0xFF1D) return 0xFF;
    if (addr == 0xFF1E) return m_ch3.nr34 | 0xBFu;

    if (addr == 0xFF20) return 0xFF;
    if (addr == 0xFF21) return m_ch4.nr42;
    if (addr == 0xFF22) return m_ch4.nr43;
    if (addr == 0xFF23) return m_ch4.nr44 | 0xBFu;

    if (addr == 0xFF24) return m_nr50;
    if (addr == 0xFF25) return m_nr51;
    if (addr == 0xFF26) {
        u8 v = m_nr52 & 0x80u;
        if (m_ch1.enabled) v |= 0x01u;
        if (m_ch2.enabled) v |= 0x02u;
        if (m_ch3.enabled) v |= 0x04u;
        if (m_ch4.enabled) v |= 0x08u;
        return v | 0x70u;
    }
    // Wave table 0xFF30–0xFF3F
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        return m_ch3.waveTable[addr - 0xFF30];
    }
    return 0xFF;
}

void APU::writeReg(u16 addr, u8 value) {
    // Ignore writes when APU is off, except NR52
    if (!(m_nr52 & 0x80u) && addr != 0xFF26) return;

    if (addr == 0xFF10) { m_ch1.nr0 = value; return; }
    if (addr == 0xFF11) { m_ch1.nr1 = value; return; }
    if (addr == 0xFF12) { m_ch1.nr2 = value; if (!(value & 0xF8u)) m_ch1.enabled = false; return; }
    if (addr == 0xFF13) { m_ch1.nr3 = value; return; }
    if (addr == 0xFF14) {
        m_ch1.nr4 = value;
        if (value & 0x80u) m_ch1.trigger();
        return;
    }
    if (addr == 0xFF16) { m_ch2.nr1 = value; return; }
    if (addr == 0xFF17) { m_ch2.nr2 = value; if (!(value & 0xF8u)) m_ch2.enabled = false; return; }
    if (addr == 0xFF18) { m_ch2.nr3 = value; return; }
    if (addr == 0xFF19) {
        m_ch2.nr4 = value;
        if (value & 0x80u) m_ch2.trigger();
        return;
    }
    if (addr == 0xFF1A) { m_ch3.nr30 = value; m_ch3.enabled = (value & 0x80u) != 0; return; }
    if (addr == 0xFF1B) { m_ch3.nr31 = value; return; }
    if (addr == 0xFF1C) { m_ch3.nr32 = value; return; }
    if (addr == 0xFF1D) { m_ch3.nr33 = value; return; }
    if (addr == 0xFF1E) {
        m_ch3.nr34 = value;
        if (value & 0x80u) m_ch3.trigger();
        return;
    }
    if (addr == 0xFF20) { m_ch4.nr41 = value; return; }
    if (addr == 0xFF21) { m_ch4.nr42 = value; if (!(value & 0xF8u)) m_ch4.enabled = false; return; }
    if (addr == 0xFF22) { m_ch4.nr43 = value; return; }
    if (addr == 0xFF23) {
        m_ch4.nr44 = value;
        if (value & 0x80u) m_ch4.trigger();
        return;
    }
    if (addr == 0xFF24) { m_nr50 = value; return; }
    if (addr == 0xFF25) { m_nr51 = value; return; }
    if (addr == 0xFF26) {
        // Bit 7: APU master power
        if (!(value & 0x80u)) {
            // Turning off APU clears all registers and disables all channels
            reset();
        }
        m_nr52 = (m_nr52 & 0x7Fu) | (value & 0x80u);
        return;
    }
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        m_ch3.waveTable[addr - 0xFF30] = value;
    }
}

} // namespace GB
