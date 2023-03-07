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

#include <vrs/FileHandler.h>
#include <vrs/os/Platform.h>

namespace pyvrs {
namespace py = pybind11;

class OssPyFileSpec {
 public:
  explicit OssPyFileSpec(const std::string& path) {
    if (spec_.fromPathJsonUri(path)) {
      throw py::value_error("Invalid path: " + path);
    }
  }

  std::string getEasyPath() const {
    return spec_.getEasyPath();
  }

  const vrs::FileSpec& getSpec() const {
    return spec_;
  }

 protected:
  vrs::FileSpec spec_;
};

void pybind_filespec(py::module& m);

} // namespace pyvrs

#if IS_VRS_OSS_CODE()
using PyFileSpec = pyvrs::OssPyFileSpec;
#else
#include "PyFileSpec_fb.h"
using PyFileSpec = pyvrs::FbPyFileSpec;
#endif
