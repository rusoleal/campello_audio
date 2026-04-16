file(GLOB_RECURSE PI_SOURCES     src/pi/*.cpp)
file(GLOB_RECURSE PULSE_SOURCES  src/pulse/*.cpp)
file(GLOB_RECURSE ALSA_SOURCES   src/alsa/*.cpp)

# Try PulseAudio first; fall back to ALSA
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE libpulse-simple)
pkg_check_modules(ALSA  alsa)

if(PULSE_FOUND)
    add_library(${PROJECT_NAME} SHARED
        ${PI_SOURCES}
        ${PULSE_SOURCES}
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${PULSE_INCLUDE_DIRS} ${dr_libs_SOURCE_DIR} ${stb_SOURCE_DIR})
    target_link_libraries(${PROJECT_NAME} vector_math ${PULSE_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE CAMPELLO_AUDIO_BACKEND_PULSE)
elseif(ALSA_FOUND)
    add_library(${PROJECT_NAME} SHARED
        ${PI_SOURCES}
        ${ALSA_SOURCES}
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${ALSA_INCLUDE_DIRS} ${dr_libs_SOURCE_DIR} ${stb_SOURCE_DIR})
    target_link_libraries(${PROJECT_NAME} vector_math ${ALSA_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE CAMPELLO_AUDIO_BACKEND_ALSA)
else()
    message(FATAL_ERROR "No supported audio backend found. Install libpulse-dev or libasound2-dev.")
endif()
