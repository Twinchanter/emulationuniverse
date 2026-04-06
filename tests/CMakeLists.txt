add_executable(gbemu_system_tests
    SystemRomTests.cpp
)

add_executable(gbemu_cpu_micro_tests
    CpuMicroTests.cpp
)

target_link_libraries(gbemu_system_tests PRIVATE gbemu_core)
target_link_libraries(gbemu_cpu_micro_tests PRIVATE gbemu_core)

target_include_directories(gbemu_system_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emulator
)

target_include_directories(gbemu_cpu_micro_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emulator
)

add_test(NAME gbemu_system_tests COMMAND gbemu_system_tests)
add_test(NAME gbemu_cpu_micro_tests COMMAND gbemu_cpu_micro_tests)
