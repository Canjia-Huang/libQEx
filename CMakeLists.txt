#
# Copyright 2013 Computer Graphics Group, RWTH Aachen University
# Author: Hans-Christian Ebke <ebke@cs.rwth-aachen.de>
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
# You should have received a copy of the GNU General Public License
# along with QEx.  If not, see <http://www.gnu.org/licenses/>.
#   

# CMake 4 removed compatibility with cmake_minimum_required(VERSION < 3.5),
# so the old "VERSION 2.6" no longer configures at all.
cmake_minimum_required (VERSION 3.10)

# Only set project name if not build from within another project.
if("${PROJECT_NAME}" STREQUAL "")
	project (QEx)
endif()

list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

#
# OpenMesh 8 and newer require at least C++11; OpenMesh 11 (the version
# shipped by current Homebrew/macOS package managers) uses C++17 constructs
# in its headers and cannot be compiled in C++98 mode.
#
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

#
# Output layout: all executables end up in <build dir>/bin, all libraries in
# <build dir>/lib - independent of the directory a target is declared in.
#
set (QEX_BIN_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Where the built executables are placed.")
set (QEX_LIB_DIR "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Where the built libraries are placed.")

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QEX_BIN_DIR}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QEX_LIB_DIR}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${QEX_LIB_DIR}")
foreach (config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${QEX_BIN_DIR}")
    set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} "${QEX_LIB_DIR}")
    set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} "${QEX_LIB_DIR}")
endforeach ()

#
# OpenMesh: it is shipped as a git submodule in third_party/, so a fresh clone
# (with --recurse-submodules) builds it automatically. A system installation is
# used as a fallback.
#
set (QEX_OPENMESH "AUTO" CACHE STRING
    "Where OpenMesh comes from: AUTO (third_party/OpenMesh if present, else a system installation), SUBMODULE or SYSTEM.")

set (QEX_OPENMESH_SUBMODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenMesh")

if (NOT QEX_OPENMESH STREQUAL "SYSTEM" AND EXISTS "${QEX_OPENMESH_SUBMODULE_DIR}/CMakeLists.txt")
    #
    # Build the bundled OpenMesh. Its CMakeLists is made for this: when it is not
    # the top level project it skips its unit tests and exports
    # OPENMESH_FOUND / OPENMESH_LIBRARIES / OPENMESH_INCLUDE_DIRS into this scope.
    #
    if (NOT EXISTS "${QEX_OPENMESH_SUBMODULE_DIR}/cmake-library/VCI/VCICommon.cmake")
        message (FATAL_ERROR
            "third_party/OpenMesh is present but its nested submodule cmake-library is "
            "missing. Run: git submodule update --init --recursive")
    endif ()
    set (BUILD_APPS OFF CACHE BOOL "Enable or disable building of apps" FORCE)
    set (OPENMESH_DOCS OFF CACHE BOOL "Enable or disable building of documentation" FORCE)
    # suppress the developer warnings OpenMesh's own cmake modules emit
    set (CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)
    add_subdirectory (third_party/OpenMesh EXCLUDE_FROM_ALL)
    unset (CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
    #
    # Link the shared variants (OpenMeshCore / OpenMeshTools): OpenMesh's mesh IO
    # modules register themselves through static initialisers, which a static
    # archive would drop (the well known "No reading modules available!" runtime
    # error).
    #
    set (OPENMESH_LIBRARIES OpenMeshCore OpenMeshTools)
    set (OPENMESH_INCLUDE_DIRS "${QEX_OPENMESH_SUBMODULE_DIR}/src")
    set (OPENMESH_LIBRARY_DIRS "${QEX_LIB_DIR}")
    message (STATUS "Using the bundled OpenMesh (${QEX_OPENMESH_SUBMODULE_DIR}).")
else ()
    if (QEX_OPENMESH STREQUAL "SUBMODULE")
        message (FATAL_ERROR
            "QEX_OPENMESH=SUBMODULE but third_party/OpenMesh is missing. "
            "Run: git submodule update --init --recursive")
    endif ()
    find_package (OpenMesh REQUIRED)
endif ()

#
# On macOS, Homebrew's OpenMeshTools library refers to OpenMeshCore through
# @rpath. Without an explicit runtime search path the build succeeds but the
# resulting binaries fail at startup with
#   Library not loaded: @rpath/libOpenMeshCore.*.dylib
#
if (APPLE)
    if (OPENMESH_LIBRARY_DIRS)
        set (CMAKE_BUILD_RPATH "${OPENMESH_LIBRARY_DIRS}")
        set (CMAKE_INSTALL_RPATH "${OPENMESH_LIBRARY_DIRS}")
    endif ()
    set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif ()

set (SOURCES "")
set (INCLUDE_DIRS "")
set (LIBRARIES "")
set (LIBRARY_DIRS "")

list (APPEND SOURCES interfaces/c/qex.cc src/predicates.c src/MeshExtractor.cc)
list (APPEND INCLUDE_DIRS ${OPENMESH_INCLUDE_DIRS})
list (APPEND LIBRARIES ${OPENMESH_LIBRARIES})

include_directories (
    ${CMAKE_SOURCE_DIR}/interfaces/c
    ${INCLUDE_DIRS}
    )

link_directories (
    ${QEX_LIB_DIR}
    ${CMAKE_BINARY_DIR}
    ${LIBRARY_DIRS}
    )

add_library (QEx SHARED ${SOURCES})
add_library (QExStatic STATIC ${SOURCES})

target_link_libraries (QEx ${LIBRARIES})
target_link_libraries (QExStatic ${LIBRARIES})

#
# In order for the exact predicates to work the compiler
# must not generate x87 FPU code as this leads to the use
# of extended precision registers which prevent lead to
# wrong results.
#
# As SSE does not have extended precision registers,
# forcing the generation of SSE code ensures that the
# exact predicates produce correct results.
#
# These flags only exist (and are only accepted by the compiler) on x86. On
# other architectures - e.g. arm64/Apple Silicon - there are no x87 extended
# precision registers at all, so nothing has to be forced and the exact
# predicates are safe with the default code generation.
#
set (QEX_ARCH_FP_FLAGS "")
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    set (QEX_ARCH_FP_FLAGS "-msse -mfpmath=sse")
endif ()

set (QEX_COMPILE_FLAGS "-Wall")
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    set (QEX_COMPILE_FLAGS "${QEX_COMPILE_FLAGS} ${QEX_ARCH_FP_FLAGS} -pedantic -Wextra")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set (QEX_COMPILE_FLAGS "${QEX_COMPILE_FLAGS} ${QEX_ARCH_FP_FLAGS} -pedantic -Weverything")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set (QEX_COMPILE_FLAGS "${QEX_COMPILE_FLAGS} ${QEX_ARCH_FP_FLAGS} -pedantic -Wextra")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    message (WARNING
        "You are using an Intel compiler which might generate x87 FPU code "
        "that breaks the exact predicates. If you know which compiler flags "
        "ensure that the Intel compiler produces SSE code, please patch "
        "the CMakeLists.txt and inform the author <ebke@cs.rwth-aachen.de>.")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# There is code in exactinit() that should make sure that nor
	# x87 extended internal precision is used.
else ()
    message (WARNING
        "You are using an unknown compiler which might generate x87 FPU code "
        "that breaks the exact predicates. If you know how to detect this compiler "
        "and which flags "
        "ensure that this compiler produces SSE code, please patch "
        "the CMakeLists.txt and inform the author <ebke@cs.rwth-aachen.de>.")
endif ()

set(STL_RANGE_CHECKS false CACHE BOOL "Include STL range checks in debug mode (This option is only used in debug mode.)")
# Add a flag to check stl vectors in debugging mode
if (STL_RANGE_CHECKS)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC" )
endif()

set_target_properties (QEx
    PROPERTIES
    COMPILE_FLAGS "${QEX_COMPILE_FLAGS}"
    DEFINE_SYMBOLS "-DQEX_EXPORT_SYMBOLS"
    )

set_target_properties (QExStatic
    PROPERTIES
    COMPILE_FLAGS "${QEX_COMPILE_FLAGS}"
    DEFINE_SYMBOLS "-DQEX_EXPORT_SYMBOLS"
    )

#
# Fake successful finder run if compiling as a dependent project.
#
if(NOT "${PROJECT_NAME}" MATCHES "QEx")
	set (QEX_FOUND true PARENT_SCOPE)
	set (QEX_LIBRARIES QEx PARENT_SCOPE)
	set (QEX_LIBRARY QEx PARENT_SCOPE)
	set (QEX_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/interfaces/c" PARENT_SCOPE)
	set (QEX_LIBRARY_DIR "${QEX_LIB_DIR}" CACHE PATH "The directory where the OpenMesh libraries can be found.")
endif()

add_subdirectory(demo/minimal_c)
add_subdirectory(demo/cmdline_tool)
add_subdirectory(demo/export_layout)

#
# geogram (https://github.com/BrunoLevy/geogram) is an optional dependency: it is
# only needed for the geogram bridge (interfaces/geogram), which converts the
# extraction result into GEO::Mesh with all information stored in attributes and
# writes it with geogram's mesh IO.
#
set (QEX_WITH_GEOGRAM "AUTO" CACHE STRING
    "Build the geogram bridge: ON, OFF or AUTO (only if geogram was found).")
set (GEOGRAM_ROOT "" CACHE PATH
    "Root of a geogram source tree (e.g. ~/geogram) or of a geogram installation. Used when geogram is not installed.")

if (NOT QEX_WITH_GEOGRAM STREQUAL "OFF")
    if (NOT GEOGRAM_ROOT)
        # a few common places, so that a plain "cmake ." works out of the box
        foreach (candidate "$ENV{HOME}/geogram" "${CMAKE_SOURCE_DIR}/../geogram" "${CMAKE_SOURCE_DIR}/geogram")
            if (EXISTS "${candidate}/src/lib/geogram/basic/common.h")
                set (GEOGRAM_ROOT "${candidate}" CACHE PATH
                    "Root of a geogram source tree (e.g. ~/geogram) or of a geogram installation." FORCE)
                break ()
            endif ()
        endforeach ()
    endif ()
    find_package (Geogram)
endif ()

if (Geogram_FOUND)
    message (STATUS "Building the geogram bridge (interfaces/geogram, demo/export_geogram).")
    add_subdirectory (interfaces/geogram)
    add_subdirectory (demo/export_geogram)
elseif (QEX_WITH_GEOGRAM STREQUAL "ON")
    message (FATAL_ERROR
        "QEX_WITH_GEOGRAM=ON but geogram was not found. Set GEOGRAM_ROOT to a geogram "
        "source tree (with a build directory) or to a geogram installation.")
else ()
    message (STATUS
        "geogram not found - the geogram bridge is disabled. Set GEOGRAM_ROOT or "
        "-DQEX_WITH_GEOGRAM=ON to enable it.")
endif ()
set(BUILD_UNIT_TESTS false CACHE BOOL "Whether to build the unit tests.")
if (BUILD_UNIT_TESTS)
    enable_testing()
    set(GTEST_DIR CACHE PATH "Source path of googletest.")
    if (NOT GTEST_DIR)
        message(FATAL_ERROR "GTEST_DIR unset")
    endif()
    add_subdirectory(${GTEST_DIR} gtest)
    add_subdirectory(tests)
endif()


#
# CLI11 (https://github.com/CLIUtils/CLI11), a git submodule in third_party/, is
# used by the "qex" command line application (apps/qex). It is header only, so
# the subproject is added with everything but the library target disabled.
#
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11/CMakeLists.txt")
    set (CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set (CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set (CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set (CLI11_SINGLE_FILE OFF CACHE BOOL "" FORCE)
    set (CLI11_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory (third_party/CLI11 EXCLUDE_FROM_ALL)
    set (QEX_HAVE_CLI11 ON)
    add_subdirectory (apps/qex)
else ()
    message (STATUS
        "third_party/CLI11 is missing (git submodule update --init --recursive) - "
        "the qex command line application is disabled.")
endif ()

find_package(Doxygen)
if (DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile @ONLY)
    add_custom_target(qex_doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc
        COMMENT "Generating Doxygen documentation" VERBATIM)
endif()
