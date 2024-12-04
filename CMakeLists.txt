# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16)

set(VRS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/csrc/vrs")
set(OSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/csrc/vrs")

# To customize where external dependencies are stored
if ("${EXTERNAL_DEPENDENCIES_DIR}" STREQUAL "")
  set(EXTERNAL_DEPENDENCIES_DIR "${CMAKE_BINARY_DIR}/external")
endif()
file(MAKE_DIRECTORY "${EXTERNAL_DEPENDENCIES_DIR}")
message(STATUS "External dependencies at ${EXTERNAL_DEPENDENCIES_DIR}")

# Initialize key cmake settings before the project is configured
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(${CMAKE_MODULE_PATH}/Platform.cmake)
include(${CMAKE_MODULE_PATH}/Compiler.cmake)
include(${CMAKE_MODULE_PATH}/Options.cmake)

# --- Version numbers for generated libraries ---
file(READ "version.txt" VRS_DEFAULT_VERSION)
# Strip trailing newline
string(REGEX REPLACE "\n$" "" VRS_DEFAULT_VERSION "${VRS_DEFAULT_VERSION}")
if ("${VRS_DEFAULT_VERSION}x" STREQUAL "x")
  message(FATAL_ERROR "Could not get version number from file 'version.txt'")
endif()

# Declare the project, make cmake chose/test the compiler, etc
project(vrsbindings
  VERSION ${VRS_DEFAULT_VERSION}
  DESCRIPTION "Meta VRS File Format Project"
  LANGUAGES CXX
)
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# If available, use ccache
include(${CMAKE_MODULE_PATH}/CCache.cmake)
# Library setup
include(${CMAKE_MODULE_PATH}/LibrariesSetup.cmake)

# Include the libraries
add_subdirectory(csrc/vrs/vrs)
add_subdirectory(csrc/pybind11)

# Keep functionality to reader only for now
file (GLOB_RECURSE CSRCS
  "csrc/Pybind11.cpp"
  "csrc/VrsBindings.cpp"
  "csrc/VrsBindings.h"
  "csrc/reader/*.cpp"
  "csrc/reader/*.h"
  "csrc/reader/*.hpp"
  "csrc/utils/*.cpp"
  "csrc/utils/*.h"
  "csrc/writer/*.cpp"
  "csrc/writer/*.h"
  "csrc/writer/datalayouts/*.cpp"
  "csrc/writer/datalayouts/*.h"
)

# Remove fb-only source files (if there is still any lying around)
file (GLOB_RECURSE FB_CSRCS "csrc/*/*_fb\.*")
if (FB_CSRCS)
  list(REMOVE_ITEM CSRCS ${FB_CSRCS})
endif()

pybind11_add_module(vrsbindings ${CSRCS})

target_include_directories(vrsbindings PUBLIC csrc)
target_link_libraries(vrsbindings
  PUBLIC
    vrslib
    vrs_utils
)
