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

from collections.abc import Sequence
from pathlib import Path
from typing import overload

from . import AsyncMultiReader, AsyncReader, MultiReader, Reader
from .record import VRSRecord


class VRSReaderSlice(Sequence):
    """A slice into a VRS file, i.e. an arbitrary collection of records. Can be further sliced and
    indexed, but looses some of the smarts present in the full VRSReader (e.g. the ability to
    filter) and some richer properties like temporal information.
    """

    def __init__(self, path: str | Path, r, indices: Sequence[int]) -> None:
        self._path = Path(path)
        self._reader = r
        self._indices = indices

    @overload
    def __getitem__(self, i: int) -> VRSRecord: ...

    @overload
    def __getitem__(self, i: slice) -> "VRSReaderSlice": ...

    def __getitem__(self, i: int | slice) -> "VRSRecord | VRSReaderSlice":
        return index_or_slice_records(self._path, self._reader, self._indices, i)

    def __len__(self) -> int:
        return len(self._indices)

    def __repr__(self) -> str:
        return f"VRSReaderSlice(path={str(self._path)!r}, len={len(self._indices)})"

    def __str__(self) -> str:
        return "\n".join(
            [str(self._path), f"Slice containing {len(self._indices)} records"]
        )


class AsyncVRSReaderSlice(Sequence):
    """A slice into a VRS file, i.e. an arbitrary collection of records. Can be further sliced and
    indexed, but looses some of the smarts present in the full VRSReader (e.g. the ability to
    filter) and some richer properties like temporal information.
    This should be created only via AsyncVRSReader.async_read_record call.
    """

    def __init__(self, path: str | Path, r, indices: Sequence[int]) -> None:
        self._path = Path(path)
        self._reader = r
        self._indices = indices

    def __aiter__(self):
        self.index = 0
        return self

    async def __anext__(self):
        if self.index == len(self):
            raise StopAsyncIteration
        result = await self[self.index]
        self.index += 1
        return result

    @overload
    async def __getitem__(self, i: int) -> VRSRecord: ...

    @overload
    async def __getitem__(self, i: slice) -> "AsyncVRSReaderSlice": ...

    async def __getitem__(self, i: int | slice) -> "VRSRecord | AsyncVRSReaderSlice":
        return await async_index_or_slice_records(
            self._path, self._reader, self._indices, i
        )

    def __len__(self) -> int:
        return len(self._indices)

    def __repr__(self) -> str:
        return (
            f"AsyncVRSReaderSlice(path={str(self._path)!r}, len={len(self._indices)})"
        )

    def __str__(self) -> str:
        return "\n".join(
            [str(self._path), f"Slice containing {len(self._indices)} records"]
        )


def index_or_slice_records(
    path: str | Path,
    reader: Reader | MultiReader,
    vrs_indices: Sequence[int],
    indices: int | slice,
) -> VRSRecord | VRSReaderSlice:
    """Shared logic to index or slice into a VRSReader or VRSReaderSlice. Returns either a
    VRSReaderSlice (if i is a slice or iterable) or a VRSRecord (if i is an integer).

    Args:
        path: a file path
        reader: a reader instance which is based on the class extended from BaseVRSReader
        vrs_indices: a list of absolute indices for the VRS file that you are interested in.
                     If you apply filter, this list should be the one after applying the filter.
        indices: A set of indices or an index for vrs_indices that you want to read.

    Returns:
        VRSReaderSlice if the given indices is a slice, otherwise read a record and returns VRSRecord object.
    """
    if isinstance(indices, slice):
        return VRSReaderSlice(path, reader, vrs_indices[indices])
    else:
        record = reader.read_record(vrs_indices[indices])
        return VRSRecord(record)


async def async_index_or_slice_records(
    path: str | Path,
    reader: AsyncReader | AsyncMultiReader,
    vrs_indices: Sequence[int],
    indices: int | slice,
) -> VRSRecord | AsyncVRSReaderSlice:
    """Shared logic to index or slice into a AsyncVRSReader or AsyncVRSReaderSlice. Returns either a
    AsyncVRSReaderSlice (if i is a slice or iterable) or a VRSRecord (if i is an integer).

    Args:
        path: a file path
        reader: a reader instance which is based on the class extended from AsyncVRSReader
        vrs_indices: a list of absolute indices for the VRS file that you are interested in.
                     If you apply filter, this list should be the one after applying the filter.
        indices: A set of indices or an index for vrs_indices that you want to read.

    Returns:
        AsyncVRSReaderSlice if the given indices is a slice, otherwise read a record and returns VRSRecord object.
    """
    if isinstance(indices, slice):
        return AsyncVRSReaderSlice(path, reader, vrs_indices[indices])
    else:
        record = await reader.async_read_record(vrs_indices[indices])
        return VRSRecord(record)
