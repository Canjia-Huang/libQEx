#
# Try to find geogram
# Once done this will define
#
# GEOGRAM_FOUND           - system has geogram
# GEOGRAM_INCLUDE_DIRS    - the geogram include directories
# GEOGRAM_LIBRARIES       - Link these to use geogram
# GEOGRAM_LIBRARY_DIR     - the directory the geogram libraries were found in
#
# geogram can either be installed (so that its headers and libraries live in the
# usual places) or it can be used straight from a source tree in which it was
# built (which is what most people do, since geogram's recommended workflow is
#   ./configure.sh && cd build/<config> && make
# ). Set GEOGRAM_ROOT to the root of such a tree - the module then picks up
#   <GEOGRAM_ROOT>/src/lib                       (headers)
#   <GEOGRAM_ROOT>/build/*/src/lib               (generated headers, e.g. version.h)
#   <GEOGRAM_ROOT>/build/*/lib                   (libgeogram.<ext>)
#
# This file is part of QEx.
#
# QEx is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# QEx is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#

IF (GEOGRAM_INCLUDE_DIR AND GEOGRAM_LIBRARY)
  # Already in cache, be silent
  SET(GEOGRAM_FIND_QUIETLY TRUE)
ENDIF ()

SET (GEOGRAM_INCLUDE_DIR NOTFOUND CACHE PATH "The directory containing geogram/basic/common.h.")
SET (GEOGRAM_LIBRARY NOTFOUND CACHE FILEPATH "The geogram library.")
SET (GEOGRAM_LIBRARY_DIR NOTFOUND CACHE PATH "The directory the geogram library was found in.")

#
# 1. an installed geogram
#
FIND_PATH (GEOGRAM_INCLUDE_DIR geogram/basic/common.h
    PATHS
        ${GEOGRAM_ROOT}/include
        ${GEOGRAM_ROOT}/src/lib
        /usr/local/include
        /usr/include
        /opt/homebrew/include
        ${CMAKE_SOURCE_DIR}/geogram/src/lib
        ${CMAKE_SOURCE_DIR}/../geogram/src/lib
    DOC "The directory containing geogram/basic/common.h."
    )

FIND_LIBRARY (GEOGRAM_LIBRARY
    NAMES geogram
    PATHS
        ${GEOGRAM_ROOT}/lib
        /usr/local/lib
        /usr/lib
        /opt/homebrew/lib
        ${CMAKE_SOURCE_DIR}/geogram/build/lib
        ${CMAKE_SOURCE_DIR}/../geogram/build/lib
    DOC "The geogram library."
    )

#
# 2. a geogram source tree with a build directory
#
IF (GEOGRAM_ROOT AND (NOT GEOGRAM_INCLUDE_DIR OR NOT GEOGRAM_LIBRARY))
    IF (NOT GEOGRAM_INCLUDE_DIR AND EXISTS "${GEOGRAM_ROOT}/src/lib/geogram/basic/common.h")
        SET (GEOGRAM_INCLUDE_DIR "${GEOGRAM_ROOT}/src/lib" CACHE PATH
            "The directory containing geogram/basic/common.h." FORCE)
    ENDIF ()
    IF (NOT GEOGRAM_LIBRARY)
        FILE (GLOB GEOGRAM_BUILD_LIBS "${GEOGRAM_ROOT}/build/*/lib/libgeogram.*")
        IF (GEOGRAM_BUILD_LIBS)
            LIST (SORT GEOGRAM_BUILD_LIBS)
            FOREACH (candidate ${GEOGRAM_BUILD_LIBS})
                GET_FILENAME_COMPONENT (candidate_ext "${candidate}" EXT)
                # prefer the plain "libgeogram.<ext>" over the versioned ones
                IF ("${candidate_ext}" STREQUAL "${CMAKE_SHARED_LIBRARY_SUFFIX}"
                        OR "${candidate_ext}" STREQUAL "${CMAKE_STATIC_LIBRARY_SUFFIX}")
                    SET (GEOGRAM_LIBRARY "${candidate}" CACHE FILEPATH "The geogram library." FORCE)
                    GET_FILENAME_COMPONENT (GEOGRAM_LIBRARY_DIR "${candidate}" DIRECTORY)
                    SET (GEOGRAM_LIBRARY_DIR "${GEOGRAM_LIBRARY_DIR}" CACHE PATH
                        "The directory the geogram library was found in." FORCE)
                    BREAK ()
                ENDIF ()
            ENDFOREACH ()
        ENDIF ()
    ENDIF ()
ENDIF ()

#
# geogram's build directory also contains generated headers (geogram/version.h),
# they have to be visible before the ones from the source tree.
#
SET (GEOGRAM_INCLUDE_DIRS "")
IF (GEOGRAM_LIBRARY_DIR)
    FILE (GLOB GEOGRAM_GENERATED_HEADER_DIRS "${GEOGRAM_LIBRARY_DIR}/../src/lib")
    FOREACH (dir ${GEOGRAM_GENERATED_HEADER_DIRS})
        IF (IS_DIRECTORY "${dir}/geogram")
            LIST (APPEND GEOGRAM_INCLUDE_DIRS "${dir}")
        ENDIF ()
    ENDFOREACH ()
ENDIF ()
IF (GEOGRAM_INCLUDE_DIR)
    LIST (APPEND GEOGRAM_INCLUDE_DIRS "${GEOGRAM_INCLUDE_DIR}")
ENDIF ()

SET (GEOGRAM_LIBRARIES "")
IF (GEOGRAM_LIBRARY)
    SET (GEOGRAM_LIBRARIES "${GEOGRAM_LIBRARY}")
    # geogram's numeric third party library is needed by the library itself, but
    # linking it explicitly does not hurt and helps with static builds
    GET_FILENAME_COMPONENT (GEOGRAM_LIBRARY_DIR_TMP "${GEOGRAM_LIBRARY}" DIRECTORY)
    FIND_LIBRARY (GEOGRAM_NUM_3RDPARTY_LIBRARY
        NAMES geogram_num_3rdparty
        PATHS "${GEOGRAM_LIBRARY_DIR_TMP}"
        NO_DEFAULT_PATH
        )
    IF (GEOGRAM_NUM_3RDPARTY_LIBRARY)
        LIST (APPEND GEOGRAM_LIBRARIES "${GEOGRAM_NUM_3RDPARTY_LIBRARY}")
    ENDIF ()
ENDIF ()

INCLUDE (FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS (Geogram DEFAULT_MSG GEOGRAM_INCLUDE_DIRS GEOGRAM_LIBRARIES)

IF (GEOGRAM_FOUND)
    MESSAGE (STATUS "Found geogram: ${GEOGRAM_LIBRARIES}")
    MESSAGE (STATUS "  geogram include dirs: ${GEOGRAM_INCLUDE_DIRS}")
ENDIF ()
