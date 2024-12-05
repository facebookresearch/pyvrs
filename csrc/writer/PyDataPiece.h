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

#include <pybind11/pybind11.h>

#include <map>
#include <string>
#include <vector>

#include <vrs/DataPieces.h>

namespace pyvrs {

namespace py = pybind11;
using namespace vrs;

class DataPieceWrapper {
 public:
  virtual ~DataPieceWrapper() = default;
};

// We should treat DataPieceString separatedly from other DataPiece types.
class DataPieceStringWrapper : public DataPieceWrapper {
 public:
  void set(std::string& v) {
    dpv_->stage(v);
  }
  void setDataPiece(DataPieceString* dpv) {
    dpv_ = dpv;
  }

 private:
  DataPieceString* dpv_;
};

#define DEFINE_DATA_PIECE_VALUE_WRAPPER(TEMPLATE_TYPE)                     \
  class DataPieceValue##TEMPLATE_TYPE##Wrapper : public DataPieceWrapper { \
   public:                                                                 \
    void set(TEMPLATE_TYPE& v) {                                           \
      dpv_->set(v);                                                        \
    }                                                                      \
    void setDataPiece(DataPieceValue<TEMPLATE_TYPE>* dpv) {                \
      dpv_ = dpv;                                                          \
    }                                                                      \
                                                                           \
   private:                                                                \
    DataPieceValue<TEMPLATE_TYPE>* dpv_;                                   \
  };

#define DEFINE_DATA_PIECE_VECTOR_WRAPPER(TEMPLATE_TYPE)                     \
  class DataPieceVector##TEMPLATE_TYPE##Wrapper : public DataPieceWrapper { \
   public:                                                                  \
    void set(std::vector<TEMPLATE_TYPE>& v) {                               \
      dpv_->stage(v);                                                       \
    }                                                                       \
    void setDataPiece(DataPieceVector<TEMPLATE_TYPE>* dpv) {                \
      dpv_ = dpv;                                                           \
    }                                                                       \
                                                                            \
   private:                                                                 \
    DataPieceVector<TEMPLATE_TYPE>* dpv_;                                   \
  };

#define DEFINE_DATA_PIECE_ARRAY_WRAPPER(TEMPLATE_TYPE)                                         \
  class DataPieceArray##TEMPLATE_TYPE##Wrapper : public DataPieceWrapper {                     \
   public:                                                                                     \
    void set(std::vector<TEMPLATE_TYPE>& v) {                                                  \
      if (v.size() > dpv_->getArraySize()) {                                                   \
        throw py::value_error("Given array does not fit in target field " + dpv_->getLabel()); \
      }                                                                                        \
      dpv_->set(v);                                                                            \
    }                                                                                          \
    void setDataPiece(DataPieceArray<TEMPLATE_TYPE>* dpv) {                                    \
      dpv_ = dpv;                                                                              \
    }                                                                                          \
                                                                                               \
   private:                                                                                    \
    DataPieceArray<TEMPLATE_TYPE>* dpv_;                                                       \
  };

#define DEFINE_DATA_PIECE_MAP_WRAPPER(TEMPLATE_TYPE)                           \
  class DataPieceStringMap##TEMPLATE_TYPE##Wrapper : public DataPieceWrapper { \
   public:                                                                     \
    void set(std::map<std::string, TEMPLATE_TYPE>& v) {                        \
      dpv_->stage(v);                                                          \
    }                                                                          \
    void setDataPiece(DataPieceStringMap<TEMPLATE_TYPE>* dpv) {                \
      dpv_ = dpv;                                                              \
    }                                                                          \
                                                                               \
   private:                                                                    \
    DataPieceStringMap<TEMPLATE_TYPE>* dpv_;                                   \
  };

#define DEFINE_ALL_DATA_PIECE_WRAPPER(TEMPLATE_TYPE) \
  DEFINE_DATA_PIECE_VALUE_WRAPPER(TEMPLATE_TYPE)     \
  DEFINE_DATA_PIECE_VECTOR_WRAPPER(TEMPLATE_TYPE)    \
  DEFINE_DATA_PIECE_ARRAY_WRAPPER(TEMPLATE_TYPE)     \
  DEFINE_DATA_PIECE_MAP_WRAPPER(TEMPLATE_TYPE)

// Define DataPieceWrapper classes
// Define & generate the code for each POD type supported.
#define POD_MACRO DEFINE_ALL_DATA_PIECE_WRAPPER
#include <vrs/helpers/PODMacro.inc>

DEFINE_DATA_PIECE_MAP_WRAPPER(string)
DEFINE_DATA_PIECE_VECTOR_WRAPPER(string)

} // namespace pyvrs
