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

#include <pybind11/pybind11.h>

#include <vrs/os/Platform.h>

#if IS_VRS_OSS_CODE()
#include "PyImageFilter.h"
using PyAsyncImageFilter = pyvrs::OssPyAsyncImageFilter;
using PyAsyncImageFilterIterator = pyvrs::OssPyAsyncImageFilterIterator;
#else
#include "PyImageFilter_fb.h"
using PyAsyncImageFilter = pyvrs::FbPyAsyncImageFilter;
using PyAsyncImageFilterIterator = pyvrs::FbPyAsyncImageFilterIterator;
#endif

namespace pyvrs {
namespace py = pybind11;

void pybind_filter(py::module& m);

} // namespace pyvrs
