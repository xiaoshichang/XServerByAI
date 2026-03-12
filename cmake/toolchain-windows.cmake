# Placeholder Windows toolchain file.
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows.cmake

if(NOT WIN32)
  message(WARNING "Using Windows toolchain file on non-Windows host.")
endif()

set(XS_TOOLCHAIN "windows")
