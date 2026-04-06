#include "SDL2Input.hpp"

#include <SDL.h>

namespace GB {

static Button mapKey(SDL_Keycode key, bool& mapped) {
    mapped = true;
    switch (key) {
        case SDLK_RIGHT: return Button::Right;
        case SDLK_LEFT:  return Button::Left;
        case SDLK_UP:    return Button::Up;
        case SDLK_DOWN:  return Button::Down;
        case SDLK_z:     return Button::A;
        case SDLK_x:     return Button::B;
        case SDLK_RSHIFT:return Button::Select;
        case SDLK_RETURN:return Button::Start;
        default:
            mapped = false;
            return Button::A;
    }
}

bool SDL2Input::pollAndDispatch(GameBoy& gb) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            return false;
        }

        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            bool mapped = false;
            Button b = mapKey(ev.key.keysym.sym, mapped);
            if (!mapped) continue;

            if (ev.type == SDL_KEYDOWN) {
                gb.pressButton(b);
            } else {
                gb.releaseButton(b);
            }
        }
    }
    return true;
}

} // namespace GB
