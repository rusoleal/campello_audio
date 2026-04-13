include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Universal tests — no audio device required, no library linkage
# ---------------------------------------------------------------------------
add_executable(campello_audio_universal_tests
    universal/test_constants.cpp
    universal/test_descriptors.cpp
    universal/test_math.cpp
    universal/test_sound_handle.cpp
)

target_include_directories(campello_audio_universal_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
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
    add_executable(campello_audio_integration_tests
        platform/test_engine.cpp
        platform/test_playback.cpp
        platform/test_3d.cpp
        platform/test_filters.cpp
    )

    target_include_directories(campello_audio_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/inc
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
