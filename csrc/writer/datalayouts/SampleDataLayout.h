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

#include <vrs/DataLayout.h>
#include <vrs/DataLayoutConventions.h>
#include <vrs/DataPieces.h>

namespace pyvrs {

using namespace vrs;

class MyConfiguration : public AutoDataLayout {
 public:
  DataPieceValue<datalayout_conventions::ImageSpecType> width{datalayout_conventions::kImageWidth};
  DataPieceValue<datalayout_conventions::ImageSpecType> height{
      datalayout_conventions::kImageHeight};
  DataPieceValue<datalayout_conventions::ImageSpecType> pixelFormat{
      datalayout_conventions::kImagePixelFormat};
  AutoDataLayoutEnd endLayout;
};

class MyMetadata : public AutoDataLayout {
 public:
  DataPieceValue<float> roomTemperature{"room_temperature"};
  DataPieceValue<int32_t> cameraId{"camera_id"};
  DataPieceStringMap<int32_t> stringIntMap{"some_string_int_map"};
  DataPieceArray<double> arrayOfDouble{"doubles", 3};
  DataPieceString aString{"some_string"};

  AutoDataLayoutEnd endLayout;
};

class PyStream;
std::unique_ptr<PyStream> createSampleStream(const string& flavor = {});
std::unique_ptr<PyStream> createSampleStreamWithImage();
std::unique_ptr<PyStream> createSampleStreamWithMultipleDataLayout();

} // namespace pyvrs
