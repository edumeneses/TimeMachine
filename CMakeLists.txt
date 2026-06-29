cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(TimeMachine CXX)

# pffft is a static lib linked into the (shared) plugin module; on Linux that
# requires position-independent code or the link fails with "recompile with
# -fPIC".
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Max/MSP externals link the static CRT (/MT) on Windows, as the official
# max-sdk-base requires. Set it for the whole addon -- before Avendish and the
# object library are created -- so every target shares one runtime; mixing /MT
# and /MD trips MSVC with LNK2038. (CMP0091 is NEW via cmake_minimum_required.)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Use the Avendish the host already provides (e.g. ossia score); otherwise fetch it.
# The same CMakeLists builds this as an ossia/score add-on or as a standalone object.
find_package(Avendish QUIET)
if(NOT Avendish_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    Avendish
    GIT_REPOSITORY "https://github.com/celtera/avendish"
    GIT_TAG 6d08f1ef040da7cb62973859b62b14159c864ffe)
  FetchContent_Populate(Avendish)
  list(APPEND CMAKE_PREFIX_PATH "${avendish_SOURCE_DIR}")
  find_package(Avendish REQUIRED)
endif()

# PFFFT: the FFT behind the spectral-freeze engine. Single precision only;
# skip its tests/benchmarks/examples/install.
include(FetchContent)
cmake_policy(SET CMP0135 NEW)
set(PFFFT_USE_TYPE_FLOAT   ON  CACHE BOOL "" FORCE)
set(PFFFT_USE_TYPE_DOUBLE  OFF CACHE BOOL "" FORCE)
set(PFFFT_USE_FFTPACK      OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(INSTALL_PFFFT          OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  pffft
  GIT_REPOSITORY "https://github.com/marton78/pffft"
  GIT_TAG a4b03590cc2a4bea56f9721996e3057835799179)
FetchContent_MakeAvailable(pffft)

avnd_addon_init(NAME TimeMachineAddon)

# Plugin sources. On Linux the VST3 SDK's pluginterfaces lib doesn't carry
# funknown.cpp into the shared module, leaving FUID::generate()/FUID()/
# FUnknownPrivate::atomicAdd undefined so hosts silently fail to load it.
# Compile it in directly when we have the SDK (CI's VST3 build sets VST3_SDK_ROOT).
set(TIMEMACHINE_SOURCES
  TimeMachine/TimeMachine.cpp
  TimeMachine/TimeMachine.hpp)
if(UNIX AND NOT APPLE AND VST3_SDK_ROOT)
  list(APPEND TIMEMACHINE_SOURCES "${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp")
endif()

# CATEGORY audioplug: audio plug-in back-ends only (VST3 / CLAP / VST2). Keeps
# the godot/pd/max/python object back-ends -- and godot-cpp -- out of the build.
avnd_addon_object(
  BASE TimeMachineAddon
  C_NAME avnd_time_machine
  CLASS TimeMachine
  CATEGORY audioplug
  MAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/TimeMachine/TimeMachine.hpp"
  SOURCES ${TIMEMACHINE_SOURCES}
  DEPENDENCIES PFFFT::PFFFT)

avnd_addon_finalize(NAME TimeMachineAddon
  UUID 7f8a9b0c-1d2e-3f4a-5b6c-7d8e9f0a1b2c VERSION 1)

# Fail the Linux VST3 link on any unresolved symbol rather than shipping a
# module hosts can't dlopen.
if(UNIX AND NOT APPLE AND TARGET avnd_time_machine_vst3)
  target_link_options(avnd_time_machine_vst3 PRIVATE "-Wl,--no-undefined")
endif()
