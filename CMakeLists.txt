cmake_minimum_required(VERSION 3.20)
project(dtu351_macos VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_library(dtu351 STATIC src/dtu351.c src/firmware.c src/stream.c)
target_include_directories(dtu351 PUBLIC include)
target_link_libraries(dtu351 PUBLIC PkgConfig::LIBUSB)
target_compile_options(dtu351 PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(dtu351-probe tools/dtu351_probe.c)
target_link_libraries(dtu351-probe PRIVATE dtu351)

add_executable(dtu351-capture tools/dtu351_capture.c)
target_link_libraries(dtu351-capture PRIVATE dtu351 Threads::Threads)

add_executable(dtu351-bootstrap tools/dtu351_bootstrap.c)
target_link_libraries(dtu351-bootstrap PRIVATE dtu351)

add_executable(dtu351-descriptors tools/dtu351_descriptors.c)
target_link_libraries(dtu351-descriptors PRIVATE PkgConfig::LIBUSB)

add_executable(dtu351-rx-debug tools/dtu351_rx_debug.c)
target_link_libraries(dtu351-rx-debug PRIVATE dtu351)

enable_testing()
add_executable(dtu351-protocol-test tests/protocol_test.c)
target_link_libraries(dtu351-protocol-test PRIVATE dtu351)
add_test(NAME protocol COMMAND dtu351-protocol-test)
