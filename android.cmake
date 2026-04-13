file(GLOB_RECURSE PI_SOURCES  src/pi/*.cpp)
file(GLOB_RECURSE AA_SOURCES  src/aaudio/*.cpp)

add_library(${PROJECT_NAME} SHARED
    ${PI_SOURCES}
    ${AA_SOURCES}
)

# AAudio requires API level 26+; OpenSL ES is the fallback for API < 26
target_link_libraries(${PROJECT_NAME}
    vector_math
    aaudio
    OpenSLES
    log
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CAMPELLO_AUDIO_BACKEND_AAUDIO)
