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
namespace py = pybind11;
using namespace vrs;
using namespace std;

class OssAsyncVRSReader;
class OssAsyncMultiVRSReader;

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

  virtual void performJob(OssAsyncVRSReader& reader) = 0;
  virtual void performJob(OssAsyncMultiVRSReader& reader) = 0;

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

  void performJob(OssAsyncVRSReader& reader) override;
  void performJob(OssAsyncMultiVRSReader& reader) override;

 private:
  uint32_t index_;
};

using AsyncJobQueue = JobQueue<std::unique_ptr<AsyncJob>>;

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
  AwaitableRecord(uint32_t index, AsyncJobQueue& queue);
  AwaitableRecord(const AwaitableRecord& other);

  py::object await() const {
    py::gil_scoped_acquire acquire;
    unique_ptr<AsyncJob> job = make_unique<AsyncReadJob>(index_);
    py::object res = job->await();
    queue_.sendJob(std::move(job));
    return res;
  }

 private:
  uint32_t index_;
  AsyncJobQueue& queue_;
};

/// \brief Helper class to manage the background async thread
template <class VrsReader>
class AsyncThreadHandler {
 public:
  AsyncThreadHandler(VrsReader& reader, AsyncJobQueue& queue)
      : reader_{reader},
        workerQueue_(queue),
        asyncThread_(&AsyncThreadHandler::asyncThreadActivity, this) {}

  void asyncThreadActivity();
  void cleanup();

 private:
  VrsReader& reader_;
  AsyncJobQueue& workerQueue_;
  atomic<bool> shouldEndAsyncThread_ = false;
  thread asyncThread_;
};

/// \brief The async VRSReader class
/// This class extends VRSReader and adds asynchronous APIs to read records.
/// AsyncVRSReader spawns a background thread to process the async jobs.
class OssAsyncVRSReader : public OssVRSReader {
 public:
  explicit OssAsyncVRSReader(bool autoReadConfigurationRecord)
      : OssVRSReader(autoReadConfigurationRecord), asyncThreadHandler_{*this, workerQueue_} {}

  ~OssAsyncVRSReader();

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
  AsyncJobQueue workerQueue_;
  AsyncThreadHandler<OssAsyncVRSReader> asyncThreadHandler_;
};

/// \brief The async MultiVRSReader class
/// This class extends VRSReader and adds asynchronous APIs to read records.
/// AsyncVRSReader spawns a background thread to process the async jobs.
class OssAsyncMultiVRSReader : public OssMultiVRSReader {
 public:
  explicit OssAsyncMultiVRSReader(bool autoReadConfigurationRecord)
      : OssMultiVRSReader(autoReadConfigurationRecord), asyncThreadHandler_{*this, workerQueue_} {}

  ~OssAsyncMultiVRSReader();

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
  AsyncJobQueue workerQueue_;
  AsyncThreadHandler<OssAsyncMultiVRSReader> asyncThreadHandler_;
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
