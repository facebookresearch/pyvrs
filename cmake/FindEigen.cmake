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

## FindEigen.cmake
# A CMake module to always fetch and configure Eigen3 v3.4.90 via FetchContent,
# ignoring any system-installed versions.
# Place this file in your project's cmake/ directory and add that directory to CMAKE_MODULE_PATH.

# Usage in your top-level CMakeLists.txt:
#
# list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
# find_package(Eigen3 REQUIRED)
# target_link_libraries(your_target PRIVATE Eigen3::Eigen)

# Prevent multiple inclusions
if(DEFINED FIND_EIGEN3_MODULE)
  return()
endif()
set(FIND_EIGEN3_MODULE TRUE)

# If Eigen3::Eigen already exists (alias or imported), assume provided and skip fetch
if(TARGET Eigen3::Eigen)
  message(STATUS "Eigen3::Eigen target already exists, skipping FetchContent.")
  set(Eigen3_FOUND TRUE)
  return()
endif()

# Fetch Eigen3 v3.4.90
include(FetchContent)
message(STATUS "Fetching Eigen3 v3.4.90 from Git via FetchContent...")
FetchContent_Declare(
  eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        19cacd3ecb9dab73c2dd7bc39d9193e06ba92bdd # v3.4.90
)
FetchContent_MakeAvailable(eigen)

# Create an imported interface target for Eigen3 only if not pre-existing
#add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
# Mark imported target to avoid being treated as alias
#set_target_properties(Eigen3::Eigen PROPERTIES
#  IMPORTED_GLOBAL TRUE
#)

#target_include_directories(Eigen3::Eigen INTERFACE
#  $<BUILD_INTERFACE:${eigen_SOURCE_DIR}>
#  $<INSTALL_INTERFACE:include>
#)

# Mark Eigen3 as found and set version
set(Eigen3_FOUND TRUE)
set(Eigen3_VERSION 3.4.90 CACHE STRING "Eigen3 version fetched by FetchContent")
