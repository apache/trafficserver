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

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In source builds are disabled.  Use a preset, -B or run from a different directory")
endif()

cmake_minimum_required(VERSION 3.20..3.27)
project(ats VERSION 10.0.0)

set(TS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(TS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(TS_VERSION_MICRO ${PROJECT_VERSION_PATCH})
set(TS_VERSION_STRING ${TS_VERSION_MAJOR}.${TS_VERSION_MINOR}.${TS_VERSION_MICRO})
math(EXPR TS_VERSION_NUMBER "${TS_VERSION_MAJOR} * 1000000 + ${TS_VERSION_MINOR} * 1000 + ${TS_VERSION_MICRO}")

# We make this a cache entry so that it can be configured to different values
# for testing purposes. For example, it can be used on CI to check compatibility
# with a newer standard than what our codebase currently has to comply with.
set(CMAKE_CXX_STANDARD
    17
    CACHE STRING "The C++ standard to compile with (default 17)"
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

include(layout)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(DEBUG _DEBUG)
endif()

# ATS uses "unix" for variable names.  Make sure its not defined
remove_definitions(-Dunix)

# Gather some environment info
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_OS)
set(BUILD_NUMBER
    "0"
    CACHE STRING "The build number"
)
execute_process(
  COMMAND id -nu
  OUTPUT_VARIABLE BUILD_PERSON
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND id -ng
  OUTPUT_VARIABLE BUILD_GROUP
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND uname -n
  OUTPUT_VARIABLE BUILD_MACHINE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Options
include(AutoOptionHelpers)
auto_option(HWLOC FEATURE_VAR TS_USE_HWLOC PACKAGE_DEPENDS hwloc)
auto_option(
  JEMALLOC
  FEATURE_VAR
  TS_HAS_JEMALLOC
  DEFAULT
  OFF
  PACKAGE_DEPENDS
  jemalloc
)
auto_option(
  MIMALLOC
  FEATURE_VAR
  TS_HAS_MIMALLOC
  DEFAULT
  OFF
  PACKAGE_DEPENDS
  mimalloc
)
auto_option(
  POSIX_CAP
  FEATURE_VAR
  TS_USE_POSIX_CAP
  PACKAGE_DEPENDS
  cap
  HEADER_DEPENDS
  "sys/prctl.h"
)
auto_option(LUAJIT PACKAGE_DEPENDS LuaJIT)
auto_option(UNWIND FEATURE_VAR TS_USE_REMOTE_UNWINDING PACKAGE_DEPENDS unwind)

option(ENABLE_ASAN "Use address sanitizer (default OFF)")
option(ENABLE_FUZZING "Enable fuzzing (default OFF)")
option(BUILD_REGRESSION_TESTING "Build regression tests (default ON)" ON)
option(BUILD_EXPERIMENTAL_PLUGINS "Build the experimental plugins (default OFF)")
set(DEFAULT_STACK_SIZE
    1048576
    CACHE STRING "Default stack size (default 1048576)"
)
option(ENABLE_FAST_SDK "Use fast SDK APIs (default OFF)")
option(ENABLE_MALLOC_ALLOCATOR "Use direct malloc allocator over freelist allocator (default OFF)")
option(ENABLE_DOCS "Build docs (default OFF)")
option(ENABLE_DISK_FAILURE_TESTS "Build disk failure tests (enables AIO fault injection, default OFF)" OFF)
if(ENABLE_DISK_FAILURE_TESTS)
  add_compile_definitions("AIO_FAULT_INJECTION")
endif()
option(ENABLE_AUTEST "Setup autest (default OFF)")
option(ENABLE_BENCHMARKS "Build benchmarks (default OFF)")
option(EXTERNAL_YAML_CPP "Use external yaml-cpp (default OFF)")

# Setup user
# NOTE: this is the user trafficserver runs as
set(WITH_USER
    nobody
    CACHE STRING "The system user (default nobody)"
)
# NOTE: I can't tell that this is used at all besides being printed
set(WITH_GROUP
    nobody
    CACHE STRING "The system group (default nobody, or specified user's group)"
)

if(WITH_GROUP STREQUAL nobody)
  execute_process(
    COMMAND id -ng ${WITH_USER}
    OUTPUT_VARIABLE TS_PKGSYSGROUP
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(TS_PKGSYSGROUP ${WITH_GROUP})
endif()
set(TS_PKGSYSUSER ${WITH_USER})

# Setup default install directory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
      /usr/local/trafficserver
      CACHE PATH "Default install path" FORCE
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  set(DEFAULT_POSIX_CAP ON)
endif()
option(ENABLE_POSIX_CAP "Use POSIX capabilities, turn OFF to use id switching. (default ON for Linux)"
       "${DEFAULT_POSIX_CAP}"
)

option(ENABLE_PROFILER "Use gperftools profiler (default OFF)")
set(ENABLE_TPROXY
    "AUTO"
    CACHE
      STRING
      "Use TPROXY to enable connection transparency. (default AUTO)
        'AUTO' for local system default,
        'NO', to disable,
        'FORCE' to use built in default,
        'X' where X is a number to use as the IP_TRANSPARENT sockopt,
        anything else to enable."
)
option(ENABLE_QUICHE "Use quiche (default OFF)")

option(ENABLE_WCCP "Use WCCP v2 (default OFF)")
option(ENABLE_EXAMPLE "Build example directory (default OFF)")
set(TS_MAX_HOST_NAME_LEN
    256
    CACHE STRING "Max host name length (default 256)"
)
set(MAX_EVENT_THREADS
    4096
    CACHE STRING "Max number of event threads (default 4096)"
)
set(MAX_THREADS_PER_TYPE
    3072
    CACHE STRING "Max number of threads per event type (default 3072)"
)
set(TS_USE_SET_RBIO
    1
    CACHE STRING "Use openssl set_rbio (default 1)"
)
set(TS_USE_DIAGS
    1
    CACHE STRING "Use diags (default 1)"
)
option(ENABLE_CRIPTS "Build and install the Cripts library and headers  (default OFF)")

set(TS_USE_FAST_SDK ${ENABLE_FAST_SDK})

set(TS_HAS_WCCP ${ENABLE_WCCP})
set(TS_MAX_NUMBER_EVENT_THREADS ${MAX_EVENT_THREADS})
set(TS_MAX_THREADS_IN_EACH_THREAD_TYPE ${MAX_THREADS_PER_TYPE})

# Check include files
include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckIncludeFileCXX)
include(CheckSymbolExists)
include(CheckTypeSize)
include(CheckSourceCompiles)
include(CheckStructHasMember)

check_include_file(alloca.h HAVE_ALLOCA_H)
check_include_file(cpio.h HAVE_CPIO_H)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(float.h HAVE_FLOAT_H)
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(ifaddrs.h HAVE_IFADDRS_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(string.h HAVE_STRING_H)
check_include_file(linux/major.h HAVE_LINUX_MAJOR_H)
check_include_file(malloc.h HAVE_MALLOC_H)
check_include_file(mcheck.h HAVE_MCHECK_H)
check_include_file(machine/endian.h HAVE_MACHINE_ENDIAN_H)
check_include_file(sys/byteorder.h HAVE_SYS_BYTEORDER_H)
check_include_file(sys/disk.h HAVE_SYS_DISK_H)
check_include_file(sys/disklabel.h HAVE_SYS_DISKLABEL_H)
check_include_file(sys/endian.h HAVE_SYS_ENDIAN_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(sys/sockio.h HAVE_SYS_SOCKIO_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/statfs.h HAVE_SYS_STATFS_H)
check_include_file(sys/statvfs.h HAVE_SYS_STATVFS_H)
check_include_file(sys/uio.h HAVE_SYS_UIO_H)
check_include_file(sys/sysinfo.h HAVE_SYS_SYSINFO_H)
check_include_file(sys/sysmacros.h HAVE_SYS_SYSMACROS_H)
check_include_file(sys/systeminfo.h HAVE_SYS_SYSTEMINFO_H)
check_include_file(sys/sysctl.h HAVE_SYS_SYSCTL_H)
check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
check_include_file(sys/mount.h HAVE_SYS_MOUNT_H)
check_include_file(sys/param.h HAVE_SYS_PARAM_H)
check_include_file(sysexits.h HAVE_SYSEXITS_H)
check_include_file(stropts.h HAVE_STROPTS_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(values.h HAVE_VALUES_H)
check_include_file(netinet/in.h HAVE_NETINET_IN_H)
check_include_file(netinet/in_systm.h HAVE_NETINET_IN_SYSTM_H)
check_include_file(netinet/tcp.h HAVE_NETINET_TCP_H)
check_include_file(netinet/ip.h HAVE_NETINET_IP_H)
check_include_file(netinet/ip_icmp.h HAVE_NETINET_IP_ICMP_H)
check_include_file(netdb.h HAVE_NETDB_H)
check_include_file(arpa/inet.h HAVE_ARPA_INET_H)
check_include_file(arpa/nameser.h HAVE_ARPA_NAMESER_H)
check_include_file(arpa/nameser_compat.h HAVE_ARPA_NAMESER_COMPAT_H)
check_include_file(siginfo.h HAVE_SIGINFO_H)
check_include_file(endian.h HAVE_ENDIAN_H)

# Find libraries
find_package(Backtrace)
if(Backtrace_FOUND)
  set(TS_HAS_BACKTRACE TRUE)
  if(Backtrace_LIBRARIES)
    message(STATUS "Backtrace found (library ${Backtrace_LIBRARIES})")
  endif()
endif()

find_package(brotli)
if(brotli_FOUND)
  set(HAVE_BROTLI_ENCODE_H TRUE)
endif()

if(TS_USE_POSIX_CAP)
  # These headers must have been found for capabilites to be enabled.
  set(HAVE_SYS_CAPABILITY_H TRUE)
  set(HAVE_SYS_PRCTL_H TRUE)
endif()

find_package(LibLZMA)
if(LibLZMA_FOUND)
  set(HAVE_LZMA_H TRUE)
endif()

find_package(PCRE REQUIRED)

include(CheckOpenSSLIsBoringSSL)
find_package(OpenSSL REQUIRED)
check_openssl_is_boringssl(OPENSSL_IS_BORINGSSL "${OPENSSL_INCLUDE_DIR}")

if(OPENSSL_VERSION VERSION_GREATER_EQUAL "3.0.0")
  set(OPENSSL_IS_OPENSSL3 TRUE)
  add_compile_definitions(OPENSSL_API_COMPAT=10002 OPENSSL_IS_OPENSSL3)
endif()

if(ENABLE_PROFILER)
  find_package(profiler REQUIRED)
  set(TS_HAS_PROFILER ${profiler_FOUND})
endif()

if(TS_HAS_JEMALLOC AND TS_HAS_MIMALLOC)
  message(FATAL_ERROR "Cannot build with both jemalloc and mimalloc.")
endif()

if(TS_HAS_JEMALLOC)
  link_libraries(jemalloc::jemalloc)
elseif(TS_HAS_MIMALLOC)
  link_libraries(mimalloc)
endif()

if(ENABLE_UNWIND)
  find_package(unwind)
  if(unwind_FOUND)
    link_libraries(unwind::unwind)
  endif()
  set(TS_USE_REMOTE_UNWINDING ${unwind_FOUND})
endif()

if(ENABLE_QUICHE)
  find_package(quiche REQUIRED)

  set(TS_HAS_QUICHE ${quiche_FOUND})
  set(TS_USE_QUIC ${TS_HAS_QUICHE})

  if(NOT OPENSSL_IS_BORINGSSL)
    message(FATAL_ERROR "Use of BoringSSL is required if quiche is used.")
  endif()
endif()

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

set(TS_USE_MALLOC_ALLOCATOR ${ENABLE_MALLOC_ALLOCATOR})
find_package(ZLIB REQUIRED)

# ncurses is used in traffic_top
find_package(Curses)
set(HAVE_CURSES_H ${CURSES_HAVE_CURSES_H})
set(HAVE_NCURSES_H ${CURSES_HAVE_NCURSES_H})
set(HAVE_NCURSES_CURSES_H ${CURSES_HAVE_NCURSES_CURSES_H})
set(HAVE_NCURSES_NCURSES_H ${CURSES_HAVE_NCURSES_NCURSES_H})

# find yaml-cpp if requested
if(EXTERNAL_YAML_CPP)
  message(STATUS "Looking for external yaml-cpp")
  find_package(yaml-cpp "0.8.0" REQUIRED)
endif()

# curl is used in traffic_top
find_package(CURL)
set(HAVE_CURL ${CURL_FOUND})

include(Check128BitCas)
include(ConfigureTransparentProxy)

# Find ccache
include(find_ccache)

# Check for IO faculties
check_symbol_exists(IN6_IS_ADDR_UNSPECIFIED "netinet/in.h" TS_HAS_IN6_IS_ADDR_UNSPECIFIED)
check_symbol_exists(IP_TOS "netinet/ip.h" TS_HAS_IP_TOS)
check_symbol_exists(SO_MARK "sys/socket.h" TS_HAS_SO_MARK)
check_symbol_exists(SO_PEERCRED "sys/socket.h" TS_HAS_SO_PEERCRED)
if(TS_NO_USE_TLS13)
  set(TS_USE_TLS13
      FALSE
      CACHE
  )
endif()
check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME)
check_symbol_exists(epoll_create "sys/epoll.h" TS_USE_EPOLL)
check_symbol_exists(kqueue "sys/event.h" TS_USE_KQUEUE)
set(CMAKE_REQUIRED_LIBRARIES uring)
check_symbol_exists(io_uring_queue_init "liburing.h" HAVE_IOURING)
unset(CMAKE_REQUIRED_LIBRARIES)
check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
check_symbol_exists(getpeereid unistd.h HAVE_GETPEEREID)
check_symbol_exists(getpeerucred unistd.h HAVE_GETPEERUCRED)
check_symbol_exists(getresuid unistd.h HAVE_GETRESUID)
check_symbol_exists(getresgid unistd.h HAVE_GETRESGID)
check_symbol_exists(malloc_usable_size malloc.h HAVE_MALLOC_USABLE_SIZE)
check_symbol_exists(mcheck_pedantic mcheck.h HAVE_MCHECK_PEDANTIC)
check_symbol_exists(posix_fadvise fcntl.h HAVE_POSIX_FADVISE)
check_symbol_exists(posix_fallocate fcntl.h HAVE_POSIX_FALLOCATE)
check_symbol_exists(posix_madvise sys/mman.h HAVE_POSIX_MADVISE)
check_symbol_exists(accept4 sys/socket.h HAVE_ACCEPT4)
check_symbol_exists(eventfd sys/eventfd.h HAVE_EVENTFD)
check_symbol_exists(sysconf unistd.h HAVE_SYSCONF)
check_symbol_exists(recvmmsg sys/socket.h HAVE_RECVMMSG)
check_symbol_exists(sendmmsg sys/socket.h HAVE_SENDMMSG)
check_symbol_exists(strlcat string.h HAVE_STRLCAT)
check_symbol_exists(strlcpy string.h HAVE_STRLCPY)
check_symbol_exists(strsignal string.h HAVE_STRSIGNAL)
if(TS_USE_HWLOC)
  check_source_compiles(
    C "#include <hwloc.h>
    int main() { return HWLOC_OBJ_PU; }" HAVE_HWLOC_OBJ_PU
  )
endif()

option(USE_IOURING "Use experimental io_uring (linux only)" 0)
if(HAVE_IOURING AND USE_IOURING)
  message(STATUS "Using io_uring")
  set(TS_USE_LINUX_IO_URING 1)
endif(HAVE_IOURING AND USE_IOURING)

check_symbol_exists(pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
check_symbol_exists(pthread_get_name_np pthread.h HAVE_PTHREAD_GET_NAME_NP)

check_source_compiles(
  C "#include <pthread.h>
  void main() { pthread_setname_np(\"name\"); }" HAVE_PTHREAD_SETNAME_NP_1
)
check_source_compiles(
  C "#include <pthread.h>
  void main() { pthread_setname_np(0, \"name\"); }" HAVE_PTHREAD_SETNAME_NP_2
)
check_source_compiles(
  C "#include <pthread.h>
  void main() { pthread_set_name_np(\"name\"); }" HAVE_PTHREAD_SET_NAME_NP_1
)
check_source_compiles(
  C "#include <pthread.h>
  void main() { pthread_set_name_np(0, \"name\"); }" HAVE_PTHREAD_SET_NAME_NP_2
)

# Check ssl functionality
list(APPEND CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
check_symbol_exists(BIO_meth_new "openssl/bio.h" HAVE_BIO_METH_NEW)
check_symbol_exists(BIO_set_data "openssl/bio.h" HAVE_BIO_SET_DATA)
check_symbol_exists(BIO_get_data "openssl/bio.h" HAVE_BIO_GET_DATA)
check_symbol_exists(BIO_get_shutdown "openssl/bio.h" HAVE_BIO_GET_SHUTDOWN)
check_symbol_exists(BIO_meth_get_ctrl "openssl/bio.h" HAVE_BIO_METH_GET_CTRL)
check_symbol_exists(BIO_meth_get_create "openssl/bio.h" HAVE_BIO_METH_GET_CREATE)
check_symbol_exists(BIO_meth_get_destroy "openssl/bio.h" HAVE_BIO_METH_GET_DESTROY)
check_symbol_exists(HMAC_CTX_new "openssl/hmac.h" HAVE_HMAC_CTX_NEW)
check_symbol_exists(DH_get_2048_256 "openssl/dh.h" TS_USE_GET_DH_2048_256)
check_symbol_exists(OPENSSL_NO_TLS_3 "openssl/ssl.h" TS_NO_USE_TLS12)
check_symbol_exists(SSL_CTX_set_client_hello_cb "openssl/ssl.h" TS_USE_HELLO_CB)
check_symbol_exists(SSL_set1_verify_cert_store "openssl/ssl.h" TS_HAS_VERIFY_CERT_STORE)
check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_evp_cb "openssl/ssl.h" TS_HAS_TLS_SESSION_TICKET)
check_symbol_exists(SSL_read_early_data "openssl/ssl.h" TS_HAS_TLS_EARLY_DATA)
check_symbol_exists(SSL_set_max_early_data "openssl/ssl.h" HAVE_SSL_SET_MAX_EARLY_DATA)
check_symbol_exists(SSL_CTX_set_ciphersuites "openssl/ssl.h" TS_USE_TLS_SET_CIPHERSUITES)
check_symbol_exists(SSL_CTX_set_keylog_callback "openssl/ssl.h" TS_HAS_TLS_KEYLOGGING)
check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_cb "openssl/ssl.h" HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_CB)
check_symbol_exists(SSL_get_all_async_fds openssl/ssl.h TS_USE_TLS_ASYNC)
check_symbol_exists(TLS1_3_VERSION "openssl/ssl.h" TS_USE_TLS13)
check_symbol_exists(MD5_Init "openssl/md5.h" HAVE_MD5_INIT)
check_symbol_exists(sysctlbyname "sys/sysctl.h" HAVE_SYSCTLBYNAME)

set(HAVE_SSL_READ_EARLY_DATA ${TS_HAS_TLS_EARLY_DATA})

set(CMAKE_REQUIRED_INCLUDES netinet/in.h netinet/tcp.h)
check_type_size("struct tcp_info" STRUCT_TCP_INFO)
unset(CMAKE_REQUIRED_INCLUDES)

check_struct_has_member("struct tcp_info" tcpi_data_segs_out "linux/tcp.h" HAVE_STRUCT_LINUX_TCP_INFO)
check_struct_has_member("struct sockaddr" sa_len "netinet/in.h" HAVE_STRUCT_SOCKADDR_SA_LEN)
check_struct_has_member("struct sockaddr_in" sin_len "netinet/in.h" HAVE_STRUCT_SOCKADDR_IN_SIN_LEN)
check_struct_has_member("struct sockaddr_in6" sin6_len "netinet/in.h" HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN)
check_struct_has_member("struct stat" st_mtimespec "sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
check_struct_has_member("struct stat" st_mtim "sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
check_struct_has_member("struct mptcp_info" mptcpi_subflows "linux/mptcp.h" HAVE_STRUCT_MPTCP_INFO_SUBFLOWS)

# find resolv library if available
find_package(resolv)

if(ENABLE_DOCS OR ENABLE_AUTEST)
  find_package(Python3 REQUIRED)
  find_program(PipEnv pipenv REQUIRED)
endif()

if(ENABLE_DOCS)
  find_package(
    Java
    COMPONENTS Runtime
    REQUIRED
  )
endif()

if(ENABLE_AUTEST)
  set(AUTEST_SANDBOX
      ${CMAKE_BINARY_DIR}/_sandbox
      CACHE STRING "Location for autest output (default
  CMAKE_BINARY_DIR/_sandbox)"
  )
  set(PROXY_VERIFIER_VERSION "v2.10.1")
  set(PROXY_VERIFIER_HASH "SHA1=0f189a37596d7488b5b81b5547df6fc1eadf56a1")
  include(proxy-verifier)
endif()

include(CTest)
set(TS_HAS_TESTS ${BUILD_REGRESSION_TESTING})

message(STATUS "Configuring for ${HOST_OS}")

if(HOST_OS STREQUAL "linux")
  set(CMAKE_THREAD_LIBS_INIT "-lpthread")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  set(CMAKE_HAVE_THREADS_LIBRARY 1)
  set(CMAKE_USE_WIN32_THREADS_INIT 0)
  set(CMAKE_USE_PTHREADS_INIT 1)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  link_libraries(dl)
endif(HOST_OS STREQUAL "linux")

if(HOST_OS STREQUAL "darwin")
  set(CMAKE_MACOSX_RPATH 1)
endif(HOST_OS STREQUAL "darwin")

if(HOST_OS STREQUAL "freebsd")
  set(CMAKE_THREAD_LIBS_INIT "-lpthread")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  set(CMAKE_HAVE_THREADS_LIBRARY 1)
  set(CMAKE_USE_WIN32_THREADS_INIT 0)
  set(CMAKE_USE_PTHREADS_INIT 1)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

# Set the rpath for installed binaries
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib64")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_compile_definitions(${HOST_OS} PACKAGE_NAME="ats" PACKAGE_VERSION="${TS_VERSION_STRING}")
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>)

# Enable fuzzing
if(ENABLE_FUZZING)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing is only supported with clang")
  endif()
endif()

# Common includes for everyone
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)

add_subdirectory(lib)

# Keep this after lib because lib is made up of third party libraries, so if
# there are warnings, we can't do anything about it.
# Note: -DCMAKE_COMPILE_WARNING_AS_ERROR=ON will turn warnings into errors.
add_compile_options(-pipe -Wall -Wextra -Wno-unused-parameter)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-noexcept-type;-Wsuggest-override>")
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation;-Wno-stringop-overflow>")

if(NOT EXTERNAL_YAML_CPP)
  include(subproject_version)
  subproject_version(YAML_CPP YAMLCPP_LIB_VERSION)
else()
  set(YAMLCPP_LIB_VERSION yaml-cpp_VERSION)
endif()

set(rel_cachedir var/trafficserver)

if(EXISTS "${PROJECT_SOURCE_DIR}/include/ink_autoconf.h")
  message(STATUS "Autoconf build detected in source tree. Removing autoconf headers.")
endif()

# In-tree autoconf configuration causes duplicate definitions of some symbols
# in generated headers. If the files don't exist, no error is emitted.
file(REMOVE "${PROJECT_SOURCE_DIR}/include/tscore/ink_config.h")
file(REMOVE "${PROJECT_SOURCE_DIR}/include/ink_autoconf.h")

configure_file(configs/storage.config.default.in configs/storage.config.default)
configure_file(configs/records.yaml.default.in configs/records.yaml.default)
configure_file(include/tscore/ink_config.h.cmake.in include/tscore/ink_config.h)
configure_file(include/ts/apidefs.h.in include/ts/apidefs.h)

add_subdirectory(src/tscpp/util)
add_subdirectory(src/tscpp/api)
add_subdirectory(src/tscore)
add_subdirectory(src/records)
add_subdirectory(src/iocore)
add_subdirectory(src/proxy)
add_subdirectory(src/mgmt/config)
add_subdirectory(src/mgmt/rpc)
add_subdirectory(src/api)
add_subdirectory(src/traffic_crashlog)
add_subdirectory(src/traffic_server)
add_subdirectory(src/traffic_ctl)
add_subdirectory(src/traffic_layout)
add_subdirectory(src/traffic_cache_tool)
add_subdirectory(src/traffic_logcat)
add_subdirectory(src/traffic_logstats)
if(CURSES_FOUND)
  add_subdirectory(src/traffic_top)
endif()
add_subdirectory(src/traffic_via)
if(ENABLE_WCCP)
  add_subdirectory(src/wccp)
  add_subdirectory(src/traffic_wccp)
endif()
if(ENABLE_CRIPTS)
  add_subdirectory(src/cripts)
endif()
if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()
if(ENABLE_AUTEST)
  add_subdirectory(tests)
endif()
if(ENABLE_FUZZING)
  add_subdirectory(tests/fuzzing)
endif()
add_subdirectory(plugins)
add_subdirectory(configs)
if(ENABLE_EXAMPLE)
  add_subdirectory(example)
endif()

if(ENABLE_DOCS)
  set(DOC_LANG en CACHE STRING "Document language option")
  add_subdirectory(doc)
endif()
add_subdirectory(rc)

if(ENABLE_BENCHMARKS)
  message(STATUS "Building benchmarks in tools/benchmark")
  add_subdirectory(tools/benchmark)
endif()

add_custom_target(
  clang-format-install
  COMMAND ${CMAKE_SOURCE_DIR}/tools/clang-format.sh --install
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  BYPRODUCTS ${CMAKE_SOURCE_DIR}/.git/fmt/.clang-format-installed
  COMMENT "Installing clang-format"
  VERBATIM
)

function(add_clang_format_target target)
  add_custom_target(
    clang-format-${target}
    ${CMAKE_SOURCE_DIR}/tools/clang-format.sh ${CMAKE_SOURCE_DIR}/${target}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking clang-format for ${target}"
    VERBATIM
  )
  add_dependencies(clang-format-${target} clang-format-install)
  list(APPEND CLANG_FORMAT_TARGETS clang-format-${target})
  set(CLANG_FORMAT_TARGETS
      ${CLANG_FORMAT_TARGETS}
      PARENT_SCOPE
  )
endfunction(add_clang_format_target)

add_clang_format_target(src)
add_clang_format_target(example)
add_clang_format_target(include)
add_clang_format_target(plugins)
add_clang_format_target(tools)
add_clang_format_target(tests)

add_custom_target(
  clang-format
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "formatting all C++ files"
  DEPENDS ${CLANG_FORMAT_TARGETS}
  VERBATIM
)

add_custom_target(
  autopep8
  ${CMAKE_SOURCE_DIR}/tools/autopep8.sh ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "formatting python files"
  VERBATIM
)

# Add a format target that depends upon both clang-format and autopep8.
add_custom_target(
  format
  DEPENDS clang-format autopep8
  COMMENT "formatting all files"
)

if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git AND NOT EXISTS ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit)
  message(STATUS "Installing github hook")
  configure_file(${CMAKE_SOURCE_DIR}/tools/git/pre-commit ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit COPYONLY)
endif()

# Add a target to run the rat tool.  If java isn't installed this will fail, but its not normally run
add_custom_target(
  rat
  COMMENT "Running Apache RAT"
  COMMAND java -jar ${CMAKE_SOURCE_DIR}/ci/apache-rat-0.13-SNAPSHOT.jar -E ${CMAKE_SOURCE_DIR}/ci/rat-regex.txt -d
          ${CMAKE_SOURCE_DIR}
)

# Create an empty directories for ATS runtime
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOGDIR})
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_RUNSTATEDIR})

# Display build summary
include(CMakePrintHelpers)
message(STATUS "Build Summary:")
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
cmake_print_variables(TS_PKGSYSUSER)
cmake_print_variables(TS_PKGSYSGROUP)
cmake_print_variables(BUILD_MACHINE)
cmake_print_variables(DEFAULT_STACK_SIZE)
cmake_print_variables(CMAKE_INSTALL_RPATH)
if(DEFINED DOC_LANG)
  cmake_print_variables(DOC_LANG)
endif(DEFINED DOC_LANG)

print_auto_options_summary()
