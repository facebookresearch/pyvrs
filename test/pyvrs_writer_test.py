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

import numpy as np
import pyvrs
from pyvrs.writer import VRSWriter


class TestAriaGen2StreamWriter(unittest.TestCase):
    """Test creating VRS files with Aria Gen2 camera streams."""

    def test_create_aria_gen2_rgb_camera_stream(self):
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_stream("aria_gen2_rgb_camera", flavor="camera-rgb")
        self.assertIsNotNone(stream)

        stream_id = stream.get_stream_id()
        self.assertIsInstance(stream_id, str)
        # RGB camera should have RecordableTypeId 214
        self.assertIn("214", stream_id)

        writer.close()
        if os.path.exists(filepath):
            os.unlink(filepath)

    def test_create_aria_gen2_slam_camera_stream(self):
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_stream(
            "aria_gen2_slam_camera", flavor="camera-slam-left"
        )
        self.assertIsNotNone(stream)

        stream_id = stream.get_stream_id()
        self.assertIsInstance(stream_id, str)
        # SLAM camera should have RecordableTypeId 1201
        self.assertIn("1201", stream_id)

        writer.close()
        if os.path.exists(filepath):
            os.unlink(filepath)

    def test_aria_gen2_rgb_camera_write_with_image(self):
        """Test writing config + data records with H.265 content block."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)
        stream = writer.create_stream("aria_gen2_rgb_camera", flavor="camera-rgb")

        # Write configuration record
        config_meta = stream.get_config_record_metadata()
        config_meta.image_width = 1408
        config_meta.image_height = 1408
        config_meta.image_pixel_format = 200  # YUV_420_NV21
        config_meta.image_codec_name = "H.265"
        stream.create_config_record(0.0, config_meta)
        writer.flush_records(0.0)

        # Write data record with fake encoded bytes
        data_meta = stream.get_data_record_metadata()
        data_meta.capture_timestamp_ns = 1000000000
        fake_h265_bytes = np.zeros(1024, dtype=np.uint8)
        stream.create_data_record(1.0, data_meta, fake_h265_bytes)
        writer.flush_records(1.0)

        writer.close()

        self.assertTrue(os.path.exists(filepath))
        self.assertGreater(os.path.getsize(filepath), 0)

        # Verify we can open it and see the stream
        reader = pyvrs.SyncVRSReader(filepath)
        stream_ids = reader.stream_ids
        self.assertGreater(len(stream_ids), 0)

        os.unlink(filepath)

    def test_multiple_aria_gen2_streams(self):
        """Test creating a VRS file with multiple Aria Gen2 camera streams."""
        with tempfile.NamedTemporaryFile(suffix=".vrs", delete=True) as f:
            filepath = f.name

        writer = VRSWriter(filepath)

        rgb_stream = writer.create_stream("aria_gen2_rgb_camera", flavor="camera-rgb")
        slam_left = writer.create_stream(
            "aria_gen2_slam_camera", flavor="camera-slam-left"
        )
        slam_right = writer.create_stream(
            "aria_gen2_slam_camera", flavor="camera-slam-right"
        )

        # All should have unique stream IDs
        ids = {
            rgb_stream.get_stream_id(),
            slam_left.get_stream_id(),
            slam_right.get_stream_id(),
        }
        self.assertEqual(len(ids), 3, "All streams should have unique IDs")

        # Write config records for each
        for stream in [rgb_stream, slam_left, slam_right]:
            config_meta = stream.get_config_record_metadata()
            config_meta.image_width = 640
            config_meta.image_height = 480
            stream.create_config_record(0.0, config_meta)

        writer.flush_records(0.0)
        writer.close()

        self.assertTrue(os.path.exists(filepath))
        os.unlink(filepath)


if __name__ == "__main__":
    unittest.main()
