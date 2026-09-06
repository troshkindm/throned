# Vendored dependencies

These sources are compiled into the desktop application. They are kept in the
repository to pin the exact code used by Windows and Linux builds.
Their build targets and include paths live in `cmake/ThronedDependencies.cmake`.

| Path | Used for |
| --- | --- |
| `fkYAML/` | Parsing Clash-compatible YAML subscriptions. |
| `QHotkey/` | Registering system-wide keyboard shortcuts. |
| `qrcodegen/` | Generating QR codes for profiles and OTP entries. |
| `quirc/` | Decoding QR codes from images and screenshots. |
| `qv2ray/` | System proxy integration, log highlighting, and route editing completion. |
| `simple-protobuf/` | Generating and serializing the C++ side of the Go core protocol. |
| `SQLiteCpp/` | SQLite C++ wrapper and the pinned SQLite amalgamation. |
| `WinCommander/` | Windows UAC process launcher. |

Application-owned adapters and widgets belong under `src/` and `include/`.
Do not add build outputs or downloaded application binaries here.
