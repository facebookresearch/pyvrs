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

// This include *must* be before any STL include! See Python C API doc.
#define PY_SSIZE_T_CLEAN
#include <Python.h> // IWYU pragma: keepo

#include "VRSWriter.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Includes needed for bindings (including marshalling STL containers)
#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define DEFAULT_LOG_CHANNEL "VRSWriter"
#include <logging/Log.h>

#include "../VrsBindings.h"
#include "../utils/PyUtils.h"
#include "StreamFactory.h"

// Open source DataLayout definitions
#include "datalayouts/SampleDataLayout.h"

namespace py = pybind11;

using PyW = pyvrs::VRSWriter;

namespace pyvrs {

VRSWriter::VRSWriter() {
  init();
}

VRSWriter::~VRSWriter() {
  close();
}

void VRSWriter::init() {
  initVrsBindings();
  writer_.setCompressionThreadPoolSize(std::thread::hardware_concurrency());
  writer_.trackBackgroundThreadQueueByteSize();

  /// Register open source stream writers (begin)
  StreamFactory::getInstance().registerStreamCreationFunction(
      "sample", []() { return createSampleStream(); });
  StreamFactory::getInstance().registerStreamCreationFunction(
      "sample_with_flavor_1", []() { return createSampleStream("flavor_1"); });
  StreamFactory::getInstance().registerStreamCreationFunction(
      "sample_with_flavor_2", []() { return createSampleStream("flavor_2"); });
  StreamFactory::getInstance().registerFlavoredStreamCreationFunction(
      "flavored_sample", [](const string& flavor) { return createSampleStream(flavor); });
  StreamFactory::getInstance().registerStreamCreationFunction(
      "sample_with_image", createSampleStreamWithImage);
  StreamFactory::getInstance().registerStreamCreationFunction(
      "sample_with_multiple_data_layout", createSampleStreamWithMultipleDataLayout);
  /// Register open source stream writers (end)

#if IS_VRS_FB_INTERNAL()
  registerFbOnlyStreamWriters();
#endif
}

void VRSWriter::resetNewInstanceIds() {
  Recordable::resetNewInstanceIds();
}

int VRSWriter::create(const std::string& filePath) {
  return writer_.createFileAsync(filePath);
}

PyStream* VRSWriter::createStream(const std::string& name) {
  streams_.emplace_back(StreamFactory::getInstance().createStream(name));
  auto stream = streams_.back().get();
  if (stream == nullptr) {
    streams_.pop_back();
    throw py::value_error("Unsupported stream name " + name);
  }
  writer_.addRecordable(stream->getRecordable());
  return stream;
}

PyStream* VRSWriter::createFlavoredStream(const std::string& name, const std::string& flavor) {
  streams_.emplace_back(StreamFactory::getInstance().createFlavoredStream(name, flavor));
  auto stream = streams_.back().get();
  if (stream == nullptr) {
    streams_.pop_back();
    throw py::value_error("Unsupported stream name " + name);
  }
  writer_.addRecordable(stream->getRecordable());
  return stream;
}

void VRSWriter::setTag(const std::string& tagName, const std::string& tagValue) {
  writer_.setTag(tagName, tagValue);
}

int VRSWriter::writeRecords(double maxTimestamp) {
  return writer_.writeRecordsAsync(maxTimestamp);
}

uint64_t VRSWriter::getBackgroundThreadQueueByteSize() {
  return writer_.getBackgroundThreadQueueByteSize();
}

int VRSWriter::close() {
  return writer_.waitForFileClosed();
}

} // namespace pyvrs
