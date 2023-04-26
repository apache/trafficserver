#### EDITOR SUPPORT ####
## This file exists ONLY to support code editors (e.g. Visual Code, CLion).
##
## !!!-> IT WILL NOT BUILD TRAFFIC SERVER <-!!!
##
## This file can be tweaked to keep editors happy. In general all projects should be defined here so
## there is only one instance of this file, not one per directory. This has been done only to the
## extent that someone using such an editor needed it, so much work is left to be done.
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
set(DEFAULT_STACK_SIZE 1048576 CACHE STRING "Default stack size (default 1048576)")
set(TS_MAX_HOST_NAME_LEN 256 CACHE STRING "Max host name length (default 256)")
set(TS_USE_SET_RBIO 1 CACHE STRING "Use openssl set_rbio (default 1)")
set(TS_USE_DIAGS 1 CACHE STRING "Use diags (default 1)")

set(TS_VERSION_MAJOR 10)
set(TS_VERSION_MINOR 0)
set(TS_VERSION_MICRO 0)
set(TS_LIBTOOL_MAJOR ${TS_VERSION_MAJOR}${TS_VERSION_MINOR})
set(TS_LIBTOOL_VERSION ${TS_LIBTOOL_MAJOR}:${TS_VERSION_MICRO}:${TS_VERSION_MINOR})
set(TS_VERSION_STRING TS_VERSION_S)
set(TS_VERSION_NUMBER TS_VERSION_N)

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
find_package(PCRE)
include(FindOpenSSL)
find_package(OpenSSL)

# Check for IO faculties
check_symbol_exists(epoll_create "sys/epoll.h" TS_USE_EPOLL)
check_symbol_exists(kqueue "sys/event.h" TS_USE_KQUEUE)
set(CMAKE_REQUIRED_LIBRARIES uring)
check_symbol_exists(io_uring_queue_init "liburing.h" HAVE_IOURING)
unset(CMAKE_REQUIRED_LIBRARIES)
check_symbol_exists(getresuid unistd.h HAVE_GETRESUID)
check_symbol_exists(getresgid unistd.h HAVE_GETRESGID)
check_symbol_exists(accept4 sys/socket.h HAVE_ACCEPT4)
check_symbol_exists(eventfd sys/eventfd.h HAVE_EVENTFD)

check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_cb openssl/ssl.h HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_CB)

option(USE_IOURING "Use experimental io_uring (linux only)" 0)
if (HAVE_IOURING AND USE_IOURING)
    message(Using io_uring)
    set(TS_USE_LINUX_IO_URING 1)
endif(HAVE_IOURING AND USE_IOURING)

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

# Catch2 for tests
set(CATCH_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/tests/include)

include(CTest)

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

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

add_compile_definitions(${HOST_OS} PACKAGE_NAME="ats" PACKAGE_VERSION="${TS_VERSION_STRING}")
add_compile_options(-Wno-invalid-offsetof)

# common includes for everyone
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

configure_file(include/tscore/ink_config.h.cmake.in include/tscore/ink_config.h)
configure_file(include/ts/apidefs.h.in include/ts/apidefs.h)

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
