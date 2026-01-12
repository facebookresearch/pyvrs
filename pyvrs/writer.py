#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import time
from typing import Dict, List, Union

import numpy as np

from . import CompressionPreset, RecordFormat, RecordType, Stream, Writer
from .datalayout import VRSDataLayout

__all__ = [
    "TimestampOrderWriteException",
    "VRSRecordFormat",
    "VRSStream",
    "VRSWriter",
]

RECORD_TYPE_TO_STR = {
    RecordType.DATA: "Data",
    RecordType.CONFIGURATION: "Configuration",
    RecordType.STATE: "State",
}

MAX_QUEUE_BYTE_SIZE = 600 * 1024 * 1024  # 600MB


class TimestampOrderWriteException(Exception):
    def __init__(self, last_flushed_timestamp: float, provided_timestamp: float):
        self.message = f"""
            Timestamps must be greater than the timestamp of the last flush operation. Timestamp {provided_timestamp} is less than the last flushed timestamp of {last_flushed_timestamp}
        """
        super().__init__(self.message)


class VRSWriter:
    def __init__(self, filepath: str) -> None:
        self._writer = Writer()
        self.filepath = filepath
        self.file_created = False
        # Timestamp is supposed to be positive value.
        self.last_flushed_timestamp = -float("inf")

    def create_stream(
        self,
        name: str,
        flavor: str = "",
        compression: CompressionPreset = CompressionPreset.Zmedium,
    ) -> "VRSStream":
        if len(flavor) > 0:
            return VRSStream(
                self._writer.createFlavoredStream(name, flavor), self, compression
            )
        else:
            return VRSStream(self._writer.createStream(name), self, compression)

    def set_tag(self, tag_name: str, tag_value: str) -> None:
        if self.file_created:
            raise Exception("Tags should be set before file is created.")
        self._writer.setTag(tag_name, tag_value)

    def assert_timestamp(self, timestamp: float) -> None:
        if self.last_flushed_timestamp > timestamp:
            raise TimestampOrderWriteException(self.last_flushed_timestamp, timestamp)

    def flush_records(self, timestamp: float) -> int:
        self.assert_timestamp(timestamp)

        self._create()
        self.last_flushed_timestamp = timestamp

        # Block write while the queue size is greater than certain size
        waiting_str = "."
        cnt = 1
        while self._writer.getBackgroundThreadQueueByteSize() > MAX_QUEUE_BYTE_SIZE:
            if cnt % 10 == 0:
                print(f"\rWaiting {waiting_str}")
                waiting_str += "."
            cnt += 1
            time.sleep(0.1)
        return self._writer.writeRecords(timestamp)

    def _create(self):
        if not self.file_created:
            self.file_created = True
            if self._writer.create(self.filepath) != 0:
                raise Exception(f"Failed to create file at {self.filepath}")

    def close(self) -> None:
        return self._writer.close()

    # Recordable instance ids are automatically assigned when Recordable objects are created.
    # This guarantees that each Recordable gets a unique ID.
    # WARNING! If your code relies on specific instance IDs, your design is weak, and you are
    # setting up your project for a world of pain in the future.
    # Use flavors and tag pairs to identify your streams instead.
    # However, when many files are generated successively, it can lead to high instance
    # id values, which can be confusing, and even problematic for unit tests.
    # Use this API to reset the instance counters for each device type, so that the next devices
    # will get an instance id of 1.
    # ATTENTION! if you call this API at the wrong time, you can end up with multiple devices with
    # the same id, and end up in a messy situation. Avoid this API if you can!
    def resetNewInstanceIds(self) -> None:
        return self._writer.resetNewInstanceIds()


class VRSStream:
    def __init__(
        self,
        stream: Stream,
        writer: VRSWriter,
        compression: CompressionPreset = CompressionPreset.Zmedium,
    ) -> None:
        self.stream = stream
        self.stream.setCompression(compression)
        self.record_formats: Dict[RecordType, VRSRecordFormat] = {
            RecordType.DATA: VRSRecordFormat(
                self.stream.createRecordFormat(RecordType.DATA), RecordType.DATA
            ),
            RecordType.CONFIGURATION: VRSRecordFormat(
                self.stream.createRecordFormat(RecordType.CONFIGURATION),
                RecordType.CONFIGURATION,
            ),
            RecordType.STATE: VRSRecordFormat(
                self.stream.createRecordFormat(RecordType.STATE), RecordType.STATE
            ),
        }
        self.writer = writer
        self.config_record_created = False

    def get_data_record_metadata(self, index: int = 0) -> VRSDataLayout:
        return self.record_formats[RecordType.DATA].data_layouts[index]

    def get_config_record_metadata(self, index: int = 0) -> VRSDataLayout:
        return self.record_formats[RecordType.CONFIGURATION].data_layouts[index]

    def get_state_record_metadata(self, index: int = 0) -> VRSDataLayout:
        return self.record_formats[RecordType.STATE].data_layouts[index]

    def parse_create_record_params(
        self,
        record_type: RecordType,
        srcs: List[Union[VRSDataLayout, np.ndarray, np.generic]],
    ) -> List[Union[np.ndarray, np.generic]]:
        data_layouts = []
        data_srcs = []
        for src in srcs:
            if isinstance(src, VRSDataLayout):
                data_layouts.append(src)
            elif isinstance(src, (np.ndarray, np.generic)):
                data_srcs.append(src)
        if len(data_layouts) != len(self.record_formats[record_type].data_layouts):
            raise TypeError(
                f"Number of DataLayouts you provided doesn't match: "
                f"{len(data_layouts)} != "
                f"{len(self.record_formats[record_type].data_layouts)}"
            )
        for dl_param, dl in zip(
            data_layouts, self.record_formats[record_type].data_layouts
        ):
            if dl_param is not dl:
                raise TypeError(
                    f"Order or type of the DataLayout doesn't match: \n"
                    f"DataLayout for {RECORD_TYPE_TO_STR[dl_param.data_layout_type]}"
                    f" Record[{dl_param.index}] vs DataLayout for "
                    f"{RECORD_TYPE_TO_STR[dl.data_layout_type]} Record[{dl.index}]"
                )
        if len(data_srcs) > 3:
            raise TypeError(
                f"You provide {len(data_srcs)} but you are allowed to"
                f"put up to 3 sources."
            )
        return data_srcs

    def create_config_record(
        self, timestamp: float, *srcs: Union[VRSDataLayout, np.ndarray, np.generic]
    ) -> None:
        self.writer.assert_timestamp(timestamp)
        data_srcs = self.parse_create_record_params(RecordType.CONFIGURATION, srcs)

        self.stream.createRecord(
            timestamp, self.record_formats[RecordType.CONFIGURATION].get(), *data_srcs
        )
        self.config_record_created = True

    def create_data_record(
        self, timestamp: float, *srcs: Union[VRSDataLayout, np.ndarray, np.generic]
    ) -> None:
        self.writer.assert_timestamp(timestamp)

        if not self.config_record_created:
            raise Exception("Configuration record is not created yet.")
        data_srcs = self.parse_create_record_params(RecordType.DATA, srcs)

        self.stream.createRecord(
            timestamp, self.record_formats[RecordType.DATA].get(), *data_srcs
        )

    def create_state_record(
        self, timestamp: float, *srcs: Union[VRSDataLayout, np.ndarray, np.generic]
    ) -> None:
        self.writer.assert_timestamp(timestamp)

        data_srcs = self.parse_create_record_params(RecordType.STATE, srcs)

        self.stream.createRecord(
            timestamp, self.record_formats[RecordType.STATE].get(), *data_srcs
        )

    def set_compression(self, preset: CompressionPreset) -> None:
        self.stream.setCompression(preset)

    def set_tag(self, tag_name: str, tag_value: str) -> None:
        if self.writer.file_created:
            raise Exception("Tags should be set before file is created.")
        self.stream.setTag(tag_name, tag_value)

    def get_stream_id(self) -> str:
        return self.stream.getStreamID()

    def __str__(self) -> str:
        s = "\n".join(
            [
                str(self.record_formats[RecordType.DATA]),
                str(self.record_formats[RecordType.CONFIGURATION]),
                str(self.record_formats[RecordType.STATE]),
            ]
        )
        return s


class VRSRecordFormat:
    def __init__(
        self, record_format: RecordFormat, data_layout_type: RecordType
    ) -> None:
        self.__dict__["record_format"] = record_format
        self.__dict__["data_layout_type"] = data_layout_type
        members = record_format.getMembers()
        json_data_layouts = record_format.getJsonDataLayouts()
        if len(members) != len(json_data_layouts):
            raise ValueError
        self.__dict__["data_layouts"] = [
            VRSDataLayout(member, json_data_layout, data_layout_type, idx)
            for idx, (member, json_data_layout) in enumerate(
                zip(members, json_data_layouts)
            )
        ]

    def get(self) -> RecordFormat:
        return self.record_format

    def get_data_layout_type_str_header(self, index: int = 0) -> str:
        return (
            f"DataLayout for {RECORD_TYPE_TO_STR[self.data_layout_type]} Record"
            + (f" [{index}]" if len(self.data_layouts) > 1 else "")
            + ": "
        )

    def __str__(self) -> str:
        data_layouts = (
            self.data_layouts
            if len(self.data_layouts) > 0
            else [VRSDataLayout(None, None, self.data_layout_type, 0)]
        )
        s = "\n".join(
            [
                self.get_data_layout_type_str_header(idx) + str(data_layout)
                for idx, data_layout in enumerate(data_layouts)
            ]
        )

        return s
