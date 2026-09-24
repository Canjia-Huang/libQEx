#
# The geogram bridge: converts the extraction result into GEO::Mesh, with all
# information stored in geogram attributes, and writes it with geogram's mesh IO.
#
# Only built if geogram was found (see cmake/FindGeogram.cmake and the
# QEX_WITH_GEOGRAM option in the top level CMakeLists.txt).
#

set (QEX_GEOGRAM_SOURCES "")
set (QEX_GEOGRAM_INCLUDE_DIRS "")
set (QEX_GEOGRAM_LIBRARIES "")
set (QEX_GEOGRAM_LIBRARY_DIRS "")

list (APPEND QEX_GEOGRAM_SOURCES qex_geogram.cc)
list (APPEND QEX_GEOGRAM_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../c
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QEX_INCLUDE_DIRS}
    ${GEOGRAM_INCLUDE_DIRS}
    )
list (APPEND QEX_GEOGRAM_LIBRARIES
    ${GEOGRAM_LIBRARIES}
    QEx
    )
if (APPLE AND GEOGRAM_LIBRARY_DIR)
    # geogram's libraries refer to each other through @rpath and are not
    # installed, so the runtime search path has to point at the build directory
    list (APPEND QEX_GEOGRAM_LIBRARY_DIRS ${GEOGRAM_LIBRARY_DIR})
endif ()

#
# A separate library, so that nothing that links QEx is forced to link geogram.
#
add_library (QExGeogram SHARED ${QEX_GEOGRAM_SOURCES})
add_library (QExGeogramStatic STATIC ${QEX_GEOGRAM_SOURCES})

foreach (target QExGeogram QExGeogramStatic)
    target_include_directories (${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../c
        ${GEOGRAM_INCLUDE_DIRS}
        )
    target_link_directories (${target} PUBLIC ${QEX_GEOGRAM_LIBRARY_DIRS})
    target_link_libraries (${target} PUBLIC ${QEX_GEOGRAM_LIBRARIES})
    target_compile_features (${target} PUBLIC cxx_std_17)
    set_target_properties (${target} PROPERTIES
        COMPILE_FLAGS "-Wall -Wextra -pedantic"
        )
endforeach ()

# everything in this directory gets geogram's include directories
include_directories (${CMAKE_CURRENT_SOURCE_DIR} ${GEOGRAM_INCLUDE_DIRS})
link_directories (${QEX_GEOGRAM_LIBRARY_DIRS} ${GEOGRAM_LIBRARY_DIR})
