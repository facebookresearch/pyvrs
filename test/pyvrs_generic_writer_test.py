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
from vrsbindings import verbatim_copy


def multi_stream_path() -> Path:
    return Path(pkg_resources.resource_filename(__name__, "test_data/multi_stream.vrs"))


class TestVerbatimCopyImport(unittest.TestCase):
    """Verify that verbatim_copy is importable."""

    def test_import_verbatim_copy(self):
        self.assertTrue(callable(verbatim_copy))

    def test_import_from_pyvrs(self):
        self.assertTrue(hasattr(pyvrs, "verbatim_copy"))


class TestVerbatimCopyAllStreams(unittest.TestCase):
    """Test verbatim copy of all streams from a VRS file."""

    @classmethod
    def setUpClass(cls):
        cls.test_file = str(multi_stream_path())

    def test_multi_stream_file_exists(self):
        self.assertTrue(os.path.exists(self.test_file))

    def test_copy_all_streams(self):
        """Copy all streams and verify stream count and tags match."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path)

        self.assertTrue(os.path.exists(output_path))
        self.assertGreater(os.path.getsize(output_path), 0)

        # Verify stream count matches
        src_reader = pyvrs.Reader(self.test_file)
        dst_reader = pyvrs.Reader(output_path)

        src_streams = src_reader.get_streams()
        dst_streams = dst_reader.get_streams()
        self.assertEqual(len(dst_streams), len(src_streams))

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_preserves_record_count(self):
        """Verify record count is preserved in the copy."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path)

        src_reader = pyvrs.Reader(self.test_file)
        dst_reader = pyvrs.Reader(output_path)

        self.assertEqual(
            dst_reader.get_available_records_size(),
            src_reader.get_available_records_size(),
        )

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_preserves_file_tags(self):
        """Verify file-level tags are preserved in the copy."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path)

        src_reader = pyvrs.Reader(self.test_file)
        dst_reader = pyvrs.Reader(output_path)

        src_tags = src_reader.get_tags()
        dst_tags = dst_reader.get_tags()
        self.assertEqual(dst_tags, src_tags)

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_preserves_stream_tags(self):
        """Verify per-stream tags are preserved in the copy."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path)

        src_reader = pyvrs.Reader(self.test_file)
        dst_reader = pyvrs.Reader(output_path)

        for sid in src_reader.get_streams():
            src_tags = src_reader.get_tags(sid)
            dst_tags = dst_reader.get_tags(sid)
            self.assertEqual(dst_tags, src_tags, f"Tags mismatch for stream {sid}")

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)


class TestVerbatimCopyFilteredStreams(unittest.TestCase):
    """Test verbatim copy with stream ID filtering."""

    @classmethod
    def setUpClass(cls):
        cls.test_file = str(multi_stream_path())

    def test_copy_single_stream(self):
        """Copy only one stream and verify only that stream is in the output."""
        src_reader = pyvrs.Reader(self.test_file)
        src_streams = src_reader.get_streams()
        self.assertGreater(len(src_streams), 1)

        target_stream = src_streams[0]
        src_reader.close()

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path, [target_stream])

        dst_reader = pyvrs.Reader(output_path)
        dst_streams = dst_reader.get_streams()
        self.assertEqual(len(dst_streams), 1)
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_subset_of_streams(self):
        """Copy a subset of streams and verify the count."""
        src_reader = pyvrs.Reader(self.test_file)
        src_streams = src_reader.get_streams()
        self.assertGreater(len(src_streams), 3)
        src_reader.close()

        subset = src_streams[:3]

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path, subset)

        dst_reader = pyvrs.Reader(output_path)
        dst_streams = dst_reader.get_streams()
        self.assertEqual(len(dst_streams), len(subset))
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_empty_stream_list_copies_all(self):
        """Passing an empty stream list should copy all streams."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        verbatim_copy(self.test_file, output_path, [])

        src_reader = pyvrs.Reader(self.test_file)
        dst_reader = pyvrs.Reader(output_path)

        self.assertEqual(
            len(dst_reader.get_streams()),
            len(src_reader.get_streams()),
        )

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)


class TestVerbatimCopyErrorHandling(unittest.TestCase):
    """Test error handling in verbatim_copy."""

    def test_invalid_input_path(self):
        """Passing a nonexistent input file should raise an error."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        with self.assertRaises(Exception):
            verbatim_copy("/nonexistent/input.vrs", output_path)

    def test_invalid_stream_id(self):
        """Passing an invalid stream ID should raise an error."""
        test_file = str(multi_stream_path())
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        with self.assertRaises(Exception):
            verbatim_copy(test_file, output_path, ["invalid-stream-id"])


class TestWriterVerbatimCopyStreams(unittest.TestCase):
    """Test verbatim copy of streams into an existing VRSWriter."""

    @classmethod
    def setUpClass(cls):
        cls.test_file = str(multi_stream_path())

    def test_copy_all_streams_via_writer(self):
        """Copy all streams from a reader into a writer."""
        src_reader = pyvrs.Reader(self.test_file)
        all_streams = src_reader.get_streams()
        src_record_count = src_reader.get_available_records_size()

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        writer = pyvrs.Writer()
        writer.addVerbatimCopyStreams(src_reader, all_streams)
        writer.create(output_path)
        writer.copyVerbatimRecords()
        writer.close()
        src_reader.close()

        dst_reader = pyvrs.Reader(output_path)
        self.assertEqual(len(dst_reader.get_streams()), len(all_streams))
        self.assertEqual(dst_reader.get_available_records_size(), src_record_count)
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_subset_via_writer(self):
        """Copy a subset of streams via writer, leaving others out."""
        src_reader = pyvrs.Reader(self.test_file)
        all_streams = src_reader.get_streams()
        self.assertGreater(len(all_streams), 3)
        subset = all_streams[:3]

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        writer = pyvrs.Writer()
        writer.addVerbatimCopyStreams(src_reader, subset)
        writer.create(output_path)
        writer.copyVerbatimRecords()
        writer.close()
        src_reader.close()

        dst_reader = pyvrs.Reader(output_path)
        self.assertEqual(len(dst_reader.get_streams()), len(subset))
        dst_reader.close()
        os.unlink(output_path)

    def test_copy_preserves_stream_tags_via_writer(self):
        """Verify per-stream tags are preserved when copying via writer.

        Note: Copier creates new Writer recordables with fresh instance IDs,
        so stream IDs in the output may differ from the source. We compare
        the multiset of per-stream tag dicts instead of matching by stream ID.
        """
        src_reader = pyvrs.Reader(self.test_file)
        all_streams = src_reader.get_streams()

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        writer = pyvrs.Writer()
        writer.addVerbatimCopyStreams(src_reader, all_streams)
        writer.create(output_path)
        writer.copyVerbatimRecords()
        writer.close()

        dst_reader = pyvrs.Reader(output_path)
        dst_streams = dst_reader.get_streams()
        self.assertEqual(len(dst_streams), len(all_streams))

        # Stream IDs may differ (Copier assigns new instance IDs), so compare
        # the sorted list of per-stream tag dicts instead of matching by ID.
        src_tag_list = sorted((str(src_reader.get_tags(sid)) for sid in all_streams))
        dst_tag_list = sorted((str(dst_reader.get_tags(sid)) for sid in dst_streams))
        self.assertEqual(dst_tag_list, src_tag_list)

        src_reader.close()
        dst_reader.close()
        os.unlink(output_path)

    def test_mixed_processed_and_verbatim_streams(self):
        """Write processed streams alongside verbatim-copied streams."""
        src_reader = pyvrs.Reader(self.test_file)
        all_streams = src_reader.get_streams()
        self.assertGreater(len(all_streams), 1)

        # Use first stream as "processed", rest as passthrough
        passthrough_streams = all_streams[1:]

        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            output_path = f.name

        writer = pyvrs.Writer()
        # Create a processed stream
        processed_stream = writer.createStream("sample")
        processed_sid = processed_stream.getStreamID()
        # Register passthrough streams
        writer.addVerbatimCopyStreams(src_reader, passthrough_streams)
        writer.create(output_path)

        # Write a processed record
        rec_fmt = processed_stream.createRecordFormat(pyvrs.RecordType.CONFIGURATION)
        processed_stream.createRecord(0.0, rec_fmt)
        writer.writeRecords(0.0)

        rec_fmt_data = processed_stream.createRecordFormat(pyvrs.RecordType.DATA)
        processed_stream.createRecord(1.0, rec_fmt_data)
        writer.writeRecords(1.0)

        # Copy passthrough records
        writer.copyVerbatimRecords()
        writer.close()
        src_reader.close()

        # Verify output has both processed and passthrough streams
        dst_reader = pyvrs.Reader(output_path)
        dst_streams = dst_reader.get_streams()
        # Should have passthrough streams + 1 processed stream
        self.assertEqual(len(dst_streams), len(passthrough_streams) + 1)
        self.assertIn(processed_sid, dst_streams)
        dst_reader.close()
        os.unlink(output_path)

    def test_invalid_stream_id_raises(self):
        """Passing an invalid stream ID to addVerbatimCopyStreams raises."""
        src_reader = pyvrs.Reader(self.test_file)

        writer = pyvrs.Writer()
        with self.assertRaises(Exception):
            writer.addVerbatimCopyStreams(src_reader, ["invalid-stream-id"])
        src_reader.close()


if __name__ == "__main__":
    unittest.main()
