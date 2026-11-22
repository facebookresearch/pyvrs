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

// Aria Gen2 camera DataLayouts for OSS pyvrs writer.
// Vendored from arvr/libraries/visiontypes/vrs/data_layouts/ImageDataLayout.h
// with namespace changed from visiontypes::detail to pyvrs.

#pragma once

#include <cstdint>

#include <vrs/DataLayout.h>
#include <vrs/DataLayoutConventions.h>
#include <vrs/DataPieces.h>

namespace pyvrs {

using vrs::OptionalDataPieces;
using vrs::datalayout_conventions::ImageSpecType;

// Additional fields to enable in ImageSensorConfigurationLayout when data was
// encoded as video before recording.
struct VideoConfigurationFields {
  vrs::DataPieceString videoCodecName{vrs::datalayout_conventions::kImageCodecName};
};

struct ImageSensorConfigurationLayout : public vrs::AutoDataLayout {
  static constexpr uint32_t kVersion = 2;

  explicit ImageSensorConfigurationLayout(bool allocateVideoFields = false)
      : videoConfigurationFields(allocateVideoFields) {}

  vrs::DataPieceString deviceType{"device_type"};
  vrs::DataPieceString deviceVersion{"device_version"};
  vrs::DataPieceString deviceSerial{"device_serial"};

  vrs::DataPieceValue<std::uint32_t> cameraId{"camera_id"};
  vrs::DataPieceValue<std::uint32_t> streamType{"stream_type"};
  vrs::DataPieceValue<std::uint32_t> streamIndex{"stream_index"};

  vrs::DataPieceString sensorModel{"sensor_model"};
  vrs::DataPieceString sensorSerial{"sensor_serial"};

  vrs::DataPieceValue<double> nominalRateHz{"nominal_rate"};

  vrs::DataPieceValue<ImageSpecType> imageWidth{vrs::datalayout_conventions::kImageWidth};
  vrs::DataPieceValue<ImageSpecType> imageHeight{vrs::datalayout_conventions::kImageHeight};
  vrs::DataPieceValue<ImageSpecType> imageStride{vrs::datalayout_conventions::kImageStride};
  vrs::DataPieceValue<ImageSpecType> imageStride2{vrs::datalayout_conventions::kImageStride2};
  vrs::DataPieceValue<ImageSpecType> pixelFormat{vrs::datalayout_conventions::kImagePixelFormat};
  vrs::DataPieceValue<ImageSpecType> plane2OffsetRows{"image_plane_2_offset_rows"};
  vrs::DataPieceValue<ImageSpecType> plane3OffsetRows{"image_plane_3_offset_rows"};

  vrs::DataPieceValue<std::uint32_t> imageOrientation{"image_orientation"};
  vrs::DataPieceValue<std::uint32_t> shutterDirection{"shutter_direction"};

  vrs::DataPieceValue<double> exposureDurationMin{"exposure_duration.min"};
  vrs::DataPieceValue<double> exposureDurationMax{"exposure_duration.max"};

  vrs::DataPieceValue<double> gainMin{"gain.min"};
  vrs::DataPieceValue<double> gainMax{"gain.max"};

  vrs::DataPieceValue<double> gammaFactor{"gamma_factor"};

  vrs::DataPieceString factoryCalibration{"factory_calibration"};
  vrs::DataPieceString onlineCalibration{"online_calibration"};

  vrs::DataPieceString description{"description"};

  vrs::DataPieceString cameraMuxModeName{"camera_mux_mode_name"};

  const OptionalDataPieces<VideoConfigurationFields> videoConfigurationFields;

  vrs::AutoDataLayoutEnd end;
};

// Additional fields to enable in ImageDataLayout when data was encoded as video
// before recording.
struct VideoDataFields {
  vrs::DataPieceValue<double> keyFrameTimestamp{
      vrs::datalayout_conventions::kImageKeyFrameTimeStamp};
  vrs::DataPieceValue<ImageSpecType> keyFrameIndex{
      vrs::datalayout_conventions::kImageKeyFrameIndex};
};

struct ImageDataLayout : public vrs::AutoDataLayout {
  static constexpr uint32_t kVersion = 2;

  explicit ImageDataLayout(bool allocateVideoFields = false)
      : videoDataFields(allocateVideoFields) {}

  vrs::DataPieceValue<std::uint64_t> groupId{"group_id"};
  vrs::DataPieceValue<std::uint64_t> groupMask{"group_mask"};
  vrs::DataPieceValue<std::uint64_t> streamIndexMask{"stream_index_mask"};
  vrs::DataPieceValue<std::uint64_t> frameNumber{"frame_number"};
  vrs::DataPieceValue<std::uint32_t> frameTag{"frame_tag"};
  vrs::DataPieceValue<double> exposureDuration{"exposure_duration_s"};
  vrs::DataPieceValue<double> gain{"gain"};
  vrs::DataPieceValue<double> readoutDurationSeconds{"readout_duration_s"};
  vrs::DataPieceValue<std::int64_t> captureTimestampNs{"capture_timestamp_ns"};
  vrs::DataPieceValue<std::int64_t> captureTimestampInProcessingClockDomainNs{
      "capture_timestamp_in_processing_clock_domain_ns"};
  vrs::DataPieceValue<std::int64_t> arrivalTimestampNs{"arrival_timestamp_ns"};
  vrs::DataPieceValue<std::int64_t> processingStartTimestampNs{"processing_start_timestamp_ns"};
  vrs::DataPieceValue<double> temperature{"temperature_deg_c"};
  vrs::DataPieceVector<uint8_t> imageMetadata{"image_metadata"};

  const OptionalDataPieces<VideoDataFields> videoDataFields;

  vrs::DataPieceValue<double> focusDistanceMm{"focus_distance_mm", -1.0};

  vrs::AutoDataLayoutEnd end;
};

class PyStream;

std::unique_ptr<PyStream> createAriaGen2ImageStream(
    const std::string& flavor,
    vrs::RecordableTypeId typeId,
    const std::string& codec = "H.265");

} // namespace pyvrs
