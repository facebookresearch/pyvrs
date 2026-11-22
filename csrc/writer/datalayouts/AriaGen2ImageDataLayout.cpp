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

#include "AriaGen2ImageDataLayout.h"

#include <vrs/RecordFormat.h>

#include "../PyRecordable.h"

using namespace vrs;

namespace pyvrs {

std::unique_ptr<PyStream> createAriaGen2ImageStream(
    const std::string& flavor,
    RecordableTypeId typeId,
    const std::string& codec) {
  constexpr uint32_t kVersion = 2;

  auto configurationRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::CONFIGURATION,
      kVersion,
      std::make_unique<ImageSensorConfigurationLayout>(/*allocateVideoFields=*/true));

  auto dataContentBlocks = codec == "H.265"
      ? std::vector<ContentBlock>{ContentBlock("H.265", ImageContentBlockSpec::kQualityUndefined)}
      : std::vector<ContentBlock>{ContentBlock(ImageFormat::RAW)};

  auto dataRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::DATA,
      kVersion,
      std::make_unique<ImageDataLayout>(/*allocateVideoFields=*/true),
      dataContentBlocks);

  return std::make_unique<PyStream>(
      typeId, flavor, std::move(configurationRecordFormat), std::move(dataRecordFormat));
}

} // namespace pyvrs
