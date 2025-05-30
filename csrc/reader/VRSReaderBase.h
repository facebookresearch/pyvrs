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

namespace pyvrs {

namespace py = pybind11;

/// @brief The VRSReaderBase class
/// This class is the base class for VRSReader and MultiVRSReader.
/// It provides the minimum common functionality between both classes needed to factorize
/// pyvrs::OssAsyncVRSReader and pyvrs::OssAsyncMultiVRSReader
class VRSReaderBase {
 public:
  virtual ~VRSReaderBase() = default;

  virtual py::object readRecord(int index) = 0;
};

} // namespace pyvrs
