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

#include <string>
#include <vector>

#include <pybind11/pybind11.h>

#include <vrs/DataSource.h>
#include <vrs/Recordable.h>

namespace pyvrs {

namespace py = pybind11;
using namespace vrs;

/// Generic writer that supports any RecordableTypeId and raw byte writing.
/// Used for verbatim copying of streams without predefined DataLayout.
class PyGenericWriter : public Recordable {
 public:
  PyGenericWriter(RecordableTypeId typeId, const std::string& flavor)
      : Recordable(typeId, flavor) {}

  /// Create a record with raw bytes - no DataLayout interpretation
  const Record* createRawRecord(
      double timestamp,
      Record::Type recordType,
      uint32_t formatVersion,
      const py::bytes& rawData) {
    std::string_view dataView(rawData);
    std::vector<int8_t> data(dataView.begin(), dataView.end());
    return Recordable::createRecord(timestamp, recordType, formatVersion, DataSource(data));
  }

  /// Create a record from a buffer (for numpy array support)
  const Record* createRawRecordFromBuffer(
      double timestamp,
      Record::Type recordType,
      uint32_t formatVersion,
      const py::buffer& buffer) {
    py::buffer_info info = buffer.request();
    const int8_t* ptr = static_cast<const int8_t*>(info.ptr);
    size_t size = info.size * info.itemsize;
    std::vector<int8_t> data(ptr, ptr + size);
    return Recordable::createRecord(timestamp, recordType, formatVersion, DataSource(data));
  }

  void setStreamTag(const std::string& name, const std::string& value) {
    Recordable::setTag(name, value);
  }

  void setStreamCompression(CompressionPreset preset) {
    Recordable::setCompression(preset);
  }

  std::string getStreamIdStr() {
    return Recordable::getStreamId().getNumericName();
  }

  // Required overrides - return nullptr as we don't auto-generate these
  const Record* createConfigurationRecord() override {
    return nullptr;
  }

  const Record* createStateRecord() override {
    return nullptr;
  }
};

} // namespace pyvrs
