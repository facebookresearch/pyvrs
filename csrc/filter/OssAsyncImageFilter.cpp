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

#include "OssAsyncImageFilter.h"

#include <cmath>

#define DEFAULT_LOG_CHANNEL "OssAsyncImageFilter"
#include <logging/Log.h>
#include <logging/Verify.h>

using namespace std;
using namespace vrs;
using namespace vrs::utils;

namespace pyvrs {

// OssThrottledFileHelper implementation

int OssThrottledFileHelper::createFile(const string& outputFilePath) {
  int result = throttledWriter_.getWriter().createFileAsync(outputFilePath);
  if (result == 0) {
    fileCreated_ = true;
  }
  return result;
}

int OssThrottledFileHelper::closeFile() {
  if (!fileCreated_) {
    return FAILURE;
  }
  fileCreated_ = false;
  return throttledWriter_.closeFile();
}

// OssAsyncRecordFilterCopier: private inner class

class OssAsyncRecordFilterCopier : public RecordFilterCopier {
 public:
  OssAsyncRecordFilterCopier(OssAsyncImageFilter& asyncImageFilter, StreamId id)
      : RecordFilterCopier(
            asyncImageFilter.getFilteredReader().reader,
            asyncImageFilter.getWriter(),
            id,
            asyncImageFilter.getCopyOptions()),
        asyncImageFilter_{asyncImageFilter} {}

  bool shouldCopyVerbatim(const CurrentRecord& /*record*/) override {
    imageChunk_ = nullptr;
    return false;
  }

  void doDataLayoutEdits(const CurrentRecord& record, size_t blockIndex, DataLayout& dl) override {
    asyncImageFilter_.doDataLayoutEdits(record, blockIndex, dl);
  }

  bool onImageRead(const CurrentRecord& record, size_t index, const ContentBlock& cb) override {
    size_t blockSize = cb.getBlockSize();
    if (blockSize == ContentBlock::kSizeUnknown) {
      return onUnsupportedBlock(record, index, cb);
    }
    unique_ptr<ContentBlockChunk> imageChunk = make_unique<ContentBlockChunk>(cb, record);
    imageChunk_ = imageChunk.get();
    chunks_.emplace_back(std::move(imageChunk));
    return true;
  }

  void finishRecordProcessing(const CurrentRecord& record) override {
    if (!skipRecord_) {
      if (copyVerbatim_) {
        writer_.createRecord(record, verbatimRecordData_);
      } else {
        if (imageChunk_ != nullptr) {
          asyncImageFilter_.processChunkedRecord(writer_, record, chunks_, imageChunk_);
        } else {
          FilteredChunksSource chunkedSource(chunks_);
          writer_.createRecord(record, chunkedSource);
        }
      }
    }
  }

 protected:
  OssAsyncImageFilter& asyncImageFilter_;
  ContentBlockChunk* imageChunk_{};
};

// OssAsyncImageFilter implementation

OssAsyncImageFilter::OssAsyncImageFilter(FilteredFileReader& filteredReader)
    : filteredReader_{filteredReader},
      throttledWriter_{copyOptions_},
      fileHelper_{throttledWriter_} {
  copyOptions_.graceWindow = 0;
}

int OssAsyncImageFilter::createOutputFile(const string& outputFilePath) {
  if (!filteredReader_.reader.isOpened()) {
    int status = filteredReader_.openFile();
    if (status != 0) {
      XR_LOGE("Can't open input file, error #{}: {}", status, errorCodeToMessage(status));
      return status;
    }
  }
  RecordFileWriter& writer = throttledWriter_.getWriter();
  writer.addTags(filteredReader_.reader.getTags());
  {
    TemporaryRecordableInstanceIdsResetter instanceIdsResetter;
    for (auto id : filteredReader_.filter.streams) {
      if (filteredReader_.reader.mightContainImages(id) &&
          shouldFilterImageStream(filteredReader_.reader, id)) {
        auto filterCopier = make_unique<OssAsyncRecordFilterCopier>(*this, id);
        if (filteredStreamsCompressionPreset_ != CompressionPreset::Undefined) {
          filterCopier->getWriter().setCompression(filteredStreamsCompressionPreset_);
        }
        adjustRecordFormatDefinitions(
            filteredReader_.reader, id, filterCopier->getWriter().getVRSTags());
        copiers_.emplace_back(std::move(filterCopier));
      } else {
        copiers_.emplace_back(
            make_unique<Copier>(filteredReader_.reader, writer, id, copyOptions_));
      }
    }
  }
  double startTimestamp{}, endTimestamp{};
  filteredReader_.getConstrainedTimeRange(startTimestamp, endTimestamp);
  writer.preallocateIndex(filteredReader_.buildIndex());
  int result = fileHelper_.createFile(outputFilePath);
  if (result == 0) {
    filteredReader_.preRollConfigAndState();
    throttledWriter_.initTimeRange(startTimestamp, endTimestamp);
    function<bool(RecordFileReader&, const IndexRecord::RecordInfo&)> f =
        [this](RecordFileReader&, const IndexRecord::RecordInfo& record) {
          records_.emplace_back(&record);
          return true;
        };
    filteredReader_.iterateAdvanced(f);
  } else {
    XR_LOGE("Can't create output file, error #{}: {}", result, errorCodeToMessage(result));
  }
  nextRecordIndex_ = 0;
  return result;
}

const IndexRecord::RecordInfo* OssAsyncImageFilter::getRecordInfo(size_t recordIndex) const {
  if (recordIndex < records_.size()) {
    return records_[recordIndex];
  }
  return nullptr;
}

bool OssAsyncImageFilter::getNextImage(
    size_t& outRecordIndex,
    ImageContentBlockSpec& outImageSpec,
    vector<uint8_t>& outFrame) {
  while (nextRecordIndex_ < records_.size()) {
    pendingRecord_.clear();
    filteredReader_.reader.readRecord(*records_[nextRecordIndex_]);
    outRecordIndex = nextRecordIndex_++;
    if (pendingRecord_.needsImageProcessing()) {
      outImageSpec = pendingRecord_.imageChunk->getContentBlock().image();
      outFrame = std::move(pendingRecord_.imageChunk->getBuffer());
      pendingRecords_[outRecordIndex] = std::move(pendingRecord_);
      return true;
    }
  }
  return false;
}

bool OssAsyncImageFilter::writeProcessedImage(
    size_t recordIndex,
    vector<uint8_t>&& processedImage) {
  const auto& iter = pendingRecords_.find(recordIndex);
  if (!XR_VERIFY(iter != pendingRecords_.end(), "Invalid record index ({})", recordIndex) ||
      !XR_VERIFY(iter->second.needsImageProcessing(), "Image {} already processed", recordIndex)) {
    return false;
  }
  iter->second.setBuffer(std::move(processedImage));
  processPendingRecords();
  return true;
}

bool OssAsyncImageFilter::discardRecord(size_t recordIndex) {
  const auto& iter = pendingRecords_.find(recordIndex);
  if (!XR_VERIFY(iter != pendingRecords_.end(), "Invalid record index ({})", recordIndex) ||
      !XR_VERIFY(iter->second.needsImageProcessing(), "Image {} already processed", recordIndex)) {
    return false;
  }
  pendingRecords_.erase(iter);
  processPendingRecords();
  return true;
}

void OssAsyncImageFilter::processPendingRecords() {
  // try to write out all possibly ready records...
  double timestamp = nan("");
  for (auto iter = pendingRecords_.begin();
       iter != pendingRecords_.end() && !iter->second.needsImageProcessing();
       iter = pendingRecords_.begin()) {
    const auto& record = *records_[iter->first];
    timestamp = record.timestamp;
    FilteredChunksSource chunkedSource(iter->second.recordChunks);
    iter->second.writer->createRecord(
        timestamp, record.recordType, iter->second.formatVersion, chunkedSource);
    pendingRecords_.erase(iter);
  }
  if (!isnan(timestamp)) {
    throttledWriter_.onRecordDecoded(timestamp);
  }
}

int OssAsyncImageFilter::closeFile() {
  if (!pendingRecords_.empty()) {
    XR_LOGE("Can't close filter: {} images still need processing.", pendingRecords_.size());
    return FAILURE;
  }
  int result = fileHelper_.closeFile();
  if (throttledWriter_.getWriter().getBackgroundThreadQueueByteSize() != 0) {
    XR_LOGE("Unexpected count of bytes left in queue after image filtering!");
  }
  copiers_.clear();
  return result;
}

void OssAsyncImageFilter::setFilteredStreamsCompression(CompressionPreset preset) {
  if (filteredStreamsCompressionPreset_ != CompressionPreset::Undefined) {
    XR_LOGW("setFilteredStreamsCompression must be set before creating the output file.");
  }
  filteredStreamsCompressionPreset_ = preset;
}

void OssAsyncImageFilter::processChunkedRecord(
    Writer& writer,
    const CurrentRecord& hdr,
    deque<unique_ptr<ContentChunk>>& chunks,
    ContentBlockChunk* imageChunk) {
  pendingRecord_.set(std::move(chunks), imageChunk, &writer, hdr.formatVersion);
}

} // namespace pyvrs
