cmake_minimum_required(VERSION 3.16)

project(Phish VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PHISH_ENABLE_LTO "Enable link-time optimization" ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

add_subdirectory(engine)
add_subdirectory(tests)

add_executable(phish main.cpp)

target_link_libraries(phish PRIVATE phish_engine)

target_include_directories(phish PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(PHISH_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set_property(TARGET phish PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET phish_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

install(TARGETS phish RUNTIME DESTINATION bin)