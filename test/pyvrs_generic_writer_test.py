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

import os
import tempfile
import unittest
from pathlib import Path

import pkg_resources
import pyvrs
from pyvrs.writer import VRSWriter


def multi_stream_path() -> Path:
    return Path(pkg_resources.resource_filename(__name__, "test_data/multi_stream.vrs"))


class TestGenericWriterImports(unittest.TestCase):
    """Verify that GenericWriter symbols are importable."""

    def test_import_generic_writer(self):
        self.assertTrue(hasattr(pyvrs, "GenericWriter"))

    def test_vrs_writer_has_create_generic_stream(self):
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name
        writer = VRSWriter(filepath)
        self.assertTrue(hasattr(writer, "create_generic_stream"))
        writer.close()


class TestGenericStreamWriter(unittest.TestCase):
    """Test creating VRS files with generic streams."""

    def test_create_generic_stream(self):
        """Test creating a generic stream with an arbitrary type ID."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        # RecordableTypeId 100 is a sample type
        stream = writer.create_generic_stream(type_id=100)
        self.assertIsNotNone(stream)

        stream_id = stream.get_stream_id()
        self.assertIsInstance(stream_id, str)
        self.assertIn("100", stream_id)

        writer.close()
        if os.path.exists(filepath):
            os.unlink(filepath)

    def test_create_generic_stream_with_flavor(self):
        """Test creating a generic stream with a flavor string."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_generic_stream(type_id=100, flavor="test_flavor")
        self.assertIsNotNone(stream)

        stream_id = stream.get_stream_id()
        self.assertIsInstance(stream_id, str)

        writer.close()
        if os.path.exists(filepath):
            os.unlink(filepath)

    def test_write_raw_record(self):
        """Test writing raw bytes to a generic stream."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_generic_stream(type_id=100)

        # Write a configuration record with raw bytes
        config_data = b"\x01\x02\x03\x04"
        stream.create_raw_record(
            timestamp=0.0,
            record_type=pyvrs.RecordType.CONFIGURATION,
            format_version=0,
            raw_data=config_data,
        )
        writer.flush_records(0.0)

        # Write a data record with raw bytes
        data_bytes = b"\x10\x20\x30\x40\x50"
        stream.create_raw_record(
            timestamp=1.0,
            record_type=pyvrs.RecordType.DATA,
            format_version=0,
            raw_data=data_bytes,
        )
        writer.flush_records(1.0)

        writer.close()

        self.assertTrue(os.path.exists(filepath))
        self.assertGreater(os.path.getsize(filepath), 0)

        # Verify we can open the file and see the stream
        reader = pyvrs.SyncVRSReader(filepath)
        stream_ids = reader.stream_ids
        self.assertGreater(len(stream_ids), 0)

        os.unlink(filepath)

    def test_multiple_generic_streams(self):
        """Test creating multiple generic streams with different type IDs."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream1 = writer.create_generic_stream(type_id=100, flavor="stream-a")
        stream2 = writer.create_generic_stream(type_id=100, flavor="stream-b")
        stream3 = writer.create_generic_stream(type_id=200, flavor="stream-c")

        ids = {
            stream1.get_stream_id(),
            stream2.get_stream_id(),
            stream3.get_stream_id(),
        }
        self.assertEqual(len(ids), 3, "All streams should have unique IDs")

        # Write records to each stream
        for i, stream in enumerate([stream1, stream2, stream3]):
            stream.create_raw_record(
                timestamp=0.0,
                record_type=pyvrs.RecordType.CONFIGURATION,
                format_version=0,
                raw_data=bytes([i]),
            )

        writer.flush_records(0.0)
        writer.close()

        self.assertTrue(os.path.exists(filepath))

        # Verify all 3 streams are present
        reader = pyvrs.SyncVRSReader(filepath)
        self.assertEqual(len(reader.stream_ids), 3)

        os.unlink(filepath)

    def test_set_tag_on_generic_stream(self):
        """Test setting tags on a generic stream."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_generic_stream(type_id=100)

        stream.set_tag("test_key", "test_value")

        stream.create_raw_record(
            timestamp=0.0,
            record_type=pyvrs.RecordType.CONFIGURATION,
            format_version=0,
            raw_data=b"\x00",
        )
        writer.flush_records(0.0)
        writer.close()

        # Read back and verify tag
        reader = pyvrs.Reader(filepath)
        streams = reader.get_streams()
        self.assertEqual(len(streams), 1)
        stream_id = streams[0]
        tags = reader.get_tags(stream_id)
        self.assertEqual(tags["test_key"], "test_value")
        reader.close()

        os.unlink(filepath)


class TestReaderRawBytesMode(unittest.TestCase):
    """Test the raw bytes capture mode on the reader."""

    @classmethod
    def setUpClass(cls):
        cls.test_file = str(multi_stream_path())

    def test_multi_stream_file_exists(self):
        self.assertTrue(os.path.exists(self.test_file))

    def test_raw_bytes_mode_flag(self):
        """Test that raw bytes mode can be toggled."""
        reader = pyvrs.Reader(self.test_file)
        reader.set_raw_bytes_mode(True)
        self.assertTrue(reader.get_raw_bytes_mode())
        reader.set_raw_bytes_mode(False)
        self.assertFalse(reader.get_raw_bytes_mode())
        reader.close()

    def test_read_record_with_raw_bytes(self):
        """Test reading records in raw bytes mode returns raw_record_bytes."""
        reader = pyvrs.Reader(self.test_file)
        reader.enable_all_streams()
        reader.set_raw_bytes_mode(True)

        record = reader.read_record(0)
        self.assertTrue(record.has_raw_record_bytes)
        self.assertIsInstance(record.raw_record_bytes, bytes)
        self.assertGreater(len(record.raw_record_bytes), 0)

        reader.close()

    def test_raw_bytes_mode_captures_all_records(self):
        """Test that multiple records can be read in raw bytes mode."""
        reader = pyvrs.Reader(self.test_file)
        reader.enable_all_streams()
        reader.set_raw_bytes_mode(True)

        # Read first 10 records
        for i in range(10):
            record = reader.read_record(i)
            self.assertTrue(record.has_raw_record_bytes)
            self.assertGreater(len(record.raw_record_bytes), 0)

        reader.close()

    def test_normal_mode_no_raw_bytes(self):
        """Test that normal mode does not populate raw_record_bytes."""
        reader = pyvrs.Reader(self.test_file)
        reader.enable_all_streams()
        reader.set_raw_bytes_mode(False)

        record = reader.read_record(0)
        self.assertFalse(record.has_raw_record_bytes)

        reader.close()


class TestVerbatimCopy(unittest.TestCase):
    """Test the full verbatim copy workflow: read raw bytes → write raw bytes."""

    @classmethod
    def setUpClass(cls):
        cls.test_file = str(multi_stream_path())

    def test_verbatim_copy_preserves_stream_count(self):
        """Copy all records verbatim and verify stream count matches."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        # Read the source file and gather stream info
        reader = pyvrs.Reader(self.test_file)
        source_stream_ids = reader.get_streams()
        source_record_count = reader.get_available_records_size()
        reader.enable_all_streams()
        reader.set_raw_bytes_mode(True)

        # Create writer with generic streams
        writer = VRSWriter(output_path)
        writer.resetNewInstanceIds()

        # Create a generic stream for each source stream
        generic_streams = {}
        for sid_str in source_stream_ids:
            # Parse stream ID "TYPE-INSTANCE" into type_id
            parts = sid_str.split("-")
            type_id = int(parts[0])
            # Get flavor from stream info (stored in VRS system tags, not user tags)
            stream_info = reader.get_stream_info(sid_str)
            flavor = stream_info.get("flavor", "")
            # Recordable class IDs (200-999) require a non-empty flavor
            if not flavor and 200 <= type_id <= 999:
                flavor = "verbatim_copy"

            gw = writer.create_generic_stream(type_id=type_id, flavor=flavor)
            # Copy user tags
            tags = reader.get_tags(sid_str)
            for key, value in tags.items():
                gw.set_tag(key, value)
            generic_streams[sid_str] = gw

        # Read and copy all records in timestamp order
        last_timestamp = -float("inf")
        for i in range(source_record_count):
            record = reader.read_record(i)
            sid = record.stream_id
            if sid not in generic_streams:
                continue

            gw = generic_streams[sid]
            timestamp = max(record.timestamp, last_timestamp)

            # Determine record type enum
            rt_str = record.record_type
            if rt_str == "configuration":
                rt = pyvrs.RecordType.CONFIGURATION
            elif rt_str == "state":
                rt = pyvrs.RecordType.STATE
            else:
                rt = pyvrs.RecordType.DATA

            gw.create_raw_record(
                timestamp=timestamp,
                record_type=rt,
                format_version=record.format_version,
                raw_data=record.raw_record_bytes,
            )

            if timestamp > last_timestamp:
                writer.flush_records(timestamp)
                last_timestamp = timestamp

        writer.close()
        reader.close()

        # Verify output file
        self.assertTrue(os.path.exists(output_path))
        self.assertGreater(os.path.getsize(output_path), 0)

        # Verify stream count matches
        output_reader = pyvrs.SyncVRSReader(output_path)
        self.assertEqual(len(output_reader.stream_ids), len(source_stream_ids))

        os.unlink(output_path)


if __name__ == "__main__":
    unittest.main()
