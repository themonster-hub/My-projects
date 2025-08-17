add_executable(phish_perft perft/run_perft.cpp)

target_link_libraries(phish_perft PRIVATE phish_engine)

target_include_directories(phish_perft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
  set_property(TARGET phish_perft PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()