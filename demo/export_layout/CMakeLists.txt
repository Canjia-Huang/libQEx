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

find_package (OpenMesh REQUIRED)

set (EXPORT_LAYOUT_SOURCES "")
set (EXPORT_LAYOUT_INCLUDE_DIRS "")
set (EXPORT_LAYOUT_LIBRARIES "")
set (EXPORT_LAYOUT_LIBRARY_DIRS "")

list (APPEND EXPORT_LAYOUT_SOURCES main.cpp)
list (APPEND EXPORT_LAYOUT_INCLUDE_DIRS
    ${OPENMESH_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../interfaces/c
    ${QEX_INCLUDE_DIRS}
)
list (APPEND EXPORT_LAYOUT_LIBRARIES
    ${OPENMESH_LIBRARIES}
    QEx
)
list (APPEND EXPORT_LAYOUT_LIBRARY_DIRS ${OPEMESH_LIBRARY_DIR})





if (NOT WIN32 )

  add_executable (export_layout ${EXPORT_LAYOUT_SOURCES})

  target_link_libraries(export_layout ${EXPORT_LAYOUT_LIBRARIES})

  # Do not pin the language standard here: the C++ standard is set once in the
  # top-level CMakeLists.txt. Pinning it to c++98 breaks the build because
  # modern OpenMesh headers require (at least) C++11.
  set_target_properties(export_layout
      PROPERTIES
      INCLUDE_DIRECTORIES "${EXPORT_LAYOUT_INCLUDE_DIRS}"
      COMPILE_FLAGS "-Wall -Wextra -pedantic"
  )

endif()

