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

namespace pyvrs {

using namespace vrs;

class AnnotationConfiguration : public AutoDataLayout {
 public:
  AutoDataLayoutEnd endLayout;
};

class PyStream;
std::unique_ptr<PyStream> createFullAnnotationStream();
std::unique_ptr<PyStream> createLabelAnnotationStream();
std::unique_ptr<PyStream> createTimeSegmentAnnotationStream();
std::unique_ptr<PyStream> createFrameAnnotationStream();
std::unique_ptr<PyStream> createBoundingBoxAnnotationStream();
std::unique_ptr<PyStream> createKeyPointsAnnotationStream();

} // namespace pyvrs
