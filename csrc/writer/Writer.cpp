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
#include <pybind11/detail/common.h>
#include <exception>
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Writer.h"

#include <string>
#include <vector>

// Includes needed for bindings (including marshalling STL containers)
#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <vrs/os/Platform.h>

#include "PyRecordable.h"
#include "VRSWriter.h"

namespace pyvrs {
namespace py = pybind11;
using namespace std;
using namespace vrs;

#define DEFINE_ALL_DATA_PIECE_PYBIND(TEMPLATE_TYPE) \
  DEFINE_DATA_PIECE_PYBIND(Value, TEMPLATE_TYPE)    \
  DEFINE_DATA_PIECE_PYBIND(Array, TEMPLATE_TYPE)    \
  DEFINE_DATA_PIECE_PYBIND(Vector, TEMPLATE_TYPE)   \
  DEFINE_DATA_PIECE_PYBIND(StringMap, TEMPLATE_TYPE)

#define DEFINE_DATA_PIECE_PYBIND(DATAPIECE_TYPE, TEMPLATE_TYPE)                                  \
  py::class_<pyvrs::DataPiece##DATAPIECE_TYPE##TEMPLATE_TYPE##Wrapper, pyvrs::DataPieceWrapper>( \
      m, "DataPiece" #DATAPIECE_TYPE #TEMPLATE_TYPE "Wrapper")                                   \
      .def(py::init<>())                                                                         \
      .def("set", &pyvrs::DataPiece##DATAPIECE_TYPE##TEMPLATE_TYPE##Wrapper::set);

#define DEFINE_MATRIX_DATA_PIECE_PYBIND(DATAPIECE_TYPE, MATRIX_TYPE, MATRIX_SIZE)        \
  py::class_<vrs::DATAPIECE_TYPE>(m, #DATAPIECE_TYPE)                                    \
      .def(py::init([](const std::vector<std::vector<MATRIX_TYPE>>& mat) {               \
        vrs::DATAPIECE_TYPE matrix;                                                      \
        if (mat.size() != MATRIX_SIZE) {                                                 \
          throw py::value_error("Matrix size must be " #MATRIX_SIZE "x" #MATRIX_SIZE);   \
        }                                                                                \
        for (int i = 0; i < mat.size(); i++) {                                           \
          if (mat[i].size() != MATRIX_SIZE) {                                            \
            throw py::value_error("Matrix size must be " #MATRIX_SIZE "x" #MATRIX_SIZE); \
          }                                                                              \
          for (int j = 0; j < mat[i].size(); j++) {                                      \
            matrix[i][j] = mat[i][j];                                                    \
          }                                                                              \
        }                                                                                \
        return matrix;                                                                   \
      }));

void pybind_writer(py::module& m) {
  py::class_<pyvrs::PyRecordFormat, std::unique_ptr<pyvrs::PyRecordFormat, py::nodelete>>(
      m, "RecordFormat")
      .def("getMembers", &pyvrs::PyRecordFormat::getMembers)
      .def("getJsonDataLayouts", &pyvrs::PyRecordFormat::getJsonDataLayouts);

  py::class_<pyvrs::PyStream, std::unique_ptr<pyvrs::PyStream, py::nodelete>>(m, "Stream")
      .def("createRecordFormat", &pyvrs::PyStream::createRecordFormat)
      .def(
          "createRecord",
          py::overload_cast<double, const pyvrs::PyRecordFormat*>(&pyvrs::PyStream::createRecord))
      .def(
          "createRecord",
          py::overload_cast<double, const pyvrs::PyRecordFormat*, py::array>(
              &pyvrs::PyStream::createRecord))
      .def(
          "createRecord",
          py::overload_cast<double, const pyvrs::PyRecordFormat*, py::array, py::array>(
              &pyvrs::PyStream::createRecord))
      .def(
          "createRecord",
          py::overload_cast<double, const pyvrs::PyRecordFormat*, py::array, py::array, py::array>(
              &pyvrs::PyStream::createRecord))
      .def("setCompression", &pyvrs::PyStream::setCompression)
      .def("setTag", &pyvrs::PyStream::setTag)
      .def("getStreamID", &pyvrs::PyStream::getStreamID);

  py::class_<pyvrs::VRSWriter>(m, "Writer")
      .def(py::init<>())
      .def("resetNewInstanceIds", &pyvrs::VRSWriter::resetNewInstanceIds)
      .def("create", py::overload_cast<const std::string&>(&pyvrs::VRSWriter::create))
      .def("createStream", &pyvrs::VRSWriter::createStream, py::return_value_policy::reference)
      .def(
          "createFlavoredStream",
          &pyvrs::VRSWriter::createFlavoredStream,
          py::return_value_policy::reference)
      .def("setTag", &pyvrs::VRSWriter::setTag)
      .def("writeRecords", &pyvrs::VRSWriter::writeRecords)
      .def("getBackgroundThreadQueueByteSize", &pyvrs::VRSWriter::getBackgroundThreadQueueByteSize)
      .def("close", &pyvrs::VRSWriter::close)
#if IS_VRS_FB_INTERNAL()
#include "Writer_fb.hpp"
#endif
      ;

  py::class_<pyvrs::DataPieceWrapper>(m, "DataPieceWrapper").def(py::init<>());

  py::class_<pyvrs::DataPieceStringWrapper, pyvrs::DataPieceWrapper>(m, "DataPieceStringWrapper")
      .def(py::init<>())
      .def("set", &pyvrs::DataPieceStringWrapper::set);

  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix2Dd, double, 2)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix2Df, float, 2)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix2Di, int, 2)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix3Dd, double, 3)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix3Df, float, 3)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix3Di, int, 3)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix4Dd, double, 4)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix4Df, float, 4)
  DEFINE_MATRIX_DATA_PIECE_PYBIND(Matrix4Di, int, 4)

// Define & generate the code for each POD type supported.
#define POD_MACRO DEFINE_ALL_DATA_PIECE_PYBIND
#include <vrs/helpers/PODMacro.inc>

  DEFINE_DATA_PIECE_PYBIND(Vector, string)
  DEFINE_DATA_PIECE_PYBIND(StringMap, string)
}
} // namespace pyvrs
