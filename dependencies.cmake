include(FetchContent)

# Mute deprecation warnings
cmake_policy(SET CMP0135 NEW)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

# 1. Download Boost Source
FetchContent_Declare(
  Boost
  URL https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz
)
FetchContent_Populate(Boost)

# 2. Tell CMake where Boost headers are
set(BOOST_ROOT "${boost_SOURCE_DIR}")
set(Boost_INCLUDE_DIR "${boost_SOURCE_DIR}" CACHE PATH "Force Boost Include" FORCE)
set(Boost_NO_SYSTEM_PATHS ON)

# 3. Create the Boost::boost target so Avendish can link to it
if(NOT TARGET Boost::boost)
  add_library(Boost::boost INTERFACE IMPORTED)
  set_target_properties(Boost::boost PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIR}"
  )
endif()

# 4. PFFFT (FFT used by the TimeMachine spectral engine)
#    Build only the single-precision static lib; skip tests/benchmarks/examples/install.
set(PFFFT_USE_TYPE_FLOAT  ON  CACHE BOOL "" FORCE)
set(PFFFT_USE_TYPE_DOUBLE OFF CACHE BOOL "" FORCE)
set(PFFFT_USE_FFTPACK     OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(INSTALL_PFFFT          OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  pffft
  GIT_REPOSITORY "https://github.com/marton78/pffft"
  GIT_TAG  a4b03590cc2a4bea56f9721996e3057835799179
  GIT_PROGRESS true
)
FetchContent_MakeAvailable(pffft)

# 5. Handle Avendish (Using MakeAvailable to ensure paths are correct)
FetchContent_Declare(
  avendish
  GIT_REPOSITORY "https://github.com/celtera/avendish"
  GIT_TAG  6c3f33df5dd95c9f9ec86c9b0b6f2b035dd93cb3
  GIT_PROGRESS true
)

FetchContent_MakeAvailable(avendish)
