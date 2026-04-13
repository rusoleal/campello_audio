file(GLOB_RECURSE PI_SOURCES  src/pi/*.cpp)
file(GLOB_RECURSE CA_SOURCES  src/coreaudio/*.cpp)

add_library(${PROJECT_NAME} SHARED
    ${PI_SOURCES}
    ${CA_SOURCES}
)

target_link_libraries(${PROJECT_NAME}
    vector_math
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework CoreAudio"
    "-framework Foundation"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CAMPELLO_AUDIO_BACKEND_COREAUDIO)
