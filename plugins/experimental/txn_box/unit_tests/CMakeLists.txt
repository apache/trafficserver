cmake_minimum_required(VERSION 3.12)
project(test_txn_box CXX)
set(CMAKE_CXX_STANDARD 17)

pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

add_executable(test_txn_box unit_test_main.cc test_txn_box.cc test_accl_utils.cc)

set_target_properties(test_txn_box PROPERTIES CLANG_FORMAT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

#target_link_libraries(test_txn_box PUBLIC PkgConfig::libswoc++ PkgConfig::yaml-cpp pcre2-8)
target_link_libraries(test_txn_box PUBLIC libswoc::libswoc PkgConfig::PCRE2 OpenSSL::SSL catch2::catch2)
# After fighting with CMake over the include paths, it's just not worth it to be correct.
# target_link_libraries should make this work but it doesn't. I can't figure out why.
# target_include_directories(test_txn_box PRIVATE ../../plugin/include)
target_include_directories(test_txn_box PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include)
# target_include_directories(test_txn_box PRIVATE ../../plugin/include ${trafficserver_INCLUDE_DIRS})
add_test(NAME test_txn_box COMMAND test_txn_box)
