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

#include <memory>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>

#include <vrs/os/Platform.h>

#if IS_VRS_FB_INTERNAL()
#include "VRSWriter_headers_fb.hpp"
#endif

#include <vrs/DataLayout.h>
#include <vrs/RecordFileWriter.h>
#include <vrs/Recordable.h>

#include "PyDataPiece.h"

namespace pyvrs {

namespace py = pybind11;
using namespace vrs;

class PyStream;

/// @brief The VRSWriter class
/// This class is a VRS file writer, optimized for Python bindings.
class VRSWriter {
 public:
  VRSWriter();
  ~VRSWriter();
  VRSWriter(const VRSWriter&) = delete;
  VRSWriter& operator=(const VRSWriter&) = delete;
  VRSWriter(VRSWriter&&) = delete;
  VRSWriter& operator=(VRSWriter&&) = delete;

  void init();

  /// Recordable instance ids are automatically assigned when Recordable objects are created.
  /// This guarantees that each Recordable gets a unique ID.
  /// WARNING! If your code relies on specific instance IDs, your design is weak, and you are
  /// setting up your project for a world of pain in the future.
  /// Use flavors and tag pairs to identify your streams instead.
  /// However, when many files are generated successively, it can lead to high instance
  /// id values, which can be confusing, and even problematic for unit tests.
  /// Use this API to reset the instance counters for each device type, so that the next devices
  /// will get an instance id of 1.
  /// ATTENTION! if you call this API at the wrong time, you can end up with multiple devices with
  /// the same id, and end up in a messy situation. Avoid this API if you can!
  void resetNewInstanceIds();

  int create(const std::string& filePath);

  PyStream* createStream(const std::string& name);
  PyStream* createFlavoredStream(const std::string& name, const std::string& flavor);

  void setTag(const std::string& tagName, const std::string& tagValue);

  void addRecordable(Recordable* recordable);

  int writeRecords(double maxTimestamp);

  uint64_t getBackgroundThreadQueueByteSize();

  int close();

#if IS_VRS_FB_INTERNAL()
#include "VRSWriter_methods_fb.hpp"
#endif

 private:
  RecordFileWriter writer_;
  std::vector<std::unique_ptr<PyStream>> streams_;
};

} // namespace pyvrs
