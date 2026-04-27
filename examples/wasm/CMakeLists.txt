cmake_minimum_required(VERSION 3.22.1)

project(campello_audio_wasm_example)

set(CMAKE_CXX_STANDARD 20)

add_executable(wasm_example main.cpp)

target_link_libraries(wasm_example
    campello_audio
    vector_math
)

target_include_directories(wasm_example PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
)

# Emscripten-specific linker flags required for AudioWorklet and pthreads support.
set_target_properties(wasm_example PROPERTIES
    LINK_FLAGS "-sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sPTHREAD_POOL_SIZE=4 -sSHARED_MEMORY=1 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=CampelloAudioModule -sEXPORTED_FUNCTIONS=['_main','_startAudio','_stopAudio'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)
