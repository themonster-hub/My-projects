add_library(phish_engine)

# Engine sources (grow over time)
target_sources(phish_engine
  PRIVATE
    uci/uci.cpp
    util/config.cpp
)

# Public include so consumers can include with "engine/..."
target_include_directories(phish_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(NOT MSVC)
  target_compile_options(phish_engine PRIVATE -O3)
endif()