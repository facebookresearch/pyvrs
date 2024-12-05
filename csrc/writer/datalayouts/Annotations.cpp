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

#include "Annotations.h"

#include <vrs/RecordFormat.h>
#include <vrs/gaia/annotations/Annotations.h>

#include "../PyRecordable.h"

namespace pyvrs {
constexpr const char* kAnnotationTypeTagField = "annotation_type";
constexpr const char* kFullAnnotation = "full_annotation";
constexpr const char* kLabelAnnotation = "label_annotation";
constexpr const char* kTimeSegmentAnnotation = "time_segment_annotation";
constexpr const char* kFrameAnnotation = "frame_annotation";
constexpr const char* kBoundingBoxAnnotation = "bounding_box_annotation";
constexpr const char* kKeyPointsAnnotation = "key_points_annotation";

template <class T>
std::unique_ptr<PyStream> createAnnotationStream(const string& annotationType) {
  // RecordFormat for configuration record.
  auto configurationRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::CONFIGURATION, 1, std::make_unique<AnnotationConfiguration>());
  // If your record format only need to hold a raw DataLayout, you can directly pass it.
  auto dataRecordFormat =
      std::make_unique<PyRecordFormat>(Record::Type::DATA, 1, std::make_unique<T>());
  auto stream = std::make_unique<PyStream>(
      RecordableTypeId::AnnotationStream,
      std::move(configurationRecordFormat),
      std::move(dataRecordFormat));
  stream->setTag(kAnnotationTypeTagField, annotationType);
  return std::move(stream);
}
std::unique_ptr<PyStream> createFullAnnotationStream() {
  return createAnnotationStream<FullAnnotationDataLayout>(kFullAnnotation);
}
std::unique_ptr<PyStream> createLabelAnnotationStream() {
  return createAnnotationStream<LabelAnnotationDataLayout>(kLabelAnnotation);
}
std::unique_ptr<PyStream> createTimeSegmentAnnotationStream() {
  return createAnnotationStream<TimeSegmentAnnotationDataLayout>(kTimeSegmentAnnotation);
}
std::unique_ptr<PyStream> createFrameAnnotationStream() {
  return createAnnotationStream<FrameAnnotationDataLayout>(kFrameAnnotation);
}
std::unique_ptr<PyStream> createBoundingBoxAnnotationStream() {
  return createAnnotationStream<BoundingBoxAnnotationDataLayout>(kBoundingBoxAnnotation);
}
std::unique_ptr<PyStream> createKeyPointsAnnotationStream() {
  return createAnnotationStream<KeyPointsAnnotationDataLayout>(kKeyPointsAnnotation);
}
} // namespace pyvrs
