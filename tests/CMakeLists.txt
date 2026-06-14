include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Universal tests — no audio device required, no platform library linkage.
#
# Platform-independent source files (src/pi/) are compiled directly into the
# test binary so that universal tests can exercise WavSource, OggSource,
# Mp3Source, MixerData, VoiceManager, etc. without linking the full
# campello_audio shared/static library.
# ---------------------------------------------------------------------------
add_executable(campello_audio_universal_tests
    # Test sources
    universal/test_constants.cpp
    universal/test_descriptors.cpp
    universal/test_math.cpp
    universal/test_sound_handle.cpp
    universal/test_decoders.cpp
    universal/test_mixer.cpp
    universal/test_audio3d.cpp
    universal/test_random_source.cpp
    universal/test_music_track.cpp
    universal/test_audio_stream.cpp
    universal/test_mini_notation.cpp
    universal/test_pattern.cpp
    universal/test_pattern_bank.cpp
    universal/test_pattern_compiler.cpp
    universal/test_pattern_track.cpp

    # PI sources compiled directly into the test binary
    ${CMAKE_SOURCE_DIR}/src/pi/audio_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/audio_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/filter.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/wav_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/ogg_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/mp3_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/decoder_wav.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/decoder_ogg.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/decoder_mp3.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/stream_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/audio_stream.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/audio_stream_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/voice_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/audio3d.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/filter_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/rtpc.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/random_source.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/music_track.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pattern/pattern.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pattern/mini_notation.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pattern/pattern_bank.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pattern/compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pattern_track.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/audio_engine_play.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/tone_source.cpp
)

target_include_directories(campello_audio_universal_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/src/pi
    ${dr_libs_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_link_libraries(campello_audio_universal_tests
    vector_math
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(campello_audio_universal_tests)

# ---------------------------------------------------------------------------
# Integration tests — require a real audio device
# ---------------------------------------------------------------------------
if(BUILD_INTEGRATION_TESTS)
    set(INTEGRATION_TEST_SOURCES
        platform/test_engine.cpp
        platform/test_playback.cpp
        platform/test_3d.cpp
        platform/test_filters.cpp
        platform/test_async_load.cpp
        platform/test_virtualization.cpp
        platform/test_bus.cpp
        platform/test_rtpc.cpp
        platform/test_snapshots.cpp
        platform/test_music.cpp
        platform/test_pattern_track.cpp
        platform/test_streaming.cpp
    )

    if(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
        list(APPEND INTEGRATION_TEST_SOURCES platform/test_wasm.cpp)
    endif()

    add_executable(campello_audio_integration_tests ${INTEGRATION_TEST_SOURCES})

    target_include_directories(campello_audio_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/inc
        ${CMAKE_SOURCE_DIR}/src/pi
        ${CMAKE_BINARY_DIR}
    )

    target_link_libraries(campello_audio_integration_tests
        campello_audio
        GTest::gtest_main
    )

    gtest_discover_tests(campello_audio_integration_tests)

    if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
        add_custom_command(TARGET campello_audio_integration_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:campello_audio>
                $<TARGET_FILE_DIR:campello_audio_integration_tests>
        )
    endif()
endif()
