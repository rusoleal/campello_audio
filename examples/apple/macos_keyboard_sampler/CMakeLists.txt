find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SAMPLER_TARGET campello_audio_macos_sampler)
set(SAMPLER_ASSET_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/audio)
set(SAMPLER_ASSET_STAMP ${SAMPLER_ASSET_DIR}/.generated.stamp)

add_custom_command(
    OUTPUT ${SAMPLER_ASSET_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SAMPLER_ASSET_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_demo_assets.py ${SAMPLER_ASSET_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SAMPLER_ASSET_STAMP}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_demo_assets.py
    VERBATIM
)

add_custom_target(${SAMPLER_TARGET}_assets DEPENDS ${SAMPLER_ASSET_STAMP})

add_executable(${SAMPLER_TARGET} MACOSX_BUNDLE
    main.mm
)

add_dependencies(${SAMPLER_TARGET} ${SAMPLER_TARGET}_assets)

set_source_files_properties(main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

target_link_libraries(${SAMPLER_TARGET} PRIVATE
    campello_audio
    "-framework AppKit"
)

set_target_properties(${SAMPLER_TARGET} PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
    MACOSX_BUNDLE_BUNDLE_NAME "Campello Audio Sampler"
)

add_custom_command(TARGET ${SAMPLER_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${SAMPLER_TARGET}>/Resources/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SAMPLER_ASSET_DIR}
            "$<TARGET_BUNDLE_CONTENT_DIR:${SAMPLER_TARGET}>/Resources/audio"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${SAMPLER_TARGET}>/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:campello_audio>
            "$<TARGET_BUNDLE_CONTENT_DIR:${SAMPLER_TARGET}>/Frameworks/"
)
