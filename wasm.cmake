# Pthreads are required for std::thread, std::mutex, and AudioWorklet support.
# -msimd128 allows SSE/AVX intrinsics in dependencies (e.g. vector_math) to compile.
add_compile_options(-pthread -msimd128)

file(GLOB_RECURSE PI_SOURCES   src/pi/*.cpp)
file(GLOB_RECURSE WASM_SOURCES src/wasm/*.cpp)

add_library(${PROJECT_NAME} STATIC
    ${PI_SOURCES}
    ${WASM_SOURCES}
)

target_link_libraries(${PROJECT_NAME} vector_math)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/src
    ${dr_libs_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CAMPELLO_AUDIO_BACKEND_WASM
)

# Linker flags required for AudioWorklet and pthreads support.
target_link_options(${PROJECT_NAME} PUBLIC
    -sAUDIO_WORKLET=1
    -sWASM_WORKERS=1
    -sPTHREAD_POOL_SIZE=4
    -sSHARED_MEMORY=1
    -sALLOW_MEMORY_GROWTH=1
)
