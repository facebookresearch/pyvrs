# pyvrs Open Source Project Change Log

This file captures the main changes made to the pyvrs open source project since
its creation. None of the changes made before pyvrs was open sourced are meant
to be covered.

# Version 1.3.0 (October 2026)

- Wheels for Python 3.13 and 3.14, on macOS (Apple Silicon) and Linux (x64). The Linux wheels for
  3.13 and 3.14 need glibc 2.28 or newer; 3.9 to 3.12 keep the glibc 2.17 floor.
- `import pyvrs` works again on Python 3.9 to 3.11.
- Compressed audio, such as Opus, is returned as its raw bytes. The buffer used to be shaped from
  the declared audio format and read past the end of the packet.
- Ground-truth depth streams are normalized as depth rather than as segmentation.
- The source distribution includes the C++ sources, so it can be built.
- Updated to VRS `dc47db2`.

# Version 1.2.2 (Feb 26, 2025)

- Async handler fixes.
- Misc changes.

# Version 1.2.1 (May 15, 2024)

- Misc changes.

# Version 1.2.0 (February 6, 2024)

- Misc changes.

# Version 1.1.0 (August 4, 2023)

- Updated to match the last VRS version bump.
- VRS moved from using Cereal's internal copy of rapidjson, to using rapidjson
  directly from GitHub.
- Updated build scripts accordingly.
- Support for Ubuntu-Latest

# Version 1.0.4 (Mar 7, 2023)

- Initial private release.
