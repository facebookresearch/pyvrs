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
#define PY_SSIZE_T_CLEAN
#include <Python.h> // IWYU pragma: keepo

// Includes needed for bindings (including marshalling STL containers)
#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define DEFAULT_LOG_CHANNEL "PyRecordable"
#include <logging/Log.h>

#include <vrs/RecordFileWriter.h>
#include <vrs/Recordable.h>

#include "PyRecordable.h"
#include "VRSWriter.h"

namespace py = pybind11;

namespace pyvrs {

bool PyRecordable::addRecordFormat(const PyRecordFormat* recordFormat) {
  return Recordable::addRecordFormat(
      recordFormat->getRecordType(),
      recordFormat->getFormatVersion(),
      recordFormat->getRecordFormat(),
      recordFormat->getDataLayouts());
}

DataSource PyRecordFormat::getDataSource(
    const DataSourceChunk& src1,
    const DataSourceChunk& src2,
    const DataSourceChunk& src3) const {
  if (dataLayouts_.size() == 1) {
    return DataSource(*dataLayouts_[0], src1, src2, src3);
  }
  if (dataLayouts_.size() == 2) {
    return DataSource(*dataLayouts_[0], *dataLayouts_[1], src1, src2, src3);
  }
  return DataSource(src1, src2, src3);
}

const std::vector<const vrs::DataLayout*> PyRecordFormat::getDataLayouts() const {
  std::vector<const vrs::DataLayout*> dataLayouts = {};
  for (auto& dataLayout : dataLayouts_) {
    dataLayouts.emplace_back(dataLayout.get());
  }
  return dataLayouts;
}

RecordFormat PyRecordFormat::getRecordFormat() const {
  RecordFormat format;
  // default behavior of DataSource::copyTo is to copy all dataLayouts buffer first,
  // then copy other buffers. To make things simple, we will use the same order here.
  for (auto& dataLayout : dataLayouts_) {
    format = format + dataLayout->getContentBlock();
  }
  for (const ContentBlock& contentBlock : additionalContentBlocks_) {
    format = format + contentBlock;
  }
  return format;
}

#define ADD_DATA_PIECE_WRAPPER_SUPPORT(DATA_PIECE_TYPE, TEMPLATE_TYPE)                             \
  if (piece->getTypeName() == "DataPiece" #DATA_PIECE_TYPE "<" #TEMPLATE_TYPE ">") {               \
    std::unique_ptr<pyvrs::DataPiece##DATA_PIECE_TYPE##TEMPLATE_TYPE##Wrapper> ptr =               \
        std::make_unique<pyvrs::DataPiece##DATA_PIECE_TYPE##TEMPLATE_TYPE##Wrapper>();             \
    ptr->setDataPiece(reinterpret_cast<pyvrs::DataPiece##DATA_PIECE_TYPE<TEMPLATE_TYPE>*>(piece)); \
    dataPieceMaps[i][piece->getLabel()] = std::move(ptr);                                          \
  }

#define ADD_ALL_DATA_PIECE_WRAPPER_SUPPORT(TEMPLATE_TYPE) \
  ADD_DATA_PIECE_WRAPPER_SUPPORT(Value, TEMPLATE_TYPE)    \
  ADD_DATA_PIECE_WRAPPER_SUPPORT(Array, TEMPLATE_TYPE)    \
  ADD_DATA_PIECE_WRAPPER_SUPPORT(Vector, TEMPLATE_TYPE)   \
  ADD_DATA_PIECE_WRAPPER_SUPPORT(StringMap, TEMPLATE_TYPE)

std::vector<std::map<std::string, std::unique_ptr<pyvrs::DataPieceWrapper>>>
PyRecordFormat::getMembers() {
  std::vector<std::map<std::string, std::unique_ptr<pyvrs::DataPieceWrapper>>> dataPieceMaps(
      dataLayouts_.size());
  for (size_t i = 0; i < dataLayouts_.size(); i++) {
    dataLayouts_[i]->forEachDataPiece([&dataPieceMaps, &i](vrs::DataPiece* piece) {
// Define & generate the code for each POD type supported.
#define POD_MACRO ADD_ALL_DATA_PIECE_WRAPPER_SUPPORT
#include <vrs/helpers/PODMacro.inc>
      ADD_DATA_PIECE_WRAPPER_SUPPORT(Vector, string)
      ADD_DATA_PIECE_WRAPPER_SUPPORT(StringMap, string)

      if (piece->getTypeName() == "DataPieceString") {
        std::unique_ptr<pyvrs::DataPieceStringWrapper> ptr =
            std::make_unique<pyvrs::DataPieceStringWrapper>();
        ptr->setDataPiece(reinterpret_cast<vrs::DataPieceString*>(piece));
        dataPieceMaps[i][piece->getLabel()] = std::move(ptr);
      }
    });
  }

  return dataPieceMaps;
}

void PyStream::init(
    RecordableTypeId typeId,
    const string& deviceFlavor,
    std::unique_ptr<PyRecordFormat>&& configurationRecordFormat,
    std::unique_ptr<PyRecordFormat>&& dataRecordFormat,
    std::unique_ptr<PyRecordFormat>&& stateRecordFormat) {
  recordable_ = std::make_unique<PyRecordable>(typeId, deviceFlavor);
  if (configurationRecordFormat != nullptr) {
    recordFormatMap_.insert(
        std::make_pair(Record::Type::CONFIGURATION, std::move(configurationRecordFormat)));
  } else {
    recordFormatMap_.insert(std::make_pair(
        Record::Type::CONFIGURATION,
        std::make_unique<PyRecordFormat>(Record::Type::CONFIGURATION)));
  }
  if (dataRecordFormat != nullptr) {
    recordFormatMap_.insert(std::make_pair(Record::Type::DATA, std::move(dataRecordFormat)));
  } else {
    recordFormatMap_.insert(
        std::make_pair(Record::Type::DATA, std::make_unique<PyRecordFormat>(Record::Type::DATA)));
  }
  if (stateRecordFormat != nullptr) {
    recordFormatMap_.insert(std::make_pair(Record::Type::STATE, std::move(stateRecordFormat)));
  } else {
    recordFormatMap_.insert(
        std::make_pair(Record::Type::STATE, std::make_unique<PyRecordFormat>(Record::Type::STATE)));
  }
}

PyStream::PyStream(PyStream&& other) {
  other.recordable_ = std::move(recordable_);
  for (auto& recordFormat : recordFormatMap_) {
    other.recordFormatMap_.insert(
        std::make_pair(recordFormat.first, std::move(recordFormat.second)));
  }
}

PyRecordFormat* PyStream::createRecordFormat(Record::Type recordType) {
  PyRecordFormat* recordFormat = nullptr;
  auto iter = recordFormatMap_.find(recordType);
  if (iter != recordFormatMap_.end()) {
    recordFormat = iter->second.get();
    recordable_->addRecordFormat(recordFormat);
    return recordFormat;
  }
  return recordFormat;
}

const Record* PyStream::createRecord(double timestamp, const PyRecordFormat* recordFormat) {
  return recordable_->createRecord(
      timestamp,
      recordFormat->getRecordType(),
      recordFormat->getFormatVersion(),
      recordFormat->getDataSource());
}

const Record*
PyStream::createRecord(double timestamp, const PyRecordFormat* recordFormat, py::array buffer) {
  py::buffer_info info = buffer.request();
  size_t size = info.itemsize;
  for (py::ssize_t i = 0; i < info.ndim; i++) {
    size *= info.shape[i];
  }
  return recordable_->createRecord(
      timestamp,
      recordFormat->getRecordType(),
      recordFormat->getFormatVersion(),
      recordFormat->getDataSource(DataSourceChunk(info.ptr, size)));
}

const Record* PyStream::createRecord(
    double timestamp,
    const PyRecordFormat* recordFormat,
    py::array buffer,
    py::array buffer2) {
  py::buffer_info info = buffer.request();
  py::buffer_info info2 = buffer2.request();
  size_t size = info.itemsize;
  for (py::ssize_t i = 0; i < info.ndim; i++) {
    size *= info.shape[i];
  }
  size_t size2 = info2.itemsize;
  for (py::ssize_t i = 0; i < info2.ndim; i++) {
    size2 *= info2.shape[i];
  }
  return recordable_->createRecord(
      timestamp,
      recordFormat->getRecordType(),
      recordFormat->getFormatVersion(),
      recordFormat->getDataSource(
          DataSourceChunk(info.ptr, size), DataSourceChunk(info2.ptr, size2)));
}

const Record* PyStream::createRecord(
    double timestamp,
    const PyRecordFormat* recordFormat,
    py::array buffer,
    py::array buffer2,
    py::array buffer3) {
  py::buffer_info info = buffer.request();
  py::buffer_info info2 = buffer2.request();
  py::buffer_info info3 = buffer3.request();
  size_t size = info.itemsize;
  for (py::ssize_t i = 0; i < info.ndim; i++) {
    size *= info.shape[i];
  }
  size_t size2 = info2.itemsize;
  for (py::ssize_t i = 0; i < info2.ndim; i++) {
    size2 *= info2.shape[i];
  }
  size_t size3 = info3.itemsize;
  for (py::ssize_t i = 0; i < info3.ndim; i++) {
    size3 *= info3.shape[i];
  }
  return recordable_->createRecord(
      timestamp,
      recordFormat->getRecordType(),
      recordFormat->getFormatVersion(),
      recordFormat->getDataSource(
          DataSourceChunk(info.ptr, size),
          DataSourceChunk(info2.ptr, size2),
          DataSourceChunk(info3.ptr, size3)));
}

void PyStream::setCompression(CompressionPreset preset) {
  recordable_->setCompression(preset);
}

void PyStream::setTag(const std::string& tagName, const std::string& tagValue) {
  recordable_->setTag(tagName, tagValue);
}

std::string PyStream::getStreamID() {
  return recordable_->getStreamId().getNumericName();
}

std::vector<std::string> PyRecordFormat::getJsonDataLayouts() const {
  std::vector<std::string> v;
  for (auto& dataLayout : dataLayouts_) {
    v.push_back(dataLayout->asJson());
  }

  return v;
}

} // namespace pyvrs
