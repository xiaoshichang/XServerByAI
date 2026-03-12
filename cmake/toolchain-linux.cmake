# Placeholder Linux toolchain file.
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux.cmake

if(WIN32)
  message(WARNING "Using Linux toolchain file on Windows host.")
endif()

set(XS_TOOLCHAIN "linux")
