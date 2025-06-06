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

#include "PyRecord.h"

#include <functional> // multiplies
#include <numeric> // accumulate
#include <string>
#include <vector>

#include <fmt/format.h>
#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>

#include "PyUtils.h"

namespace py = pybind11;

using namespace std;
using namespace vrs;

namespace pyvrs {
const constexpr char* kRecordFormatVersionKey = "record_format_version";
const constexpr char* kRecordIndexKey = "record_index";
const constexpr char* kRecordTypeKey = "record_type";
const constexpr char* kRecordableIdKey = "recordable_id";
const constexpr char* kStreamIdKey = "stream_id";
const constexpr char* kTimestampKey = "timestamp";
const constexpr char* kAudioBlockCountKey = "audio_block_count";
const constexpr char* kCustomBlockCountKey = "custom_block_count";
const constexpr char* kImageCountKey = "image_count";
const constexpr char* kMetadataCountKey = "metadata_count";
const constexpr char* kUnsupportedBlockCountKey = "unsupported_block_count";

PyRecord::PyRecord(const IndexRecord::RecordInfo& info, int32_t recordIndex_) {
  recordIndex = recordIndex_;
  recordType = lowercaseTypeName(info.recordType);
  recordTimestamp = info.timestamp;
  streamId = info.streamId.getNumericName();
}

PyRecord::PyRecord(const IndexRecord::RecordInfo& info, int32_t recordIndex_, RecordCache& record)
    : PyRecord(info, recordIndex_) {
  recordFormatVersion = record.recordFormatVersion;
  datalayoutBlocks = std::move(record.datalayoutBlocks);
  imageBlocks = std::move(record.images);
  audioBlocks = std::move(record.audioBlocks);
  customBlocks = std::move(record.customBlocks);
  unsupportedBlocks = std::move(record.unsupportedBlocks);

  // Set specs
  for (const auto& audioBlock : audioBlocks) {
    audioSpecs.emplace_back(audioBlock.spec.audio());
  }
  for (const auto& customBlock : customBlocks) {
    customBlockSpecs.emplace_back(customBlock.spec);
  }
  for (const auto& imageBlock : imageBlocks) {
    imageSpecs.emplace_back(imageBlock.spec.image());
  }
}

void PyRecord::initAttributesMap() {
  if (attributesMap.empty()) {
    attributesMap[kRecordFormatVersionKey] = PYWRAP(recordFormatVersion);
    attributesMap[kRecordIndexKey] = PYWRAP(recordIndex);
    attributesMap[kRecordTypeKey] = PYWRAP(recordType);
    attributesMap[kRecordableIdKey] = PYWRAP(streamId);
    attributesMap[kStreamIdKey] = PYWRAP(streamId);
    attributesMap[kTimestampKey] = PYWRAP(recordTimestamp);
    attributesMap[kImageCountKey] = PYWRAP(static_cast<uint32_t>(imageBlocks.size()));
    attributesMap[kAudioBlockCountKey] = PYWRAP(static_cast<uint32_t>(audioBlocks.size()));
    attributesMap[kCustomBlockCountKey] = PYWRAP(static_cast<uint32_t>(customBlocks.size()));
    attributesMap[kMetadataCountKey] = PYWRAP(static_cast<uint32_t>(datalayoutBlocks.size()));
    if (!unsupportedBlocks.empty()) {
      attributesMap[kUnsupportedBlockCountKey] =
          PYWRAP(static_cast<uint32_t>(unsupportedBlocks.size()));
    }
  }
}

#if IS_VRS_OSS_CODE()
void pybind_record(py::module& m) {
  py::enum_<vrs::ImageFormat>(m, "ImageFormat", py::arithmetic())
      .value("UNDEFINED", vrs::ImageFormat::UNDEFINED)
      .value("RAW", vrs::ImageFormat::RAW)
      .value("JPG", vrs::ImageFormat::JPG)
      .value("JXL", vrs::ImageFormat::JXL)
      .value("PNG", vrs::ImageFormat::PNG)
      .value("VIDEO", vrs::ImageFormat::VIDEO);

  py::enum_<vrs::PixelFormat>(m, "PixelFormat", py::arithmetic())
      .value("UNDEFINED", vrs::PixelFormat::UNDEFINED)
      .value("GREY8", vrs::PixelFormat::GREY8)
      .value("BGR8", vrs::PixelFormat::BGR8)
      .value("DEPTH32F", vrs::PixelFormat::DEPTH32F)
      .value("RGB8", vrs::PixelFormat::RGB8)
      .value("YUV_I420_SPLIT", vrs::PixelFormat::YUV_I420_SPLIT)
      .value("RGBA8", vrs::PixelFormat::RGBA8)
      .value("RGB10", vrs::PixelFormat::RGB10)
      .value("RGB12", vrs::PixelFormat::RGB12)
      .value("GREY10", vrs::PixelFormat::GREY10)
      .value("GREY12", vrs::PixelFormat::GREY12)
      .value("GREY16", vrs::PixelFormat::GREY16)
      .value("RGB32F", vrs::PixelFormat::RGB32F)
      .value("SCALAR64F", vrs::PixelFormat::SCALAR64F)
      .value("YUY2", vrs::PixelFormat::YUY2)
      .value("RGB_IR_RAW_4X4", vrs::PixelFormat::RGB_IR_RAW_4X4)
      .value("RGBA32F", vrs::PixelFormat::RGB_IR_RAW_4X4)
      .value("BAYER8_RGGB", vrs::PixelFormat::BAYER8_RGGB)
      .value("RAW10", vrs::PixelFormat::RAW10)
      .value("RAW10_BAYER_RGGB", vrs::PixelFormat::RAW10_BAYER_RGGB)
      .value("RAW10_BAYER_BGGR", vrs::PixelFormat::RAW10_BAYER_BGGR)
      .value("YUV_420_NV21", vrs::PixelFormat::YUV_420_NV21)
      .value("YUV_420_NV12", vrs::PixelFormat::YUV_420_NV12);

  static_assert(int(vrs::PixelFormat::COUNT) == 23, "vrs::PixelFormat Python bindings incomplete");

  py::enum_<vrs::AudioSampleFormat>(m, "AudioSampleFormat", py::arithmetic())
      .value("UNDEFINED", vrs::AudioSampleFormat::UNDEFINED)
      .value("S8", vrs::AudioSampleFormat::S8)
      .value("U8", vrs::AudioSampleFormat::U8)
      .value("A_LAW", vrs::AudioSampleFormat::A_LAW)
      .value("MU_LAW", vrs::AudioSampleFormat::MU_LAW)
      .value("S16_LE", vrs::AudioSampleFormat::S16_LE)
      .value("U16_LE", vrs::AudioSampleFormat::U16_LE)
      .value("S16_BE", vrs::AudioSampleFormat::S16_BE)
      .value("U16_BE", vrs::AudioSampleFormat::U16_BE)
      .value("S24_LE", vrs::AudioSampleFormat::S24_LE)
      .value("U24_LE", vrs::AudioSampleFormat::U24_LE)
      .value("S24_BE", vrs::AudioSampleFormat::S24_BE)
      .value("U24_BE", vrs::AudioSampleFormat::U24_BE)
      .value("S32_LE", vrs::AudioSampleFormat::S32_LE)
      .value("U32_LE", vrs::AudioSampleFormat::U32_LE)
      .value("S32_BE", vrs::AudioSampleFormat::S32_BE)
      .value("U32_BE", vrs::AudioSampleFormat::U32_BE)
      .value("F32_LE", vrs::AudioSampleFormat::F32_LE)
      .value("F32_BE", vrs::AudioSampleFormat::F32_BE)
      .value("F64_LE", vrs::AudioSampleFormat::F64_LE)
      .value("F64_BE", vrs::AudioSampleFormat::F64_BE);

  static_assert(
      int(vrs::AudioSampleFormat::COUNT) == 21,
      "vrs::AudioSampleFormat Python bindings incomplete");

  py::enum_<vrs::RecordableTypeId>(m, "RecordableTypeId");
  py::class_<vrs::StreamId>(m, "RecordableId")
      .def("get_type_id", &vrs::StreamId::getTypeId)
      .def("get_instance_id", &vrs::StreamId::getInstanceId)
      .def("is_valid", &vrs::StreamId::isValid)
      .def("get_type_name", &vrs::StreamId::getTypeName)
      .def("get_name", &vrs::StreamId::getName)
      .def("get_numeric_name", &vrs::StreamId::getNumericName);

  m.def("recordable_type_id_name", [](const std::string& recordableIdAsString) {
    const vrs::StreamId recId = vrs::StreamId::fromNumericName(recordableIdAsString);
    return vrs::toString(recId.getTypeId());
  });

  py::class_<vrs::Record, std::unique_ptr<vrs::Record, py::nodelete>>(m, "Record");

  py::enum_<vrs::Record::Type>(m, "RecordType")
      .value("UNDEFINED", vrs::Record::Type::UNDEFINED)
      .value("STATE", vrs::Record::Type::STATE)
      .value("CONFIGURATION", vrs::Record::Type::CONFIGURATION)
      .value("DATA", vrs::Record::Type::DATA)
      .export_values();

  auto record =
      py::class_<pyvrs::PyRecord, std::unique_ptr<pyvrs::PyRecord>>(m, "VRSRecord")
          .def_readonly("record_index", &pyvrs::PyRecord::recordIndex)
          .def_readonly("record_type", &pyvrs::PyRecord::recordType)
          .def_readonly("timestamp", &pyvrs::PyRecord::recordTimestamp)
          .def_readonly("stream_id", &pyvrs::PyRecord::streamId)
          .def_readonly("recordable_id", &pyvrs::PyRecord::streamId)
          .def_readonly("format_version", &pyvrs::PyRecord::recordFormatVersion)
          .def_property_readonly(
              "n_metadata_blocks",
              [](const pyvrs::PyRecord& record) { return record.datalayoutBlocks.size(); })
          .def_property_readonly(
              "n_image_blocks",
              [](const pyvrs::PyRecord& record) { return record.imageBlocks.size(); })
          .def_property_readonly(
              "n_audio_blocks",
              [](const pyvrs::PyRecord& record) { return record.audioBlocks.size(); })
          .def_property_readonly(
              "n_custom_blocks",
              [](const pyvrs::PyRecord& record) { return record.customBlocks.size(); })
          .def_property_readonly(
              "n_blocks_in_total",
              [](const pyvrs::PyRecord& record) {
                return record.datalayoutBlocks.size() + record.imageBlocks.size() +
                    record.audioBlocks.size() + record.customBlocks.size();
              })
          .def_readonly(
              "metadata_blocks",
              &pyvrs::PyRecord::datalayoutBlocks,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "image_blocks",
              &pyvrs::PyRecord::imageBlocks,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "audio_blocks",
              &pyvrs::PyRecord::audioBlocks,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "custom_blocks",
              &pyvrs::PyRecord::customBlocks,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "audio_specs",
              &pyvrs::PyRecord::audioSpecs,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "custom_block_specs",
              &pyvrs::PyRecord::customBlockSpecs,
              pybind11::return_value_policy::reference_internal)
          .def_readonly(
              "image_specs",
              &pyvrs::PyRecord::imageSpecs,
              pybind11::return_value_policy::reference_internal)
          .def(
              "__repr__",
              [](const pyvrs::PyRecord& record) {
                return fmt::format(
                    "VRSRecord(index={}, id={}, type={}, timestamp={})",
                    record.recordIndex,
                    record.streamId,
                    record.recordType,
                    record.recordTimestamp);
              })
          .def("__str__", [](const pyvrs::PyRecord& record) {
            return fmt::format(
                "{} record for {} @ {}s [{}]\n"
                "{} audio blocks, {} custom blocks, {} image blocks, {} metadata blocks",
                toupper(record.recordType),
                record.streamId,
                record.recordTimestamp,
                record.recordIndex,
                record.audioBlocks.size(),
                record.customBlocks.size(),
                record.imageBlocks.size(),
                record.datalayoutBlocks.size());
          });

  DEF_DICT_FUNC(record, PyRecord);

  py::class_<vrs::Bool>(m, "Bool").def(py::init<bool>());
  py::class_<vrs::Point2Dd>(m, "Point2Dd").def(py::init<double, double>());
  py::class_<vrs::Point2Df>(m, "Point2Df").def(py::init<float, float>());
  py::class_<vrs::Point2Di>(m, "Point2Di").def(py::init<int, int>());
  py::class_<vrs::Point3Dd>(m, "Point3Dd").def(py::init<double, double, double>());
  py::class_<vrs::Point3Df>(m, "Point3Df").def(py::init<float, float, float>());
  py::class_<vrs::Point3Di>(m, "Point3Di").def(py::init<int, int, int>());
  py::class_<vrs::Point4Dd>(m, "Point4Dd").def(py::init<double, double, double, double>());
  py::class_<vrs::Point4Df>(m, "Point4Df").def(py::init<float, float, float, float>());
  py::class_<vrs::Point4Di>(m, "Point4Di").def(py::init<int, int, int, int>());
}
#endif
} // namespace pyvrs
