cmake_minimum_required(VERSION 3.16)
project(ZenithMemory
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "A modern C++ memory allocator library for game engines"
)

# ── Options ──────────────────────────────────────────────────────────
# When someone adds ZenithMemory via add_subdirectory() or FetchContent,
# they usually don't want the demo/benchmark executables. This option
# lets them skip those. It defaults to ON when this is the top-level
# project, OFF when it's a subdirectory.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(ZENITH_IS_TOP_LEVEL ON)
else()
    set(ZENITH_IS_TOP_LEVEL OFF)
endif()

option(ZENITH_BUILD_EXAMPLES  "Build demo and benchmark executables" ${ZENITH_IS_TOP_LEVEL})
option(ZENITH_INSTALL         "Generate install targets"             ${ZENITH_IS_TOP_LEVEL})

# ── C++ Standard ─────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)        # No GNU extensions — pure ISO C++

# ── Compiler Warnings (only when building this project directly) ─────
add_library(ZenithMemory STATIC
    src/LinearAllocator.cpp
    src/StackAllocator.cpp
    src/PoolAllocator.cpp
    src/FreeListAllocator.cpp
)

# Create an alias so FetchContent/add_subdirectory users can write:
#   target_link_libraries(MyApp PRIVATE Zenith::Memory)
# This is the modern CMake convention (namespace::target).
add_library(Zenith::Memory ALIAS ZenithMemory)

# Set the export name so installed targets are also Zenith::Memory
# (not Zenith::ZenithMemory). This keeps the name consistent whether
# the library is consumed via add_subdirectory or find_package.
set_target_properties(ZenithMemory PROPERTIES EXPORT_NAME Memory)

# PUBLIC include path:
#   - At BUILD time: use the source tree's include/ folder.
#   - At INSTALL time: use the installed include/ folder.
# The $<BUILD_INTERFACE> / $<INSTALL_INTERFACE> generator expressions
# handle both cases correctly.
target_include_directories(ZenithMemory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Encode the C++ standard requirement on the target itself.
# Any project linking against ZenithMemory will automatically
# get C++17 (or higher) enabled — no manual set(CMAKE_CXX_STANDARD) needed.
target_compile_features(ZenithMemory PUBLIC cxx_std_17)

# Keep project-specific warnings off downstream consumers.
if(ZENITH_IS_TOP_LEVEL)
    target_compile_options(ZenithMemory PRIVATE
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wshadow -Wdouble-promotion
    )
endif()

# ── Demo & Benchmark (only when building standalone) ─────────────────
if(ZENITH_BUILD_EXAMPLES)
    add_executable(ZenithDemo demo/main.cpp)
    target_link_libraries(ZenithDemo PRIVATE ZenithMemory)

    add_executable(ZenithBenchmark benchmark/main.cpp)
    target_link_libraries(ZenithBenchmark PRIVATE ZenithMemory)
endif()

# ── Install Targets ──────────────────────────────────────────────────
# This section makes `cmake --install .` work. It puts:
#   - libZenithMemory.a  → <prefix>/lib/
#   - include/Zenith/*   → <prefix>/include/Zenith/
#   - CMake config files → <prefix>/lib/cmake/ZenithMemory/
#
# After installing, other projects can do:
#   find_package(ZenithMemory REQUIRED)
#   target_link_libraries(MyApp PRIVATE Zenith::Memory)
if(ZENITH_INSTALL)
    include(GNUInstallDirs)       # Provides CMAKE_INSTALL_LIBDIR, etc.
    include(CMakePackageConfigHelpers)
    set(ZENITH_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ZenithMemory")

    # Install the library binary
    install(TARGETS ZenithMemory
        EXPORT ZenithMemoryTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Install the header files
    install(DIRECTORY include/Zenith
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Generate and install the export file (ZenithMemoryTargets.cmake)
    # This file defines the Zenith::Memory imported target.
    install(EXPORT ZenithMemoryTargets
        FILE ZenithMemoryTargets.cmake
        NAMESPACE Zenith::
        DESTINATION ${ZENITH_CMAKE_INSTALL_DIR}
    )

    # Generate the package config file used by find_package().
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ZenithMemoryConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ZenithMemoryConfig.cmake"
        INSTALL_DESTINATION "${ZENITH_CMAKE_INSTALL_DIR}"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    # Generate the package version file.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ZenithMemoryConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install the generated package config files.
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ZenithMemoryConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ZenithMemoryConfigVersion.cmake"
        DESTINATION ${ZENITH_CMAKE_INSTALL_DIR}
    )
endif()

# ── Build Type Defaults ──────────────────────────────────────────────
get_property(ZENITH_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ZENITH_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
