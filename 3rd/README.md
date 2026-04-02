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
| `nlohmann_json/` | https://github.com/nlohmann/json | `v3.12.0` | Header-only JSON library used for native-side JSON serialization/deserialization and config file IO. |
| `asio-1.36.0/` | https://github.com/chriskohlhoff/asio | `1.36.0` | Standalone Asio headers used for native networking and event-loop utilities; no other Boost components are vendored alongside it. |
| `kcp/` | https://github.com/skywind3000/kcp | `master@f4f3a89cc632647dabdcb146932d2afd5591e62e` | Upstream `ikcp.c` / `ikcp.h` snapshot used for Gate-side client KCP sessions and future UDP listener integration. |
| `dotnet_host/` | https://www.nuget.org/packages/Microsoft.NETCore.App.Host.win-x64 and https://www.nuget.org/packages/Microsoft.NETCore.App.Host.linux-x64 | `10.0.0` | Official .NET native hosting headers plus the platform `nethost` link artifacts used by `M5-01` for CLR bootstrap on Windows/Linux (`nethost.lib` + `nethost.dll` on Windows, `libnethost.a` on Linux). |

## Build Integration

- Root `CMakeLists.txt` adds [`3rd/CMakeLists.txt`](./CMakeLists.txt) before
  `src/native`.
- `3rd/CMakeLists.txt` builds `spdlog` and `zeromq` from vendored source,
  integrates header-only `nlohmann/json` from vendored source, builds the
  vendored `kcp` snapshot from local source, and exposes standalone `asio`
  through project-side `INTERFACE` targets.
- `3rd/CMakeLists.txt` also exposes vendored .NET hosting headers and the
  platform `nethost` link artifact through a project-side `INTERFACE` target.
- Internal aliases `xs::thirdparty::spdlog`, `xs::thirdparty::zeromq`,
  `xs::thirdparty::asio`, `xs::thirdparty::nlohmann_json`,
  `xs::thirdparty::kcp`, and `xs::thirdparty::nethost` shield project code
  from upstream target naming or directory layout details.
- `xs::thirdparty::asio` sets `ASIO_STANDALONE=1` and links required threading
  and socket system libraries for supported platforms.
- `xs::thirdparty::nethost` links the vendored host artifact and adds the
  platform integration required by the official hosting API (`dl` on Linux and
  `nethost.dll` deployment on Windows).
