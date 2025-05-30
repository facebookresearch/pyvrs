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

#include "VRSReader.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>

#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define DEFAULT_LOG_CHANNEL "VRSReader"
#include <logging/Log.h>
#include <logging/Verify.h>

#include <vrs/ErrorCode.h>
#include <vrs/RecordFormat.h>
#include <vrs/helpers/Strings.h>
#include <vrs/utils/FrameRateEstimator.h>
#include <vrs/utils/PixelFrame.h>

#include "../VrsBindings.h"
#include "../utils/PyExceptions.h"
#include "../utils/PyRecord.h"
#include "../utils/PyUtils.h"
#include "FactoryHelper.hpp"

namespace {

PyObject* getRecordInfo(const IndexRecord::RecordInfo& record, int32_t recordIndex) {
  PyObject* dic = PyDict_New();
  pyDict_SetItemWithDecRef(dic, pyObject("record_index"), pyObject(recordIndex));
  string type = lowercaseTypeName(record.recordType);
  pyDict_SetItemWithDecRef(dic, pyObject("record_type"), pyObject(type));
  pyDict_SetItemWithDecRef(dic, pyObject("record_timestamp"), pyObject(record.timestamp));
  pyDict_SetItemWithDecRef(dic, pyObject("stream_id"), pyObject(record.streamId.getNumericName()));
  pyDict_SetItemWithDecRef(
      dic, pyObject("recordable_id"), pyObject(record.streamId.getNumericName()));
  return dic;
}

} // namespace

namespace pyvrs {
using namespace vrs;
using namespace std;

void OssVRSReader::init() {
  initVrsBindings();
}

void OssVRSReader::open(const PyFileSpec& spec) {
  return open(spec.getSpec());
}

void OssVRSReader::open(const string& path) {
  FileSpec spec;
  if (RecordFileReader::vrsFilePathToFileSpec(path, spec) != 0) {
    throw runtime_error("Invalid path given: " + path);
  }
  return open(spec);
}

void OssVRSReader::open(const FileSpec& spec) {
  nextRecordIndex_ = 0;
  int status = reader_.openFile(spec);
  if (status != 0) {
    throw runtime_error(
        fmt::format("Could not open '{}': {}", spec, errorCodeToMessageWithCode(status)));
  }
  if (autoReadConfigurationRecord_) {
    for (const auto& streamId : reader_.getStreams()) {
      lastReadConfigIndex_[streamId] = numeric_limits<uint32_t>::max();
    }
  }
}

int OssVRSReader::close() {
  return reader_.closeFile();
}

void OssVRSReader::setEncoding(const string& encoding) {
  encoding_ = encoding;
}

string OssVRSReader::getEncoding() {
  return encoding_;
}

py::object OssVRSReader::getFileChunks() const {
  vector<pair<string, int64_t>> chunks = reader_.getFileChunks();
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

double OssVRSReader::getMaxAvailableTimestamp() {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (index.empty()) {
    return 0;
  }
  return index.back().timestamp;
}

double OssVRSReader::getMinAvailableTimestamp() {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (index.empty()) {
    return 0;
  }
  return index.front().timestamp;
}

size_t OssVRSReader::getAvailableRecordsSize() {
  return reader_.getIndex().size();
}

set<string> OssVRSReader::getAvailableRecordTypes() {
  if (recordTypes_.empty()) {
    initRecordSummaries();
  }
  return recordTypes_;
}

set<string> OssVRSReader::getAvailableStreamIds() {
  set<string> streamIds;
  for (const auto& streamId : reader_.getStreams()) {
    streamIds.insert(streamId.getNumericName());
  }
  return streamIds;
}

map<string, int> OssVRSReader::recordCountByTypeFromStreamId(const string& streamId) {
  if (recordCountsByTypeAndStreamIdMap_.empty()) {
    initRecordSummaries();
  }
  StreamId id = getStreamId(streamId);
  return recordCountsByTypeAndStreamIdMap_[id];
}

bool OssVRSReader::VRSReaderStreamPlayer::processRecordHeader(
    const CurrentRecord& record,
    DataReference& outDataRef) {
  reader_.lastRecord_.recordFormatVersion = record.formatVersion;
  return RecordFormatStreamPlayer::processRecordHeader(record, outDataRef);
}

PyObject* dataLayoutToPyDict(DataLayout& dl, const string& encoding) {
  PyObject* dic = PyDict_New();
  dl.forEachDataPiece(
      [dic](const DataPiece* piece) { getDataPieceValuePyObjectorRegistry().map(dic, piece); },
      DataPieceType::Value);

  dl.forEachDataPiece(
      [dic](const DataPiece* piece) { getDataPieceArrayPyObjectorRegistry().map(dic, piece); },
      DataPieceType::Array);

  dl.forEachDataPiece(
      [dic](const DataPiece* piece) { getDataPieceVectorPyObjectorRegistry().map(dic, piece); },
      DataPieceType::Vector);

  dl.forEachDataPiece(
      [dic, &encoding](const DataPiece* piece) {
        getDataPieceStringMapPyObjectorRegistry().map(dic, piece, encoding);
      },
      DataPieceType::StringMap);

  dl.forEachDataPiece(
      [dic, &encoding](const DataPiece* piece) {
        const auto& value = reinterpret_cast<const DataPieceString*>(piece)->get();
        string errors;
        pyDict_SetItemWithDecRef(
            dic,
            Py_BuildValue("(s,s)", piece->getLabel().c_str(), "string"),
            unicodeDecode(value, encoding, errors));
      },
      DataPieceType::String);
  return dic;
}

PyObject* BaseVRSReaderStreamPlayer::readDataLayout(DataLayout& dl, const string& encoding) {
  return dataLayoutToPyDict(dl, encoding);
}

bool OssVRSReader::VRSReaderStreamPlayer::onDataLayoutRead(
    const CurrentRecord& record,
    size_t blkIdx,
    DataLayout& dl) {
  reader_.lastRecord_.datalayoutBlocks.emplace_back(pyWrap(readDataLayout(dl, reader_.encoding_)));
  return checkSkipTrailingBlocks(record, blkIdx);
}

bool OssVRSReader::VRSReaderStreamPlayer::onImageRead(
    const CurrentRecord& record,
    size_t blkIdx,
    const ContentBlock& cb) {
  return setBlock(reader_.lastRecord_.images, record, blkIdx, cb);
}

bool OssVRSReader::VRSReaderStreamPlayer::onAudioRead(
    const CurrentRecord& record,
    size_t blkIdx,
    const ContentBlock& cb) {
  return setBlock(reader_.lastRecord_.audioBlocks, record, blkIdx, cb);
}

bool OssVRSReader::VRSReaderStreamPlayer::onCustomBlockRead(
    const CurrentRecord& rec,
    size_t bix,
    const ContentBlock& cb) {
  return setBlock(reader_.lastRecord_.customBlocks, rec, bix, cb);
}

bool OssVRSReader::VRSReaderStreamPlayer::onUnsupportedBlock(
    const CurrentRecord& cr,
    size_t bix,
    const ContentBlock& cb) {
  return setBlock(reader_.lastRecord_.unsupportedBlocks, cr, bix, cb);
}

bool OssVRSReader::VRSReaderStreamPlayer::checkSkipTrailingBlocks(
    const CurrentRecord& record,
    size_t blockIndex) {
  // check whether we should stop reading further as the next content block will be considered
  // trailing for this device type and currently processed record
  auto trailingBlockCount = reader_.firstSkippedTrailingBlockIndex_.find(
      {record.streamId.getTypeId(), record.recordType});
  if (trailingBlockCount != reader_.firstSkippedTrailingBlockIndex_.end()) {
    return (blockIndex + 1) < trailingBlockCount->second;
  } else {
    return true;
  }
}

ImageConversion OssVRSReader::VRSReaderStreamPlayer::getImageConversion(
    const CurrentRecord& record) {
  return reader_.getImageConversion(record.streamId);
}

bool BaseVRSReaderStreamPlayer::setBlock(
    vector<ContentBlockBuffer>& blocks,
    const CurrentRecord& record,
    size_t blockIndex,
    const ContentBlock& contentBlock) {
  // When we are reading video encoded files "and" we jump to certain frame, this method will be
  // called for the non-requested frame.
  // This is because if there is a missing frames (we need to start reading from the key frame), we
  // call readMissingFrames via recordReadComplete method.
  // This directly invokes the readRecord in RecordFileReader and doesn't go through any of
  // readRecord method in VRSReader class, which doesn't reset the last read records.
  // We need to make sure that the lastRecord_ only contains the block that the requested record's
  // block.
  if (blocks.size() >= blockIndex) {
    blocks.clear();
  }
  blocks.emplace_back(contentBlock);
  size_t blockSize = contentBlock.getBlockSize();
  if (blockSize == ContentBlock::kSizeUnknown) {
    XR_LOGW("Block size unknown for {}", contentBlock.asString());
    return false;
  }
  if (blockSize > 0) {
    ContentBlockBuffer& block = blocks.back();
    ImageConversion imageConversion = getImageConversion(record);
    if (contentBlock.getContentType() != ContentType::IMAGE ||
        imageConversion == ImageConversion::Off) {
      // default handling
      auto& data = block.bytes;
      data.resize(blockSize);
      record.reader->read(data);
      block.bytesAdjusted = false;
      if (contentBlock.getContentType() == ContentType::IMAGE) {
        // for raw images, we return a structured array based on the image format
        block.structuredArray = (contentBlock.image().getImageFormat() == ImageFormat::RAW);
      } else {
        block.structuredArray = (contentBlock.getContentType() == ContentType::AUDIO);
      }
    } else if (imageConversion == ImageConversion::RawBuffer) {
      // default handling
      auto& data = block.bytes;
      data.resize(blockSize);
      record.reader->read(data);
      block.bytesAdjusted = false;
      block.structuredArray = false;
    } else {
      // image conversion handling
      if (imageConversion == ImageConversion::RecordUnreadBytesBackdoor) {
        // Grab all remaining bytes in the record (NOTE: including
        // the bytes of any subsequent content blocks!) and return
        // them as a byte image of height 1. This is a backdoor for
        // accessing image content block data in legacy VRS files
        // with incorrect image specs that cannot practically be
        // rewritten to be compliant. This backdoor should be used
        // with care, and only as a last resort.
        uint32_t unreadBytes = record.reader->getUnreadBytes();
        block.spec = ContentBlock(
            ImageContentBlockSpec(
                ImageFormat::RAW,
                PixelFormat::GREY8,
                unreadBytes, // width
                1), // height
            unreadBytes);
        auto& data = block.bytes;
        data.resize(unreadBytes);
        record.reader->read(data);
        block.structuredArray = false;
      } else {
        shared_ptr<utils::PixelFrame> frame = make_shared<utils::PixelFrame>();
        bool frameValid = readFrame(*frame, record, contentBlock);
        if (XR_VERIFY(frameValid)) {
          block.structuredArray = true;
          // the image was read & maybe decompressed. Does it need to be converted, too?
          if (imageConversion == ImageConversion::Normalize ||
              imageConversion == ImageConversion::NormalizeGrey8) {
            shared_ptr<utils::PixelFrame> convertedFrame;
            bool grey16supported = (imageConversion == ImageConversion::Normalize);
            utils::PixelFrame::normalizeFrame(frame, convertedFrame, grey16supported);
            block.spec = convertedFrame->getSpec();
            block.bytes.swap(convertedFrame->getBuffer());
          } else {
            block.spec = frame->getSpec();
            block.bytes.swap(frame->getBuffer());
          }
        } else {
          // we failed to produce something usable, just return the raw buffer...
          block.structuredArray = false;
        }
      }
    }
  }
  return checkSkipTrailingBlocks(record, blockIndex);
}

py::object OssVRSReader::getTags() {
  const auto& tags = reader_.getTags();
  PyObject* dic = _PyDict_NewPresized(tags.size());
  string errors;
  for (const auto& iter : tags) {
    PyDict_SetItem(
        dic,
        unicodeDecode(iter.first, encoding_, errors),
        unicodeDecode(iter.second, encoding_, errors));
  }
  return pyWrap(dic);
}

py::object OssVRSReader::getTags(const string& streamId) {
  StreamId id = getStreamId(streamId);
  const auto& tags = reader_.getTags(id).user;
  PyObject* dic = _PyDict_NewPresized(tags.size());
  string errors;
  for (const auto& iter : tags) {
    PyDict_SetItem(
        dic,
        unicodeDecode(iter.first, encoding_, errors),
        unicodeDecode(iter.second, encoding_, errors));
  }
  return pyWrap(dic);
}

vector<string> OssVRSReader::getStreams() {
  const auto& streams = reader_.getStreams();
  vector<string> streamIds;
  streamIds.reserve(streams.size());
  for (const auto& id : streams) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}

vector<string> OssVRSReader::getStreams(RecordableTypeId recordableTypeId) {
  const auto& streams = reader_.getStreams();
  vector<string> streamIds;
  for (const auto& id : streams) {
    if (id.getTypeId() == recordableTypeId) {
      streamIds.emplace_back(id.getNumericName());
    }
  }
  return streamIds;
}

vector<string> OssVRSReader::getStreams(RecordableTypeId recordableTypeId, const string& flavor) {
  auto streams = reader_.getStreams(recordableTypeId, flavor);
  vector<string> streamIds;
  streamIds.reserve(streams.size());
  for (const auto& id : streams) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}

string OssVRSReader::getStreamForFlavor(
    RecordableTypeId recordableTypeId,
    const string& flavor,
    const uint32_t indexNumber) {
  auto stream = reader_.getStreamForFlavor(recordableTypeId, flavor, indexNumber);
  return stream.getNumericName();
}

string OssVRSReader::findStream(
    RecordableTypeId recordableTypeId,
    const string& tagName,
    const string& tagValue) {
  StreamId id = reader_.getStreamForTag(tagName, tagValue, recordableTypeId);
  if (!id.isValid()) {
    throw StreamNotFoundError(recordableTypeId, reader_.getStreams());
  }
  return id.getNumericName();
}

py::object OssVRSReader::getStreamInfo(const string& streamId) {
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

void OssVRSReader::addStreamInfo(PyObject* dic, const StreamId& id, Record::Type recordType) {
  addRecordInfo(dic, "first_", recordType, reader_.getRecord(id, recordType, 0));
  addRecordInfo(dic, "last_", recordType, reader_.getLastRecord(id, recordType));
}

int64_t OssVRSReader::getStreamSize(const string& streamId) {
  StreamId id = getStreamId(streamId);
  int64_t size = 0;
  const auto& index = reader_.getIndex();
  for (uint32_t k = 0; k < index.size(); ++k) {
    if (index[k].streamId == id) {
      size += reader_.getRecordSize(k);
    }
  }
  return size;
}

void OssVRSReader::addRecordInfo(
    PyObject* dic,
    const string& prefix,
    Record::Type recordType,
    const IndexRecord::RecordInfo* record) {
  if (record) {
    string type = lowercaseTypeName(recordType);
    int32_t recordIndex = static_cast<int32_t>(record - reader_.getIndex().data());
    PyDict_SetItem(dic, pyObject(prefix + type + "_record_index"), pyObject(recordIndex));
    PyDict_SetItem(dic, pyObject(prefix + type + "_record_timestamp"), pyObject(record->timestamp));
  }
}

void OssVRSReader::enableStream(const StreamId& id) {
  reader_.setStreamPlayer(id, &streamPlayer_);
  enabledStreams_.insert(id);
}

void OssVRSReader::enableStream(const string& streamId) {
  enableStream(getStreamId(streamId));
}

int OssVRSReader::enableStreams(RecordableTypeId recordableTypeId, const string& flavor) {
  int count = 0;
  auto streams = reader_.getStreams(recordableTypeId, flavor);
  for (auto id : streams) {
    enableStream(id);
    count++;
  }
  return count;
}

int OssVRSReader::enableStreamsByIndexes(const vector<int>& indexes) {
  int count = 0;
  const auto& recordables = reader_.getStreams();
  vector<StreamId> playable_streams;
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
    advance(it, index);
    enableStream(*it);
    count++;
  }
  return count;
}

int OssVRSReader::enableAllStreams() {
  const auto& recordables = reader_.getStreams();
  for (const auto& id : recordables) {
    enableStream(id);
  }
  return static_cast<int>(recordables.size());
}

vector<string> OssVRSReader::getEnabledStreams() {
  vector<string> streamIds;
  streamIds.reserve(enabledStreams_.size());
  for (const auto& id : enabledStreams_) {
    streamIds.emplace_back(id.getNumericName());
  }
  return streamIds;
}

ImageConversion OssVRSReader::getImageConversion(const StreamId& id) {
  auto iter = streamImageConversion_.find(id);
  return iter == streamImageConversion_.end() ? imageConversion_ : iter->second;
}

void OssVRSReader::setImageConversion(ImageConversion conversion) {
  imageConversion_ = conversion;
  streamImageConversion_.clear();
  streamPlayer_.resetVideoFrameHandler();
}

void OssVRSReader::setImageConversion(const StreamId& id, ImageConversion conversion) {
  if (!id.isValid()) {
    throw py::value_error("Invalid stream ID: " + id.getFullName());
  }
  streamImageConversion_[id] = conversion;
  streamPlayer_.resetVideoFrameHandler(id);
}

void OssVRSReader::setImageConversion(const string& streamId, ImageConversion conversion) {
  setImageConversion(getStreamId(streamId), conversion);
}

int OssVRSReader::setImageConversion(
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

bool OssVRSReader::mightContainImages(const string& streamId) {
  return reader_.mightContainImages(getStreamId(streamId));
}

bool OssVRSReader::mightContainAudio(const string& streamId) {
  return reader_.mightContainAudio(getStreamId(streamId));
}

double OssVRSReader::getEstimatedFrameRate(const string& streamId) {
  return utils::frameRateEstimationFps(reader_.getIndex(), getStreamId(streamId));
}

int OssVRSReader::getRecordsCount(const string& streamId, const Record::Type recordType) {
  if (recordCountsByTypeAndStreamIdMap_.empty()) {
    initRecordSummaries();
  }
  StreamId id = getStreamId(streamId);
  return recordCountsByTypeAndStreamIdMap_[id][lowercaseTypeName(recordType)];
}

py::object OssVRSReader::getAllRecordsInfo() {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  Py_ssize_t listSize = static_cast<Py_ssize_t>(index.size());
  PyObject* list = PyList_New(listSize);
  int32_t recordIndex = 0;
  for (const auto& record : index) {
    auto pyRecordInfo = getRecordInfo(record, recordIndex);
    PyList_SetItem(list, recordIndex, pyRecordInfo);
    recordIndex++;
  }
  return pyWrap(list);
}

py::object OssVRSReader::getRecordsInfo(int32_t firstIndex, int32_t count) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  size_t first = static_cast<size_t>(firstIndex);
  if (first >= index.size()) {
    throw py::stop_iteration("No more records");
  }
  if (count <= 0) {
    throw py::value_error("Invalid number of records requested: " + to_string(count));
  }
  size_t last = min<size_t>(index.size(), first + static_cast<size_t>(count));
  PyObject* list = PyList_New(last - first);
  int32_t recordIndex = 0;
  for (size_t sourceIndex = first; sourceIndex < last; ++sourceIndex, ++recordIndex) {
    auto pyRecordInfo = getRecordInfo(index[sourceIndex], sourceIndex);
    PyList_SetItem(list, recordIndex, pyRecordInfo);
  }
  return pyWrap(list);
}

py::object OssVRSReader::getEnabledStreamsRecordsInfo() {
  if (enabledStreams_.size() == reader_.getStreams().size()) {
    return getAllRecordsInfo(); // we're reading everything: use faster version
  }
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  PyObject* list = PyList_New(0);
  if (enabledStreams_.size() > 0) {
    int32_t recordIndex = 0;
    for (const auto& record : index) {
      if (enabledStreams_.find(record.streamId) != enabledStreams_.end()) {
        auto pyRecordInfo = getRecordInfo(record, recordIndex);
        PyList_Append(list, pyRecordInfo);
        Py_DECREF(pyRecordInfo);
      }
      recordIndex++;
    }
  }
  return pyWrap(list);
}

py::object OssVRSReader::gotoRecord(int index) {
  nextRecordIndex_ = static_cast<uint32_t>(index);
  return getNextRecordInfo("Invalid record index");
}

py::object OssVRSReader::gotoTime(double timestamp) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  IndexRecord::RecordInfo seekTime;
  seekTime.timestamp = timestamp;
  nextRecordIndex_ =
      static_cast<uint32_t>(lower_bound(index.begin(), index.end(), seekTime) - index.begin());
  return getNextRecordInfo("No record found for given time");
}

py::object OssVRSReader::getNextRecordInfo(const char* errorMessage) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (nextRecordIndex_ >= index.size()) {
    nextRecordIndex_ = static_cast<uint32_t>(index.size());
    throw py::index_error(errorMessage);
  }
  return pyWrap(getRecordInfo(index[nextRecordIndex_], static_cast<int32_t>(nextRecordIndex_)));
}

py::object OssVRSReader::readNextRecord() {
  skipIgnoredRecords();
  return readNextRecordInternal();
}

py::object OssVRSReader::readNextRecord(const string& streamId) {
  return readNextRecord(streamId, "any");
}

py::object OssVRSReader::readNextRecord(RecordableTypeId recordableTypeId) {
  return readNextRecord(recordableTypeId, "any");
}

py::object OssVRSReader::readNextRecord(const string& streamId, const string& recordType) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error(
        "Stream " + streamId + " is not enabled. To read record you need to enable it first.");
  }
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter: " + recordType);
  }
  while (nextRecordIndex_ < index.size() && !match(index[nextRecordIndex_], id, type)) {
    ++nextRecordIndex_;
  }
  return readNextRecordInternal();
}
py::object OssVRSReader::readNextRecord(RecordableTypeId typeId, const string& recordType) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && helpers::strcasecmp(recordType.c_str(), "any") != 0) {
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
  while (nextRecordIndex_ < index.size() && !match(index[nextRecordIndex_], typeId, type)) {
    ++nextRecordIndex_;
  }
  return readNextRecordInternal();
}

py::object OssVRSReader::readRecord(int index) {
  if (static_cast<uint32_t>(index) >= reader_.getIndex().size()) {
    throw py::index_error("No record at index: " + to_string(index));
  }
  nextRecordIndex_ = static_cast<uint32_t>(index);
  return readNextRecordInternal();
}

py::object OssVRSReader::readRecord(const string& streamId, const string& recordType, int index) {
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error(
        "Stream " + streamId + " is not enabled. To read record you need to enable it first.");
  }
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter: " + recordType);
  }
  const IndexRecord::RecordInfo* record;
  if (helpers::strcasecmp(recordType.c_str(), "any") == 0) {
    record = reader_.getRecord(id, static_cast<uint32_t>(index));
  } else {
    record = reader_.getRecord(id, type, static_cast<uint32_t>(index));
  }
  if (record == nullptr) {
    nextRecordIndex_ = static_cast<uint32_t>(reader_.getIndex().size());
    throw py::index_error("Invalid record index");
  }
  nextRecordIndex_ = static_cast<uint32_t>(record - reader_.getIndex().data());
  return readNextRecordInternal();
}

py::object OssVRSReader::readNextRecordInternal() {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (nextRecordIndex_ >= index.size()) {
    throw py::stop_iteration("No more records");
  }

  // Video codecs require sequential decoding of images.
  // Keep the index of last read record and if the nextRecordIndex_ matches, reuse lastRecord_.
  const IndexRecord::RecordInfo& record = index[nextRecordIndex_];

  if (autoReadConfigurationRecord_ && record.recordType == Record::Type::DATA) {
    readConfigurationRecord(record.streamId, nextRecordIndex_);
  }

  int status = reader_.readRecord(record);
  if (status != 0) {
    throw runtime_error("Read error: " + errorCodeToMessageWithCode(status));
  }

  return py::cast(PyRecord(record, nextRecordIndex_++, lastRecord_));
}

void OssVRSReader::readConfigurationRecord(const StreamId& streamId, uint32_t idx) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (configIndex_.empty()) {
    for (uint32_t i = 0; i < index.size(); i++) {
      if (index[i].recordType == Record::Type::CONFIGURATION) {
        configIndex_[index[i].streamId].push_back(i);
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

  const IndexRecord::RecordInfo& record = index[*it];
  int status = reader_.readRecord(record);
  if (status != 0) {
    throw py::index_error("Failed to read prior configuration record.");
  }
  lastReadConfigIndex_[streamId] = *it;
  // Need to clear lastRecord_ here to avoid having content block for both config record & data
  // record.
  lastRecord_.clear();
}

void OssVRSReader::skipIgnoredRecords() {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  while (nextRecordIndex_ < index.size() &&
         enabledStreams_.find(index[nextRecordIndex_].streamId) == enabledStreams_.end()) {
    nextRecordIndex_++;
  }
}

void OssVRSReader::skipTrailingBlocks(
    RecordableTypeId recordableTypeId,
    Record::Type recordType,
    size_t firstTrailingContentBlockIndex) {
  streamPlayer_.resetVideoFrameHandler();
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

vector<int32_t> OssVRSReader::regenerateEnabledIndices(
    const set<string>& recordTypes,
    const set<string>& streamIds,
    double minEnabledTimestamp,
    double maxEnabledTimestamp) {
  vector<int32_t> enabledIndices;
  int32_t recordIndex = 0;
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();

  set<StreamId> streamIdSet;
  vector<int> recordTypeExist(static_cast<int>(Record::Type::COUNT));
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

  for (const auto& record : index) {
    if (record.timestamp > maxEnabledTimestamp) {
      break;
    }
    if (record.timestamp >= minEnabledTimestamp) {
      if (recordTypeExist[static_cast<int>(record.recordType)] &&
          streamIdSet.find(record.streamId) != streamIdSet.end()) {
        enabledIndices.push_back(recordIndex);
      }
    }
    recordIndex++;
  }
  return enabledIndices;
}

double OssVRSReader::getTimestampForIndex(int idx) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (idx < 0 || idx >= index.size()) {
    throw py::index_error("Index out of range.");
  }
  const IndexRecord::RecordInfo& record = index[idx];
  return record.timestamp;
}

string OssVRSReader::getStreamIdForIndex(int recordIndex) {
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  if (recordIndex < 0 || recordIndex >= index.size()) {
    throw py::index_error("Index out of range.");
  }
  const IndexRecord::RecordInfo& record = index[recordIndex];
  return record.streamId.getNumericName();
}

string OssVRSReader::getSerialNumberForStream(const string& streamId) const {
  const StreamId id = reader_.getStreamForName(streamId);
  if (!id.isValid()) {
    throw StreamNotFoundError(streamId, reader_.getStreams());
  }
  return reader_.getSerialNumber(id);
}

string OssVRSReader::getStreamForSerialNumber(const string& streamSerialNumber) const {
  return reader_.getStreamForSerialNumber(streamSerialNumber).getNumericName();
}

int32_t OssVRSReader::getRecordIndexByTime(const string& streamId, double timestamp) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getRecordByTime(id, timestamp);
  if (record == nullptr) {
    throw py::value_error(
        "No record at timestamp " + to_string(timestamp) + " in stream " + streamId);
  }
  return reader_.getRecordIndex(record);
}

int32_t OssVRSReader::getRecordIndexByTime(
    const string& streamId,
    const Record::Type recordType,
    double timestamp) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getRecordByTime(id, recordType, timestamp);
  if (record == nullptr) {
    throw py::value_error(
        "No record at timestamp " + to_string(timestamp) + " in stream " + streamId);
  }
  return reader_.getRecordIndex(record);
}

int32_t OssVRSReader::getNearestRecordIndexByTime(
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

int32_t OssVRSReader::getNearestRecordIndexByTime(
    double timestamp,
    double epsilon,
    const string& streamId,
    const Record::Type recordType) {
  StreamId id = getStreamId(streamId);
  auto record = reader_.getNearestRecordByTime(timestamp, epsilon, id, recordType);
  if (record == nullptr) {
    throw TimestampNotFoundError(timestamp, epsilon, id, recordType);
  }
  return reader_.getRecordIndex(record);
}

vector<double> OssVRSReader::getTimestampListForIndices(const vector<int32_t>& indices) {
  vector<double> timestamps;
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  for (auto idx : indices) {
    if (idx < 0 || idx >= index.size()) {
      throw py::index_error("Index out of range.");
    }
    const IndexRecord::RecordInfo& record = index[idx];
    timestamps.push_back(record.timestamp);
  }
  return timestamps;
}

int32_t OssVRSReader::getNextIndex(const string& streamId, const string& recordType, int index) {
  const vector<IndexRecord::RecordInfo>& indexes = reader_.getIndex();
  StreamId id = getStreamId(streamId);
  Record::Type type = toEnum<Record::Type>(recordType);
  int nextIndex = index;
  while (nextIndex < indexes.size() && !match(indexes[nextIndex], id, type)) {
    nextIndex++;
  }
  if (nextIndex == indexes.size()) {
    throw py::index_error("There are no record for " + streamId + " after " + to_string(index));
  }
  return nextIndex;
}

int32_t OssVRSReader::getPrevIndex(const string& streamId, const string& recordType, int index) {
  const vector<IndexRecord::RecordInfo>& indexes = reader_.getIndex();
  StreamId id = getStreamId(streamId);
  Record::Type type = toEnum<Record::Type>(recordType);
  int prevIndex = index;
  while (prevIndex >= 0 && !match(indexes[prevIndex], id, type)) {
    prevIndex--;
  }
  if (prevIndex < 0) {
    throw py::index_error("There are no record for " + streamId + " before " + to_string(index));
  }
  return prevIndex;
}

void OssVRSReader::initRecordSummaries() {
  recordCountsByTypeAndStreamIdMap_.clear();
  recordTypes_.clear();

  int recordTypeSize = static_cast<int>(Record::Type::COUNT);
  vector<int> countsByRecordType(recordTypeSize);
  map<StreamId, vector<int>> recordCountsByTypeAndStreamId;
  for (const auto& streamId : reader_.getStreams()) {
    recordCountsByTypeAndStreamId[streamId] = vector<int>(recordTypeSize);
  }

  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  for (const auto& record : index) {
    recordCountsByTypeAndStreamId[record.streamId][static_cast<int>(record.recordType)]++;
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

StreamId OssVRSReader::getStreamId(const string& streamId) {
  // "NNN-DDD" or "NNN+DDD", two uint numbers separated by a '-' or '+'.
  const StreamId id = reader_.getStreamForName(streamId);
  if (!id.isValid()) {
    throw StreamNotFoundError(streamId, reader_.getStreams());
  }
  return id;
}

bool OssVRSReader::match(
    const IndexRecord::RecordInfo& record,
    StreamId id,
    Record::Type recordType) const {
  return record.streamId == id && enabledStreams_.find(id) != enabledStreams_.end() &&
      (recordType == Record::Type::UNDEFINED || record.recordType == recordType);
}
bool OssVRSReader::match(
    const IndexRecord::RecordInfo& record,
    RecordableTypeId typeId,
    Record::Type recordType) const {
  return record.streamId.getTypeId() == typeId &&
      enabledStreams_.find(record.streamId) != enabledStreams_.end() &&
      (recordType == Record::Type::UNDEFINED || record.recordType == recordType);
}

bool OssVRSReader::setCachingStrategy(CachingStrategy cachingStrategy) {
  return reader_.setCachingStrategy(cachingStrategy);
}
CachingStrategy OssVRSReader::getCachingStrategy() const {
  return reader_.getCachingStrategy();
}

bool OssVRSReader::prefetchRecordSequence(
    const vector<uint32_t>& recordIndexes,
    bool clearSequence) {
  vector<const IndexRecord::RecordInfo*> records;
  records.reserve(recordIndexes.size());
  const vector<IndexRecord::RecordInfo>& index = reader_.getIndex();
  for (const auto recordIndex : recordIndexes) {
    if (recordIndex < index.size()) {
      records.push_back(&index[recordIndex]);
    }
  }
  return reader_.prefetchRecordSequence(records, clearSequence);
}

bool OssVRSReader::purgeFileCache() {
  return reader_.purgeFileCache();
}

#if IS_VRS_OSS_CODE()
void pybind_vrsreader(py::module& m) {
  py::class_<PyVRSReader>(m, "Reader")
      .def(py::init<>())
      .def(py::init<bool>())
      .def(py::init<const string&>())
      .def(py::init<const PyFileSpec&>())
      .def(py::init<const string&, bool>())
      .def(py::init<const PyFileSpec&, bool>())
      .def("open", py::overload_cast<const string&>(&PyVRSReader::open))
      .def("open", py::overload_cast<const PyFileSpec&>(&PyVRSReader::open))
      .def("close", &PyVRSReader::close)
      .def("set_encoding", &PyVRSReader::setEncoding)
      .def("get_encoding", &PyVRSReader::getEncoding)
      .def(
          "set_image_conversion",
          py::overload_cast<pyvrs::ImageConversion>(&PyVRSReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<const string&, pyvrs::ImageConversion>(
              &PyVRSReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<RecordableTypeId, pyvrs::ImageConversion>(
              &PyVRSReader::setImageConversion))
      .def("get_file_chunks", &PyVRSReader::getFileChunks)
      .def("get_streams", py::overload_cast<>(&PyVRSReader::getStreams))
      .def("get_streams", py::overload_cast<RecordableTypeId>(&PyVRSReader::getStreams))
      .def(
          "get_streams",
          py::overload_cast<RecordableTypeId, const string&>(&PyVRSReader::getStreams))
      .def("get_stream_for_flavor", &PyVRSReader::getStreamForFlavor)
      .def("find_stream", &PyVRSReader::findStream)
      .def("get_stream_info", &PyVRSReader::getStreamInfo)
      .def("get_stream_size", &PyVRSReader::getStreamSize)
      .def("enable_stream", py::overload_cast<const string&>(&PyVRSReader::enableStream))
      .def("enable_streams", &PyVRSReader::enableStreams)
      .def("enable_streams_by_indexes", &PyVRSReader::enableStreamsByIndexes)
      .def("enable_all_streams", &PyVRSReader::enableAllStreams)
      .def("get_enabled_streams", &PyVRSReader::getEnabledStreams)
      .def("get_all_records_info", &PyVRSReader::getAllRecordsInfo)
      .def("get_records_info", &PyVRSReader::getRecordsInfo)
      .def("get_enabled_streams_records_info", &PyVRSReader::getEnabledStreamsRecordsInfo)
      .def("might_contain_images", &PyVRSReader::mightContainImages)
      .def("might_contain_audio", &PyVRSReader::mightContainAudio)
      .def("get_estimated_frame_rate", &PyVRSReader::getEstimatedFrameRate)
      .def("goto_record", &PyVRSReader::gotoRecord)
      .def("goto_time", &PyVRSReader::gotoTime)
      .def("read_next_record", py::overload_cast<>(&PyVRSReader::readNextRecord))
      .def("read_next_record", py::overload_cast<const string&>(&PyVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<const string&, const string&>(&PyVRSReader::readNextRecord))
      .def("read_next_record", py::overload_cast<RecordableTypeId>(&PyVRSReader::readNextRecord))
      .def(
          "read_next_record",
          py::overload_cast<RecordableTypeId, const string&>(&PyVRSReader::readNextRecord))
      .def(
          "read_record",
          py::overload_cast<const string&, const string&, int>(&PyVRSReader::readRecord))
      .def("read_record", py::overload_cast<int>(&PyVRSReader::readRecord))
      .def("skip_trailing_blocks", &PyVRSReader::skipTrailingBlocks)
      .def("get_tags", py::overload_cast<>(&PyVRSReader::getTags))
      .def("get_tags", py::overload_cast<const string&>(&PyVRSReader::getTags))
      .def("get_max_available_timestamp", &PyVRSReader::getMaxAvailableTimestamp)
      .def("get_min_available_timestamp", &PyVRSReader::getMinAvailableTimestamp)
      .def("get_available_record_types", &PyVRSReader::getAvailableRecordTypes)
      .def("get_available_stream_ids", &PyVRSReader::getAvailableStreamIds)
      .def("get_timestamp_for_index", &PyVRSReader::getTimestampForIndex)
      .def("get_records_count", &PyVRSReader::getRecordsCount)
      .def("get_available_records_size", &PyVRSReader::getAvailableRecordsSize)
      .def("record_count_by_type_from_stream_id", &PyVRSReader::recordCountByTypeFromStreamId)
      .def("regenerate_enabled_indices", &PyVRSReader::regenerateEnabledIndices)
      .def("get_stream_id_for_index", &PyVRSReader::getStreamIdForIndex)
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, double>(&PyVRSReader::getRecordIndexByTime))
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, Record::Type, double>(
              &PyVRSReader::getRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&>(
              &PyVRSReader::getNearestRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&, Record::Type>(
              &PyVRSReader::getNearestRecordIndexByTime))
      .def("get_timestamp_list_for_indices", &PyVRSReader::getTimestampListForIndices)
      .def("get_next_index", &PyVRSReader::getNextIndex)
      .def("get_prev_index", &PyVRSReader::getPrevIndex)
      .def("set_caching_strategy", &PyVRSReader::setCachingStrategy)
      .def("get_caching_strategy", &PyVRSReader::getCachingStrategy)
      .def(
          "prefetch_record_sequence",
          &PyVRSReader::prefetchRecordSequence,
          py::arg("sequence"),
          py::arg("clearSequence") = true)
      .def("purge_file_cache", &PyVRSReader::purgeFileCache)
      .def(
          "__enter__",
          [](PyVRSReader& r) { return &r; },
          "Enter the runtime context related to this VRSReader")
      .def(
          "__exit__",
          [](PyVRSReader& r, const py::args&) { r.close(); },
          "Exit the runtime context related to this VRSReader");
}
#endif
} // namespace pyvrs
