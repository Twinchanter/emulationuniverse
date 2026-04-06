cmake_minimum_required(VERSION 3.20)
project(EmulationUniverse VERSION 0.1.0 LANGUAGES CXX)

# ── C++17 required for std::array constexpr, std::unique_ptr, etc. ────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Strict warnings ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Source files ───────────────────────────────────────────────────────────────
set(EMU_SOURCES
    # CPU
    src/emulator/cpu/CPU.cpp
    src/emulator/cpu/Opcodes.cpp

    # Memory
    src/emulator/memory/MMU.cpp

    # Cartridge
    src/emulator/cartridge/Cartridge.cpp
    # MBC classes are header-only (template-free, simple enough inline)

    # PPU
    src/emulator/ppu/PPU.cpp

    # APU
    src/emulator/apu/APU.cpp

    # Timer
    src/emulator/timer/Timer.cpp

    # Joypad
    src/emulator/joypad/Joypad.cpp

    # Orchestrator
    src/emulator/GameBoy.cpp

    # Save states + debugger
    src/emulator/savestate/SaveState.cpp
    src/emulator/debug/Debugger.cpp
)

# ── Emulator library (reusable by tests and the frontend) ─────────────────────
add_library(gbemu_core STATIC ${EMU_SOURCES})

target_include_directories(gbemu_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emulator
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(emulationuniverse
    src/emulator/main.cpp
)

target_link_libraries(emulationuniverse PRIVATE gbemu_core)

# ── Optional: SDL2 frontend (set -DENABLE_SDL2=ON to activate) ────────────────
option(ENABLE_SDL2 "Build with SDL2 display and audio backend" OFF)
if(ENABLE_SDL2)
    find_package(SDL2 REQUIRED)
    target_compile_definitions(gbemu_core PUBLIC GB_SDL2_BACKEND)
    target_link_libraries(gbemu_core PUBLIC SDL2::SDL2)
    target_sources(emulationuniverse PRIVATE
        src/frontend/SDL2Display.cpp
        src/frontend/SDL2Audio.cpp
        src/frontend/SDL2Input.cpp
    )
endif()

# ── Test harness (set -DENABLE_TESTS=ON to build) ────────────────────────────
option(ENABLE_TESTS "Build unit tests" OFF)
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS emulationuniverse DESTINATION bin)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "=== EmulationUniverse Build Configuration ===")
message(STATUS "  C++ Standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  SDL2 backend : ${ENABLE_SDL2}")
message(STATUS "  Tests        : ${ENABLE_TESTS}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
