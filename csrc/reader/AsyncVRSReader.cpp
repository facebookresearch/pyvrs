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

#include "AsyncVRSReader.h"

#define DEFAULT_LOG_CHANNEL "AsyncVRSReader"
#include <logging/Log.h>
#include <logging/Verify.h>

#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <vrs/helpers/Strings.h>
#include <vrs/utils/PixelFrame.h>

namespace pyvrs {

void AsyncReadJob::performJob(OssAsyncVRSReader& reader) {
  py::gil_scoped_acquire acquire;
  py::object record = reader.readRecord(index_);
  loop_.attr("call_soon_threadsafe")(future_.attr("set_result"), record);
}

void AsyncReadJob::performJob(OssAsyncMultiVRSReader& reader) {
  py::gil_scoped_acquire acquire;
  py::object record = reader.readRecord(index_);
  loop_.attr("call_soon_threadsafe")(future_.attr("set_result"), record);
}

AwaitableRecord
OssAsyncVRSReader::asyncReadRecord(const string& streamId, const string& recordType, int index) {
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error("Stream not setup for reading");
  }
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && vrs::helpers::strcasecmp(recordType.c_str(), "any") != 0) {
    throw py::value_error("Unsupported record type filter");
  }
  const IndexRecord::RecordInfo* record;
  if (vrs::helpers::strcasecmp(recordType.c_str(), "any") == 0) {
    record = reader_.getRecord(id, static_cast<uint32_t>(index));
  } else {
    record = reader_.getRecord(id, type, static_cast<uint32_t>(index));
  }
  if (record == nullptr) {
    nextRecordIndex_ = static_cast<uint32_t>(reader_.getIndex().size());
    throw py::index_error("Invalid record index");
  }
  return AwaitableRecord(static_cast<uint32_t>(record - reader_.getIndex().data()), workerQueue_);
}

AwaitableRecord OssAsyncVRSReader::asyncReadRecord(int index) {
  if (static_cast<size_t>(index) >= reader_.getIndex().size()) {
    throw py::index_error("No record for this index");
  }
  return AwaitableRecord(static_cast<uint32_t>(index), workerQueue_);
}

OssAsyncVRSReader::~OssAsyncVRSReader() {
  shouldEndAsyncThread_ = true;
  if (asyncThread_.joinable()) {
    asyncThread_.join();
  }
  reader_.closeFile();
}

void OssAsyncVRSReader::asyncThreadActivity() {
  std::unique_ptr<AsyncJob> job;
  while (!shouldEndAsyncThread_) {
    if (workerQueue_.waitForJob(job, 1) && !shouldEndAsyncThread_) {
      job->performJob(*this);
    }
  }
}

AwaitableRecord OssAsyncMultiVRSReader::asyncReadRecord(
    const string& streamId,
    const string& recordType,
    int index) {
  StreamId id = getStreamId(streamId);
  if (enabledStreams_.find(id) == enabledStreams_.end()) {
    throw py::value_error("Stream not setup for reading");
  }
  bool anyRecordType = vrs::helpers::strcasecmp(recordType.c_str(), "any") == 0;
  Record::Type type = toEnum<Record::Type>(recordType);
  if (type == Record::Type::UNDEFINED && !anyRecordType) {
    throw py::value_error("Unsupported record type filter");
  }
  const IndexRecord::RecordInfo* record;
  if (anyRecordType) {
    record = reader_.getRecord(id, static_cast<uint32_t>(index));
  } else {
    record = reader_.getRecord(id, type, static_cast<uint32_t>(index));
  }
  if (record == nullptr) {
    nextRecordIndex_ = reader_.getRecordCount();
    throw py::index_error("Invalid record index: " + to_string(index));
  }
  return AwaitableRecord(reader_.getRecordIndex(record), workerQueue_);
}

AwaitableRecord OssAsyncMultiVRSReader::asyncReadRecord(int index) {
  if (static_cast<uint32_t>(index) >= reader_.getRecordCount()) {
    throw py::index_error("No record for this index");
  }
  return AwaitableRecord(static_cast<uint32_t>(index), workerQueue_);
}

OssAsyncMultiVRSReader::~OssAsyncMultiVRSReader() {
  shouldEndAsyncThread_ = true;
  if (asyncThread_.joinable()) {
    asyncThread_.join();
  }
  reader_.close();
}

void OssAsyncMultiVRSReader::asyncThreadActivity() {
  std::unique_ptr<AsyncJob> job;
  while (!shouldEndAsyncThread_) {
    if (workerQueue_.waitForJob(job, 1) && !shouldEndAsyncThread_) {
      job->performJob(*this);
    }
  }
}

#if IS_VRS_OSS_CODE()
void pybind_asyncvrsreaders(py::module& m) {
  py::class_<PyAsyncReader>(m, "AsyncReader")
      .def(py::init<bool&>())
      .def("open", py::overload_cast<const std::string&>(&PyAsyncReader::open))
      .def("open", py::overload_cast<const PyFileSpec&>(&PyAsyncReader::open))
      .def("close", &PyAsyncReader::close)
      .def("set_encoding", &PyAsyncReader::setEncoding)
      .def("get_encoding", &PyAsyncReader::getEncoding)
      .def(
          "set_image_conversion",
          py::overload_cast<pyvrs::ImageConversion>(&PyAsyncReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<const std::string&, pyvrs::ImageConversion>(
              &PyAsyncReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<vrs::RecordableTypeId, pyvrs::ImageConversion>(
              &PyAsyncReader::setImageConversion))
      .def("get_file_chunks", &PyAsyncReader::getFileChunks)
      .def("get_streams", py::overload_cast<>(&PyAsyncReader::getStreams))
      .def("get_streams", py::overload_cast<vrs::RecordableTypeId>(&PyAsyncReader::getStreams))
      .def(
          "get_streams",
          py::overload_cast<vrs::RecordableTypeId, const std::string&>(&PyAsyncReader::getStreams))
      .def("get_stream_for_flavor", &PyAsyncReader::getStreamForFlavor)
      .def("find_stream", &PyAsyncReader::findStream)
      .def("get_stream_info", &PyAsyncReader::getStreamInfo)
      .def("get_stream_size", &PyAsyncReader::getStreamSize)
      .def("enable_stream", py::overload_cast<const string&>(&PyAsyncReader::enableStream))
      .def("enable_streams", &PyAsyncReader::enableStreams)
      .def("enable_streams_by_indexes", &PyAsyncReader::enableStreamsByIndexes)
      .def("enable_all_streams", &PyAsyncReader::enableAllStreams)
      .def("get_enabled_streams", &PyAsyncReader::getEnabledStreams)
      .def("get_all_records_info", &PyAsyncReader::getAllRecordsInfo)
      .def("get_records_info", &PyAsyncReader::getRecordsInfo)
      .def("get_enabled_streams_records_info", &PyAsyncReader::getEnabledStreamsRecordsInfo)
      .def("might_contain_images", &PyAsyncReader::mightContainImages)
      .def("might_contain_audio", &PyAsyncReader::mightContainAudio)
      .def(
          "async_read_record",
          py::overload_cast<const std::string&, const std::string&, int>(
              &PyAsyncReader::asyncReadRecord))
      .def("async_read_record", py::overload_cast<int>(&PyAsyncReader::asyncReadRecord))
      .def("skip_trailing_blocks", &PyAsyncReader::skipTrailingBlocks)
      .def("get_tags", py::overload_cast<>(&PyAsyncReader::getTags))
      .def("get_tags", py::overload_cast<const std::string&>(&PyAsyncReader::getTags))
      .def("get_max_available_timestamp", &PyAsyncReader::getMaxAvailableTimestamp)
      .def("get_min_available_timestamp", &PyAsyncReader::getMinAvailableTimestamp)
      .def("get_available_record_types", &PyAsyncReader::getAvailableRecordTypes)
      .def("get_available_stream_ids", &PyAsyncReader::getAvailableStreamIds)
      .def("get_timestamp_for_index", &PyAsyncReader::getTimestampForIndex)
      .def("get_records_count", &PyAsyncReader::getRecordsCount)
      .def("get_available_records_size", &PyAsyncReader::getAvailableRecordsSize)
      .def("record_count_by_type_from_stream_id", &PyAsyncReader::recordCountByTypeFromStreamId)
      .def("regenerate_enabled_indices", &PyAsyncReader::regenerateEnabledIndices)
      .def("get_stream_id_for_index", &PyAsyncReader::getStreamIdForIndex)
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, double>(&PyAsyncReader::getRecordIndexByTime))
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, Record::Type, double>(
              &PyAsyncReader::getRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&>(
              &PyAsyncReader::getNearestRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&, Record::Type>(
              &PyAsyncReader::getNearestRecordIndexByTime))
      .def("get_timestamp_list_for_indices", &PyAsyncReader::getTimestampListForIndices)
      .def("get_next_index", &PyAsyncReader::getNextIndex)
      .def("get_prev_index", &PyAsyncReader::getPrevIndex);

  py::class_<PyAsyncMultiReader>(m, "AsyncMultiReader")
      .def(py::init<bool>())
      .def("open", py::overload_cast<const std::string&>(&PyAsyncMultiReader::open))
      .def("open", py::overload_cast<const PyFileSpec&>(&PyAsyncMultiReader::open))
      .def("open", py::overload_cast<const std::vector<std::string>&>(&PyAsyncMultiReader::open))
      .def("open", py::overload_cast<const std::vector<PyFileSpec>&>(&PyAsyncMultiReader::open))
      .def("close", &PyAsyncMultiReader::close)
      .def("set_encoding", &PyAsyncMultiReader::setEncoding)
      .def("get_encoding", &PyAsyncMultiReader::getEncoding)
      .def(
          "set_image_conversion",
          py::overload_cast<pyvrs::ImageConversion>(&PyAsyncMultiReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<const std::string&, pyvrs::ImageConversion>(
              &PyAsyncMultiReader::setImageConversion))
      .def(
          "set_image_conversion",
          py::overload_cast<vrs::RecordableTypeId, pyvrs::ImageConversion>(
              &PyAsyncMultiReader::setImageConversion))
      .def("get_file_chunks", &PyAsyncMultiReader::getFileChunks)
      .def("get_streams", py::overload_cast<>(&PyAsyncMultiReader::getStreams))
      .def("get_streams", py::overload_cast<vrs::RecordableTypeId>(&PyAsyncMultiReader::getStreams))
      .def(
          "get_streams",
          py::overload_cast<vrs::RecordableTypeId, const std::string&>(
              &PyAsyncMultiReader::getStreams))
      .def("find_stream", &PyAsyncMultiReader::findStream)
      .def("get_stream_info", &PyAsyncMultiReader::getStreamInfo)
      .def("get_stream_size", &PyAsyncMultiReader::getStreamSize)
      .def("enable_stream", py::overload_cast<const string&>(&PyAsyncMultiReader::enableStream))
      .def("enable_streams", &PyAsyncMultiReader::enableStreams)
      .def("enable_streams_by_indexes", &PyAsyncMultiReader::enableStreamsByIndexes)
      .def("enable_all_streams", &PyAsyncMultiReader::enableAllStreams)
      .def("get_enabled_streams", &PyAsyncMultiReader::getEnabledStreams)
      .def("get_all_records_info", &PyAsyncMultiReader::getAllRecordsInfo)
      .def("get_records_info", &PyAsyncMultiReader::getRecordsInfo)
      .def("get_enabled_streams_records_info", &PyAsyncMultiReader::getEnabledStreamsRecordsInfo)
      .def(
          "async_read_record",
          py::overload_cast<const std::string&, const std::string&, int>(
              &PyAsyncMultiReader::asyncReadRecord))
      .def("async_read_record", py::overload_cast<int>(&PyAsyncMultiReader::asyncReadRecord))
      .def("skip_trailing_blocks", &PyAsyncMultiReader::skipTrailingBlocks)
      .def("get_tags", py::overload_cast<>(&PyAsyncMultiReader::getTags))
      .def("get_tags", py::overload_cast<const std::string&>(&PyAsyncMultiReader::getTags))
      .def("get_max_available_timestamp", &PyAsyncMultiReader::getMaxAvailableTimestamp)
      .def("get_min_available_timestamp", &PyAsyncMultiReader::getMinAvailableTimestamp)
      .def("get_available_record_types", &PyAsyncMultiReader::getAvailableRecordTypes)
      .def("get_available_stream_ids", &PyAsyncMultiReader::getAvailableStreamIds)
      .def("get_timestamp_for_index", &PyAsyncMultiReader::getTimestampForIndex)
      .def("get_records_count", &PyAsyncMultiReader::getRecordsCount)
      .def("get_available_records_size", &PyAsyncMultiReader::getAvailableRecordsSize)
      .def(
          "record_count_by_type_from_stream_id", &PyAsyncMultiReader::recordCountByTypeFromStreamId)
      .def("regenerate_enabled_indices", &PyAsyncMultiReader::regenerateEnabledIndices)
      .def("get_stream_id_for_index", &PyAsyncMultiReader::getStreamIdForIndex)
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, double>(&PyAsyncMultiReader::getRecordIndexByTime))
      .def(
          "get_record_index_by_time",
          py::overload_cast<const string&, Record::Type, double>(
              &PyAsyncMultiReader::getRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&>(
              &PyAsyncMultiReader::getNearestRecordIndexByTime))
      .def(
          "get_nearest_record_index_by_time",
          py::overload_cast<double, double, const string&, Record::Type>(
              &PyAsyncMultiReader::getNearestRecordIndexByTime))
      .def("get_timestamp_list_for_indices", &PyAsyncMultiReader::getTimestampListForIndices)
      .def("get_next_index", &PyAsyncMultiReader::getNextIndex)
      .def("get_prev_index", &PyAsyncMultiReader::getPrevIndex);

  py::class_<AwaitableRecord>(m, "AwaitableRecord")
      .def("__await__", [](const AwaitableRecord& awaitable) {
        py::object loop, fut;
        {
          py::gil_scoped_acquire acquire;
          loop = py::module_::import("asyncio.events").attr("get_event_loop")();
          fut = loop.attr("create_future")();
        }
        unique_ptr<AsyncJob> job = make_unique<AsyncReadJob>(loop, fut, awaitable.getIndex());
        awaitable.scheduleJob(std::move(job));
        return fut.attr("__await__")();
      });
}
#endif
} // namespace pyvrs
