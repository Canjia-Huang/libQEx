#
# Exports the quad layout (Q-edge polylines, the subdivision of the triangle mesh
# and the quad face <-> triangle face correspondence) into geogram meshes and
# writes them with geogram's mesh IO (.geogram, .mesh, .obj, ...).
#

set (EXPORT_GEOGRAM_SOURCES "")
set (EXPORT_GEOGRAM_INCLUDE_DIRS "")
set (EXPORT_GEOGRAM_LIBRARIES "")

list (APPEND EXPORT_GEOGRAM_SOURCES main.cpp)
list (APPEND EXPORT_GEOGRAM_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/interfaces/c
    ${CMAKE_SOURCE_DIR}/interfaces/geogram
    ${GEOGRAM_INCLUDE_DIRS}
    ${OPENMESH_INCLUDE_DIRS}
    ${QEX_INCLUDE_DIRS}
    )
list (APPEND EXPORT_GEOGRAM_LIBRARIES
    QExGeogram
    QEx
    ${GEOGRAM_LIBRARIES}
    ${OPENMESH_LIBRARIES}
    )

add_executable (export_geogram ${EXPORT_GEOGRAM_SOURCES})
target_include_directories (export_geogram PRIVATE ${EXPORT_GEOGRAM_INCLUDE_DIRS})
target_link_directories (export_geogram PRIVATE ${GEOGRAM_LIBRARY_DIR} ${QEX_GEOGRAM_LIBRARY_DIRS})
target_link_libraries (export_geogram PRIVATE ${EXPORT_GEOGRAM_LIBRARIES})
target_compile_features (export_geogram PRIVATE cxx_std_17)
set_target_properties (export_geogram PROPERTIES COMPILE_FLAGS "-Wall -Wextra -pedantic")
