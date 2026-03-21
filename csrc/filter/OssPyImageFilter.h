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
#include <mutex>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "OssAsyncImageFilter.h"

#include "../reader/FilteredFileReader.h"
#include "../utils/PyBuffer.h"
#include "../utils/PyRecord.h"

namespace py = pybind11;

namespace pyvrs {

class FilteredFileReader;

struct ImageRecordInfo {
  ImageRecordInfo() = default;
  ImageRecordInfo(double timestamp, const StreamId& streamId)
      : timestamp(timestamp), streamId(streamId) {}

  double timestamp;
  StreamId streamId;
};

class OssPyAsyncImageFilter : public OssAsyncImageFilter {
 public:
  explicit OssPyAsyncImageFilter(std::shared_ptr<pyvrs::FilteredFileReader> filteredReader)
      : OssAsyncImageFilter(filteredReader->getFilteredReader()),
        initialized_{false},
        filteredReader_{filteredReader} {}

  int createOutputFile(const string& outputFilePath) {
    initialized_ = true;
    return OssAsyncImageFilter::createOutputFile(outputFilePath);
  }

  void needRecordMetadata() {
    std::unique_lock<std::mutex> lock(mutex_);
    collectMetadata_ = true;
  }

  ImageBuffer getNextImage();

  ImageRecordInfo getImageRecordInfo(int64_t recordIndex);

  py::object getRecordMetadata(int64_t recordIndex);

  bool writeProcessedImage(ImageBuffer& imageBuffer);

  bool discardRecord(ImageBuffer& imageBuffer);

  int getPendingCount() const {
    return static_cast<int>(OssAsyncImageFilter::getPendingCount());
  }

  int closeFile() {
    return OssAsyncImageFilter::closeFile();
  }

 protected:
  void doDataLayoutEdits(const CurrentRecord& record, size_t blockIndex, DataLayout& dl) override;

  bool initialized_;
  std::mutex mutex_;
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
