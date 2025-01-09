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

#include <memory>
#include <string>

#include <pybind11/pybind11.h>

#include <vrs/RecordFileReader.h>
#include <vrs/RecordFormat.h>
#include <vrs/helpers/JobQueue.h>

#include "MultiVRSReader.h"
#include "VRSReader.h"

namespace pyvrs {

using namespace vrs;
using namespace std;

/// \brief The base class for asynchronous job
/// This class captures the asyncio's event loop, and creates a future for that loop so that we can
/// later call loop.call_soon_threadsafe(future.set_result(<result>)) to set that future's result.
class AsyncJob {
 public:
  AsyncJob()
      : loop_{py::module_::import("asyncio").attr("get_running_loop")()},
        future_{loop_.attr("create_future")()} {}
  AsyncJob(const AsyncJob& other) = delete;
  virtual ~AsyncJob() = default;

  AsyncJob& operator=(const AsyncJob&) = delete;

  virtual void performJob(VRSReaderBase& reader) = 0;

  py::object await() {
    return future_.attr("__await__")();
  }

 protected:
  py::object loop_;
  py::object future_;
};

/// \brief Asynchronous job class for reading a record
class AsyncReadJob : public AsyncJob {
 public:
  explicit AsyncReadJob(uint32_t index) : index_(index) {}

  void performJob(VRSReaderBase& reader) override;

 private:
  uint32_t index_;
};

using AsyncJobQueue = JobQueue<std::unique_ptr<AsyncJob>>;
class AsyncReadHandler;

/// \brief Python awaitable record
/// This class only exposes __await__ method to Python which does the following:
/// - Creates an AsyncReadJob object that:
///   1 - captures asyncio.events.get_running_loop,
///   2 - creates a future for that loop via loop.create_future(),
///   3 - captures the record index
/// - Send a job to AsyncReader's background thread
/// - Call future.__await__() and Python side waits until set_result will be called by AsyncReader
class AwaitableRecord {
 public:
  AwaitableRecord(uint32_t index, AsyncReadHandler& readHandler);
  AwaitableRecord(const AwaitableRecord& other);

  py::object await() const;

 private:
  uint32_t index_;
  AsyncReadHandler& readHandler_;
};

/// \brief Helper class to manage the background async thread
class AsyncReadHandler {
 public:
  AsyncReadHandler(VRSReaderBase& reader)
      : reader_{reader}, asyncThread_(&AsyncReadHandler::asyncThreadActivity, this) {}

  VRSReaderBase& getReader() const {
    return reader_;
  }
  AsyncJobQueue& getQueue() {
    return workerQueue_;
  }

  void asyncThreadActivity();
  void cleanup();

 private:
  VRSReaderBase& reader_;
  AsyncJobQueue workerQueue_;
  atomic<bool> shouldEndAsyncThread_ = false;
  thread asyncThread_;
};

/// \brief The async VRSReader class
/// This class extends VRSReader and adds asynchronous APIs to read records.
/// AsyncVRSReader spawns a background thread to process the async jobs.
class OssAsyncVRSReader : public OssVRSReader {
 public:
  explicit OssAsyncVRSReader(bool autoReadConfigurationRecord)
      : OssVRSReader(autoReadConfigurationRecord), readHandler_{*this} {}

  ~OssAsyncVRSReader() override;

  /// Read a stream's record, by record type & index.
  /// @param streamId: VRS stream id to read.
  /// @param recordType: record type to read, or "any".
  /// @param index: the index of the record to read.
  /// @return AwaitableRecord: by calling await, caller will receive the same data as calling
  /// readRecord
  AwaitableRecord asyncReadRecord(const string& streamId, const string& recordType, int index);

  /// Read a specifc record, by index.
  /// @param index: a record index.
  /// @return AwaitableRecord: by calling await, caller will receive the same data as calling
  /// readRecord
  AwaitableRecord asyncReadRecord(int index);

 private:
  AsyncReadHandler readHandler_;
};

/// \brief The async MultiVRSReader class
/// This class extends VRSReader and adds asynchronous APIs to read records.
/// AsyncVRSReader spawns a background thread to process the async jobs.
class OssAsyncMultiVRSReader : public OssMultiVRSReader {
 public:
  explicit OssAsyncMultiVRSReader(bool autoReadConfigurationRecord)
      : OssMultiVRSReader(autoReadConfigurationRecord), readHandler_{*this} {}

  ~OssAsyncMultiVRSReader() override;

  /// Read a stream's record, by record type & index.
  /// @param streamId: VRS stream id to read.
  /// @param recordType: record type to read, or "any".
  /// @param index: the index of the record to read.
  /// @return AwaitableRecord: by calling await, caller will receive the same data as calling
  /// readRecord
  AwaitableRecord asyncReadRecord(const string& streamId, const string& recordType, int index);

  /// Read a specifc record, by index.
  /// @param index: a record index.
  /// @return AwaitableRecord: by calling await, caller will receive the same data as calling
  /// readRecord
  AwaitableRecord asyncReadRecord(int index);

 private:
  AsyncReadHandler readHandler_;
};

/// Binds methods and classes for AsyncVRSReader.
void pybind_asyncvrsreaders(py::module& m);
} // namespace pyvrs

#if IS_VRS_OSS_CODE()
using PyAsyncReader = pyvrs::OssAsyncVRSReader;
using PyAsyncMultiReader = pyvrs::OssAsyncMultiVRSReader;
#else
#include "AsyncVRSReader_fb.h"
#endif
