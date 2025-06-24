.. pyvrs2 documentation master file

Welcome to pyvrs2's documentation!
==================================

pyvrs2 is a comprehensive Python library that provides both read and write capabilities for VRS files with a Pythonic API. It uses pybind11 to interface with the official VRS C++ API, offering high performance and full feature compatibility.

What is VRS?
------------

VRS is a file format designed to record, store and playback streams of sensor and other data, such as images, audio samples, and any other discrete sensors (IMU, temperature, etc.), stored in per-device streams of time-stamped records.

Key Features
------------

- **Pythonic API**: List-like behavior with indexing, slicing, and iteration
- **High Performance**: Built on top of the official VRS C++ library
- **Flexible Reading**: Support for filtering by stream, record type, and timestamp
- **Multiple Data Types**: Images, audio, IMU data, and custom data layouts
- **Streaming Support**: Read from local files, Gaia, Manifold, and other sources
- **Async Support**: Both synchronous and asynchronous reading APIs
- **Writing Capabilities**: Create VRS files with multiple streams and data types

Quick Start
-----------

Reading VRS Files
~~~~~~~~~~~~~~~~~

.. code-block:: python

    from pyvrs2 import SyncVRSReader

    # Open a VRS file
    with SyncVRSReader("path/to/file.vrs") as reader:
        print(f"File contains {len(reader)} records")
        print(f"Available streams: {reader.stream_ids}")

        # Iterate through all records
        for record in reader:
            print(f"Record {record.record_index}: {record.stream_id} at {record.timestamp}s")

Filtering Records
~~~~~~~~~~~~~~~~~

.. code-block:: python

    # Filter by stream ID and record type
    filtered_reader = reader.filtered_by_fields(
        stream_ids={'1013-1', '1013-2'},  # Specific streams
        record_types={'data'},            # Only data records
        min_timestamp=5.0,                # From 5 seconds
        max_timestamp=10.0                # To 10 seconds
    )

    for record in filtered_reader:
        # Process only filtered records
        pass

Working with Images
~~~~~~~~~~~~~~~~~~~

.. code-block:: python

    from pyvrs2 import ImageConversion

    # Set image conversion policy
    reader.set_image_conversion(ImageConversion.DECOMPRESS)

    # Filter to image streams
    image_reader = reader.filtered_by_fields(stream_ids='1013*')

    for record in image_reader:
        if record.image_blocks:
            image = record.image_blocks[0]  # numpy array
            print(f"Image: {image.shape}, dtype: {image.dtype}")

Writing VRS Files
~~~~~~~~~~~~~~~~~

.. code-block:: python

    from pyvrs2 import VRSWriter
    import numpy as np

    with VRSWriter("output.vrs") as writer:
        # Create a stream
        stream = writer.create_stream("camera_stream")

        # Set up data layout
        config_layout = stream.get_config_record_metadata()
        config_layout.add_image_spec("image", 640, 480, "rgb8")

        data_layout = stream.get_data_record_metadata()
        data_layout.add_image_spec("image", 640, 480, "rgb8")

        # Write configuration record
        stream.create_config_record(0.0, config_layout)

        # Write data records
        for i in range(100):
            timestamp = i * 0.033  # 30 FPS
            image_data = np.random.randint(0, 255, (480, 640, 3), dtype=np.uint8)
            stream.create_data_record(timestamp, data_layout, image_data)

            if i % 10 == 0:
                writer.flush_records(timestamp)

Build from source
~~~~~~~~~~~~~~~~~

See `Build from source <https://github.com/facebookresearch/vrs#getting-started>`_ for detailed instructions.

Contents
--------

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   modules/index
   :caption: Additional Information
