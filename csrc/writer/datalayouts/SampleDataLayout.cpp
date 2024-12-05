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

#include "SampleDataLayout.h"

#include <vrs/RecordFormat.h>

#include "../PyRecordable.h"

namespace pyvrs {

/*
 * Terms:
 *  - PyStream: Wrapper class of Recordable that represents stream.
 *    This class can contain 3 RecordFormats, Configuration, Data and State RecordFormat.
 *    ex. Camera stream, imu stream.
 *  - PyRecordFormat: Wrapper class of RecordFormat.
 *    You should add your own DataLayouts and ContentBlocks to define how each record
 *    will look like when you write it to file.
 *
 * This sample code demonstrates how to set up PyRecordFormat and PyStream.
 */

/*
 * Use this template to create a stream with records containing only metadata (ex. imu stream).
 *  - Configuration RecordFormat: 1 DataLayout
 *  - Data RecordFormat: 1 DataLayout
 */
std::unique_ptr<PyStream> createSampleStream(const string& flavor) {
  // RecordFormat for configuration record.
  auto configurationRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::CONFIGURATION, 1, std::make_unique<MyConfiguration>());

  // If your record format only need to hold a raw DataLayout, you can directly pass it.
  auto dataRecordFormat =
      std::make_unique<PyRecordFormat>(Record::Type::DATA, 1, std::make_unique<MyMetadata>());

  return flavor.empty() ? std::make_unique<PyStream>(
                              RecordableTypeId::UnitTest1,
                              std::move(configurationRecordFormat),
                              std::move(dataRecordFormat))
                        : std::make_unique<PyStream>(
                              RecordableTypeId::UnitTest1,
                              flavor,
                              std::move(configurationRecordFormat),
                              std::move(dataRecordFormat));
}

/*
 * Use this template to create a stream with records containing metadata plus an image.
 *  - Configuration RecordFormat: 1 DataLayout
 *  - Data RecordFormat: 1 DataLayout + 1 image
 * You can change the ContentBlock from image to something else (audio, custom).
 */
std::unique_ptr<PyStream> createSampleStreamWithImage() {
  // RecordFormat for configuration record.
  auto configurationRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::CONFIGURATION, 1, std::make_unique<MyConfiguration>());

  // To add image in record, you need to create an additional ContentBlocks
  std::vector<ContentBlock> dataContentBlocks = {ContentBlock(ImageFormat::RAW)};

  // If you want to add additional content blocks, you have to create a wrapper
  auto dataRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::DATA, 1, std::make_unique<MyMetadata>(), dataContentBlocks);

  return std::make_unique<PyStream>(
      RecordableTypeId::UnitTest1,
      std::move(configurationRecordFormat),
      std::move(dataRecordFormat));
}

/*
 * Use this template to create a stream with two metadata blocks plus one image (Polaris records?)
 *  - Configuration RecordFormat: 1 DataLayout
 *  - Data RecordFormat: 2 DataLayouts + 1 image

 */
std::unique_ptr<PyStream> createSampleStreamWithMultipleDataLayout() {
  // RecordFormat for configuration record.
  auto configurationRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::CONFIGURATION, 1, std::make_unique<MyConfiguration>());

  std::vector<ContentBlock> dataContentBlock = {ContentBlock(ImageFormat::RAW)};
  std::vector<ContentBlock> dataContentBlock2 = {};

  // Pass a vector of unique_ptr<PythonDataLayout> to the constructor of record format.
  auto dataRecordFormat = std::make_unique<PyRecordFormat>(
      Record::Type::DATA,
      1,
      std::make_unique<MyMetadata>(),
      std::make_unique<MyMetadata>(),
      dataContentBlock,
      dataContentBlock2);

  return std::make_unique<PyStream>(
      RecordableTypeId::UnitTest1,
      std::move(configurationRecordFormat),
      std::move(dataRecordFormat));
}
} // namespace pyvrs
