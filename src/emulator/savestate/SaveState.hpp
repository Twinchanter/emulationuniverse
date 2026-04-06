#pragma once

#include "../GameBoy.hpp"

#include <string>

namespace GB {

class SaveState {
public:
    // Snapshot selected emulator state to a binary file.
    static bool saveToFile(const GameBoy& gb, const std::string& path, std::string* errorOut = nullptr);

    // Restore emulator state from a binary file.
    static bool loadFromFile(GameBoy& gb, const std::string& path, std::string* errorOut = nullptr);
};

} // namespace GB
