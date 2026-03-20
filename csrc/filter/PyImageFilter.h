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

#include <pybind11/detail/common.h>
#include <functional> // multiplies
#include <map>
#include <mutex>
#include <numeric> // accumulate
#include <string>
#include <vector>

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <vrs/FileHandlerFactory.h>
#include <vrs/utils/AsyncImageFilter.h>
#include <vrs/utils/ImageFilter.h>

#include "../VrsBindings.h"
#include "../reader/FilteredFileReader.h"
#include "../utils/PyBuffer.h"
#include "../utils/PyRecord.h"

namespace py = pybind11;

namespace pyvrs {
using namespace std;
using namespace vrs;
using namespace utils;

struct ImageRecordInfo {
  ImageRecordInfo() = default;
  ImageRecordInfo(double timestamp, const StreamId& streamId)
      : timestamp(timestamp), streamId(streamId) {}

  double timestamp;
  StreamId streamId;
};

class OssPyAsyncImageFilter : public AsyncImageFilter {
 public:
  explicit OssPyAsyncImageFilter(std::shared_ptr<pyvrs::FilteredFileReader> filteredReader)
      : AsyncImageFilter(filteredReader->getFilteredReader()),
        initialized_{false},
        filteredReader_{filteredReader} {}

  int createOutputFile(const string& outputFilePath) {
    initialized_ = true;
    return AsyncImageFilter::createOutputFile(outputFilePath);
  }

  void needRecordMetadata() {
    unique_lock<mutex> lock(mutex_);
    collectMetadata_ = true;
  }

  ImageBuffer getNextImage();

  ImageRecordInfo getImageRecordInfo(int64_t recordIndex);

  py::object getRecordMetadata(int64_t recordIndex);

  bool writeProcessedImage(ImageBuffer& imageBuffer);

  bool discardRecord(ImageBuffer& imageBuffer);

  int getPendingCount() const {
    return static_cast<int>(AsyncImageFilter::getPendingCount());
  }

  int closeFile() {
    return AsyncImageFilter::closeFile();
  }

 protected:
  void doDataLayoutEdits(const CurrentRecord& record, size_t blockIndex, DataLayout& dl) override;

  bool initialized_;
  mutex mutex_;
  bool collectMetadata_{false};
  map<int64_t, PyRecord> metadata_;
  std::shared_ptr<pyvrs::FilteredFileReader> filteredReader_;
};

class OssPyAsyncImageFilterIterator {
 public:
  explicit OssPyAsyncImageFilterIterator(OssPyAsyncImageFilter& filter) : filter_(filter) {}

  ImageBuffer next() {
    ImageBuffer buffer = filter_.getNextImage();

    if (buffer.recordIndex == -1) {
      throw py::stop_iteration();
    }
    return buffer;
  }

 private:
  OssPyAsyncImageFilter& filter_;
};
} // namespace pyvrs
