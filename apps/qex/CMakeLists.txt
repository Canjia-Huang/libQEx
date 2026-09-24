#
# qex - the QEx command line application.
#
# Reads a triangle mesh with a relaxed integer grid map, extracts the quad mesh
# and writes the quad mesh, the layout (subdivision, polylines, correspondence)
# and - if geogram is available - the meshes in geogram format.
#
# It uses CLI11 (third_party/CLI11, a git submodule).
#

set (QEX_APP_SOURCES "")
set (QEX_APP_LIBRARIES "")
set (QEX_APP_DEFINITIONS "")

list (APPEND QEX_APP_SOURCES main.cpp)
list (APPEND QEX_APP_LIBRARIES CLI11::CLI11 QEx ${OPENMESH_LIBRARIES})

if (Geogram_FOUND)
    list (APPEND QEX_APP_LIBRARIES QExGeogram)
    list (APPEND QEX_APP_DEFINITIONS QEX_HAVE_GEOGRAM=1)
endif ()

add_executable (qex ${QEX_APP_SOURCES})

target_include_directories (qex PRIVATE
    ${CMAKE_SOURCE_DIR}/interfaces/c
    ${CMAKE_SOURCE_DIR}/interfaces/geogram
    ${OPENMESH_INCLUDE_DIRS}
    ${GEOGRAM_INCLUDE_DIRS}
    ${QEX_INCLUDE_DIRS}
    )

target_link_directories (qex PRIVATE ${QEX_LIB_DIR} ${GEOGRAM_LIBRARY_DIR})
target_link_libraries (qex PRIVATE ${QEX_APP_LIBRARIES})
target_compile_definitions (qex PRIVATE ${QEX_APP_DEFINITIONS})
target_compile_features (qex PRIVATE cxx_std_17)
set_target_properties (qex PROPERTIES COMPILE_FLAGS "-Wall -Wextra -pedantic")
