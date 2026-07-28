cmake_minimum_required(VERSION 3.25)

project(
  windows-pointer-linux
  VERSION 1.0.0
  DESCRIPTION "The Windows 11 desktop pointer algorithm for Linux"
  LANGUAGES CXX
)

if(WIN32)
  set(WINDOWS_POINTER_PLUGIN_DEFAULT OFF)
else()
  set(WINDOWS_POINTER_PLUGIN_DEFAULT ON)
endif()

option(
  WINDOWS_POINTER_BUILD_PLUGIN
  "Build the Hyprland plugin"
  ${WINDOWS_POINTER_PLUGIN_DEFAULT}
)
option(WINDOWS_POINTER_BUILD_TOOLS "Build trace and audit tools" ON)
option(WINDOWS_POINTER_BUILD_BENCHMARKS "Build the pointer-path microbenchmark" OFF)
option(WINDOWS_POINTER_BUILD_FUZZER "Build the libFuzzer target" OFF)
option(WINDOWS_POINTER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(windows_pointer_target_defaults target)
  target_compile_features(${target} PRIVATE cxx_std_23)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(WINDOWS_POINTER_ENABLE_SANITIZERS)
    if(MSVC)
      message(FATAL_ERROR "The sanitizer preset currently requires GCC or Clang")
    endif()
    target_compile_options(
      ${target}
      PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(windows_pointer_engine STATIC src/engine.cpp)
add_library(windows_pointer::engine ALIAS windows_pointer_engine)

target_include_directories(
  windows_pointer_engine
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(windows_pointer_engine PUBLIC cxx_std_23)
windows_pointer_target_defaults(windows_pointer_engine)
set_target_properties(windows_pointer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WINDOWS_POINTER_BUILD_TOOLS)
  add_executable(windows-pointer-replay tools/replay.cpp)
  windows_pointer_target_defaults(windows-pointer-replay)
  target_link_libraries(windows-pointer-replay PRIVATE windows_pointer::engine)

  if(WIN32)
    add_executable(windows-pointer-windows-capture tools/windows-capture.cpp)
    windows_pointer_target_defaults(windows-pointer-windows-capture)
    target_link_libraries(windows-pointer-windows-capture PRIVATE user32 wtsapi32)
    if(MINGW)
      target_link_options(
        windows-pointer-windows-capture
        PRIVATE
          -static
          -static-libgcc
          -static-libstdc++
      )
    endif()
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(windows-pointer-uinput tools/uinput-mouse.cpp)
    windows_pointer_target_defaults(windows-pointer-uinput)
  endif()

  install(TARGETS windows-pointer-replay RUNTIME DESTINATION bin)
endif()

if(WINDOWS_POINTER_BUILD_BENCHMARKS)
  add_executable(windows_pointer_benchmark benchmarks/engine_benchmark.cpp)
  windows_pointer_target_defaults(windows_pointer_benchmark)
  target_link_libraries(windows_pointer_benchmark PRIVATE windows_pointer::engine)
endif()

if(WINDOWS_POINTER_BUILD_FUZZER)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WINDOWS_POINTER_BUILD_FUZZER requires Clang")
  endif()

  add_executable(
    windows_pointer_fuzzer
    src/engine.cpp
    tests/fuzz_engine.cpp
  )
  target_include_directories(windows_pointer_fuzzer PRIVATE include)
  windows_pointer_target_defaults(windows_pointer_fuzzer)
  target_compile_options(
    windows_pointer_fuzzer
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    windows_pointer_fuzzer
    PRIVATE
      -fsanitize=fuzzer,address,undefined
  )
endif()

if(WINDOWS_POINTER_BUILD_PLUGIN)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

  add_library(windows-pointer-linux MODULE src/plugin.cpp)
  windows_pointer_target_defaults(windows-pointer-linux)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(windows-pointer-linux PRIVATE -fno-gnu-unique)
  endif()
  target_compile_definitions(
    windows-pointer-linux
    PRIVATE WINDOWS_POINTER_VERSION="${PROJECT_VERSION}"
  )
  target_link_libraries(
    windows-pointer-linux
    PRIVATE
      windows_pointer::engine
      PkgConfig::HYPRLAND
  )
  set_target_properties(windows-pointer-linux PROPERTIES PREFIX "")
  install(
    TARGETS windows-pointer-linux
    LIBRARY DESTINATION lib/hyprland/plugins
  )
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

  add_executable(
    windows_pointer_engine_test
    tests/engine_test.cpp
    tests/model_test.cpp
    tests/scenario_test.cpp
  )
  windows_pointer_target_defaults(windows_pointer_engine_test)
  target_link_libraries(
    windows_pointer_engine_test
    PRIVATE
      windows_pointer::engine
      GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(windows_pointer_engine_test)
endif()
