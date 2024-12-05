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

#include "StreamFactory.h"

#include <vrs/RecordFormat.h>

namespace pyvrs {

StreamFactory& StreamFactory::getInstance() {
  static StreamFactory instance;
  return instance;
}

void StreamFactory::registerStreamCreationFunction(
    const std::string& name,
    std::function<std::unique_ptr<PyStream>()> func) {
  streamCreationFunctionMap_[name] = func;
}

std::unique_ptr<PyStream> StreamFactory::createStream(const std::string& name) {
  auto func = streamCreationFunctionMap_.find(name);
  if (func != streamCreationFunctionMap_.end()) {
    return func->second();
  }
  return std::unique_ptr<PyStream>(nullptr);
}

void StreamFactory::registerFlavoredStreamCreationFunction(
    const std::string& name,
    FlavoredStreamCreationFunc func) {
  flavoredStreamCreationFunctionMap_[name] = func;
}

std::unique_ptr<PyStream> StreamFactory::createFlavoredStream(
    const std::string& name,
    const std::string& flavor) {
  auto func = flavoredStreamCreationFunctionMap_.find(name);
  if (func != flavoredStreamCreationFunctionMap_.end()) {
    return func->second(flavor);
  }
  return std::unique_ptr<PyStream>(nullptr);
}

} // namespace pyvrs
