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
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <set>
#include <sstream>
#include <string>

#include <pybind11/pybind11.h>

#include <fmt/format.h>

#include <vrs/Record.h>
#include <vrs/StreamId.h>

namespace pyvrs {
namespace py = pybind11;

/// Following classes are custom exceptions for VRS.
/// \brief Custom exception class VRS when record doesn't exist for timestamp.
class TimestampNotFoundError : public std::exception {
 public:
  explicit TimestampNotFoundError(
      double timestamp,
      double epsilon = 0,
      vrs::StreamId streamId = {},
      vrs::Record::Type recordType = vrs::Record::Type::UNDEFINED) {
    std::stringstream ss;

    ss << "Record not found";
    if (streamId.isValid()) {
      ss << fmt::format(" for stream {}", streamId.getFullName());
    }
    if (recordType != vrs::Record::Type::UNDEFINED) {
      ss << fmt::format(" for record type {}", toString(recordType));
    }
    if (epsilon != 0) {
      ss << fmt::format(" in range ({0}-{1})-({0}+{1})", timestamp, epsilon);
    } else {
      ss << fmt::format(" at timestamp {}", timestamp);
    }
    message_ = ss.str();
  }

  const char* what() const noexcept override {
    return message_.c_str();
  }

 private:
  std::string message_;
};

/// \brief Custom exception class for VRS when the stream doesn't exist.
class StreamNotFoundError : public std::exception {
 public:
  explicit StreamNotFoundError(
      vrs::RecordableTypeId recordableTypeId,
      const std::set<vrs::StreamId>& availableStreamIds)
      : StreamNotFoundError(vrs::toString(recordableTypeId), availableStreamIds) {}
  explicit StreamNotFoundError(
      const std::string& streamId,
      const std::set<vrs::StreamId>& availableStreamIds) {
    std::stringstream ss;
    ss << fmt::format("No matching stream for {}. Available streams are:\n", streamId);
    for (auto it : availableStreamIds) {
      ss << "  " << it.getFullName() << "\n";
    }
    message_ = ss.str();
  }
  const char* what() const noexcept override {
    return message_.c_str();
  }

 private:
  std::string message_;
};

/// Binds methods and classes for PyExceptions.
void pybind_exception(py::module& m);
} // namespace pyvrs
