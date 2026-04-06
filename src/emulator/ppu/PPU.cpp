/**
 * PPU.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Pixel Processing Unit implementation.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "PPU.hpp"
#include <algorithm>
#include <cstring>

namespace GB {

// Static palette initialisation (declared constexpr in header, defined here)
constexpr Pixel PPU::DMG_PALETTE[4];

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

PPU::PPU(IDisplay* display, std::function<void(Interrupt)> irqCallback)
    : m_display(display), m_irqCallback(std::move(irqCallback)) {}

void PPU::init()  { reset(); }

void PPU::reset() {
    m_vram.fill(0); m_oam.fill(0);
    m_lcdc = 0x91; m_stat = 0x85;
    m_scy = m_scx = m_ly = m_lyc = 0;
    m_bgp = 0xFC; m_obp0 = m_obp1 = 0xFF;
    m_wy = m_wx = 0;
    m_cycleCounter = 0;
    m_modeDotsRemaining = 80;
    m_windowLine   = 0;
    m_mode         = PPUMode::OAMScan;
    m_statLineHigh = false;
    m_lcdStartupPending = false;
    m_firstEnabledLine = false;
    m_currentMode3Cycles = 172;
    m_currentHBlankCycles = 204;
    m_fetcherPhase = 0;
    m_fetcherDiscardCycles = 0;
    m_mode3DotsConsumed = 0;
    m_spritesFetched = 0;
    m_frameBuffer.fill({0xFF, 0xFF, 0xFF, 0xFF}); // White screen

    setMode(PPUMode::OAMScan);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick – advance PPU state machine by T-cycles
// ─────────────────────────────────────────────────────────────────────────────

u32 PPU::tick(u32 cycles) {
    if (!lcdEnabled()) {
        // LCD disabled: LY stays 0, mode stays HBlank per Pan Docs
        m_ly = 0;
        m_cycleCounter = 0;
        m_modeDotsRemaining = 204;
        setMode(PPUMode::HBlank);
        return cycles;
    }

    for (u32 i = 0; i < cycles; ++i) {
        if (m_modeDotsRemaining > 0) {
            --m_modeDotsRemaining;
        }
        if (m_modeDotsRemaining > 0) {
            continue;
        }

        switch (m_mode) {
            case PPUMode::OAMScan:
                m_currentMode3Cycles = computeMode3CyclesForCurrentLine();
                m_currentHBlankCycles = 456 - 80 - m_currentMode3Cycles;
                // Initialize fetcher state for this line
                m_fetcherPhase = 0;
                m_fetcherDiscardCycles = static_cast<int>(m_scx & 0x07u);
                m_mode3DotsConsumed = 0;
                m_spritesFetched = 0;
                setMode(PPUMode::Drawing);
                m_modeDotsRemaining = m_currentMode3Cycles;
                break;

            case PPUMode::Drawing:
                renderScanline();
                if (m_firstEnabledLine) {
                    // First post-enable line has a 72T startup segment before mode 3.
                    m_currentHBlankCycles = 456 - 72 - m_currentMode3Cycles;
                    m_firstEnabledLine = false;
                }
                setMode(PPUMode::HBlank);
                m_modeDotsRemaining = m_currentHBlankCycles;
                break;

            case PPUMode::HBlank:
                if (m_lcdStartupPending) {
                    m_lcdStartupPending = false;
                    m_firstEnabledLine = true;
                    m_currentMode3Cycles = computeMode3CyclesForCurrentLine();
                    // Initialize fetcher state for startup line
                    m_fetcherPhase = 0;
                    m_fetcherDiscardCycles = static_cast<int>(m_scx & 0x07u);
                    m_mode3DotsConsumed = 0;
                    m_spritesFetched = 0;
                    setMode(PPUMode::Drawing);
                    m_modeDotsRemaining = m_currentMode3Cycles;
                    break;
                }

                ++m_ly;
                updateCoincidenceFlag();
                updateStatInterruptLine();

                if (m_ly == 144) {
                    setMode(PPUMode::VBlank);
                    m_modeDotsRemaining = 456;
                    m_irqCallback(Interrupt::VBlank);
                    m_windowLine = 0;
                    if (m_display) {
                        m_display->present(m_frameBuffer);
                    }
                } else {
                    setMode(PPUMode::OAMScan);
                    m_modeDotsRemaining = 80;
                }
                break;

            case PPUMode::VBlank:
                ++m_ly;
                updateCoincidenceFlag();
                updateStatInterruptLine();

                if (m_ly > 153) {
                    m_ly = 0;
                    updateCoincidenceFlag();
                    setMode(PPUMode::OAMScan);
                    m_modeDotsRemaining = 80;
                } else {
                    m_modeDotsRemaining = 456;
                }
                break;
        }
    }

    return cycles;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode update + STAT register maintenance
// ─────────────────────────────────────────────────────────────────────────────

void PPU::setMode(PPUMode mode) {
    m_mode = mode;
    // Update lower 2 bits of STAT with current mode
    m_stat = (m_stat & 0xFCu) | static_cast<u8>(mode);
    updateCoincidenceFlag();
    updateStatInterruptLine();
}

void PPU::updateCoincidenceFlag() {
    if (!lcdEnabled()) {
        return;
    }

    if (m_ly == m_lyc) {
        m_stat |= 0x04u;
    } else {
        m_stat &= ~0x04u;
    }
}

void PPU::updateStatInterruptLine() {
    if (!lcdEnabled()) {
        // While LCD is off, keep the internal STAT signal latched.
        return;
    }

    const bool mode2Source = ((m_stat & 0x20u) != 0) && (
        m_mode == PPUMode::OAMScan ||
        // DMG/SGB quirk: line 144 transition to VBlank can also satisfy mode2 source.
        (m_mode == PPUMode::VBlank && m_ly == 144)
    );

    const bool line = lcdEnabled() && (
        (((m_stat & 0x08u) != 0) && m_mode == PPUMode::HBlank) ||
        (((m_stat & 0x10u) != 0) && m_mode == PPUMode::VBlank) ||
        mode2Source ||
        (((m_stat & 0x40u) != 0) && ((m_stat & 0x04u) != 0))
    );

    if (!m_statLineHigh && line) {
        m_irqCallback(Interrupt::LCDStat);
    }

    m_statLineHigh = line;
}

int PPU::computeMode3CyclesForCurrentLine() const {
    // Dot-level fetcher model: mode3 duration depends on SCX phase and sprites
    int mode3 = 172;  // Base duration for 160 pixels at 8T per tile
    
    const int scxMod = static_cast<int>(m_scx & 0x07u);

    // Pan Docs: At start of Mode 3, rendering pauses for SCX%8 dots while
    // discarding scrolled pixels. This directly extends Mode 3 by SCX%8 T-cycles.
    mode3 += scxMod;

    // OBJ penalty algorithm (Pan Docs):
    // For each OBJ overlapping this scanline (up to 10):
    //   - OAM X==0: 11-dot penalty (completely off left of screen)
    //   - Otherwise: 6 dots (tile fetch) + max(0, (7 - tile_pixel_offset) - 2)
    //     where tile_pixel_offset = (oamX - 1 + scxMod) & 7
    //     Each BG background "tile" consulted for a second OBJ that falls in the
    //     same tile incurs 0 extra dots for the BG portion (tile already in cache).
    if (spritesEnabled() && m_ly < SCREEN_HEIGHT) {
        const int spriteHeight = tallSprites() ? 16 : 8;
        int considered = 0;
        // Track which BG tiles have been "consulted" to implement tile-cache logic
        bool tileConsidered[21] = {};  // tiles 0..20 cover 160 pixels with 8px tiles

        for (int i = 0; i < OAM_ENTRIES && considered < 10; ++i) {
            const u16 base = static_cast<u16>(i * 4);
            const int oamY = static_cast<int>(m_oam[base + 0]);
            const int oamX = static_cast<int>(m_oam[base + 1]);
            // Y check: sprite is visible on line m_ly if oamY-16 <= m_ly < oamY-16+height
            const int spriteScreenY = oamY - 16;
            if (static_cast<int>(m_ly) < spriteScreenY ||
                static_cast<int>(m_ly) >= spriteScreenY + spriteHeight) {
                continue;
            }

            ++considered;

            if (oamX == 0) {
                // Completely off-screen left: 11-dot penalty
                mode3 += 11;
            } else {
                // Tile index of the BG tile that contains this sprite's leftmost pixel
                // The pixel in that tile: (oamX - 1 + scxMod) % 8
                const int tilePixelOffset = (oamX - 1 + scxMod) & 0x07;
                const int tileIdx        = (oamX - 1 + scxMod) >> 3;

                // 6-dot flat penalty (OBJ tile fetch)
                int penalty = 6;

                // BG fetch wait: only if this is the first OBJ considering this tile
                if (tileIdx < 21 && !tileConsidered[tileIdx]) {
                    tileConsidered[tileIdx] = true;
                    // Pixels strictly to the right of The Pixel in the tile: 7 - tilePixelOffset
                    const int bgWait = std::max(0, (7 - tilePixelOffset) - 2);
                    penalty += bgWait;
                }

                mode3 += penalty;
            }
        }
    }

    return mode3;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scanline rendering
// ─────────────────────────────────────────────────────────────────────────────

void PPU::renderScanline() {
    int line = m_ly;
    if (line >= SCREEN_HEIGHT) return;

    if (bgEnabled())     renderBackground(line);
    if (windowEnabled()) renderWindow(line);
    if (spritesEnabled())renderSprites(line);
}

void PPU::renderBackground(int line) {
    u16 tileMap  = bgTileMapBase();
    u16 tileData = bgTileDataBase();

    int screenY = line;
    int bgY     = (screenY + m_scy) & 0xFF; // Wrap within 256-pixel BG map
    int tileRow = bgY / 8;                  // Which row of tiles
    int tilePixelY = bgY & 7;               // Which row within the tile (0-7)

    for (int screenX = 0; screenX < SCREEN_WIDTH; ++screenX) {
        int bgX       = (screenX + m_scx) & 0xFF;
        int tileCol   = bgX / 8;
        int tilePixelX = bgX & 7;

        // Fetch tile ID from the background tile map
        u16 mapAddr = tileMap + static_cast<u16>(tileRow * 32 + tileCol);
        u8  tileId  = m_vram[mapAddr - 0x8000u];

        // Fetch the two data bytes for this pixel row
        u8 lo, hi;
        fetchTileRow(tileData, tileId, tilePixelY, lo, hi);

        // Extract the 2-bit colour index for this pixel (bit 7 = left-most pixel)
        u8 bit      = static_cast<u8>(7 - tilePixelX);
        u8 colourId = static_cast<u8>(((hi >> bit) & 1u) << 1u | ((lo >> bit) & 1u));

        int idx = line * SCREEN_WIDTH + screenX;
        m_frameBuffer[idx] = applyPalette(colourId, m_bgp);
    }
}

void PPU::renderWindow(int line) {
    // Window is only drawn when it overlaps the current scanline
    if (m_wy > line || m_wx < 7) return;
    if (!windowEnabled()) return;

    u16 tileMap  = winTileMapBase();
    u16 tileData = bgTileDataBase();
    int winY     = m_windowLine;
    int tileRow  = winY / 8;
    int tilePixelY = winY & 7;

    bool drewPixel = false;
    for (int screenX = 0; screenX < SCREEN_WIDTH; ++screenX) {
        int winX = screenX - (m_wx - 7);
        if (winX < 0) continue;

        int tileCol    = winX / 8;
        int tilePixelX = winX & 7;

        u16 mapAddr = tileMap + static_cast<u16>(tileRow * 32 + tileCol);
        u8  tileId  = m_vram[mapAddr - 0x8000u];

        u8 lo, hi;
        fetchTileRow(tileData, tileId, tilePixelY, lo, hi);

        u8 bit      = static_cast<u8>(7 - tilePixelX);
        u8 colourId = static_cast<u8>(((hi >> bit) & 1u) << 1u | ((lo >> bit) & 1u));

        int idx = line * SCREEN_WIDTH + screenX;
        m_frameBuffer[idx] = applyPalette(colourId, m_bgp);
        drewPixel = true;
    }
    if (drewPixel) ++m_windowLine;
}

void PPU::renderSprites(int line) {
    int spriteHeight = tallSprites() ? 16 : 8;
    int spriteCount  = 0;

    // Hardware limit: maximum 10 sprites per scanline
    for (int i = 0; i < OAM_ENTRIES && spriteCount < 10; ++i) {
        u16 base = static_cast<u16>(i * 4);
        int y    = m_oam[base + 0] - 16;  // OAM Y is offset by 16
        int x    = m_oam[base + 1] - 8;   // OAM X is offset by 8
        u8  tile = m_oam[base + 2];
        u8  flags= m_oam[base + 3];

        // Check if this sprite falls on the current scanline
        if (line < y || line >= y + spriteHeight) continue;
        ++spriteCount;

        int spriteRow = line - y;
        if (flags & OBJ_FLIP_Y) spriteRow = spriteHeight - 1 - spriteRow;

        // For 8×16 sprites, bit 0 of the tile number is ignored
        if (tallSprites()) tile &= 0xFEu;

        u8 lo, hi;
        u16 tileAddr = 0x8000u + static_cast<u16>(tile) * 16u
                                + static_cast<u16>(spriteRow) * 2u;
        lo = m_vram[tileAddr     - 0x8000u];
        hi = m_vram[tileAddr + 1 - 0x8000u];

        for (int px = 0; px < 8; ++px) {
            int screenX = x + px;
            if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;

            int bitPos  = (flags & OBJ_FLIP_X) ? px : (7 - px);
            u8 colourId = static_cast<u8>(((hi >> bitPos) & 1u) << 1u | ((lo >> bitPos) & 1u));

            if (colourId == 0) continue; // Colour 0 is always transparent for sprites

            u8  palette = (flags & OBJ_PALETTE) ? m_obp1 : m_obp0;
            int idx     = line * SCREEN_WIDTH + screenX;

            // Behind-BG priority: skip if BG pixel is non-zero
            if ((flags & OBJ_PRIORITY) && (m_frameBuffer[idx].r != 0xFF)) continue;

            m_frameBuffer[idx] = applyPalette(colourId, palette);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tile data fetcher
// ─────────────────────────────────────────────────────────────────────────────

void PPU::fetchTileRow(u16 tileDataBase, u8 tileId, int row, u8& lo, u8& hi) const {
    u16 addr;
    if (tileDataBase == 0x8000u) {
        // Unsigned addressing (0x0000–0x00FF)
        addr = tileDataBase + static_cast<u16>(tileId) * 16u + static_cast<u16>(row) * 2u;
    } else {
        // Signed addressing: tile IDs 128–255 map to -128..-1 (base 0x9000)
        s16 signedId = static_cast<s8>(tileId); // sign-extend from 8-bit
        addr = static_cast<u16>(0x9000 + signedId * 16 + row * 2);
    }
    lo = m_vram[addr     - 0x8000u];
    hi = m_vram[addr + 1 - 0x8000u];
}

// ─────────────────────────────────────────────────────────────────────────────
// Palette application
// ─────────────────────────────────────────────────────────────────────────────

Pixel PPU::applyPalette(u8 colourId, u8 paletteReg) const {
    // Each palette byte packs 4 × 2-bit colour indices:
    //   bits 1-0 = colour for index 0
    //   bits 3-2 = colour for index 1  etc.
    u8 shade = (paletteReg >> (colourId * 2u)) & 0x03u;
    return DMG_PALETTE[shade];
}

// ─────────────────────────────────────────────────────────────────────────────
// Memory-mapped register access
// ─────────────────────────────────────────────────────────────────────────────

u8 PPU::readVRAM(u16 addr) const {
    // VRAM inaccessible during mode 3
    if (m_mode == PPUMode::Drawing) return 0xFF;
    return m_vram[addr - 0x8000u];
}
void PPU::writeVRAM(u16 addr, u8 value) {
    if (m_mode != PPUMode::Drawing) m_vram[addr - 0x8000u] = value;
}

u8 PPU::readOAM(u16 addr) const {
    if (m_mode == PPUMode::OAMScan || m_mode == PPUMode::Drawing) return 0xFF;
    return m_oam[addr - OAM_START];
}
void PPU::writeOAM(u16 addr, u8 value) {
    if (m_mode != PPUMode::OAMScan && m_mode != PPUMode::Drawing) {
        m_oam[addr - OAM_START] = value;
    }
}

u8 PPU::readReg(u16 addr) const {
    switch (addr) {
        case REG_LCDC: return m_lcdc;
        case REG_STAT: return m_stat | 0x80u; // Bit 7 always set
        case REG_SCY:  return m_scy;
        case REG_SCX:  return m_scx;
        case REG_LY:   return m_ly;
        case REG_LYC:  return m_lyc;
        case REG_BGP:  return m_bgp;
        case REG_OBP0: return m_obp0;
        case REG_OBP1: return m_obp1;
        case REG_WY:   return m_wy;
        case REG_WX:   return m_wx;
        default:       return 0xFF;
    }
}

void PPU::writeReg(u16 addr, u8 value) {
    switch (addr) {
        case REG_LCDC:
        {
            const bool wasEnabled = lcdEnabled();
            m_lcdc = value;
            const bool nowEnabled = lcdEnabled();

            if (wasEnabled && !nowEnabled) {
                m_cycleCounter = 0;
                m_modeDotsRemaining = 204;
                m_ly = 0;
                m_windowLine = 0;
                m_lcdStartupPending = false;
                m_firstEnabledLine = false;
                m_currentMode3Cycles = 172;
                m_currentHBlankCycles = 204;
                // Reset fetcher state
                m_fetcherPhase = 0;
                m_fetcherDiscardCycles = 0;
                m_mode3DotsConsumed = 0;
                m_spritesFetched = 0;
                setMode(PPUMode::HBlank);
            } else if (!wasEnabled && nowEnabled) {
                m_cycleCounter = 0;
                // DMG startup quirk: first post-enable pre-draw delay is ~18 M-cycles.
                m_modeDotsRemaining = 72;
                m_ly = 0;
                m_windowLine = 0;
                m_lcdStartupPending = true;
                m_firstEnabledLine = false;
                m_currentMode3Cycles = 172;
                m_currentHBlankCycles = 204;
                // Initialize fetcher state
                m_fetcherPhase = 0;
                m_fetcherDiscardCycles = static_cast<int>(m_scx & 0x07u);
                m_mode3DotsConsumed = 0;
                m_spritesFetched = 0;
                setMode(PPUMode::HBlank);
            } else {
                updateCoincidenceFlag();
                updateStatInterruptLine();
            }
            break;
        }
        case REG_STAT:
            m_stat = (m_stat & 0x07u) | (value & 0x78u); // Lower 3 bits read-only
            updateStatInterruptLine();
            break;
        case REG_SCY:  m_scy  = value; break;
        case REG_SCX:  m_scx  = value; break;
        case REG_LY:
            m_ly = 0; // Writing LY resets it
            updateCoincidenceFlag();
            updateStatInterruptLine();
            break;
        case REG_LYC:
            m_lyc = value;
            updateCoincidenceFlag();
            updateStatInterruptLine();
            break;
        case REG_BGP:  m_bgp  = value; break;
        case REG_OBP0: m_obp0 = value; break;
        case REG_OBP1: m_obp1 = value; break;
        case REG_WY:   m_wy   = value; break;
        case REG_WX:   m_wx   = value; break;
    }
}

} // namespace GB
