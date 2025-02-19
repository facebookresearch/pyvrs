/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pybind11/pybind11.h>

#include <vrs/os/Platform.h>
#include <vrs/utils/FilteredFileReader.h>

#if IS_VRS_FB_INTERNAL()
#include "FilteredFileReader_fb.h"
#endif

namespace pyvrs {

using namespace vrs;
namespace py = pybind11;

/// @brief The FilteredFileReader class
/// This class enables selective reading of VRS files.
#if IS_VRS_OSS_CODE()
class FilteredFileReader {
 public:
  explicit FilteredFileReader(const std::string& filePath);

  void after(double minTime, bool isRelativeMinTime = false);
  void before(double maxTime, bool isRelativeMaxTime = false);
  void range(
      double minTime,
      double maxTime,
      bool isRelativeMinTime = false,
      bool isRelativeMaxTime = false);

  vrs::utils::FilteredFileReader& getFilteredReader();

 private:
  vrs::utils::FilteredFileReader filteredReader_;
  vrs::utils::RecordFilterParams filters_;
};
#endif

/// Binds methods and classes for FilteredFileReader.
void pybind_filtered_filereader(py::module& m);
} // namespace pyvrs
