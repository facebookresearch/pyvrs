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

#include "MultiVRSReader.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define DEFAULT_LOG_CHANNEL "MultiVRSReader"
#include <logging/Log.h>
#include <logging/Verify.h>

#include <vrs/ErrorCode.h>
#include <vrs/FileHandlerFactory.h>
#include <vrs/helpers/Strings.h>
#include <vrs/utils/PixelFrame.h>
#include <vrs/utils/VideoRecordFormatStreamPlayer.h>

#include "../VrsBindings.h"
#include "../utils/PyExceptions.h"
#include "../utils/PyRecord.h"
#include "../utils/PyUtils.h"
#include "FactoryHelper.hpp"

using UniqueStreamId = vrs::MultiRecordFileReader::UniqueStreamId;

namespace pyvrs {

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::checkSkipTrailingBlocks(
    const CurrentRecord& record,
    size_t blockIndex) {
  // check whether we should stop reading further as the next content block will be considered
  // trailing for this device type and currently processed record
  const UniqueStreamId streamId =
      multiVRSReader_.getUniqueStreamIdForRecordIndex(multiVRSReader_.nextRecordIndex_);
  auto trailingBlockCount = multiVRSReader_.firstSkippedTrailingBlockIndex_.find(
      {streamId.getTypeId(), record.recordType});
  if (trailingBlockCount != multiVRSReader_.firstSkippedTrailingBlockIndex_.end()) {
    return (blockIndex + 1) < trailingBlockCount->second;
  } else {
    return true;
  }
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::processRecordHeader(
    const CurrentRecord& record,
    DataReference& outDataRef) {
  multiVRSReader_.lastRecord_.recordFormatVersion = record.formatVersion;
  return RecordFormatStreamPlayer::processRecordHeader(record, outDataRef);
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::onDataLayoutRead(
    const CurrentRecord& record,
    size_t blkIdx,
    DataLayout& dl) {
  multiVRSReader_.lastRecord_.datalayoutBlocks.emplace_back(
      pyWrap(readDataLayout(dl, multiVRSReader_.encoding_)));
  return checkSkipTrailingBlocks(record, blkIdx);
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::onImageRead(
    const CurrentRecord& record,
    size_t blkIdx,
    const ContentBlock& cb) {
  return setBlock(multiVRSReader_.lastRecord_.images, record, blkIdx, cb);
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::onAudioRead(
    const CurrentRecord& record,
    size_t blkIdx,
    const ContentBlock& cb) {
  return setBlock(multiVRSReader_.lastRecord_.audioBlocks, record, blkIdx, cb);
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::onCustomBlockRead(
    const CurrentRecord& rec,
    size_t bix,
    const ContentBlock& cb) {
  return setBlock(multiVRSReader_.lastRecord_.customBlocks, rec, bix, cb);
}

bool OssMultiVRSReader::MultiVRSReaderStreamPlayer::onUnsupportedBlock(
    const CurrentRecord& cr,
    size_t bix,
    const ContentBlock& cb) {
  return setBlock(multiVRSReader_.lastRecord_.unsupportedBlocks, cr, bix, cb);
}

ImageConversion OssMultiVRSReader::MultiVRSReaderStreamPlayer::getImageConversion(
    const CurrentRecord& record) {
  return multiVRSReader_.getImageConversion(
      multiVRSReader_.getUniqueStreamIdForRecordIndex(multiVRSReader_.nextRecordIndex_));
}

void OssMultiVRSReader::init() {
  initVrsBindings();
}

void OssMultiVRSReader::open(const PyFileSpec& spec) {
  open({spec.getSpec()});
}

void OssMultiVRSReader::open(const std::vector<PyFileSpec>& pySpecs) {
  std::vector<FileSpec> specs;
  specs.reserve(pySpecs.size());
  for (const auto& pySpec : pySpecs) {
    specs.emplace_back(pySpec.getSpec());
  }
  return open(specs);
}

void OssMultiVRSReader::open(const std::string& path) {
  std::vector<std::string> pathVector;
  pathVector.emplace_back(path);
  open(pathVector);
}

void OssMultiVRSReader::open(const std::vector<std::string>& paths) {
  std::vector<FileSpec> specs(paths.size());
  for (size_t i = 0; i < paths.size(); i++) {
    int status = RecordFileReader::vrsFilePathToFileSpec(paths[i], specs[i]);
    if (status != 0) {
      throw py::value_error(
          fmt::format("Invalid path '{}': {}", paths[i], errorCodeToMessageWithCode(status)));
    }
  }
  return open(specs);
}

void OssMultiVRSReader::open(const std::vector<FileSpec>& specs) {
  nextRecordIndex_ = 0;
  int status = reader_.open(specs);
  if (status != 0) {
    close();
    throw std::runtime_error(fmt::format(
        "Could not open '{}': {}", fmt::join(specs, ", "), errorCodeToMessageWithCode(status)));
  }
  if (autoReadConfigurationRecord_) {
    for (const auto& streamId : reader_.getStreams()) {
      lastReadConfigIndex_[streamId] = std::numeric_limits<uint32_t>::max();
    }
  }
}

int OssMultiVRSReader::close() {
  return reader_.close();
}

void OssMultiVRSReader::setEncoding(const string& encoding) {
  encoding_ = encoding;
}

string OssMultiVRSReader::getEncoding() {
  return encoding_;
}

py::object OssMultiVRSReader::getFileChunks() const {
  vector<std::pair<string, int64_t>> chunks = reader_.getFileChunks();
  Py_ssize_t listSize = static_cast<Py_ssize_t>(chunks.size());
  PyObject* list = PyList_New(listSize);
  Py_ssize_t index = 0;
  for (const auto& chunk : chunks) {
    PyList_SetItem(
        list,
        index++,
        Py_BuildValue("{s:s,s:i}", "path", chunk.first.c_str(), "size", chunk.second));
  }
  return pyWrap(list);
}

double OssMultiVRSReader::getMaxAvailableTimestamp() {
  const uint32_t recordCount = reader_.getRecordCount();
  if (recordCount == 0) {
    return 0;
  }
  return reader_.getRecord(recordCount - 1)->timestamp;
}

double OssMultiVRSReader::getMinAvailableTimestamp() {
  if (reader_.getRecordCount() == 0) {
    return 0;
  }
  return reader_.getRecord(0)->timestamp;
}

size_t OssMultiVRSReader::getAvailableRecordsSize() {
  return reader_.getRecordCount();
}

std::set<string> OssMultiVRSReader::getAvailableRecordTypes() {
  if (recordTypes_.empty()) {
    initRecordSummaries();
  }
  return recordTypes_;
}

std::set<string> OssMultiVRSReader::getAvailableStreamIds() {
  std::set<string> streamIds;
  for (const auto& streamId : reader_.getStreams()) {
    streamIds.insert(streamId.getNumericName());
  }
  return streamIds;
}

std::map<string, int> OssMultiVRSReader::recordCountByTypeFromStreamId(const string& streamId) {
  if (recordCountsByTypeAndStreamIdMap_.empty()) {
    initRecordSummaries();
  }
  StreamId id = getStreamId(streamId);
  return recordCountsByTypeAndStreamIdMap_[id];
}

py::object OssMultiVRSReader::getTags() {
  const auto& tags = reader_.getTags();
  PyObject* dic = _PyDict_NewPresized(tags.size());
  std::string errors;
  for (const auto& iter : tags) {
    PyDict_SetItem(
        dic,
        unicodeDecode(iter.first, encoding_, errors),
        unicodeDecode(iter.second, encoding_, errors));
  }
  return pyWrap(dic);
}
py::object OssMultiVRSReader::getTags(const string& streamId) {
  StreamId id = getStreamId(streamId);
  const auto& tags = reader_.getTags(id).user;
  PyObject* dic = _PyDict_NewPresized(tags.size());
  std::string errors;
  for (const auto& iter : tags) {
    PyDict_SetItem(
        dic,
        unicodeDecode(iter.first, encoding_, errors),
        unicodeDecode(iter.second, encoding_, errors));
  }
  return pyWrap(dic);
}

std::vector<string> OssMultiVRSReader::getStreams() {
  const auto& streams = reader_.getStreams();
  std::vector<string> streamIds;
  streamIds.reserve(streams.size());
  for (const auto& id : streams) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}
std::vector<string> OssMultiVRSReader::getStreams(RecordableTypeId recordableTypeId) {
  const auto& streams = reader_.getStreams();
  std::vector<string> streamIds;
  for (const auto& id : streams) {
    if (id.getTypeId() == recordableTypeId) {
      streamIds.emplace_back(id.getNumericName());
    }
  }
  return streamIds;
}
std::vector<string> OssMultiVRSReader::getStreams(
    RecordableTypeId recordableTypeId,
    const string& flavor) {
  auto streams = reader_.getStreams(recordableTypeId, flavor);
  std::vector<string> streamIds;
  streamIds.reserve(streams.size());
  for (const auto& id : streams) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}

string OssMultiVRSReader::findStream(
    RecordableTypeId recordableTypeId,
    const string& tagName,
    const string& tagValue) {
  StreamId id = reader_.getStreamForTag(tagName, tagValue, recordableTypeId);
  if (!id.isValid()) {
    throw StreamNotFoundError(recordableTypeId, reader_.getStreams());
  }
  return id.getNumericName();
}

py::object OssMultiVRSReader::getStreamInfo(const string& streamId) {
  StreamId id = getStreamId(streamId);
  PyObject* dic = PyDict_New();
  int config = 0;
  int state = 0;
  int data = 0;
  for (const auto& recordInfo : reader_.getIndex(id)) {
    if (recordInfo->recordType == Record::Type::DATA) {
      data++;
    } else if (recordInfo->recordType == Record::Type::CONFIGURATION) {
      config++;
    } else if (recordInfo->recordType == Record::Type::STATE) {
      state++;
    }
  }
  PyDict_SetItem(dic, pyObject("configuration_records_count"), pyObject(config));
  PyDict_SetItem(dic, pyObject("state_records_count"), pyObject(state));
  PyDict_SetItem(dic, pyObject("data_records_count"), pyObject(data));
  PyDict_SetItem(dic, pyObject("device_name"), pyObject(reader_.getOriginalRecordableTypeName(id)));
  string flavor = reader_.getFlavor(id);
  if (!flavor.empty()) {
    PyDict_SetItem(dic, pyObject("flavor"), pyObject(flavor));
  }
  addStreamInfo(dic, id, Record::Type::CONFIGURATION);
  addStreamInfo(dic, id, Record::Type::STATE);
  addStreamInfo(dic, id, Record::Type::DATA);
  return pyWrap(dic);
}

void OssMultiVRSReader::addStreamInfo(PyObject* dic, const StreamId& id, Record::Type recordType) {
  addRecordInfo(dic, "first_", recordType, reader_.getRecord(id, recordType, 0));
  addRecordInfo(dic, "last_", recordType, reader_.getLastRecord(id, recordType));
}

int64_t OssMultiVRSReader::getStreamSize(const string& streamId) {
  StreamId id = getStreamId(streamId);
  size_t size = 0;
  uint32_t recordCount = reader_.getRecordCount();
  for (uint32_t k = 0; k < recordCount; ++k) {
    if (reader_.getRecord(k)->streamId == id) {
      size += reader_.getRecordSize(k);
    }
  }
  return size;
}

void OssMultiVRSReader::addRecordInfo(
    PyObject* dic,
    const string& prefix,
    Record::Type recordType,
    const IndexRecord::RecordInfo* record) {
  if (record) {
    string type = lowercaseTypeName(recordType);
    int32_t recordIndex = static_cast<int32_t>(reader_.getRecordIndex(record));
    PyDict_SetItem(dic, pyObject(prefix + type + "_record_index"), pyObject(recordIndex));
    PyDict_SetItem(dic, pyObject(prefix + type + "_record_timestamp"), pyObject(record->timestamp));
  }
}

void OssMultiVRSReader::enableStream(const StreamId& id) {
  auto playerByStreamIdIt = playerByStreamIdMap_.emplace(id, *this).first;
  reader_.setStreamPlayer(id, &playerByStreamIdIt->second);
  enabledStreams_.insert(id);
}

void OssMultiVRSReader::enableStream(const string& streamId) {
  enableStream(getStreamId(streamId));
}

int OssMultiVRSReader::enableStreams(RecordableTypeId recordableTypeId, const std::string& flavor) {
  int count = 0;
  auto streams = reader_.getStreams(recordableTypeId, flavor);
  for (auto id : streams) {
    enableStream(id);
    count++;
  }
  return count;
}

int OssMultiVRSReader::enableStreamsByIndexes(const std::vector<int>& indexes) {
  int count = 0;
  const auto& recordables = reader_.getStreams();
  std::vector<StreamId> playable_streams;
  playable_streams.reserve(recordables.size());

  for (const auto& id : recordables) {
    RecordFormatMap formats;
    if (reader_.getRecordFormats(id, formats) > 0) {
      for (const auto& format : formats) {
        if (format.second.getBlocksOfTypeCount(ContentType::IMAGE) > 0) {
          playable_streams.push_back(id);
          break;
        }
      }
    }
  }

  for (const auto& index : indexes) {
    if (index >= playable_streams.size()) {
      continue;
    }
    auto it = playable_streams.begin();
    std::advance(it, index);
    enableStream(*it);
    count++;
  }
  return count;
}

int OssMultiVRSReader::enableAllStreams() {
  const auto& recordables = reader_.getStreams();
  for (const auto& id : recordables) {
    enableStream(id);
  }
  return static_cast<int>(recordables.size());
}

std::vector<string> OssMultiVRSReader::getEnabledStreams() {
  vector<string> streamIds;
  streamIds.reserve(enabledStreams_.size());
  for (const auto& id : enabledStreams_) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}

ImageConversion OssMultiVRSReader::getImageConversion(const StreamId& id) {
  auto iter = streamImageConversion_.find(id);
  return iter == streamImageConversion_.end() ? imageConversion_ : iter->second;
}

void OssMultiVRSReader::setImageConversion(ImageConversion conversion) {
  imageConversion_ = conversion;
  streamImageConversion_.clear();
  resetVideoFrameHandler();
}

void OssMultiVRSReader::setImageConversion(const StreamId& id, ImageConversion conversion) {
  if (!id.isValid()) {
    throw py::value_error("Invalid recordable ID");
  }
  streamImageConversion_[id] = conversion;
  resetVideoFrameHandler(id);
}

void OssMultiVRSReader::setImageConversion(const string& streamId, ImageConversion conversion) {
  setImageConversion(getStreamId(streamId), conversion);
}

int OssMultiVRSReader::setImageConversion(
    RecordableTypeId recordableTypeId,
    ImageConversion conversion) {
  int count = 0;
  for (const auto& id : reader_.getStreams()) {
    if (id.getTypeId() == recordableTypeId) {
      setImageConversion(id, conversion);
      count++;
    }
  }
  return count;
}

int OssMultiVRSReader::getRecordsCount(const string& streamId, const Record::Type recordType) {
  if (recordCountsByTypeAndStreamIdMap_.empty()) {
    initRecordSummaries();
  }
  StreamId id = getStreamId(streamId);
  return recordCountsByTypeAndStreamIdMap_[id][lowercaseTypeName(recordType)];
}

py::object OssMultiVRSReader::getAllRecordsInfo() {
  const uint32_t recordCount = reader_.getRecordCount();
  Py_ssize_t listSize = static_cast<Py_ssize_t>(recordCount);
  PyObject* list = PyList_New(listSize);
  int32_t recordIndex = 0;
  for (uint32_t i = 0; i < recordCount; ++i, ++recordIndex) {
    const auto pyRecordInfo = getRecordInfo(*reader_.getRecord(i), recordIndex);
    PyList_SetItem(list, recordIndex, pyRecordInfo);
  }
  return pyWrap(list);
}

py::object OssMultiVRSReader::getRecordsInfo(int32_t firstIndex, int32_t count) {
  const uint32_t recordCount = reader_.getRecordCount();
  uint32_t first = static_cast<uint32_t>(firstIndex);
  if (first >= recordCount) {
    throw py::stop_iteration("No more records");
  }
  if (count <= 0) {
    throw py::value_error("invalid number of records requested: " + to_string(count));
  }
  uint32_t last = std::min<uint32_t>(recordCount, first + static_cast<uint32_t>(count));
  PyObject* list = PyList_New(static_cast<size_t>(last - first));
  int32_t recordIndex = 0;
  for (uint32_t sourceIndex = first; sourceIndex < last; ++sourceIndex, ++recordIndex) {
    const auto pyRecordInfo = getRecordInfo(*reader_.getRecord(sourceIndex), recordIndex);
    PyList_SetItem(list, recordIndex, pyRecordInfo);
  }
  return pyWrap(list);
}

py::object OssMultiVRSReader::getEnabledStreamsRecordsInfo() {
  if (enabledStreams_.size() == reader_.getStreams().size()) {
    return getAllRecordsInfo(); // we're reading everything: use faster version
  }
  PyObject* list = PyList_New(0);
  if (enabledStreams_.size() > 0) {
    int32_t recordIndex = 0;
    for (uint32_t i = 0; i < reader_.getRecordCount(); ++i, ++recordIndex) {
      const auto* record = reader_.getRecord(i);
      if (enabledStreams_.find(reader_.getUniqueStreamId(record)) != enabledStreams_.end()) {
        auto pyRecordInfo = getRecordInfo(*record, recordIndex);
        PyList_Append(list, pyRecordInfo);
        Py_DECREF(pyRecordInfo);
      }
    }
  }
  return pyWrap(list);
}

py::object OssMultiVRSReader::gotoRecord(int index) {
  nextRecordIndex_ = static_cast<uint32_t>(index);
  return getNextRecordInfo("Invalid record index");
}

py::object OssMultiVRSReader::gotoTime(double timestamp) {
  const IndexRecord::RecordInfo* record = reader_.getRecordByTime(timestamp);
  nextRecordIndex_ = reader_.getRecordIndex(record);
  return getNextRecordInfo("No record found for given time");
}

py::object OssMultiVRSReader::getNextRecordInfo(const char* errorMessage) {
  const uint32_t recordCount = reader_.getRecordCount();
  if (nextRecordIndex_ >= recordCount) {
    nextRecordIndex_ = recordCount;
    throw py::index_error(errorMessage);
  }
  const auto* record = reader_.getRecord(nextRecordIndex_);
  if (record == nullptr) {
    throw py::index_error(errorMessage);
  }
  return pyWrap(getRecordInfo(*record, static_cast<int32_t>(nextRecordIndex_)));
}

py::object OssMultiVRSReader::readNextRecord() {
  skipIgnoredRecords();
  return readNextRecordInternal();
}

py::object OssMultiVRSReader::readNextRecord(const string& streamId) {
  return readNextRecord(streamId, "any");
}

py::object OssMultiVRSReader::readNextRecord(RecordableTypeId recordableTypeId) {
  return readNextRecord(recordableTypeId, "any");
}

py::object OssMultiVRSReader::readNextRecord(const string& streamId, const string& recordType) {
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error(
        "Stream " + streamId + " is not enabled. To read record you need to enable it first.");
  }
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && vrs::helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter: " + recordType);
  }
  while (nextRecordIndex_ < reader_.getRecordCount() &&
         !match(*reader_.getRecord(nextRecordIndex_), id, type)) {
    ++nextRecordIndex_;
  }
  return readNextRecordInternal();
}

py::object OssMultiVRSReader::readNextRecord(RecordableTypeId typeId, const string& recordType) {
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && vrs::helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter: " + recordType);
  }
  bool candidateStreamFound = false;
  for (const auto& id : enabledStreams_) {
    if (id.getTypeId() == typeId) {
      candidateStreamFound = true;
      break;
    }
  }
  if (!candidateStreamFound) {
    throw StreamNotFoundError(typeId, reader_.getStreams());
  }
  while (nextRecordIndex_ < reader_.getRecordCount() &&
         !match(*reader_.getRecord(nextRecordIndex_), typeId, type)) {
    ++nextRecordIndex_;
  }
  return readNextRecordInternal();
}

py::object
OssMultiVRSReader::readRecord(const string& streamId, const string& recordType, int index) {
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error(
        "Stream " + streamId + " is not enabled. To read record you need to enable it first.");
  }
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && vrs::helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter: " + recordType);
  }
  const IndexRecord::RecordInfo* record;
  if (vrs::helpers::strcasecmp(recordType.c_str(), "any") == 0) {
    record = reader_.getRecord(id, static_cast<uint32_t>(index));
  } else {
    record = reader_.getRecord(id, type, static_cast<uint32_t>(index));
  }
  if (record == nullptr) {
    nextRecordIndex_ = static_cast<uint32_t>(reader_.getRecordCount());

    throw py::index_error("Invalid record index");
  }
  nextRecordIndex_ = reader_.getRecordIndex(record);
  return readNextRecordInternal();
}

py::object OssMultiVRSReader::readRecord(int index) {
  if (index < 0 || static_cast<uint32_t>(index) >= reader_.getRecordCount()) {
    throw py::index_error("No record at index: " + to_string(index));
  }
  nextRecordIndex_ = static_cast<uint32_t>(index);
  return readNextRecordInternal();
}

py::object OssMultiVRSReader::readNextRecordInternal() {
  if (nextRecordIndex_ >= reader_.getRecordCount()) {
    throw py::stop_iteration("No more records");
  }

  // Video codecs require sequential decoding of images.
  // Keep the index of last read record and if the nextRecordIndex_ matches, reuse lastRecord_.
  const IndexRecord::RecordInfo& record = *reader_.getRecord(nextRecordIndex_);

  // Automatically read configuration record if specified.
  if (autoReadConfigurationRecord_ && record.recordType == Record::Type::DATA) {
    readConfigurationRecord(record.streamId, nextRecordIndex_);
  }

  int status = reader_.readRecord(record);
  if (status != 0) {
    throw std::runtime_error("Read error: " + errorCodeToMessageWithCode(status));
  }

  auto r = PyRecord(record, nextRecordIndex_, lastRecord_);
  nextRecordIndex_++;
  return py::cast(r);
}

void OssMultiVRSReader::skipTrailingBlocks(
    RecordableTypeId recordableTypeId,
    Record::Type recordType,
    size_t firstTrailingContentBlockIndex) {
  resetVideoFrameHandler();
  if (recordType != Record::Type::UNDEFINED) {
    if (firstTrailingContentBlockIndex) {
      firstSkippedTrailingBlockIndex_[{recordableTypeId, recordType}] =
          firstTrailingContentBlockIndex;
    } else {
      firstSkippedTrailingBlockIndex_.erase({recordableTypeId, recordType});
    }
  } else {
    // "any" value of recordType
    for (auto t :
         {Record::Type::STATE,
          Record::Type::DATA,
          Record::Type::CONFIGURATION,
          Record::Type::TAGS}) {
      if (firstTrailingContentBlockIndex) {
        firstSkippedTrailingBlockIndex_[{recordableTypeId, t}] = firstTrailingContentBlockIndex;
      } else {
        firstSkippedTrailingBlockIndex_.erase({recordableTypeId, t});
      }
    }
  }
}

std::vector<int32_t> OssMultiVRSReader::regenerateEnabledIndices(
    const std::set<string>& recordTypes,
    const std::set<string>& streamIds,
    double minEnabledTimestamp,
    double maxEnabledTimestamp) {
  std::vector<int32_t> enabledIndices;
  std::set<StreamId> streamIdSet;
  std::vector<int> recordTypeExist(static_cast<int>(Record::Type::COUNT));
  for (const auto& recordType : recordTypes) {
    Record::Type type = toEnum<Record::Type>(recordType);
    recordTypeExist[static_cast<int>(type)] = 1;
  }

  for (const auto& streamId : streamIds) {
    const StreamId id = reader_.getStreamForName(streamId);
    if (!id.isValid()) {
      throw StreamNotFoundError(streamId, reader_.getStreams());
    }
    streamIdSet.insert(id);
  }

  for (uint32_t recordIndex = 0; recordIndex < reader_.getRecordCount(); ++recordIndex) {
    const IndexRecord::RecordInfo* record = reader_.getRecord(recordIndex);
    if (record->timestamp > maxEnabledTimestamp) {
      break;
    }
    if (record->timestamp >= minEnabledTimestamp) {
      if (recordTypeExist[static_cast<int>(record->recordType)] &&
          streamIdSet.find(reader_.getUniqueStreamId(record)) != streamIdSet.end()) {
        enabledIndices.push_back(recordIndex);
      }
    }
  }
  return enabledIndices;
}

double OssMultiVRSReader::getTimestampForIndex(int idx) {
  const IndexRecord::RecordInfo* record = reader_.getRecord(static_cast<uint32_t>(idx));
  if (record == nullptr) {
    throw py::index_error("Index out of range.");
  }
  return record->timestamp;
}

string OssMultiVRSReader::getStreamIdForIndex(int recordIndex) {
  if (recordIndex < 0 || recordIndex >= reader_.getRecordCount()) {
    throw py::index_error("Index out of range.");
  }
  return getUniqueStreamIdForRecordIndex(static_cast<uint32_t>(recordIndex)).getNumericName();
}

string OssMultiVRSReader::getSerialNumberForStream(const string& streamId) const {
  const StreamId id = reader_.getStreamForName(streamId);
  if (!id.isValid()) {
    throw StreamNotFoundError(streamId, reader_.getStreams());
  }
  return reader_.getSerialNumber(id);
}

string OssMultiVRSReader::getStreamForSerialNumber(const string& streamSerialNumber) const {
  return reader_.getStreamForSerialNumber(streamSerialNumber).getNumericName();
}

int32_t OssMultiVRSReader::getRecordIndexByTime(const string& streamId, double timestamp) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getRecordByTime(id, timestamp);
  if (record == nullptr) {
    throw py::value_error(
        "No record at timestamp " + to_string(timestamp) + " in stream " + streamId);
  }
  return reader_.getRecordIndex(record);
}

int32_t OssMultiVRSReader::getRecordIndexByTime(
    const string& streamId,
    Record::Type recordType,
    double timestamp) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getRecordByTime(id, recordType, timestamp);

  if (record == nullptr) {
    throw py::value_error(
        "No record at timestamp " + to_string(timestamp) + " in stream " + streamId);
  }
  return reader_.getRecordIndex(record);
}

int32_t OssMultiVRSReader::getNearestRecordIndexByTime(
    double timestamp,
    double epsilon,
    const string& streamId) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getNearestRecordByTime(timestamp, epsilon, id);
  if (record == nullptr) {
    throw TimestampNotFoundError(timestamp, epsilon, id);
  }
  return reader_.getRecordIndex(record);
}

int32_t OssMultiVRSReader::getNearestRecordIndexByTime(
    double timestamp,
    double epsilon,
    const string& streamId,
    Record::Type recordType) {
  StreamId id = getStreamId(streamId);

  auto record = reader_.getNearestRecordByTime(timestamp, epsilon, id, recordType);
  if (record == nullptr) {
    throw TimestampNotFoundError(timestamp, epsilon, id, recordType);
  }
  return reader_.getRecordIndex(record);
}

std::vector<double> OssMultiVRSReader::getTimestampListForIndices(
    const std::vector<int32_t>& indices) {
  std::vector<double> timestamps;
  timestamps.reserve(indices.size());
  for (const auto idx : indices) {
    const IndexRecord::RecordInfo* record = reader_.getRecord(static_cast<uint32_t>(idx));
    if (record == nullptr) {
      throw py::index_error("Index out of range.");
    }
    timestamps.push_back(record->timestamp);
  }
  return timestamps;
}

int32_t
OssMultiVRSReader::getNextIndex(const string& streamId, const string& recordType, int index) {
  const StreamId id = getStreamId(streamId);
  Record::Type type = toEnum<Record::Type>(recordType);
  uint32_t nextIndex = static_cast<uint32_t>(index);
  const IndexRecord::RecordInfo* record;
  while ((record = reader_.getRecord(nextIndex)) != nullptr && !match(*record, id, type)) {
    nextIndex++;
  }
  if (record == nullptr) {
    throw py::index_error("There are no record for " + streamId + " after " + to_string(index));
  }
  return nextIndex;
}

int32_t
OssMultiVRSReader::getPrevIndex(const string& streamId, const string& recordType, int index) {
  StreamId id = getStreamId(streamId);
  Record::Type type = toEnum<Record::Type>(recordType);
  int prevIndex = index;
  const IndexRecord::RecordInfo* record = nullptr;
  while (prevIndex >= 0 &&
         (record = reader_.getRecord(static_cast<uint32_t>(prevIndex))) != nullptr &&
         !match(*record, id, type)) {
    prevIndex--;
  }
  if (record == nullptr) {
    throw py::index_error("There are no record for " + streamId + " before " + to_string(index));
  }
  return prevIndex;
}

bool OssMultiVRSReader::setCachingStrategy(CachingStrategy cachingStrategy) {
  return reader_.setCachingStrategy(cachingStrategy);
}

CachingStrategy OssMultiVRSReader::getCachingStrategy() const {
  return reader_.getCachingStrategy();
}

bool OssMultiVRSReader::prefetchRecordSequence(
    const vector<uint32_t>& recordIndexes,
    bool clearSequence) {
  vector<const IndexRecord::RecordInfo*> records;
  records.reserve(recordIndexes.size());
  for (const auto recordIndex : recordIndexes) {
    const IndexRecord::RecordInfo* record = reader_.getRecord(recordIndex);
    if (record == nullptr) {
      XR_LOGW("Attempting to prefetch invalid index {}", recordIndex);
      return false;
    } else {
      records.push_back(record);
    }
  }
  return reader_.prefetchRecordSequence(records, clearSequence);
}

bool OssMultiVRSReader::purgeFileCache() {
  return reader_.purgeFileCache();
}

void OssMultiVRSReader::readConfigurationRecord(const StreamId& streamId, uint32_t idx) {
  if (configIndex_.empty()) {
    for (uint32_t i = 0; i < reader_.getRecordCount(); i++) {
      const auto* record = reader_.getRecord(i);
      if (record->recordType == Record::Type::CONFIGURATION) {
        configIndex_[record->streamId].push_back(i);
      }
    }
  }
  auto it = lower_bound(configIndex_[streamId].begin(), configIndex_[streamId].end(), idx);
  if (it != configIndex_[streamId].begin()) {
    it--;
  } else {
    XR_LOGE("{} doesn't have config record before reading {}", streamId.getNumericName(), idx);
    return;
  }

  if (lastReadConfigIndex_[streamId] == *it) {
    return;
  }

  const IndexRecord::RecordInfo& record = *reader_.getRecord(*it);
  int status = reader_.readRecord(record);
  if (status != 0) {
    throw py::index_error("Failed to read prior configuration record.");
  }
  lastReadConfigIndex_[streamId] = *it;
  // Need to clear lastRecord_ here to avoid having content block for both config record & data
  // record.
  lastRecord_.clear();
}

void OssMultiVRSReader::resetVideoFrameHandler() {
  for (auto& player : playerByStreamIdMap_) {
    player.second.resetVideoFrameHandler();
  }
}

void OssMultiVRSReader::resetVideoFrameHandler(const StreamId& id) {
  for (auto& player : playerByStreamIdMap_) {
    player.second.resetVideoFrameHandler(id);
  }
}

void OssMultiVRSReader::skipIgnoredRecords() {
  while (nextRecordIndex_ < reader_.getRecordCount() &&
         enabledStreams_.find(getUniqueStreamIdForRecordIndex(nextRecordIndex_)) ==
             enabledStreams_.end()) {
    nextRecordIndex_++;
  }
}

void OssMultiVRSReader::initRecordSummaries() {
  recordCountsByTypeAndStreamIdMap_.clear();
  recordTypes_.clear();

  int recordTypeSize = static_cast<int>(Record::Type::COUNT);
  std::vector<int> countsByRecordType(recordTypeSize);
  std::map<StreamId, std::vector<int>> recordCountsByTypeAndStreamId;
  for (const auto& streamId : reader_.getStreams()) {
    recordCountsByTypeAndStreamId[streamId] = std::vector<int>(recordTypeSize);
  }

  for (uint32_t recordIndex = 0; recordIndex < reader_.getRecordCount(); ++recordIndex) {
    const IndexRecord::RecordInfo& record = *reader_.getRecord(recordIndex);
    recordCountsByTypeAndStreamId[reader_.getUniqueStreamId(&record)]
                                 [static_cast<int>(record.recordType)]++;
    countsByRecordType[static_cast<int>(record.recordType)]++;
  }

  for (int i = 0; i < recordTypeSize; i++) {
    if (countsByRecordType[i] > 0) {
      string type = lowercaseTypeName(static_cast<Record::Type>(i));
      recordTypes_.insert(type);
    }
  }

  for (const auto& recordCounts : recordCountsByTypeAndStreamId) {
    recordCountsByTypeAndStreamIdMap_[recordCounts.first][lowercaseTypeName(
        Record::Type::CONFIGURATION)] =
        recordCounts.second[static_cast<int>(Record::Type::CONFIGURATION)];
    recordCountsByTypeAndStreamIdMap_[recordCounts.first][lowercaseTypeName(Record::Type::DATA)] =
        recordCounts.second[static_cast<int>(Record::Type::DATA)];
    recordCountsByTypeAndStreamIdMap_[recordCounts.first][lowercaseTypeName(Record::Type::STATE)] =
        recordCounts.second[static_cast<int>(Record::Type::STATE)];
  }
}

StreamId OssMultiVRSReader::getStreamId(const string& streamId) {
  // "NNN-DDD" or "NNN+DDD", two uint numbers separated by a '-' or '+'.
  const StreamId id = reader_.getStreamForName(streamId);
  if (!id.isValid()) {
    throw StreamNotFoundError(streamId, reader_.getStreams());
  }
  return id;
}

PyObject* OssMultiVRSReader::getRecordInfo(
    const IndexRecord::RecordInfo& record,
    int32_t recordIndex) {
  PyObject* dic = PyDict_New();
  pyDict_SetItemWithDecRef(dic, pyObject("record_index"), pyObject(recordIndex));
  string type = lowercaseTypeName(record.recordType);
  pyDict_SetItemWithDecRef(dic, pyObject("record_type"), pyObject(type));
  pyDict_SetItemWithDecRef(dic, pyObject("record_timestamp"), pyObject(record.timestamp));
  const std::string streamIdName = reader_.getUniqueStreamId(&record).getNumericName();
  pyDict_SetItemWithDecRef(dic, pyObject("stream_id"), pyObject(streamIdName));
  pyDict_SetItemWithDecRef(dic, pyObject("recordable_id"), pyObject(streamIdName));
  return dic;
}

bool OssMultiVRSReader::match(
    const IndexRecord::RecordInfo& record,
    StreamId id,
    Record::Type recordType) const {
  return reader_.getUniqueStreamId(&record) == id &&
      enabledStreams_.find(id) != enabledStreams_.end() &&
      (recordType == Record::Type::UNDEFINED || record.recordType == recordType);
}
bool OssMultiVRSReader::match(
    const IndexRecord::RecordInfo& record,
    RecordableTypeId typeId,
    Record::Type recordType) const {
  const UniqueStreamId steamId = reader_.getUniqueStreamId(&record);
  return steamId.getTypeId() == typeId && enabledStreams_.find(steamId) != enabledStreams_.end() &&
      (recordType == Record::Type::UNDEFINED || record.recordType == recordType);
}

#if IS_VRS_OSS_CODE()
void pybind_multivrsreader(py::module& m) {
  // WARNING: Do not use `MultiReader` for production code yet as the behavior/APIs might change.
  py::class_<PyMultiVRSReader>(m, "MultiReader")
      .def(py::init<bool>())
      .def("open", py::overload_cast<const std::string&>(&PyMultiVRSReader::open))
      .def("open", py::overload_cast<const PyFileSpec&>(&PyMultiVRSReader::open))
      .def("open", py::overload_cast<const std::vector<std::string>&>(&PyMultiVRSReader::open))
      .def("open", py::overload_cast<const std::vector<PyFileSpec>&>(&PyMultiVRSReader::open))
      .def("close", &PyMultiVRSReader::close)
      .def("set_encoding", &PyMultiVRSReader::setEncoding)
      .def("get_encoding", &PyMultiVRSReader::getEncoding)
      .def(
          "set_image_conversion",
          py::overload_cast<pyvrs::ImageConversion>(&PyMultiVRSReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<const std::string&, pyvrs::ImageConversion>(
              &PyMultiVRSReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<vrs::RecordableTypeId, pyvrs::ImageConversion>(
              &PyMultiVRSReader::setImageConversion))
      .def("get_file_chunks", &PyMultiVRSReader::getFileChunks)
      .def("get_streams", py::overload_cast<>(&PyMultiVRSReader::getStreams))
      .def("get_streams", py::overload_cast<vrs::RecordableTypeId>(&PyMultiVRSReader::getStreams))
      .def(
          "get_streams",
          py::overload_cast<vrs::RecordableTypeId, const std::string&>(
              &PyMultiVRSReader::getStreams))
      .def("find_stream", &PyMultiVRSReader::findStream)
      .def("get_stream_info", &PyMultiVRSReader::getStreamInfo)
      .def("get_stream_size", &PyMultiVRSReader::getStreamSize)
      .def("enable_stream", py::overload_cast<const string&>(&PyMultiVRSReader::enableStream))
      .def("enable_streams", &PyMultiVRSReader::enableStreams)
      .def("enable_streams_by_indexes", &PyMultiVRSReader::enableStreamsByIndexes)
      .def("enable_all_streams", &PyMultiVRSReader::enableAllStreams)
      .def("get_enabled_streams", &PyMultiVRSReader::getEnabledStreams)
      .def("get_all_records_info", &PyMultiVRSReader::getAllRecordsInfo)
      .def("get_records_info", &PyMultiVRSReader::getRecordsInfo)
      .def("get_enabled_streams_records_info", &PyMultiVRSReader::getEnabledStreamsRecordsInfo)
      .def("goto_record", &PyMultiVRSReader::gotoRecord)
      .def("goto_time", &PyMultiVRSReader::gotoTime)
      .def("read_next_record", py::overload_cast<>(&PyMultiVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<const std::string&>(&PyMultiVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<const std::string&, const std::string&>(
              &PyMultiVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<vrs::RecordableTypeId>(&PyMultiVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<vrs::RecordableTypeId, const std::string&>(
              &PyMultiVRSReader::readNextRecord))
      .def(
          "read_record",
          py::overload_cast<const std::string&, const std::string&, int>(
              &PyMultiVRSReader::readRecord))
      .def("read_record", py::overload_cast<int>(&PyMultiVRSReader::readRecord))
      .def("skip_trailing_blocks", &PyMultiVRSReader::skipTrailingBlocks)
      .def("get_tags", py::overload_cast<>(&PyMultiVRSReader::getTags))
      .def("get_tags", py::overload_cast<const std::string&>(&PyMultiVRSReader::getTags))
      .def("get_max_available_timestamp", &PyMultiVRSReader::getMaxAvailableTimestamp)
      .def("get_min_available_timestamp", &PyMultiVRSReader::getMinAvailableTimestamp)
      .def("get_available_record_types", &PyMultiVRSReader::getAvailableRecordTypes)
      .def("get_available_stream_ids", &PyMultiVRSReader::getAvailableStreamIds)
      .def("get_timestamp_for_index", &PyMultiVRSReader::getTimestampForIndex)
      .def("get_records_count", &PyMultiVRSReader::getRecordsCount)
      .def("get_available_records_size", &PyMultiVRSReader::getAvailableRecordsSize)
      .def("record_count_by_type_from_stream_id", &PyMultiVRSReader::recordCountByTypeFromStreamId)
      .def("regenerate_enabled_indices", &PyMultiVRSReader::regenerateEnabledIndices)
      .def("get_stream_id_for_index", &PyMultiVRSReader::getStreamIdForIndex)
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, double>(&PyMultiVRSReader::getRecordIndexByTime))
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, Record::Type, double>(
              &PyMultiVRSReader::getRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&>(
              &PyMultiVRSReader::getNearestRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&, Record::Type>(
              &PyMultiVRSReader::getNearestRecordIndexByTime))
      .def("get_timestamp_list_for_indices", &PyMultiVRSReader::getTimestampListForIndices)
      .def("get_next_index", &PyMultiVRSReader::getNextIndex)
      .def("get_prev_index", &PyMultiVRSReader::getPrevIndex)
      .def("set_caching_strategy", &PyMultiVRSReader::setCachingStrategy)
      .def("get_caching_strategy", &PyMultiVRSReader::getCachingStrategy)
      .def(
          "prefetch_record_sequence",
          &PyMultiVRSReader::prefetchRecordSequence,
          py::arg("sequence"),
          py::arg("clearSequence") = true)
      .def("purge_file_cache", &PyMultiVRSReader::purgeFileCache);
}
#endif
} // namespace pyvrs
