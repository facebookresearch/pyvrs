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

#include "Filter.h"

namespace pyvrs {
namespace py = pybind11;

#if IS_VRS_OSS_CODE()
void pybind_filter(py::module& m) {
  py::class_<pyvrs::ImageRecordInfo>(m, "ImageRecordInfo")
      .def_readonly("timestamp", &pyvrs::ImageRecordInfo::timestamp)
      .def_readonly("recordableId", &pyvrs::ImageRecordInfo::streamId)
      .def_readonly("streamId", &pyvrs::ImageRecordInfo::streamId);

  /// Aynchronous image filter handler. This class doesn't actually perform any filtering,
  /// but it provides an easy way to iterate over all the images of a VRS file so one can give back
  /// modified versions of the images "later", opening the door to multithreaded processing.
  /// Note that PyAsyncImageFilter itself knows nothing of multithreading (though VRS read & write
  /// infra will use multithreading, in paricular when reading and/or writing to the cloud).

  py::class_<PyAsyncImageFilter>(m, "AsyncImageFilter")
      .def(py::init<std::shared_ptr<pyvrs::FilteredFileReader>>())

      .def(
          "createOutputFile",
          py::overload_cast<const string&>(&PyAsyncImageFilter::createOutputFile))
      .def(
          "create_output_file",
          py::overload_cast<const string&>(&PyAsyncImageFilter::createOutputFile))

      /// Get the next image from the input file. Use its record_index to get the record info.
      /// Note that record_index are indexes internal to this AsyncImageFilter instance.
      /// For every image retrieved using getNextImage(), a corresponding writeProcessedImage() or
      /// discardRecord() call **must** be made with the same record_index, or the output file can
      /// not be closed.
      .def("getNextImage", &PyAsyncImageFilter::getNextImage)
      .def("get_next_image", &PyAsyncImageFilter::getNextImage)

      /// After filtering (or not), for each image buffer getNextImage() returned, send a
      /// corresponding image to be written out.
      /// Note that writeProcessedImage() immmediately takes ownership of the image buffer to
      /// avoid unnecessary copies, so once an image buffer is sent to writeProcessedImage(), it
      /// should be considered off-limits. Note that while you may return images out of
      /// sequence, VRS must writte them out in order. This means that VRS will build-up an
      /// in-memory backlog of records to be written out as long as it doesn't have a complete
      /// sequence. So while strict order isn't required between matching
      /// getNextImage/writeProcessedImage pairs, if you hold a specific image for too long
      /// while processing many more images, you may run out of memory.
      .def("writeProcessedImage", &PyAsyncImageFilter::writeProcessedImage)
      .def("write_processed_image", &PyAsyncImageFilter::writeProcessedImage)

      /// Discard a record containing an image from the output file.
      /// The image referenced must have been provided by get_next_image() and not have been handed
      /// to write_processed_image().
      .def("discard_record", &PyAsyncImageFilter::discardRecord)

      /// Get stream information details for an image buffer using its record_index.
      .def("getImageRecordInfo", &PyAsyncImageFilter::getImageRecordInfo)
      .def("get_image_record_info", &PyAsyncImageFilter::getImageRecordInfo)

      /// To access records metadata, you must first call need_metadata() to indicate that you need
      /// metadata, then call get_metadata() to get the metadata for a record being processed.
      /// Returns a PyRecord, for consistency, which only contains metadata blocks.
      .def("need_record_metadata", &PyAsyncImageFilter::needRecordMetadata)
      .def("get_record_metadata", &PyAsyncImageFilter::getRecordMetadata)

      /// Get the count of images retrieved using getNextImage(), but not yet returned using
      /// writeProcessedImage().
      .def("getPendingCount", &PyAsyncImageFilter::getPendingCount)
      .def("get_pending_count", &PyAsyncImageFilter::getPendingCount)

      /// Finish processing pending data & flush all pending buffers. When this call returns,
      /// the output file is either successfuly written out, or an error occurred.
      .def("closeFile", &PyAsyncImageFilter::closeFile)
      .def("close_file", &PyAsyncImageFilter::closeFile)

      .def("__iter__", [](PyAsyncImageFilter& self) { return PyAsyncImageFilterIterator(self); });

  py::class_<PyAsyncImageFilterIterator>(m, "AsyncImageFilterIterator")
      .def(
          "__iter__",
          [](PyAsyncImageFilterIterator& it) -> PyAsyncImageFilterIterator& { return it; })
      .def("__next__", &PyAsyncImageFilterIterator::next);
}
#endif
} // namespace pyvrs
