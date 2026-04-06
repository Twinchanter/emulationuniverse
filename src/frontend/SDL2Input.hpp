#pragma once

#include "../emulator/GameBoy.hpp"

namespace GB {

class SDL2Input {
public:
    static bool pollAndDispatch(GameBoy& gb);
};

} // namespace GB
