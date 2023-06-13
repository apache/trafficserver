add_library(esi-common DocNode.cc gzip.cc Utils.cc)
target_link_libraries(esi-common PRIVATE ZLIB::ZLIB)
target_include_directories(esi-common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(esi-common PROPERTIES POSITION_INDEPENDENT_CODE ON)
