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

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <vrs/RecordFileReader.h>
#include <vrs/RecordFormat.h>
#include <vrs/os/Platform.h>
#include <vrs/utils/VideoRecordFormatStreamPlayer.h>

#include "VRSReaderBase.h"

#include "../utils/PyBuffer.h"
#include "../utils/PyFileSpec.h"
#include "../utils/PyRecord.h"

namespace pyvrs {

using namespace vrs;

enum class ImageConversion {
  Off, ///< no conversion. Returns the images bytes as stored in the file, without processing.
  Decompress, ///< decompress jpg, png and video codec compressed data into raw pixel buffers.
  Normalize, ///< decompress & convert to RGB8, RGBA8, Grey16, or Grey 8 as appropriate.
  NormalizeGrey8, ///< decompress & convert to RGB8, RGBA8, or Grey 8 as appropriate (no grey16).
  RawBuffer, ///< no conversion. Returns the images bytes as stored in the file as `1d array`,
             ///< without processing.

  /// Grab all remaining bytes in the record (NOTE: including the bytes
  /// of any subsequent content blocks!) and return them as a byte
  /// image of height 1. This is a backdoor for accessing image content
  /// block data in legacy VRS files with incorrect image specs that
  /// cannot practically be rewritten to be compliant. This backdoor
  /// should be used with care, and only as a last resort.
  RecordUnreadBytesBackdoor,
};

PyObject* dataLayoutToPyDict(DataLayout& dl, const string& encoding);

/// @brief BaseVRSReaderStreamPlayer class to factorize VRSReader and MultiVRSReader handling.
class BaseVRSReaderStreamPlayer : public vrs::utils::VideoRecordFormatStreamPlayer {
 protected:
  virtual bool checkSkipTrailingBlocks(const CurrentRecord& record, size_t blockIndex) = 0;
  virtual ImageConversion getImageConversion(const CurrentRecord& record) = 0;

  static PyObject* readDataLayout(DataLayout& dl, const string& encoding);

  /// Set the data we read from VRS record into ContentBlockBuffer (ContentBlockBuffer is a
  /// class that's exposed to Python via protocol_buffer).
  /// When the content type is image, we decode the data based on image spec.
  /// @param blocks: A vector of ContentBlockBuffer we want to write the data into.
  /// @param record: Record that's read in callback (onImageRead, onAudioRead, etc...)
  /// @param blockIndex: Index of the block we are writing.
  /// @param contentBlock: The description of this content block buffer.
  bool setBlock(
      vector<ContentBlockBuffer>& blocks,
      const CurrentRecord& ranges,
      size_t blockIndex,
      const ContentBlock& contentBlock);

  int recordReadComplete(RecordFileReader& reader, const IndexRecord::RecordInfo& rinfo) override {
    return readMissingFrames(reader, rinfo, true);
  }
};

/// @brief The VRSReader class
/// This class is a VRS file reader, optimized for Python bindings.
/// It is exposed to Python using PyBind, which makes the job really simple,
/// but it's also using the Python C APIs, frequently returning py::object, so as to generate
/// directly the most natural structures to Python, such as dictionaries, using the most direct
/// conversions possible from C++ native types to Python types.
///
/// VRS files contain multiple stream of records. Streams are identified by a unique StreamId.
/// StreamId objects are represented as strings, similar to what VRStool does,
/// with the recordable type id as an int, followed by the instance id, separated by a '-'.
///
/// Buffers are passed to Python using PyBind's protocol_buffer, which let's you build Numpy arrays
/// without copying the underlying numeric values if you choose to.
///
/// Most of the methods might throw a Python exception when appropriate, for instance:
/// - StopIteration might be thrown, when trying to read a record, but there are no more to read.
/// - IndexError might be thrown, if you pass an invalid index.
/// - ValueError might be thrown, if you pass an invalid RecordableTypeId, an invalid StreamId,
///   or an invalid record type filter.
class OssVRSReader : public VRSReaderBase {
  class VRSReaderStreamPlayer : public BaseVRSReaderStreamPlayer {
   public:
    explicit VRSReaderStreamPlayer(OssVRSReader& reader) : reader_(reader) {}

    bool processRecordHeader(const CurrentRecord& record, DataReference& outDataReference) override;
    bool onDataLayoutRead(const CurrentRecord& record, size_t blockIndex, DataLayout& dl) override;
    bool onImageRead(const CurrentRecord& record, size_t blockIndex, const ContentBlock& cb)
        override;
    bool onAudioRead(const CurrentRecord& record, size_t blockIndex, const ContentBlock& cb)
        override;
    bool onCustomBlockRead(const CurrentRecord& record, size_t bi, const ContentBlock& cb) override;
    bool onUnsupportedBlock(const CurrentRecord& record, size_t bi, const ContentBlock& cb)
        override;

    bool checkSkipTrailingBlocks(const CurrentRecord& record, size_t blockIndex) override;
    ImageConversion getImageConversion(const CurrentRecord& record) override;

   private:
    OssVRSReader& reader_;
  };

 public:
  /// @param autoReadConfigurationRecord: If this is true, we try to automatically read the
  /// configuration if it's not already read.
  explicit OssVRSReader(bool autoReadConfigurationRecord)
      : streamPlayer_{*this}, autoReadConfigurationRecord_{autoReadConfigurationRecord} {
    init();
  }
  OssVRSReader() : OssVRSReader(false) {}
  explicit OssVRSReader(const string& path) : OssVRSReader(false) {
    open(path);
  }
  OssVRSReader(const string& path, bool autoReadConfigurationRecord)
      : OssVRSReader(autoReadConfigurationRecord) {
    open(path);
  }
  explicit OssVRSReader(const PyFileSpec& spec) : OssVRSReader(false) {
    open(spec);
  }
  OssVRSReader(const PyFileSpec& spec, bool autoReadConfigurationRecord)
      : OssVRSReader(autoReadConfigurationRecord) {
    open(spec);
  }

  ~OssVRSReader() override {
    close();
  }

  // ---------------------
  // File level operations
  // ---------------------

  /// Initialize the module by calling initVrsBindings()
  void init();

  void open(const string& path);
  void open(const PyFileSpec& spec);

  int close();

  /// Set the character encoding to use when reading strings from the file.
  void setEncoding(const string& encoding);

  /// Get the character encoding that's being used when reading strings from the file.
  string getEncoding();

  /// Get an array of chunks, as a pair of path & size in bytes.
  py::object getFileChunks() const;

  /// Get the last timestamp present on any data records in VRS file.
  /// @return Last timestamp for data records
  double getMaxAvailableTimestamp();

  /// Get the first timestamp present on any data records in VRS file.
  /// @return First timestamp for data records
  double getMinAvailableTimestamp();

  /// Get the number of all records.
  size_t getAvailableRecordsSize();

  std::set<string> getAvailableRecordTypes();

  std::set<string> getAvailableStreamIds();

  std::map<string, int> recordCountByTypeFromStreamId(const string& streamId);

  // ---------------------------------------------------------------------------
  // Discovering tags, streams, their details, and choosing which to read/enable
  // ---------------------------------------------------------------------------
  /// Get the file's tags.
  /// @return The file's tags.
  py::object getTags();

  /// Get a stream's tags.
  /// @param streamId: VRS stream id of the stream to get the tags of.
  /// @return The stream's tags.
  py::object getTags(const string& streamId);

  /// Get the list of recordable ids each representing a stream.
  /// @return Vector of recordable ids for each VRS stream.
  std::vector<string> getStreams();

  /// Get a list of recordable ids for a specific recordable type id (device type).
  /// @param recordableTypeId: Device type of the streams to look for.
  /// @return Vector of recordable ids for each VRS stream.
  std::vector<string> getStreams(RecordableTypeId recordableTypeId);

  /// Get a list of recordable ids for a specific recordable type id (device type) and flavor.
  /// @param recordableTypeId: Device type of the streams to look for.
  /// Use RecordableTypeId::Undefined to match any recordable type.
  /// @param flavor: A flavor of device to look for.
  /// @return Vector of recordable ids for each VRS stream.
  std::vector<string> getStreams(RecordableTypeId recordableTypeId, const string& flavor);

  /// Get a recordable id for a specific recordable type id (device type), flavor and index number.
  /// @param recordableTypeId: Device type of the streams to look for.
  /// Use RecordableTypeId::Undefined to match any recordable type.
  /// @param flavor: A flavor of device to look for.
  /// @param indexNumber: The number of the index of the stream. Defaults to 0.
  /// @return Vector of recordable ids for each VRS stream.
  string getStreamForFlavor(
      RecordableTypeId recordableTypeId,
      const string& flavor,
      const uint32_t indexNumber = 0);

  /// Find a stream of a specific device type, with a specific tag name & tag value.
  /// @param recordableTypeId: Device type of the streams to check.
  /// @param tagName: tag name to check.
  /// @param tagValue: tag value to find.
  /// @return A recordable id of a VRS stream matching the request.
  string
  findStream(RecordableTypeId recordableTypeId, const string& tagName, const string& tagValue);

  /// Get a stream's details.
  /// @param streamId: VRS stream id.
  /// @return Stream information, including the following keys:
  /// "configuration_records_count": number of configuration records.
  /// "first_configuration_record_index": index of the first configuration record, if any.
  /// "first_configuration_record_timestamp": timestamp of the first configuration record, if any.
  /// "last_configuration_record_index": index of the last configuration record, if any.
  /// "last_configuration_record_timestamp": timestamp of the last configuration record, if any.
  /// "state_records_count": number of state records.
  /// "first_state_record_index": index of the first state record, if any.
  /// "first_state_record_timestamp": timestamp of the first state record, if any.
  /// "last_state_record_index": index of the last state record, if any.
  /// "last_state_record_timestamp": timestamp of the last state record, if any.
  /// "data_records_count": number of data records.
  /// "first_data_record_index": index of the first data record, if any.
  /// "first_data_record_timestamp": timestamp of the first data record, if any.
  /// "last_data_record_index": index of the last data record, if any.
  /// "last_data_record_timestamp": timestamp of the last data record, if any.
  /// "device_name": device type english name.
  /// "flavor": device flavor, if set.
  py::object getStreamInfo(const string& streamId);

  /// Get a stream's footprint on disk.
  /// This API is fairly expensive, which is why it's not folded into getStreamInfo().
  /// @param streamId: VRS stream id.
  /// @return Stream disk size, in bytes.
  int64_t getStreamSize(const string& streamId);

  /// Enable reading the records of a specific device.
  /// @param streamId: VRS stream id to enable for reading.
  /// @return True if the stream was found and is now enabled for reading.
  void enableStream(const string& streamId);
  void enableStream(const StreamId& id);

  /// Enable reading records of all streams of a specific recordable type id (device type).
  /// @param recordableTypeId: Device type of the streams to enable for reading.
  /// Use RecordableTypeId::Undefined to match any recordable type.
  /// @return Number of streams setup for reading with that RecordableTypeId.
  int enableStreams(RecordableTypeId recordableTypeId, const std::string& flavor = {});

  int enableStreamsByIndexes(const std::vector<int>& indexes);
  /// Enable all of the file's streams for reading.
  /// @return Number of streams enabled for reading, total.
  int enableAllStreams();

  /// Get the list of streams enabled for reading.
  /// @return Vector of recordable ids for each VRS stream.
  std::vector<string> getEnabledStreams();

  ImageConversion getImageConversion(const StreamId& id);

  /// Set default image conversion policy when reading images and clear any per stream override.
  void setImageConversion(ImageConversion conversion);
  /// Set image conversion policy for a specific stream.
  void setImageConversion(const string& streamId, ImageConversion conversion);
  void setImageConversion(const StreamId& id, ImageConversion conversion);
  /// Set image conversion policy for all the streams of a specific type.
  /// Returns the number of streams of that type.
  int setImageConversion(RecordableTypeId recordableTypeId, ImageConversion conversion);

  /// Tell if a stream might contains images
  bool mightContainImages(const string& streamId);
  /// Tell if a stream might contains audio
  bool mightContainAudio(const string& streamId);
  /// Get estimated frame rate for given stream ID.
  double getEstimatedFrameRate(const string& streamId);

  /// Get the number record for the specified stream & record type.
  int getRecordsCount(const string& streamId, const Record::Type recordType);

  // ---------------------------------
  // Getting information about records
  // ---------------------------------

  /// Get basic record information for all records in the file.
  /// @return a Python list of dictionaries, each including the following information:
  /// "record_index": the index of the record.
  /// "record_timestamp": timestamp of the record.
  /// "stream_id": streamId of the record.
  /// "record_type": record type, either "configuration", "state" or "data".
  py::object getAllRecordsInfo();

  /// Get basic record information for a number of records in the file.
  /// @param firstIndex: index of the first record to provide information for
  /// @param count: max number of records to return
  /// @return a Python list of dictionaries, each including the following information:
  /// "record_index": the index of the record.
  /// "record_timestamp": timestamp of the record.
  /// "stream_id": streamId of the record.
  /// "record_type": record type, either "configuration", "state" or "data".
  py::object getRecordsInfo(int32_t firstIndex, int32_t count);

  /// Get basic record information for all the read enabled streams' records.
  /// @return a Python list of dictionaries, each including the following information:
  /// "record_index": the index of the record.
  /// "record_timestamp": timestamp of the record.
  /// "stream_id": streamId of the record.
  /// "record_type": record type, either "configuration", "state" or "data".
  py::object getEnabledStreamsRecordsInfo();

  // ---------------
  // Reading records
  // ---------------

  /// Go to a specifc record, by index.
  /// @param index: a record index.
  /// @return results in a dictionary, including:
  /// "record_index": the index of the requested record.
  /// "record_timestamp": timestamp of the record.
  /// "stream_id": streamId of the record.
  /// "record_type": record type, either "configuration", "state" or "data".
  /// If there are no records for this index, throws an IndexError exception.
  py::object gotoRecord(int index);

  /// Go to the first record at or after a timestamp.
  /// @param timestamp: a timestamp.
  /// @return results in a dictionary, including:
  /// "record_index": index of the first record matching.
  /// "record_timestamp": timestamp of the record.
  /// "stream_id": streamId of the record.
  /// "record_type": record type, either "configuration", "state" or "data".
  /// If there are no records at or after this timestamp, throws an IndexError exception.
  py::object gotoTime(double timestamp);

  /// Read the next record of any stream enabled for reading.
  /// If there are no more records to read, throws a StopIteration exception.
  py::object readNextRecord();

  /// Read the next record of a specific stream.
  /// The stream must have been enabled for reading before.
  /// @param streamId: VRS stream id to read.
  /// @return Record details (see readRecord(int index)).
  /// If there are no more records to read, throws a StopIteration exception.
  py::object readNextRecord(const string& streamId);

  /// Read the next record of a specific stream and of a specific type.
  /// The stream must have been enabled for reading before.
  /// @param streamId: VRS stream id to read.
  /// @param recordType: record type to read (or "any" for any record of that stream).
  /// @return Record details (see readRecord(int index)).
  /// If there are no more records to read, throws a StopIteration exception.
  py::object readNextRecord(const string& streamId, const string& recordType);

  /// Read the next record from any stream of a specific recordable type id (device type).
  /// @param recordableTypeId: Device type of the streams to read.
  /// @return Record details (see readRecord(int index)).
  /// If there are no more records to read, throws a StopIteration exception.
  py::object readNextRecord(RecordableTypeId recordableTypeId);

  /// Read the next record from any stream of a specific recordable type id (device type)
  /// and of a specific type.
  /// @param recordableTypeId: Device type of the streams to read.
  /// @param recordType: record type to read (or "any" for any record of that stream).
  /// @return Record details (see readRecord(int index)).
  /// If there are no more records to read, throws a StopIteration exception.
  py::object readNextRecord(RecordableTypeId recordableTypeId, const string& recordType);

  /// Read a stream's record, by record type & index.
  /// @param streamId: VRS stream id to read.
  /// @param recordType: record type to read, or "any".
  /// @param index: the index of the record to read.
  /// @return Record details (see readRecord(int index)).
  py::object readRecord(const string& streamId, const string& recordType, int index);

  /// Read a specifc record, by index.
  /// @param index: a record index.
  /// @return Record details, including the following keys:
  /// "record_index": the index of the read record.
  /// "record_timestamp": timestamp of the record, as number of seconds (double).
  /// "stream_id": stream id of the stream the record was read from .
  /// "record_type": record type, either "configuration", "state" or "data".
  /// "metadata_count": number of metadata blocks read. Use getMetadata() to get them.
  /// "image_count": number of image blocks read. Use getImageXXX() to get them.
  /// "audio_block_count": number of audio blocks read. Use getAudioBlockXXX() to get them.
  /// "custom_block_count": number of custom blocks read. Use getCustomBlockXXX() to get them.
  /// If a block can't be recognized/decoded properly, as a warning, you might also get:
  /// "unsupported_block_count": number of unrecognized blocks.
  /// This might happen if you read a data record containing an image,
  /// before reading the stream's configuration record describing the image format.
  py::object readRecord(int index) override;

  // Skip reading content blocks of the record after reading preset number of them
  /// @param recordableTypeId: Device type of the stream to skip trailing blocks.
  /// @param recordType: Device type of the stream to skip trailing blocks.
  /// @param firstTrailingContentBlockIndex: Index of the first content block that is considered
  /// trailing and hence skipped. Use 0 to turn off the skipping of the blocks for the device type.
  void skipTrailingBlocks(
      RecordableTypeId recordableTypeId,
      Record::Type recordType,
      size_t firstTrailingContentBlockIndex);

  // ---------------------------------
  // Get the indexes / timestamps
  // ---------------------------------
  /// Get all indices after applying the filter.
  /// This method is used to obtain a subset of index, such as getting indices for stream 1001-1
  /// whose record type is DATA.
  /// @param recordTypes: Set of record types {"state", "configuration", "data"} that you want to
  /// enable.
  /// @param streamIds: Set of stream IDs you want to enable.
  /// @param minEnabledTimestamp: Minimum timestamp to read from.
  /// @param maxEnabledTimestamp: Maximum timestamp to read to.
  std::vector<int32_t> regenerateEnabledIndices(
      const std::set<string>& recordTypes,
      const std::set<string>& streamIds,
      double minEnabledTimestamp,
      double maxEnabledTimestamp);

  /// Get the timestamp corresponding to the index
  /// @param index: The absolute index in the file.
  double getTimestampForIndex(int index);

  /// Get the StreamId corresponding to the Record located at the position recordIndex.
  /// @param recordIndex: Position of the record whose StreamId you are trying to look up.
  /// @return StreamId in the form of a string corresponding to the given recordIndex.
  string getStreamIdForIndex(int recordIndex);

  /// Get a stream's serial number.
  /// When streams are created, they are assigned a unique serial number by their Recordable object.
  /// That serial number is universally unique and it will be preserved during file copies, file
  /// processing, and other manipulations that preserve stream tags.
  /// @param streamId: StreamId of the record stream to consider.
  /// @return The stream's serial number, or the empty string if the stream ID is not
  /// valid. When opening files created before stream serial numbers were introduced,
  /// RecordFileReader automatically generates a stable serial number for every stream based on the
  /// file tags, the stream's tags (both user and VRS internal tags), and the stream type and
  /// sequence number. This serial number is stable and preserved during copy and filtering
  /// operations that preserve stream tags.
  string getSerialNumberForStream(const string& streamId) const;

  /// Find the stream with the specified stream serial number.
  string getStreamForSerialNumber(const string& streamSerialNumber) const;

  /// Get the index based on streamId and timestamp, lower_bound is used for searching record.
  /// @param streamId: Stream ID you are interested in.
  /// @param timestamp: A timestamp
  int32_t getRecordIndexByTime(const string& streamId, double timestamp);

  /// Get the index based on streamId and timestamp, lower_bound is used for searching record.
  /// @param streamId: Stream ID you are interested in.
  /// @param recordType: Record type to find.
  /// @param timestamp: A timestamp
  int32_t getRecordIndexByTime(const string& streamId, Record::Type recordType, double timestamp);

  /// Get the index based on streamId and timestamp, lower_bound is used for searching record.
  /// If there are no record within timestamp +- epsilon range, we throw TimestampNotFoundError.
  /// @param timestamp: A timestamp
  /// @param epsilon: A timestamp range we search for records.
  /// The range will be [timestamp - epsilon, timestamp + epsilon]
  /// @param streamId: Stream ID you are interested in.
  int32_t getNearestRecordIndexByTime(double timestamp, double epsilon, const string& streamId);

  /// Get the index based on streamId and timestamp, lower_bound is used for searching record.
  /// If there are no record within timestamp +- epsilon range, we throw TimestampNotFoundError.
  /// @param timestamp: A timestamp
  /// @param epsilon: A timestamp range we search for records.
  /// The range will be [timestamp - epsilon, timestamp + epsilon]
  /// @param streamId: Stream ID you are interested in.
  /// @param recordType: Record type to find.
  int32_t getNearestRecordIndexByTime(
      double timestamp,
      double epsilon,
      const string& streamId,
      Record::Type recordType);

  /// Get the timestamp list corresponds to the given index.
  /// @param indices: A list of index that you want to get the corresponding timestamp.
  std::vector<double> getTimestampListForIndices(const std::vector<int32_t>& indices);

  /// Get the next index of given streamId after given index.
  /// @param streamId: Stream ID we are interested in.
  /// @param recordType: Record type we are interested in.
  /// @param index: index that you start searching from.
  /// throw IndexError if we can't find any records
  int32_t getNextIndex(const string& streamId, const string& recordType, int index);

  /// Get the previous index of given streamId before given index.
  /// @param streamId: Stream ID we are interested in.
  /// @param recordType: Record type we are interested in.
  /// @param index: index that you start searching from.
  /// throw IndexError if we can't find any records
  int32_t getPrevIndex(const string& streamId, const string& recordType, int index);

  // ---------------------------------
  // File cache optimizations
  // ---------------------------------
  /// Set & get the current file handler's Caching strategy.
  /// This should be called *after* opening the file, as open might replace the file handler.
  bool setCachingStrategy(CachingStrategy cachingStrategy);
  CachingStrategy getCachingStrategy() const;

  /// When accessing data over a network, optimize caching for the specific read sequence.
  /// @param recordIndexes: a sequence of records in the exact order they will be read. It's ok to
  /// skip one or more records, but:
  /// - don't try to read "past" records, or you'll confuse the caching strategy, possibly leading
  /// to much worse performance.
  /// - if you read a single record out of the sequence, the prefetch list will be cleared.
  /// You may call this method as often as you like, and any previous read sequence will be cleared,
  /// but whatever is already in the cache will remain.
  /// @param clearSequence: Flag on whether to cancel any pre-existing custom read sequence upon
  /// caching starts.
  /// @return True if the file handler backend supports this request, false if it was ignored.
  bool prefetchRecordSequence(const vector<uint32_t>& recordIndexes, bool clearSequence = true);

  /// If the underlying file handler caches data on reads, purge its caches to free memory.
  /// Sets the caching strategy to Passive, and clears any pending read sequence.
  /// @return True if the caches were purged, false if they weren't for some reason.
  /// Note: this is a best effort. If transactions are pending, their cache blocks won't be cleared.
  bool purgeFileCache();

 protected:
  void open(const FileSpec& spec);
  // Similar to readNextRecord() except that this does not invoke skipIgnoredRecords()
  py::object readNextRecordInternal();

  // When we read data record, most of the time we want to read configuration record corresponds to
  // that record first. This method builds a map of data record to configuration record and read the
  // corresponding configuration record if it's not already read.
  void readConfigurationRecord(const StreamId& streamId, uint32_t idx);

  void skipIgnoredRecords();

  /// Initialize record summary based on index records.
  /// This method will go through index records and build map/set such as
  /// - The number of records per stream
  /// - The number of records per stream & record type
  /// - The number of record types
  /// - The number of records per record type
  void initRecordSummaries();

  /// Set the data we read from VRS record into ContentBlockBuffer (ContentBlockBuffer is a
  /// class that's exposed to Python via protocol_buffer).
  /// When the content type is image, we decode the data based on image spec.
  /// @param blocks: A vector of ContentBlockBuffer we want to write the data into.
  /// @param record: Record that's read in callback (onImageRead, onAudioRead, etc...)
  /// @param blockIndex: Index of the block we are writing.
  /// @param contentBlock: The description of this content block buffer.
  bool setBlock(
      vector<ContentBlockBuffer>& blocks,
      const CurrentRecord& record,
      size_t blockIndex,
      const ContentBlock& contentBlock);

  /// Convert the streamId in string into StreamId class.
  StreamId getStreamId(const string& streamId);

  /// Helper functions for getStreamInfo.
  void addStreamInfo(PyObject* dic, const StreamId& id, Record::Type recordType);
  void addRecordInfo(
      PyObject* dic,
      const string& prefix,
      Record::Type recordType,
      const IndexRecord::RecordInfo* record);

  /// Helper function to check if the given record matches StreamId/RecordableTypeId and Record
  /// type.
  bool match(const IndexRecord::RecordInfo& record, StreamId id, Record::Type recordType) const;
  bool match(
      const IndexRecord::RecordInfo& record,
      RecordableTypeId typeId,
      Record::Type recordType) const;

  py::object getNextRecordInfo(const char* errorMessage);

  static constexpr const char* kUtf8 = "utf-8";

  RecordFileReader reader_;
  VRSReaderStreamPlayer streamPlayer_;
  RecordCache lastRecord_;
  uint32_t nextRecordIndex_;
  set<StreamId> enabledStreams_;
  map<pair<RecordableTypeId, Record::Type>, size_t> firstSkippedTrailingBlockIndex_;
  map<StreamId, map<string, int>> recordCountsByTypeAndStreamIdMap_;
  set<string> recordTypes_;
  ImageConversion imageConversion_ = ImageConversion::Off; // default image conversion
  map<StreamId, ImageConversion> streamImageConversion_; // per stream image conversion
  string encoding_ = kUtf8;

  map<StreamId, vector<uint32_t>> configIndex_;
  map<StreamId, uint32_t> lastReadConfigIndex_;
  bool autoReadConfigurationRecord_ = false;
};

/// Binds methods and classes for VRSReader.
void pybind_vrsreader(py::module& m);

} // namespace pyvrs

#if IS_VRS_OSS_CODE()
using PyVRSReader = pyvrs::OssVRSReader;
#else
#include "VRSReader_fb.h"
#endif
