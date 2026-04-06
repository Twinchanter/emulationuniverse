/**
 * APU.hpp / APU.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Audio Processing Unit – Game Boy sound hardware skeleton.
 *
 * The GB APU has four sound channels:
 *   CH1 – Tone + Sweep     (square wave with frequency sweep)
 *   CH2 – Tone             (square wave)
 *   CH3 – Wave Output      (user-defined 4-bit samples)
 *   CH4 – Noise            (LFSR-based noise)
 *
 * This implementation provides:
 *   - Full register read/write interface (for game compatibility)
 *   - Correct channel enable/disable logic
 *   - Placeholder audio synthesis (zeroed output)
 *     A concrete SDL2 audio backend can be plugged in by extending this class.
 *
 * Extending for the all-in-one system:
 *   The APU is decoupled from any OS audio API.  A backend mixin (AudioSDL,
 *   AudioNull) can be injected at construction to route sample output without
 *   modifying the core logic here.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IComponent.hpp"
#include <array>
#include <functional>

namespace GB {

// Audio sample callback: called each time a pair of stereo samples is ready.
// Left and right are in range [-1.0, 1.0].
using AudioCallback = std::function<void(float left, float right)>;

class APU final : public IComponent {
public:
    explicit APU(AudioCallback audioOut = nullptr);
    ~APU() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override;
    void reset() override;
    u32  tick(u32 cycles) override;

    // ── I/O register access (MMU calls these) ─────────────────────────────────
    u8   readReg (u16 address) const;
    void writeReg(u16 address, u8 value);

private:
    // ── Square wave channel state ─────────────────────────────────────────────
    struct SquareChannel {
        u8   nr0 = 0;  // Sweep (CH1 only)
        u8   nr1 = 0;  // Duty / length
        u8   nr2 = 0;  // Envelope
        u8   nr3 = 0;  // Frequency low
        u8   nr4 = 0;  // Frequency high / control
        bool enabled = false;
        int  freqTimer  = 0;
        int  dutyStep   = 0;
        int  envTimer   = 0;
        u8   envVolume  = 0;
        int  sweepTimer = 0;
        u16  sweepFreq  = 0;
        int  lengthTimer= 0;

        void trigger();
        float sample() const;  // Returns {-1,0,1} based on duty and volume
    };

    // ── Wave channel state ────────────────────────────────────────────────────
    struct WaveChannel {
        u8   nr30 = 0; // Enable
        u8   nr31 = 0; // Length
        u8   nr32 = 0; // Output level
        u8   nr33 = 0; // Frequency low
        u8   nr34 = 0; // Frequency high / control
        bool enabled  = false;
        int  freqTimer = 0;
        int  sampleIdx = 0;
        int  lengthTimer = 0;
        std::array<u8, 16> waveTable{};

        void trigger();
        float sample() const;
    };

    // ── Noise channel state ───────────────────────────────────────────────────
    struct NoiseChannel {
        u8   nr41 = 0; // Length
        u8   nr42 = 0; // Envelope
        u8   nr43 = 0; // Polynomial counter
        u8   nr44 = 0; // Control
        bool enabled   = false;
        int  freqTimer = 0;
        u16  lfsr      = 0x7FFF;
        int  envTimer  = 0;
        u8   envVolume = 0;
        int  lengthTimer= 0;

        void trigger();
        float sample() const;
    };

    // ── Master control ────────────────────────────────────────────────────────
    u8  m_nr50 = 0;      // Master volume / VIN panning
    u8  m_nr51 = 0;      // Sound panning
    u8  m_nr52 = 0xF1;   // Sound on/off

    SquareChannel m_ch1;
    SquareChannel m_ch2;
    WaveChannel   m_ch3;
    NoiseChannel  m_ch4;

    AudioCallback m_audioOut;

    // Frame sequencer: clocked at 512 Hz (every 8192 T-cycles)
    int  m_frameSeqTimer = 0;
    u8   m_frameSeqStep  = 0;

    void tickFrameSequencer();
    void clockLengthCounters();
    void clockEnvelopes();
    void clockSweep();
    void mixAndOutput();
};

} // namespace GB
