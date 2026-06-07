# Modern CMake build for the C++ project.
cmake_minimum_required(VERSION 3.20)

project(
    cpp_app
    VERSION 1.0.0
    DESCRIPTION "A modern C++ project"
    LANGUAGES CXX
)

# Use C++23 with no compiler-specific extensions.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Release build when none is specified (single-config generators).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# Export compile_commands.json for editors / clang-tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Symlink the generated compile_commands.json into the project root so clangd
# (and other editor tooling) picks it up without extra configuration.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  file(CREATE_LINK
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
  )
endif()

add_executable(main
  main.cpp #main
  player.cpp
)

# The store is thread-safe; link the platform threading library.
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)

# Enable a healthy set of warnings per compiler.
if(MSVC)
  target_compile_options(main PRIVATE /W4 /permissive-)
else()
  target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic)
endif()
