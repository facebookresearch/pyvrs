API Reference
=============

This section provides detailed documentation for all pyvrs2 modules and classes.

Core Classes
------------

The main classes you'll interact with when using pyvrs2:

.. toctree::
   :maxdepth: 2

   reader
   record
   writer

Reader Classes
~~~~~~~~~~~~~~

- **SyncVRSReader**: Synchronous VRS file reader with list-like behavior
- **AsyncVRSReader**: Asynchronous VRS file reader for concurrent processing
- **FilteredVRSReader**: Filtered view of VRS records based on criteria

Record and Data
~~~~~~~~~~~~~~~

- **VRSRecord**: Individual record containing data and metadata
- **VRSReaderSlice**: Slice of VRS records supporting batch operations

Writer Classes
~~~~~~~~~~~~~~

- **VRSWriter**: Main class for creating VRS files
- **VRSStream**: Stream within a VRS file for writing records
- **VRSDataLayout**: Data layout specification for records

Supporting Modules
------------------

.. toctree::
   :maxdepth: 2

   filter
   slice
   base
   utils

Filtering and Selection
~~~~~~~~~~~~~~~~~~~~~~~

- **RecordFilter**: Criteria for filtering VRS records
- **FilteredVRSReader**: Apply filters to limit record access

Slicing and Indexing
~~~~~~~~~~~~~~~~~~~~

- **VRSReaderSlice**: Handle sliced access to VRS records
- **AsyncVRSReaderSlice**: Asynchronous version of record slicing

Base Classes and Utilities
~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **BaseVRSReader**: Base functionality shared by all readers
- **Utility functions**: Helper functions for common operations

Enums and Constants
-------------------

Key enumerations and constants used throughout the API:

**ImageConversion**
    - ``OFF``: No image conversion
    - ``DECOMPRESS``: Decompress images to numpy arrays
    - ``NORMALIZE``: Normalize pixel values
    - ``RAW_BUFFER``: Access raw image buffer data

**RecordType**
    - ``DATA``: Data records containing sensor measurements
    - ``CONFIGURATION``: Configuration records defining data format
    - ``STATE``: State records containing device state information

**CompressionPreset**
    - ``Zfast``: Fast compression with moderate ratio
    - ``Zmedium``: Balanced compression speed and ratio
    - ``Zslow``: Slow compression with best ratio

Exception Classes
-----------------

**StreamNotFoundError**
    Raised when a requested stream is not found in the VRS file.

**TimestampNotFoundError**
    Raised when a record at the specified timestamp cannot be found.

**TimestampOrderWriteException**
    Raised when attempting to write records with non-monotonic timestamps.

Usage Examples
--------------

Basic Reading
~~~~~~~~~~~~~

.. code-block:: python

    from pyvrs2 import SyncVRSReader

    with SyncVRSReader("file.vrs") as reader:
        for record in reader:
            print(f"Record: {record.stream_id} at {record.timestamp}")

Filtered Reading
~~~~~~~~~~~~~~~~

.. code-block:: python

    filtered = reader.filtered_by_fields(
        stream_ids={'1013-1'},
        record_types={'data'},
        min_timestamp=5.0
    )

    for record in filtered:
        if record.image_blocks:
            image = record.image_blocks[0]

Writing Files
~~~~~~~~~~~~~

.. code-block:: python

    from pyvrs2 import VRSWriter

    with VRSWriter("output.vrs") as writer:
        stream = writer.create_stream("sensor")
        # Configure and write records...

Complete API Documentation
--------------------------

.. toctree::
   :maxdepth: 3

   reader
   record
   filter
   slice
   base
   utils
