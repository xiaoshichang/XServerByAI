# Third-Party Dependencies

This directory vendors upstream source code directly into the repository. The
project integrates these libraries from local source via `add_subdirectory(...)`
or project-side `INTERFACE` targets and does not download dependencies during
CMake configure or build.

## Current Pins

| Directory | Upstream | Version | Notes |
| --- | --- | --- | --- |
| `spdlog/` | https://github.com/gabime/spdlog | `v1.17.0` | Logging library used by native runtime modules. |
| `zeromq/` | https://github.com/zeromq/libzmq | `v4.3.5` | ZeroMQ core library used for node-to-node TCP messaging. |
| `asio-1.36.0/` | https://github.com/chriskohlhoff/asio | `1.36.0` | Standalone Asio headers used for native networking and event-loop utilities; no other Boost components are vendored alongside it. |

## Build Integration

- Root `CMakeLists.txt` adds [`3rd/CMakeLists.txt`](./CMakeLists.txt) before
  `src/native`.
- `3rd/CMakeLists.txt` builds `spdlog` and `zeromq` from vendored source, and
  exposes standalone `asio` through a project-side `INTERFACE` target.
- Internal aliases `xs::thirdparty::spdlog`, `xs::thirdparty::zeromq` and
  `xs::thirdparty::asio` shield project code from upstream target naming or
  directory layout details.
- `xs::thirdparty::asio` sets `ASIO_STANDALONE=1` and links required threading
  and socket system libraries for supported platforms.