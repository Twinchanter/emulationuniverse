/**
 * PPU.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Pixel Processing Unit – Game Boy LCD controller.
 *
 * The PPU runs in lock-step with the CPU (same clock).  Each scanline takes
 * 456 T-cycles; the full 154-line frame (144 visible + 10 VBlank) is 70224
 * T-cycles ≈ 59.73 fps.
 *
 * Mode timing on each scanline (T-cycles):
 *   0–79    Mode 2 (OAM Scan)
 *   80–251  Mode 3 (Drawing)  [variable length, simplified to 172 here]
 *   252–455 Mode 0 (HBlank)
 *   Lines 144–153: Mode 1 (VBlank)
 *
 * Pixel colours are emitted to a FrameBuffer as RGBA.  A concrete IDisplay
 * presents the buffer to the screen.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../core/interfaces/IComponent.hpp"
#include "../core/interfaces/IDisplay.hpp"
#include <array>
#include <functional>

namespace GB {

// Number of OAM entries (sprites)
static constexpr int OAM_ENTRIES = 40;

// Each OAM entry is 4 bytes: Y, X, Tile#, Flags
struct OAMEntry {
    u8 y;
    u8 x;
    u8 tile;
    u8 flags;
};

// Sprite attribute flags
static constexpr u8 OBJ_PRIORITY = 0x80; // 0=above BG, 1=behind BG colour 1-3
static constexpr u8 OBJ_FLIP_Y   = 0x40;
static constexpr u8 OBJ_FLIP_X   = 0x20;
static constexpr u8 OBJ_PALETTE  = 0x10; // 0=OBP0, 1=OBP1

class PPU final : public IComponent {
public:
    /**
     * @param display  Optional renderer; may be nullptr for headless operation.
     * @param irqCallback  Called whenever the PPU needs to raise VBlank/STAT IRQ.
     */
    explicit PPU(IDisplay* display,
                 std::function<void(Interrupt)> irqCallback);
    ~PPU() override = default;

    // ── IComponent ──────────────────────────────────────────────────────────
    void init()  override;
    void reset() override;
    u32  tick(u32 cycles) override;

    // ── MMU-facing memory access ─────────────────────────────────────────────
    u8   readVRAM (u16 address) const;
    void writeVRAM(u16 address, u8 value);
    u8   readOAM  (u16 address) const;
    void writeOAM (u16 address, u8 value);

    // ── I/O register access (called by MMU) ──────────────────────────────────
    u8   readReg (u16 address) const;
    void writeReg(u16 address, u8 value);

    // ── Frame buffer output ──────────────────────────────────────────────────
    const FrameBuffer& frameBuffer() const { return m_frameBuffer; }

private:
    // ── Rendering pipeline ────────────────────────────────────────────────────
    void renderScanline();
    void renderBackground(int line);
    void renderWindow(int line);
    void renderSprites(int line);

    /// Map a 2-bit colour index through a palette register to an RGBA Pixel.
    Pixel applyPalette(u8 colourId, u8 paletteReg) const;

    // ── LCD control helpers ───────────────────────────────────────────────────
    inline bool lcdEnabled()       const { return (m_lcdc & 0x80u) != 0; }
    inline bool windowEnabled()    const { return (m_lcdc & 0x20u) != 0; }
    inline bool spritesEnabled()   const { return (m_lcdc & 0x02u) != 0; }
    inline bool bgEnabled()        const { return (m_lcdc & 0x01u) != 0; }
    inline u16  bgTileMapBase()    const { return (m_lcdc & 0x08u) ? 0x9C00u : 0x9800u; }
    inline u16  winTileMapBase()   const { return (m_lcdc & 0x40u) ? 0x9C00u : 0x9800u; }
    inline u16  bgTileDataBase()   const { return (m_lcdc & 0x10u) ? 0x8000u : 0x8800u; }
    inline bool tallSprites()      const { return (m_lcdc & 0x04u) != 0; }

    /// Fetch a tile row (2 bytes → 8 pixels of 2-bit colour indices).
    void fetchTileRow(u16 tileDataBase, u8 tileId, int row, u8& lo, u8& hi) const;

    // ── Update PPU mode and fire interrupts ───────────────────────────────────
    void setMode(PPUMode mode);
    void updateCoincidenceFlag();
    void updateStatInterruptLine();
    int  computeMode3CyclesForCurrentLine() const;

    // ── Internal state ────────────────────────────────────────────────────────
    IDisplay*                      m_display;
    std::function<void(Interrupt)> m_irqCallback;

    std::array<u8, 0x2000> m_vram{};       // 8 KB VRAM 0x8000–0x9FFF
    std::array<u8, 0xA0>   m_oam{};        // 160 B OAM 0xFE00–0xFE9F

    // LCD registers
    u8 m_lcdc = 0x91; // LCD Control         (power-on default)
    u8 m_stat = 0x85; // LCD Status
    u8 m_scy  = 0;    // BG Scroll Y
    u8 m_scx  = 0;    // BG Scroll X
    u8 m_ly   = 0;    // Current scanline
    u8 m_lyc  = 0;    // LY Compare
    u8 m_bgp  = 0xFC; // BG Palette
    u8 m_obp0 = 0xFF; // OBJ Palette 0
    u8 m_obp1 = 0xFF; // OBJ Palette 1
    u8 m_wy   = 0;    // Window Y
    u8 m_wx   = 0;    // Window X

    int    m_cycleCounter  = 0;
    int    m_modeDotsRemaining = 80;
    int    m_windowLine    = 0; // Internal window line counter
    PPUMode m_mode         = PPUMode::OAMScan;
    bool   m_statLineHigh  = false;
    bool   m_lcdStartupPending = false;
    bool   m_firstEnabledLine = false;
    int    m_currentMode3Cycles = 172;
    int    m_currentHBlankCycles = 204;

    // ── Fetcher state machine (dot-level timing) ────────────────────────────
    int    m_fetcherPhase = 0;        // 0-7: which dot within 8-dot tile fetch
    int    m_fetcherDiscardCycles = 0;// SCX-dependent prefetch discard
    int    m_mode3DotsConsumed = 0;   // Track actual dots used in mode3
    int    m_spritesFetched = 0;      // Count of sprites fetched this line

    FrameBuffer m_frameBuffer{};

    // DMG grey palette: 0=White, 1=LightGray, 2=DarkGray, 3=Black
    static constexpr Pixel DMG_PALETTE[4] = {
        {0xFF, 0xFF, 0xFF, 0xFF}, // White
        {0xAA, 0xAA, 0xAA, 0xFF}, // Light Gray
        {0x55, 0x55, 0x55, 0xFF}, // Dark Gray
        {0x00, 0x00, 0x00, 0xFF}  // Black
    };
};

} // namespace GB
