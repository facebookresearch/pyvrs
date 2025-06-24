Writer Module
=============

The writer module provides classes for creating and writing VRS files.

VRSWriter
---------

.. autoclass:: pyvrs2.VRSWriter
   :members:
   :undoc-members:
   :show-inheritance:

   The main class for creating VRS files. Provides methods to create streams, set file-level tags, and manage the writing process.

   **Key Features:**

   - Create multiple streams within a single VRS file
   - Set file-level metadata tags
   - Automatic timestamp validation
   - Background writing with queue management
   - Context manager support for automatic cleanup

   **Usage Example:**

   .. code-block:: python

       from pyvrs2 import VRSWriter
       import numpy as np

       with VRSWriter("output.vrs") as writer:
           # Set file-level tags
           writer.set_tag("description", "My VRS recording")
           writer.set_tag("device", "camera_system")

           # Create a stream
           stream = writer.create_stream("camera_stream")

           # Configure data layouts
           config_layout = stream.get_config_record_metadata()
           config_layout.add_image_spec("image", 640, 480, "rgb8")

           data_layout = stream.get_data_record_metadata()
           data_layout.add_image_spec("image", 640, 480, "rgb8")

           # Write configuration
           stream.create_config_record(0.0, config_layout)

           # Write data records
           for i in range(100):
               timestamp = i * 0.033  # 30 FPS
               image_data = np.random.randint(0, 255, (480, 640, 3), dtype=np.uint8)
               stream.create_data_record(timestamp, data_layout, image_data)

               # Flush periodically
               if i % 10 == 0:
                   writer.flush_records(timestamp)

   **Methods:**

   .. method:: __init__(filepath: str)

      Initialize a new VRS writer.

      :param filepath: Path where the VRS file will be created
      :type filepath: str

   .. method:: create_stream(name: str, flavor: str = "", compression: CompressionPreset = CompressionPreset.Zmedium) -> VRSStream

      Create a new stream in the VRS file.

      :param name: Name of the stream
      :type name: str
      :param flavor: Optional flavor identifier for the stream
      :type flavor: str
      :param compression: Compression preset to use
      :type compression: CompressionPreset
      :return: The created stream object
      :rtype: VRSStream

   .. method:: set_tag(tag_name: str, tag_value: str) -> None

      Set a file-level tag.

      :param tag_name: Name of the tag
      :type tag_name: str
      :param tag_value: Value of the tag
      :type tag_value: str
      :raises Exception: If called after file creation has started

   .. method:: flush_records(timestamp: float) -> int

      Flush pending records to disk.

      :param timestamp: Timestamp for the flush operation
      :type timestamp: float
      :return: Number of records flushed
      :rtype: int
      :raises TimestampOrderWriteException: If timestamp is not monotonic

   .. method:: close() -> None

      Close the writer and finalize the file.

   .. method:: resetNewInstanceIds() -> None

      Reset instance ID counters for device types.

      .. warning::
         Use with caution. This can lead to duplicate instance IDs if used incorrectly.

VRSStream
---------

.. autoclass:: pyvrs2.VRSStream
   :members:
   :undoc-members:
   :show-inheritance:

   Represents a stream within a VRS file. Each stream can contain multiple types of records (configuration, data, state) and has its own metadata.

   **Key Features:**

   - Support for configuration, data, and state records
   - Stream-level metadata tags
   - Flexible data layouts for different record types
   - Compression settings per stream

   **Usage Example:**

   .. code-block:: python

       # Create stream with specific compression
       stream = writer.create_stream("imu_data", compression=CompressionPreset.Zfast)

       # Set stream tags
       stream.set_tag("sensor_type", "IMU")
       stream.set_tag("location", "head")

       # Configure data layout
       data_layout = stream.get_data_record_metadata()
       data_layout.add_vector3d("acceleration")
       data_layout.add_vector3d("gyroscope")

       # Write records
       timestamp = 0.0
       stream.create_config_record(timestamp, config_layout)

       for accel, gyro in sensor_data:
           stream.create_data_record(timestamp, data_layout, accel, gyro)
           timestamp += 0.01  # 100 Hz

   **Methods:**

   .. method:: get_data_record_metadata(index: int = 0) -> VRSDataLayout

      Get the data layout for data records.

      :param index: Index of the data layout (for multiple layouts)
      :type index: int
      :return: Data layout object
      :rtype: VRSDataLayout

   .. method:: get_config_record_metadata(index: int = 0) -> VRSDataLayout

      Get the data layout for configuration records.

      :param index: Index of the data layout
      :type index: int
      :return: Data layout object
      :rtype: VRSDataLayout

   .. method:: get_state_record_metadata(index: int = 0) -> VRSDataLayout

      Get the data layout for state records.

      :param index: Index of the data layout
      :type index: int
      :return: Data layout object
      :rtype: VRSDataLayout

   .. method:: create_config_record(timestamp: float, *srcs) -> None

      Create a configuration record.

      :param timestamp: Timestamp for the record
      :type timestamp: float
      :param srcs: Data sources (VRSDataLayout and/or numpy arrays)
      :raises Exception: If configuration record format is invalid

   .. method:: create_data_record(timestamp: float, *srcs) -> None

      Create a data record.

      :param timestamp: Timestamp for the record
      :type timestamp: float
      :param srcs: Data sources (VRSDataLayout and/or numpy arrays)
      :raises Exception: If no configuration record exists or format is invalid

   .. method:: create_state_record(timestamp: float, *srcs) -> None

      Create a state record.

      :param timestamp: Timestamp for the record
      :type timestamp: float
      :param srcs: Data sources (VRSDataLayout and/or numpy arrays)

   .. method:: set_compression(preset: CompressionPreset) -> None

      Set compression preset for the stream.

      :param preset: Compression preset to use
      :type preset: CompressionPreset

   .. method:: set_tag(tag_name: str, tag_value: str) -> None

      Set a stream-level tag.

      :param tag_name: Name of the tag
      :type tag_name: str
      :param tag_value: Value of the tag
      :type tag_value: str
      :raises Exception: If called after file creation has started

   .. method:: get_stream_id() -> str

      Get the unique stream ID.

      :return: Stream ID in format "recordable_type_id-instance_id"
      :rtype: str

VRSDataLayout
-------------

.. autoclass:: pyvrs2.VRSDataLayout
   :members:
   :undoc-members:
   :show-inheritance:

   Defines the structure and format of data within VRS records. Used to specify what types of data will be stored and how they should be interpreted.

   **Key Features:**

   - Support for various data types (images, audio, vectors, scalars)
   - Flexible metadata specification
   - Type-safe data layout definition

   **Usage Example:**

   .. code-block:: python

       # Image data layout
       layout = stream.get_data_record_metadata()
       layout.add_image_spec("camera", 1920, 1080, "rgb8")
       layout.add_scalar("exposure_time", "float32")
       layout.add_vector3d("camera_position")

       # Audio data layout
       audio_layout = stream.get_data_record_metadata()
       audio_layout.add_audio_spec("microphone", 48000, 2, "float32")

       # Custom data layout
       sensor_layout = stream.get_data_record_metadata()
       sensor_layout.add_vector3d("acceleration")
       sensor_layout.add_vector3d("angular_velocity")
       sensor_layout.add_scalar("temperature", "float32")

   **Common Data Types:**

   - **Images**: RGB, grayscale, depth, compressed formats
   - **Audio**: Multi-channel audio with various sample rates
   - **Vectors**: 2D, 3D, and N-dimensional vectors
   - **Scalars**: Integer and floating-point values
   - **Binary**: Raw binary data blocks

Exceptions
----------

TimestampOrderWriteException
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. autoexception:: pyvrs2.TimestampOrderWriteException

   Raised when attempting to write records with non-monotonic timestamps.

   VRS requires that records be written in timestamp order. This exception is raised when:

   - A record timestamp is less than the last flushed timestamp
   - Records within a stream are not in chronological order

   **Example:**

   .. code-block:: python

       try:
           writer.flush_records(10.0)  # Last flush at 10.0 seconds
           stream.create_data_record(5.0, data_layout, data)  # Error: 5.0 < 10.0
       except TimestampOrderWriteException as e:
           print(f"Timestamp order error: {e}")

Best Practices
--------------

Timestamp Management
~~~~~~~~~~~~~~~~~~~~

- Always use monotonically increasing timestamps
- Flush records periodically to avoid memory buildup
- Use consistent time units (typically seconds)

.. code-block:: python

    # Good: Monotonic timestamps
    timestamps = [0.0, 0.033, 0.066, 0.1]

    # Bad: Non-monotonic timestamps
    timestamps = [0.0, 0.1, 0.033]  # Will raise exception

Stream Organization
~~~~~~~~~~~~~~~~~~~

- Use descriptive stream names and flavors
- Set meaningful tags for stream identification
- Group related data in the same stream

.. code-block:: python

    # Good: Descriptive naming
    left_camera = writer.create_stream("camera", "left_eye")
    right_camera = writer.create_stream("camera", "right_eye")

    # Set identifying tags
    left_camera.set_tag("camera_id", "left")
    left_camera.set_tag("resolution", "1920x1080")

Memory Management
~~~~~~~~~~~~~~~~~

- Flush records regularly to prevent memory buildup
- Use appropriate compression settings
- Close writers properly using context managers

.. code-block:: python

    # Flush every 100 records
    for i, data in enumerate(data_stream):
        stream.create_data_record(timestamp, layout, data)
        if i % 100 == 0:
            writer.flush_records(timestamp)
