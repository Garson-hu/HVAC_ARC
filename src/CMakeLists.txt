#This is primarily a cpp code but will use log4c to log both the C and C++ aspects
include(FindPkgConfig)
pkg_check_modules(MERCURY REQUIRED IMPORTED_TARGET mercury)
pkg_check_modules(LOG4C REQUIRED IMPORTED_TARGET log4c)

#Dynamic Target
add_library(hvac_client SHARED hvac_client.cpp hvac_data_mover.cpp hvac_comm.cpp hvac_comm_client.cpp wrappers.c hvac_logging.c) # hvac_multi_source_read.cpp
target_compile_definitions(hvac_client PUBLIC HVAC_CLIENT)
target_compile_definitions(hvac_client PUBLIC HVAC_PRELOAD)
target_include_directories(hvac_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hvac_client PRIVATE pthread dl PkgConfig::LOG4C PkgConfig::MERCURY)

#Server Daemon
add_executable(hvac_server hvac_server.cpp hvac_data_mover.cpp hvac_comm.cpp hvac_logging.c) # hvac_cache_policy.cpp
target_compile_definitions(hvac_server PUBLIC HVAC_SERVER)
target_include_directories(hvac_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hvac_server PRIVATE pthread PkgConfig::LOG4C rt PkgConfig::MERCURY)

install(TARGETS hvac_client DESTINATION lib)
install(TARGETS hvac_server DESTINATION bin)
