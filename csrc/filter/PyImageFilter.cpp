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

#include "PyImageFilter.h"

#include "../reader/VRSReader.h"

namespace pyvrs {

using namespace std;
using namespace vrs;
using namespace vrs::utils;

ImageBuffer OssPyAsyncImageFilter::getNextImage() {
  if (!initialized_) {
    throw py::value_error("You must call createOutputFile before calling getNextImage.");
  }
  size_t recordIndex;
  ImageBuffer imageBuffer;
  if (AsyncImageFilter::getNextImage(
          recordIndex, imageBuffer.spec.getImageContentBlockSpec(), imageBuffer.bytes)) {
    imageBuffer.recordIndex = static_cast<int64_t>(recordIndex);
  }
  return imageBuffer;
}

ImageRecordInfo OssPyAsyncImageFilter::getImageRecordInfo(int64_t recordIndex) {
  const auto* record = getRecordInfo(static_cast<size_t>(recordIndex));
  return record != nullptr ? ImageRecordInfo(record->timestamp, record->streamId)
                           : ImageRecordInfo();
}

py::object OssPyAsyncImageFilter::getRecordMetadata(int64_t recordIndex) {
  if (collectMetadata_) {
    unique_lock<mutex> lock(mutex_);
    const auto& iter = metadata_.find(recordIndex);
    if (iter != metadata_.end()) {
      return py::cast(iter->second);
    }
    return py::none();
  }
  throw std::runtime_error("need_record_metadata() has not been called to collect metadata.");
}

bool OssPyAsyncImageFilter::writeProcessedImage(ImageBuffer& imageBuffer) {
  if (collectMetadata_) {
    unique_lock<mutex> lock(mutex_);
    metadata_.erase(imageBuffer.recordIndex);
  }
  return AsyncImageFilter::writeProcessedImage(
      static_cast<size_t>(imageBuffer.recordIndex), std::move(imageBuffer.bytes));
}

bool OssPyAsyncImageFilter::discardRecord(ImageBuffer& imageBuffer) {
  if (collectMetadata_) {
    unique_lock<mutex> lock(mutex_);
    metadata_.erase(imageBuffer.recordIndex);
  }
  return AsyncImageFilter::discardRecord(static_cast<size_t>(imageBuffer.recordIndex));
}

void OssPyAsyncImageFilter::doDataLayoutEdits(
    const CurrentRecord& record,
    size_t /*blockIndex*/,
    DataLayout& dl) {
  // This callback in synchronous, so we can't easily support DataLayout edits in Python.
  // We capture metadata only if requested, because it's expensive.
  if (collectMetadata_) {
    PyObject* m = dataLayoutToPyDict(dl, "utf-8-safe");
    unique_lock<mutex> lock(mutex_);
    int64_t recordIndex = record.fileReader->getRecordIndex(record.recordInfo);
    auto iter = metadata_.find(recordIndex);
    if (iter == metadata_.end()) {
      iter = metadata_.emplace(recordIndex, PyRecord(*record.recordInfo, recordIndex)).first;
    }
    iter->second.datalayoutBlocks.emplace_back(pyWrap(m));
  }
}

} // namespace pyvrs
