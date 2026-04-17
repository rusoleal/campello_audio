cmake_minimum_required(VERSION 3.22.1)

project(campello_audio VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

include_directories(inc)

configure_file(src/campello_audio_config.h.in campello_audio_config.h)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    vector_math
    GIT_REPOSITORY https://github.com/rusoleal/vector_math.git
    GIT_TAG        v0.3.5
)
FetchContent_MakeAvailable(vector_math)

# dr_libs (dr_wav + dr_mp3) — header-only, no CMakeLists.txt
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        wav-0.14.5  # latest release tag
)
FetchContent_GetProperties(dr_libs)
if(NOT dr_libs_POPULATED)
    FetchContent_Populate(dr_libs)
endif()

# stb (stb_vorbis) — header-only, no CMakeLists.txt
# stb has no release tags; master is the conventional way to consume it.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    include(android.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    include(macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    include(ios.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    include(windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(linux.cmake)
endif()

# BUILD_TESTS=ON: Universal tests (constants, descriptors, math), no audio device required
# BUILD_INTEGRATION_TESTS=ON: Platform audio tests, requires device, implies BUILD_TESTS=ON
option(BUILD_TESTS             "Build unit tests (universal, no audio device)"    OFF)
option(BUILD_INTEGRATION_TESTS "Build audio integration tests (per-platform)"    OFF)

if(BUILD_INTEGRATION_TESTS)
    set(BUILD_TESTS ON)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# BUILD_EXAMPLES=ON: Platform-specific example applications
option(BUILD_EXAMPLES "Build example applications" OFF)

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_subdirectory(examples/windows)
endif()

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    add_subdirectory(examples/apple)
endif()

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    add_subdirectory(examples/android)
endif()
