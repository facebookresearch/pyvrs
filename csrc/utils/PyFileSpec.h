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

#include <fmt/format.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <vrs/FileHandler.h>
#include <vrs/RecordFileReader.h>
#include <vrs/helpers/Strings.h>
#include <vrs/os/Platform.h>

#include "../VrsBindings.h"

namespace pyvrs {
namespace py = pybind11;

class OssPyFileSpec {
 public:
  OssPyFileSpec() {}
  explicit OssPyFileSpec(const std::string& path) {
    initVrsBindings();
    int status = vrs::RecordFileReader::vrsFilePathToFileSpec(path, spec_, true);
    if (status != 0) {
      throw py::value_error(
          fmt::format("Invalid path '{}': {}", path, vrs::errorCodeToMessageWithCode(status)));
    }
  }

  std::string getEasyPath() const {
    return spec_.getEasyPath();
  }

  const std::string& getFileHandlerName() const {
    return spec_.fileHandlerName;
  }

  const std::vector<std::string>& getChunks() const {
    return spec_.chunks;
  }

  const std::vector<int64_t>& getChunkSizes() const {
    return spec_.chunkSizes;
  }

  const std::string& getFileName() const {
    return spec_.fileName;
  }

  const std::string& getUri() const {
    return spec_.uri;
  }

  const vrs::FileSpec& getSpec() const {
    return spec_;
  }

  const std::string toJson() const {
    return spec_.toJson();
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

template <>
struct fmt::formatter<vrs::FileSpec> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const vrs::FileSpec& spec, FormatContext& ctx) const {
    return fmt::format_to(ctx.out(), "{}", spec.getEasyPath());
  }
};
