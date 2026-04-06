/**
 * IDisplay.hpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Renderer contract segregated from hardware emulation.
 * The PPU produces a FrameBuffer; any renderer (SDL2, OpenGL, headless test)
 * that implements this interface can consume it without modifying the PPU.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "../Types.hpp"

namespace GB {

class IDisplay {
public:
    virtual ~IDisplay() = default;

    /// Present a completed 160×144 frame to the output surface.
    virtual void present(const FrameBuffer& frame) = 0;

    /// Set window title (emulator name, ROM title, FPS, etc.)
    virtual void setTitle(const std::string& title) = 0;

    /// Returns false when the user has closed the window.
    virtual bool isOpen() const = 0;
};

} // namespace GB
