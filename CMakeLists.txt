cmake_minimum_required(VERSION 3.25)

project(
  windows-pointer-linux
  VERSION 0.1.0
  DESCRIPTION "Windows 11 pointer motion for Hyprland"
  LANGUAGES CXX
)

option(WINDOWS_POINTER_BUILD_PLUGIN "Build the Hyprland plugin" ON)

add_library(windows_pointer_engine STATIC src/engine.cpp)
add_library(windows_pointer::engine ALIAS windows_pointer_engine)

target_include_directories(
  windows_pointer_engine
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(windows_pointer_engine PUBLIC cxx_std_23)
target_compile_options(
  windows_pointer_engine
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
set_target_properties(windows_pointer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WINDOWS_POINTER_BUILD_PLUGIN)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

  add_library(windows-pointer-linux MODULE src/plugin.cpp)
  target_compile_features(windows-pointer-linux PRIVATE cxx_std_23)
  target_compile_options(
    windows-pointer-linux
    PRIVATE
      -fno-gnu-unique
      -Wall
      -Wextra
      -Wpedantic
  )
  target_link_libraries(
    windows-pointer-linux
    PRIVATE
      windows_pointer::engine
      PkgConfig::HYPRLAND
  )
  set_target_properties(windows-pointer-linux PROPERTIES PREFIX "")
endif()

include(CTest)

if(BUILD_TESTING)
  find_package(GTest QUIET)

  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
      URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(windows_pointer_engine_test tests/engine_test.cpp)
  target_compile_features(windows_pointer_engine_test PRIVATE cxx_std_23)
  target_compile_options(
    windows_pointer_engine_test
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
  )
  target_link_libraries(
    windows_pointer_engine_test
    PRIVATE
      windows_pointer::engine
      GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(windows_pointer_engine_test)
endif()
