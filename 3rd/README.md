# Third-Party Dependencies

This directory vendors upstream source code directly into the repository. The
project builds these libraries from local source via `add_subdirectory(...)`
and does not download dependencies during CMake configure or build.

## Current Pins

| Directory | Upstream | Version | Notes |
| --- | --- | --- | --- |
| `spdlog/` | https://github.com/gabime/spdlog | `v1.17.0` | Logging library used by native runtime modules. |
| `zeromq/` | https://github.com/zeromq/libzmq | `v4.3.5` | ZeroMQ core library used for node-to-node TCP messaging. |

## Build Integration

- Root `CMakeLists.txt` adds [`3rd/CMakeLists.txt`](./CMakeLists.txt) before
  `src/native`.
- `3rd/CMakeLists.txt` disables upstream examples, tests, docs and packaging
  targets that are not needed by this repository.
- Internal aliases `xs::thirdparty::spdlog` and `xs::thirdparty::zeromq`
  shield project code from upstream target naming details.
