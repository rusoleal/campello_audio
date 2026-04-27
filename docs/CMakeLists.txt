find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found — docs target disabled")
    return()
endif()

set(DOXYGEN_IN  "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} COPYONLY)

add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
)
