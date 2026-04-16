file(GLOB_RECURSE PI_SOURCES   src/pi/*.cpp)
file(GLOB_RECURSE WA_SOURCES   src/wasapi/*.cpp)

add_library(${PROJECT_NAME} SHARED
    ${PI_SOURCES}
    ${WA_SOURCES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_link_libraries(${PROJECT_NAME}
    vector_math
    Ole32
    Avrt
    Winmm
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${dr_libs_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CAMPELLO_AUDIO_BACKEND_WASAPI
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
