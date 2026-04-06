#pragma once

#include "../emulator/core/interfaces/IDisplay.hpp"

#include <SDL.h>
#include <string>

namespace GB {

class SDL2Display final : public IDisplay {
public:
    SDL2Display();
    ~SDL2Display() override;

    void present(const FrameBuffer& frame) override;
    void setTitle(const std::string& title) override;
    bool isOpen() const override { return m_open; }

private:
    SDL_Window*   m_window  = nullptr;
    SDL_Renderer* m_renderer= nullptr;
    SDL_Texture*  m_texture = nullptr;
    bool          m_open    = true;
};

} // namespace GB
