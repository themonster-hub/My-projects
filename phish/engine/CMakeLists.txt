add_library(phish_engine)

# Engine sources (grow over time)
target_sources(phish_engine
  PRIVATE
    uci/uci.cpp
    util/config.cpp
    util/types.h
    util/zobrist.cpp
    bitboard/bitboard.cpp
    board/position.cpp
    search/search.cpp
)

# Public include so consumers can include with "engine/..."
target_include_directories(phish_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(NOT MSVC)
  include(CheckCXXCompilerFlag)
  # Base opts
  target_compile_options(phish_engine PRIVATE -O3 -DNDEBUG)
  # Native tuning if supported
  check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
  if(HAS_MARCH_NATIVE)
    target_compile_options(phish_engine PRIVATE -march=native -mtune=native)
  endif()
  # Fast-math optional
  option(PHISH_ENABLE_OFAST "Enable -Ofast (may break strict IEEE semantics)" OFF)
  if(PHISH_ENABLE_OFAST)
    target_compile_options(phish_engine PRIVATE -Ofast)
  endif()
  # Lean binary options
  check_cxx_compiler_flag("-fno-exceptions" HAS_NO_EXC)
  if(HAS_NO_EXC)
    target_compile_options(phish_engine PRIVATE -fno-exceptions)
  endif()
  check_cxx_compiler_flag("-fno-rtti" HAS_NO_RTTI)
  if(HAS_NO_RTTI)
    target_compile_options(phish_engine PRIVATE -fno-rtti)
  endif()
  # Linker hints
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_link_options(phish_engine PRIVATE -Wl,-O1 -Wl,--as-needed)
  endif()
endif()