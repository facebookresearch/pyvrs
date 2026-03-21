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

#include <deque>
#include <map>
#include <memory>
#include <vector>

#include <vrs/Compressor.h>
#include <vrs/IndexRecord.h>
#include <vrs/StreamPlayer.h>
#include <vrs/utils/FilterCopyHelpers.h>
#include <vrs/utils/FilteredFileReader.h>
#include <vrs/utils/ThrottleHelpers.h>

namespace pyvrs {

using std::deque;
using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

using vrs::CompressionPreset;
using vrs::CurrentRecord;
using vrs::DataLayout;
using vrs::ImageContentBlockSpec;
using vrs::RecordFileReader;
using vrs::RecordFileWriter;
using vrs::StreamId;
using vrs::StreamPlayer;
using vrs::IndexRecord::RecordInfo;
using vrs::utils::ContentBlockChunk;
using vrs::utils::ContentChunk;
using vrs::utils::CopyOptions;

using vrs::utils::ThrottledWriter;
using vrs::utils::Writer;

/// Helper class to handle creating & closing local files.
/// OSS replacement for the internal ThrottledFileHelper, without Gaia upload support.
class OssThrottledFileHelper {
 public:
  explicit OssThrottledFileHelper(ThrottledWriter& throttledWriter)
      : throttledWriter_{throttledWriter} {}

  /// Create a VRS file for async writes.
  int createFile(const string& outputFilePath);
  /// Close the file, wait for all data to be written out.
  int closeFile();

 private:
  ThrottledWriter& throttledWriter_;
  bool fileCreated_{false};
};

struct OssPendingRecord {
  OssPendingRecord() = default;
  OssPendingRecord(OssPendingRecord&& other) noexcept = default;
  OssPendingRecord& operator=(OssPendingRecord&& other) noexcept = default;
  ~OssPendingRecord() = default;

  OssPendingRecord& operator=(const OssPendingRecord&) = delete;
  OssPendingRecord(const OssPendingRecord&) = delete;

  void set(
      deque<unique_ptr<ContentChunk>>&& _recordChunks,
      ContentBlockChunk* _imageChunk,
      Writer* _writer,
      uint32_t _formatVersion) {
    recordChunks = std::move(_recordChunks);
    imageChunk = _imageChunk;
    writer = _writer;
    formatVersion = _formatVersion;
  }
  void clear() {
    recordChunks.clear();
    imageChunk = nullptr;
    writer = nullptr;
    formatVersion = 0;
  }
  bool needsImageProcessing() const {
    return imageChunk != nullptr;
  }
  void setBuffer(vector<uint8_t>&& processedImage) {
    imageChunk->getBuffer() = std::move(processedImage);
    imageChunk = nullptr;
  }

  deque<unique_ptr<ContentChunk>> recordChunks;
  ContentBlockChunk* imageChunk{};
  Writer* writer{};
  uint32_t formatVersion{};
};

class OssAsyncImageFilter {
 public:
  /// Create an async image filter.
  explicit OssAsyncImageFilter(vrs::utils::FilteredFileReader& filteredReader);
  virtual ~OssAsyncImageFilter() = default;

  /// Create the output file and initialize the image filter.
  /// @param outputFilePath: path of the output file.
  /// @return A status code, 0 for success.
  int createOutputFile(const string& outputFilePath);

  /// Get an image to process, until there are no more.
  /// @param outRecordIndex: on success, set to the index of the record containing the image.
  /// @param outImageSpec: on success, set to the image spec of the image to process.
  /// @param outFrame: on success, set to the image buffer to process.
  /// @return True if an image to process was found & the data returned.
  /// False when no more images to process can be found.
  bool getNextImage(
      size_t& outRecordIndex,
      ImageContentBlockSpec& outImageSpec,
      vector<uint8_t>& outFrame);

  /// Return an image returned by getNextImage() after the pixels have been processed as desired.
  /// @param recordIndex: index of the image returned.
  /// @param processedImage: processed image data.
  /// @return True if the corresponding image was found.
  bool writeProcessedImage(size_t recordIndex, vector<uint8_t>&& processedImage);

  /// Discard/drop the whole record associated with a particular image returned by getNextImage().
  /// @param recordIndex: index of the record to be skipped.
  /// @return true if the record was discarded/dropped, false if recordIndex wasn't valid.
  bool discardRecord(size_t recordIndex);

  /// Helper method to find out about a particular record.
  const RecordInfo* getRecordInfo(size_t recordIndex) const;

  /// Helper method to know how many records have been retrieved, but not yet returned.
  size_t getPendingCount() const {
    return pendingRecords_.size();
  }

  /// Access some rarely needed options. Make changes before creating the output file.
  CopyOptions& getCopyOptions() {
    return copyOptions_;
  }

  /// Close the output file after all images have been processed.
  /// @return A status code, 0 for success.
  int closeFile();

  /// Tell if a particular image stream should be filtered.
  /// By default, all image streams are filtered, while the others are copied as is.
  virtual bool shouldFilterImageStream(RecordFileReader& reader, StreamId id) {
    return reader.mightContainImages(id);
  }

  /// When converting image data from one image type to another (raw to jpg for instance),
  /// it might be required to edit the RecordFormat definitions defined in the stream's VRS tags.
  virtual void adjustRecordFormatDefinitions(
      RecordFileReader& /*reader*/,
      StreamId /*id*/,
      map<string, string>& /*vrsTags*/) {}

  /// When converting image formats (e.g. changing pixel format or image dimensions), the
  /// filter might need to update the image spec.
  /// This modification must be completed synchronously, even for data/image records.
  virtual void
  doDataLayoutEdits(const CurrentRecord& /*record*/, size_t /*blockIndex*/, DataLayout& /*dl*/) {}

  /// Override stream compression setting for filtered streams.
  /// Must be called before the output file is created.
  void setFilteredStreamsCompression(CompressionPreset preset);

  /// Get a cached expected layout for a particular record, mapped to the datalayout found.
  template <class T>
  inline const T&
  getExpectedLayout(const CurrentRecord& record, DataLayout& layout, size_t blockIndex) {
    auto& layoutCache =
        expectedDataLayouts_[{record.streamId, record.recordType, record.formatVersion}];
    if (layoutCache.size() <= blockIndex) {
      layoutCache.resize(blockIndex + 1);
    }
    if (!layoutCache[blockIndex]) {
      T* expectedLayout = new T;
      layoutCache[blockIndex].reset(expectedLayout);
      expectedLayout->mapLayout(layout);
    }
    return reinterpret_cast<T&>(*layoutCache[blockIndex].get());
  }

 protected:
  vrs::utils::FilteredFileReader& getFilteredReader() const {
    return filteredReader_;
  }
  RecordFileWriter& getWriter() {
    return throttledWriter_.getWriter();
  }
  void processChunkedRecord(
      Writer& writer,
      const CurrentRecord& hdr,
      deque<unique_ptr<ContentChunk>>& chunks,
      ContentBlockChunk* imageChunk);
  void processPendingRecords();
  friend class OssAsyncRecordFilterCopier;

  CopyOptions copyOptions_{false};

 private:
  vrs::utils::FilteredFileReader& filteredReader_;
  vector<unique_ptr<StreamPlayer>> copiers_;
  ThrottledWriter throttledWriter_;
  OssThrottledFileHelper fileHelper_;
  deque<const RecordInfo*> records_;
  size_t nextRecordIndex_{};
  OssPendingRecord pendingRecord_;
  map<size_t, OssPendingRecord> pendingRecords_;
  CompressionPreset filteredStreamsCompressionPreset_{CompressionPreset::Undefined};
  map<std::tuple<StreamId, vrs::Record::Type, uint32_t>, vector<unique_ptr<DataLayout>>>
      expectedDataLayouts_;
};

} // namespace pyvrs
