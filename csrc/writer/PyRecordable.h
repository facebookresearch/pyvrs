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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <vrs/RecordFormat.h>
#include <vrs/Recordable.h>

#include "VRSWriter.h"

namespace pyvrs {

class PyStream;

namespace py = pybind11;
using namespace vrs;

class PyRecordFormat {
 public:
  ~PyRecordFormat() = default;
  PyRecordFormat() {}
  PyRecordFormat(
      const Record::Type& recordType,
      uint32_t formatVersion = 0,
      std::unique_ptr<vrs::DataLayout>&& dataLayout = nullptr)
      : recordType_(recordType), formatVersion_(formatVersion) {
    if (dataLayout) {
      dataLayouts_.push_back(std::move(dataLayout));
    }
  }

  PyRecordFormat(
      const Record::Type& recordType,
      uint32_t formatVersion,
      std::unique_ptr<vrs::DataLayout>&& dataLayout,
      const std::vector<ContentBlock>& additionalContentBlocks)
      : recordType_(recordType), formatVersion_(formatVersion) {
    if (dataLayout) {
      dataLayouts_.push_back(std::move(dataLayout));
    }
    additionalContentBlocks_.push_back(additionalContentBlocks);
  }

  PyRecordFormat(
      const Record::Type& recordType,
      uint32_t formatVersion,
      std::unique_ptr<vrs::DataLayout>&& dataLayout1,
      std::unique_ptr<vrs::DataLayout>&& dataLayout2,
      const std::vector<ContentBlock>& additionalContentBlocks1,
      const std::vector<ContentBlock>& additionalContentBlocks2)
      : recordType_(recordType), formatVersion_(formatVersion) {
    if (dataLayout1) {
      dataLayouts_.push_back(std::move(dataLayout1));
    }
    if (dataLayout2) {
      dataLayouts_.push_back(std::move(dataLayout2));
    }
    additionalContentBlocks_.push_back(additionalContentBlocks1);
    additionalContentBlocks_.push_back(additionalContentBlocks2);
  }

  PyRecordFormat(PyRecordFormat&& other) {
    dataLayouts_.swap(other.dataLayouts_);
    additionalContentBlocks_ = other.additionalContentBlocks_;
    recordType_ = other.recordType_;
    formatVersion_ = other.formatVersion_;
  }

  const std::vector<const vrs::DataLayout*> getDataLayouts() const;

  Record::Type getRecordType() const {
    return recordType_;
  }

  uint32_t getFormatVersion() const {
    return formatVersion_;
  }

  DataSource getDataSource(
      const DataSourceChunk& src1 = {},
      const DataSourceChunk& src2 = {},
      const DataSourceChunk& src3 = {}) const;

  RecordFormat getRecordFormat() const;

  std::vector<std::string> getJsonDataLayouts() const;

  std::vector<std::map<std::string, std::unique_ptr<pyvrs::DataPieceWrapper>>> getMembers();

 private:
  std::vector<std::unique_ptr<vrs::DataLayout>> dataLayouts_;
  std::vector<std::vector<ContentBlock>> additionalContentBlocks_;
  Record::Type recordType_;
  uint32_t formatVersion_;
};

class PyRecordable : public Recordable {
 public:
  using Recordable::createRecord;
  PyRecordable(RecordableTypeId typeId, const string& deviceFlavor)
      : Recordable(typeId, deviceFlavor) {}

  bool addRecordFormat(const PyRecordFormat* recordFormat);

  const Record* createConfigurationRecord() override {
    // PYBIND11_OVERLOAD_PURE(const Record*, Recordable, createConfigurationRecord, );
    return NULL;
  }

  const Record* createStateRecord() override {
    // PYBIND11_OVERLOAD_PURE(const Record*, Recordable, createStateRecord, );
    return NULL;
  }
};

class PyStream {
 public:
  ~PyStream(){};

  PyStream(
      RecordableTypeId typeId,
      std::unique_ptr<PyRecordFormat>&& configurationRecordFormat = nullptr,
      std::unique_ptr<PyRecordFormat>&& dataRecordFormat = nullptr,
      std::unique_ptr<PyRecordFormat>&& stateRecordFormat = nullptr) {
    init(
        typeId,
        {},
        std::move(configurationRecordFormat),
        std::move(dataRecordFormat),
        std::move(stateRecordFormat));
  }
  PyStream(
      RecordableTypeId typeId,
      const string& deviceFlavor,
      std::unique_ptr<PyRecordFormat>&& configurationRecordFormat = nullptr,
      std::unique_ptr<PyRecordFormat>&& dataRecordFormat = nullptr,
      std::unique_ptr<PyRecordFormat>&& stateRecordFormat = nullptr) {
    init(
        typeId,
        deviceFlavor,
        std::move(configurationRecordFormat),
        std::move(dataRecordFormat),
        std::move(stateRecordFormat));
  }

  PyStream(PyStream&& other);

  PyRecordable* getRecordable() {
    return recordable_.get();
  }

  PyRecordFormat* createRecordFormat(Record::Type recordType);

  const Record* createRecord(double timestamp, const PyRecordFormat* recordFormat);

  const Record*
  createRecord(double timestamp, const PyRecordFormat* recordFormat, py::array buffer);

  const Record* createRecord(
      double timestamp,
      const PyRecordFormat* recordFormat,
      py::array buffer,
      py::array buffer2);

  const Record* createRecord(
      double timestamp,
      const PyRecordFormat* recordFormat,
      py::array buffer,
      py::array buffer2,
      py::array buffer3);

  void setCompression(CompressionPreset preset);

  void setTag(const std::string& tagName, const std::string& tagValue);

  std::string getStreamID();

 private:
  void init(
      RecordableTypeId typeId,
      const string& deviceFlavor,
      std::unique_ptr<PyRecordFormat>&& configurationRecordFormat,
      std::unique_ptr<PyRecordFormat>&& dataRecordFormat,
      std::unique_ptr<PyRecordFormat>&& stateRecordFormat);

  std::unique_ptr<PyRecordable> recordable_;
  std::map<Record::Type, std::unique_ptr<PyRecordFormat>> recordFormatMap_;
};

} // namespace pyvrs
