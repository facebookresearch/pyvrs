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

// This include *must* be before any STL include! See Python C API doc.
#include <pybind11/detail/common.h>
#include <exception>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "PyFileSpec.h"

namespace pyvrs {
namespace py = pybind11;
#if IS_VRS_OSS_CODE()
void pybind_filespec(py::module& m) {
  py::class_<PyFileSpec>(m, "FileSpec")
      .def(py::init<const std::string&>())
      .def("get_easy_path", &PyFileSpec::getEasyPath)
      .def("get_filehandler_name", &PyFileSpec::getFileHandlerName)
      .def("get_chunks", &PyFileSpec::getChunks)
      .def("get_chunk_sizes", &PyFileSpec::getChunkSizes)
      .def("get_filename", &PyFileSpec::getFileName)
      .def("get_uri", &PyFileSpec::getUri);
}
#endif
} // namespace pyvrs
