#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

cmake_minimum_required(VERSION 3.20)
project(ats)

enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

# ATS uses "unix" for variable names.  Make sure its not defined
remove_definitions(-Dunix)

# Gather some environment info
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_OS)
set(BUILD_NUMBER "0" CACHE STRING "The build number")
execute_process(COMMAND id -nu OUTPUT_VARIABLE BUILD_PERSON OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND id -ng OUTPUT_VARIABLE BUILD_GROUP OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -n OUTPUT_VARIABLE BUILD_MACHINE OUTPUT_STRIP_TRAILING_WHITESPACE)

# Options
option(BUILD_REGRESSION_TESTING "Build regression tests (default ON)" ON)
set(DEFAULT_STACK_SIZE 1048576 CACHE STRING "Default stack size (default 1048576)")
option(ENABLE_FAST_SDK "Use fast SDK APIs (default OFF)")
option(ENABLE_JEMALLOC "Use jemalloc (default OFF)")
option(ENABLE_MIMALLOC "Use mimalloc (default OFF)")
option(ENABLE_POSIX_CAP
    "Use POSIX capabilities, turn OFF to use id switching. (default ON)"
    ON
)
option(ENABLE_PROFILER "Use gperftools profiler (default OFF)")
option(ENABLE_TCMALLOC "Use TCMalloc (default OFF)")
option(ENABLE_UNWIND   "Use libunwind if found on system (default ON)" ON)
option(ENABLE_WCCP     "Use WCCP v2 (default OFF)")
set(TS_MAX_HOST_NAME_LEN 256 CACHE STRING "Max host name length (default 256)")
set(TS_USE_SET_RBIO 1 CACHE STRING "Use openssl set_rbio (default 1)")
set(TS_USE_DIAGS 1 CACHE STRING "Use diags (default 1)")
option(TS_USE_HWLOC "Use hwloc (default OFF)")

set(TS_USE_FAST_SDK ${ENABLE_FAST_SDK})

set(TS_VERSION_MAJOR 10)
set(TS_VERSION_MINOR 0)
set(TS_VERSION_MICRO 0)
set(TS_LIBTOOL_MAJOR ${TS_VERSION_MAJOR}${TS_VERSION_MINOR})
set(TS_LIBTOOL_VERSION ${TS_LIBTOOL_MAJOR}:${TS_VERSION_MICRO}:${TS_VERSION_MINOR})
set(TS_VERSION_STRING TS_VERSION_S)
set(TS_VERSION_NUMBER TS_VERSION_N)

set(TS_HAS_WCCP ${ENABLE_WCCP})


# Check include files
include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckIncludeFileCXX)
include(CheckSymbolExists)

CHECK_INCLUDE_FILE(dlfcn.h HAVE_DLFCN_H)
CHECK_INCLUDE_FILE(float.h HAVE_FLOAT_H)
CHECK_INCLUDE_FILE(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILE(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILE(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILE(string.h HAVE_STRING_H)
CHECK_INCLUDE_FILE(linux/hdreg.h HAVE_LINUX_HDREG_H)
CHECK_INCLUDE_FILE(linux/major.h HAVE_LINUX_MAJOR_H)
CHECK_INCLUDE_FILE(sys/disk.h HAVE_SYS_DISK_H)
CHECK_INCLUDE_FILE(sys/disklabel.h HAVE_SYS_DISKLABEL_H)
CHECK_INCLUDE_FILE(sys/ioctl.h HAVE_SYS_IOCTL_H)
CHECK_INCLUDE_FILE(sys/types.h HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILE(sys/stat.h HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILE(sys/statvfs.h HAVE_SYS_STATVFS_H)
CHECK_INCLUDE_FILE(sys/uio.h HAVE_SYS_UIO_H)
CHECK_INCLUDE_FILE(sys/sysmacros.h HAVE_SYS_SYSMACROS_H)
CHECK_INCLUDE_FILE(sys/mman.h HAVE_SYS_MMAN_H)
CHECK_INCLUDE_FILE(sys/mount.h HAVE_SYS_MOUNT_H)
CHECK_INCLUDE_FILE(sys/param.h HAVE_SYS_PARAM_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILE(netinet/in.h HAVE_NETINET_IN_H)
CHECK_INCLUDE_FILE(netinet/in_systm.h HAVE_NETINET_IN_SYSTM_H)
CHECK_INCLUDE_FILE(netinet/tcp.h HAVE_NETINET_TCP_H)
CHECK_INCLUDE_FILE(netinet/ip_icmp.h HAVE_NETINET_IP_ICMP_H)
CHECK_INCLUDE_FILE(netdb.h HAVE_NETDB_H)
CHECK_INCLUDE_FILE(arpa/inet.h HAVE_ARPA_INET_H)
CHECK_INCLUDE_FILE(arpa/nameser.h HAVE_ARPA_NAMESER_H)
CHECK_INCLUDE_FILE(arpa/nameser_compat.h HAVE_ARPA_NAMESER_COMPAT_H)
CHECK_INCLUDE_FILE(siginfo.h HAVE_SIGINFO_H)

# Find libraries
find_package(Backtrace)
if(Backtrace_FOUND)
    set(TS_HAS_BACKTRACE TRUE)
endif()

find_package(brotli)
if(brotli_FOUND)
    set(HAVE_BROTLI_ENCODE_H TRUE)
endif()

if(ENABLE_POSIX_CAP)
    find_package(cap REQUIRED)
    set(TS_USE_POSIX_CAP ${cap_FOUND})
    find_path(PRCTL_HEADER NAMES sys/prctl.h)
    # just making sure
    if(NOT PRCTL_HEADER)
        message(FATAL_ERROR "Have cap but sys/prctl.h not found.")
    endif()
    set(HAVE_SYS_PRCTL_H TRUE)
    unset(PRCTL_HEADER)
else()
    find_package(cap QUIET)
endif()
set(HAVE_SYS_CAPABILITY_H ${cap_FOUND})

find_package(hwloc)
if(hwloc_FOUND)
    set(TS_USE_HWLOC TRUE)
endif()

find_package(LibLZMA)
if(LibLZMA_FOUND)
        set(HAVE_LZMA_H TRUE)
endif()

find_package(PCRE)

include(FindOpenSSL)
find_package(OpenSSL)

if(ENABLE_PROFILER)
    find_package(profiler REQUIRED)
    set(TS_HAS_PROFILER ${profiler_FOUND})
endif()

find_package(TSMallocReplacement QUIET MODULE)
if(TSMallocReplacement_FOUND)
    link_libraries(ts::TSMallocReplacement)
endif()

if(ENABLE_UNWIND)
    find_package(unwind)
    if(unwind_FOUND)
            link_libraries(unwind::unwind)
    endif()
    set(TS_USE_REMOTE_UNWINDING ${unwind_FOUND})
endif()

find_package(ZLIB REQUIRED)

include(Check128BitCas)

# Find ccache
include(find_ccache)

# Check for IO faculties
check_symbol_exists(IN6_IS_ADDR_UNSPECIFIED "netinet/in.h" TS_HAS_IN6_IS_ADDR_UNSPECIFIED)
check_symbol_exists(SO_MARK "sys/socket.h" TS_HAS_SO_MARK)
check_symbol_exists(SO_PEERCRED "sys/socket.h" TS_HAS_SO_PEERCRED)
check_symbol_exists(TLS1_3_VERSION "${OPENSSL_INCLUDE_DIR}/openssl/ssl.h" TS_USE_TLS13)
check_symbol_exists(OPENSSL_NO_TLS1_3 "${OPENSSL_INCLUDE_DIR}/ssl.h" TS_NO_USE_TLS13)
if(TS_NO_USE_TLS13)
    set(TS_USE_TLS13 FALSE CACHE)
endif()
check_symbol_exists(epoll_create "sys/epoll.h" TS_USE_EPOLL)
check_symbol_exists(kqueue "sys/event.h" TS_USE_KQUEUE)
set(CMAKE_REQUIRED_LIBRARIES uring)
check_symbol_exists(io_uring_queue_init "liburing.h" HAVE_IOURING)
unset(CMAKE_REQUIRED_LIBRARIES)
set(CMAKE_REQUIRED_LIBRARIES aio)
check_symbol_exists(io_submit "libaio.h" HAVE_LIBAIO)
unset(CMAKE_REQUIRED_LIBRARIES)
check_symbol_exists(getresuid unistd.h HAVE_GETRESUID)
check_symbol_exists(getresgid unistd.h HAVE_GETRESGID)
check_symbol_exists(accept4 sys/socket.h HAVE_ACCEPT4)
check_symbol_exists(eventfd sys/eventfd.h HAVE_EVENTFD)

option(USE_IOURING "Use experimental io_uring (linux only)" 0)
if (HAVE_IOURING AND USE_IOURING)
    message(Using io_uring)
    set(TS_USE_LINUX_IO_URING 1)
endif(HAVE_IOURING AND USE_IOURING)

option(USE_LIBAIO "Use experimental libaio (linux only)" 0)
if (HAVE_LIBAIO AND USE_LIBAIO)
    message(Using libaio)
    set(TS_USE_LINUX_NATIVE_AIO 1)
endif(HAVE_LIBAIO AND USE_LIBAIO)

# Check ssl functionality
list(APPEND CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY})
check_symbol_exists(BIO_meth_new "openssl/bio.h" HAVE_BIO_METH_NEW)
check_symbol_exists(BIO_set_data "openssl/bio.h" HAVE_BIO_SET_DATA)
check_symbol_exists(BIO_get_data "openssl/bio.h" HAVE_BIO_GET_DATA)
check_symbol_exists(BIO_get_shutdown "openssl/bio.h" HAVE_BIO_GET_SHUTDOWN)
check_symbol_exists(BIO_meth_get_ctrl "openssl/bio.h" HAVE_BIO_METH_GET_CTRL)
check_symbol_exists(BIO_meth_get_create "openssl/bio.h" HAVE_BIO_METH_GET_CREATE)
check_symbol_exists(BIO_meth_get_destroy "openssl/bio.h" HAVE_BIO_METH_GET_DESTROY)
check_symbol_exists(DH_get_2048_256 "openssl/dh.h" TS_USE_GET_DH_2048_256)
check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_cb openssl/ssl.h HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_CB)

# Catch2 for tests
set(CATCH_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/catch2)

include(CTest)
set(TS_HAS_TESTS ${BUILD_REGRESSION_TESTING})

message("Configuring for ${HOST_OS}")

if(HOST_OS STREQUAL "linux")
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    link_libraries(dl)
endif(HOST_OS STREQUAL "linux")

if (HOST_OS STREQUAL "darwin")
    set(CMAKE_MACOSX_RPATH 1)
endif(HOST_OS STREQUAL "darwin")

# Set the rpath for installed binaries
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_compile_definitions(${HOST_OS} PACKAGE_NAME="ats" PACKAGE_VERSION="${TS_VERSION_STRING}")
add_compile_options(-Wno-invalid-offsetof)

# Common includes for everyone
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${PCRE_INCLUDE_DIRS}
)

if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_libraries(${OPENSSL_CRYPTO_LIBRARY})
endif(OPENSSL_FOUND)

add_subdirectory(lib)
include_directories(
        ${CMAKE_SOURCE_DIR}/lib/swoc/include
        ${YAML_CPP_SOURCE_DIR}/include
)

include(subproject_version)

subproject_version(YAML_CPP YAMLCPP_LIB_VERSION)

set(rel_cachedir var/trafficserver)
configure_file(configs/storage.config.default.in configs/storage.config.default)
configure_file(configs/records.yaml.default.in configs/records.yaml.default)
configure_file(include/tscore/ink_config.h.cmake.in include/tscore/ink_config.h)
configure_file(include/ts/apidefs.h.in include/ts/apidefs.h)

enable_testing()

add_subdirectory(src/tscpp/util)
add_subdirectory(src/tscpp/api)
add_subdirectory(src/tscore)
add_subdirectory(src/records)
add_subdirectory(iocore)
add_subdirectory(proxy)
add_subdirectory(mgmt/utils)
add_subdirectory(mgmt/config)
add_subdirectory(mgmt/rpc)
add_subdirectory(src/traffic_server)
add_subdirectory(src/traffic_ctl)
if(ENABLE_WCCP)
    add_subdirectory(src/wccp)
    add_subdirectory(src/traffic_wccp)
endif()
add_subdirectory(src/tests)
add_subdirectory(plugins)
add_subdirectory(configs)

add_custom_target(clang-format-install
        COMMAND ${CMAKE_SOURCE_DIR}/tools/clang-format.sh --install
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/.git/fmt/.clang-format-installed
        COMMENT "Installing clang-format"
        VERBATIM
        )

function(add_clang_format_target target)
    add_custom_target(clang-format-${target}
            ${CMAKE_SOURCE_DIR}/tools/clang-format.sh ${CMAKE_SOURCE_DIR}/${target}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Checking clang-format for ${target}"
            VERBATIM
            )
    add_dependencies(clang-format-${target} clang-format-install)
    list(APPEND CLANG_FORMAT_TARGETS clang-format-${target})
    set(CLANG_FORMAT_TARGETS ${CLANG_FORMAT_TARGETS} PARENT_SCOPE)
endfunction(add_clang_format_target)

add_clang_format_target(src)
add_clang_format_target(example)
add_clang_format_target(iocore)
add_clang_format_target(include)
add_clang_format_target(mgmt)
add_clang_format_target(plugins)
add_clang_format_target(proxy)
add_clang_format_target(tools)
add_clang_format_target(tests)

add_custom_target(clang-format
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "formatting all files"
        DEPENDS ${CLANG_FORMAT_TARGETS}
        VERBATIM
        )

add_custom_target(autopep8 ${CMAKE_SOURCE_DIR}/tools/autopep8.sh ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "formatting python files"
        VERBATIM
        )

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit)
    message("Installing github hook")
    configure_file(${CMAKE_SOURCE_DIR}/tools/git/pre-commit ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit COPYONLY)
endif()

# Create an empty directories for ATS runtime
install(DIRECTORY DESTINATION var/log/trafficserver)
install(DIRECTORY DESTINATION var/trafficserver)

# Display build summary
include(CMakePrintHelpers)
message("Build Summary:")
cmake_print_variables(CMAKE_SYSTEM_NAME)
cmake_print_variables(CMAKE_SYSTEM_VERSION)
cmake_print_variables(CMAKE_SYSTEM_PROCESSOR)
cmake_print_variables(CMAKE_GENERATOR)
cmake_print_variables(CMAKE_BUILD_TYPE)
cmake_print_variables(CMAKE_INSTALL_PREFIX)
cmake_print_variables(CMAKE_CXX_COMPILER)
cmake_print_variables(CMAKE_C_COMPILER)
cmake_print_variables(CMAKE_CXX_FLAGS)
cmake_print_variables(CMAKE_C_FLAGS)
cmake_print_variables(BUILD_PERSON)
cmake_print_variables(BUILD_GROUP)
cmake_print_variables(BUILD_MACHINE)
cmake_print_variables(DEFAULT_STACK_SIZE)
cmake_print_variables(CMAKE_INSTALL_RPATH)
