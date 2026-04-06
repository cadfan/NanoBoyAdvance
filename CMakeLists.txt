cmake_minimum_required(VERSION 3.11...3.28)
project(NanoBoyAdvance LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLATFORM_QT "Build Qt frontend." ON)
option(PLATFORM_HEADLESS "Build headless capture binary." OFF)

add_subdirectory(src/nba)

if (PLATFORM_QT OR NOT PLATFORM_HEADLESS)
  add_subdirectory(src/platform/core)
endif()

if (PLATFORM_QT)
  add_subdirectory(src/platform/qt ${CMAKE_CURRENT_BINARY_DIR}/bin/qt/)
endif()

if (PLATFORM_HEADLESS)
  add_subdirectory(src/platform/headless ${CMAKE_CURRENT_BINARY_DIR}/bin/headless/)
endif()
