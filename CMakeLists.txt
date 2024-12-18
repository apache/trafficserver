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

# Adjust default RelWithDebInfo flags to be in line with "Release with Debug".
# The default RelWithDebInfo uses -O2, but Release uses -O3. This difference can be astonishing to users
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In source builds are disabled.  Use a preset, -B or run from a different directory")
endif()

cmake_minimum_required(VERSION 3.20..3.27)
project(ats VERSION 10.1.0)

set(TS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(TS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(TS_VERSION_MICRO ${PROJECT_VERSION_PATCH})
set(TS_VERSION_STRING ${TS_VERSION_MAJOR}.${TS_VERSION_MINOR}.${TS_VERSION_MICRO})
math(EXPR TS_VERSION_NUMBER "${TS_VERSION_MAJOR} * 1000000 + ${TS_VERSION_MINOR} * 1000 + ${TS_VERSION_MICRO}")

# We make this a cache entry so that it can be configured to different values
# for testing purposes. For example, it can be used on CI to check compatibility
# with a newer standard than what our codebase currently has to comply with.
set(CMAKE_CXX_STANDARD
    20
    CACHE STRING "The C++ standard to compile with (default 20)"
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE -DATS_BUILD)

include(layout)
include(ClangTidy)

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
find_package(PkgConfig)
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
auto_option(LUAJIT PACKAGE_DEPENDS luajit)
auto_option(UNWIND FEATURE_VAR TS_USE_REMOTE_UNWINDING PACKAGE_DEPENDS unwind)

option(ENABLE_ASAN "Use address sanitizer (default OFF)")
option(ENABLE_FUZZING "Enable fuzzing (default OFF)")
option(ENABLE_FIPS "Enable fips compliance (default OFF)")
option(ENABLE_EVENT_TRACKER "Enable event tracking (default OFF)")
option(BUILD_REGRESSION_TESTING "Build regression tests (default ON)" ON)
option(BUILD_EXPERIMENTAL_PLUGINS "Build the experimental plugins (default OFF)")
set(DEFAULT_STACK_SIZE
    1048576
    CACHE STRING "Default stack size (default 1048576)"
)
option(ENABLE_FAST_SDK "Use fast SDK APIs (default OFF)")
option(ENABLE_MALLOC_ALLOCATOR "Use direct malloc allocator over freelist allocator (default OFF)")
option(ENABLE_ALLOCATOR_METRICS "Enable metrics for Allocators (default OFF)")
option(ENABLE_DOCS "Build docs (default OFF)")
option(ENABLE_DISK_FAILURE_TESTS "Build disk failure tests (enables AIO fault injection, default OFF)" OFF)
if(ENABLE_DISK_FAILURE_TESTS)
  add_compile_definitions("AIO_FAULT_INJECTION")
endif()
option(ENABLE_AUTEST "Setup autest (default OFF)")
option(ENABLE_BENCHMARKS "Build benchmarks (default OFF)")
option(EXTERNAL_YAML_CPP "Use external yaml-cpp (default OFF)")
option(EXTERNAL_LIBSWOC "Use external libswoc (default OFF)")
option(LINK_PLUGINS "Link core libraries to plugins (default OFF)")
option(ENABLE_PROBES "Enable ATS SystemTap probes (default OFF)")
option(ENABLE_VERIFY_PLUGINS "Enable plugin verification tests (default ON)" ON)

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
set(TS_USE_DIAGS
    1
    CACHE STRING "Use diags (default 1)"
)
option(ENABLE_CRIPTS "Build and install the Cripts library and headers  (default OFF)")

set(TS_USE_FAST_SDK ${ENABLE_FAST_SDK})

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

check_include_file(ifaddrs.h HAVE_IFADDRS_H)

# Find libraries
if(ENABLE_CRIPTS)
  # needed for cripts
  # however this might become a general requirement
  find_package(fmt 8.1 REQUIRED)
  set(TS_HAS_CRIPTS TRUE)
endif()

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

find_package(LibLZMA)
if(LibLZMA_FOUND)
  set(HAVE_LZMA_H TRUE)
endif()

find_package(PCRE REQUIRED)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

include(CheckOpenSSLIsBoringSSL)
include(CheckOpenSSLIsQuictls)
include(CheckOpenSSLIsAwsLc)
find_package(OpenSSL REQUIRED)
check_openssl_is_boringssl(SSLLIB_IS_BORINGSSL BORINGSSL_VERSION "${OPENSSL_INCLUDE_DIR}")
check_openssl_is_awslc(SSLLIB_IS_AWSLC AWSLC_VERSION "${OPENSSL_INCLUDE_DIR}")

if(SSLLIB_IS_BORINGSSL)
  # The consensus is a commit newer than a1843d660b47116207877614af53defa767be46a
  # The commit that changes API_VERSION to 27 is actually a little bit older than the commit but still a reasonable commit
  set(min_bssl "27")
  if(BORINGSSL_VERSION VERSION_LESS "${min_bssl}")
    message(FATAL_ERROR "BoringSSL API version >= ${min_bssl} or another SSL library required")
  endif()
elseif(SSLLIB_IS_AWSLC)
  set(min_assl "27")
  if(AWSLC_VERSION VERSION_LESS "${min_assl}")
    message(FATAL_ERROR "AWS-LC API version >= ${min_assl} or anonther SSL library required")
  endif()
else()
  set(min_ossl "1.1.1")
  if(OPENSSL_VERSION VERSION_LESS "${min_ossl}")
    message(FATAL_ERROR "OpenSSL version >= ${min_ossl} or another SSL library required")
  endif()
endif()
check_openssl_is_quictls(SSLLIB_IS_QUICTLS "${OPENSSL_INCLUDE_DIR}")

if(OPENSSL_VERSION VERSION_GREATER_EQUAL "3.0.0")
  set(SSLLIB_IS_OPENSSL3 TRUE)
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

if(ENABLE_QUICHE)
  find_package(quiche REQUIRED)

  set(TS_HAS_QUICHE ${quiche_FOUND})
  set(TS_USE_QUIC ${TS_HAS_QUICHE})
  if(NOT SSLLIB_IS_BORINGSSL AND NOT SSLLIB_IS_QUICTLS)
    message(FATAL_ERROR "Use of BoringSSL or OPENSSL/QUICTLS is required if quiche is used.")
  endif()

  if(SSLLIB_IS_QUICTLS)
    # Until we get quictls support integrated with quiche, we just print this message.
    # Once the above that is done, then we can just validate the version.
    message(
      "WARNING - Using quictls requires using a special version of quiche. Make sure quictls is supported in quiche."
    )
  endif()
endif()

find_package(maxminddb) # Header_rewrite experimental/maxmind_acl

if(ENABLE_ASAN)
  if(ENABLE_JEMALLOC OR ENABLE_MIMALLOC)
    message(FATAL_ERROR "ENABLE_JEMALLOC and ENABLE_MIMALLOC are not compatible with asan builds")
  endif()
  add_compile_options(-g -fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-g -fsanitize=address)
endif()

if(ENABLE_PROBES)
  add_compile_options("-DENABLE_SYSTEMTAP_PROBES")
endif()

set(TS_USE_MALLOC_ALLOCATOR ${ENABLE_MALLOC_ALLOCATOR})
set(TS_USE_ALLOCATOR_METRICS ${ENABLE_ALLOCATOR_METRICS})
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
  set(YAMLCPP_LIB_VERSION ${yaml-cpp_VERSION})
  find_package_message(
    yaml-cpp "Found yaml-cpp: ${yaml-cpp_CONFIG} (found version \"${yaml-cpp_VERSION}\")" "${yaml-cpp_CONFIG}"
  )
endif()

if(EXTERNAL_LIBSWOC)
  message(STATUS "Looking for external libswoc")
  find_package(libswoc REQUIRED)
endif()

include(Check128BitCas)
include(ConfigureTransparentProxy)

# Find ccache
include(find_ccache)

# Check for IO faculties
set(CMAKE_REQUIRED_FLAGS -fPIC)
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
check_symbol_exists(malloc_usable_size "malloc.h;malloc_np.h" HAVE_MALLOC_USABLE_SIZE)
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
check_symbol_exists(sysinfo sys/sysinfo.h HAVE_SYSINFO)
check_symbol_exists(prctl "sys/prctl.h" HAVE_PRCTL)
if(TS_USE_HWLOC)
  check_source_compiles(
    C "#include <hwloc.h>
    int main() { return HWLOC_OBJ_PU; }" HAVE_HWLOC_OBJ_PU
  )
endif()

check_symbol_exists(SO_TXTIME "sys/socket.h" SO_TXTIME_FOUND)
set(CMAKE_EXTRA_INCLUDE_FILES "linux/net_tstamp.h")
check_type_size("struct sock_txtime" STRUCT_SOCK_TXTIME_FOUND)
unset(CMAKE_EXTRA_INCLUDE_FILES)

if(SO_TXTIME_FOUND AND STRUCT_SOCK_TXTIME_FOUND)
  set(HAVE_SO_TXTIME TRUE)
endif()

option(USE_IOURING "Use experimental io_uring (linux only)" 0)
if(HAVE_IOURING AND USE_IOURING)
  message(STATUS "Using io_uring")
  set(TS_USE_LINUX_IO_URING 1)
endif(HAVE_IOURING AND USE_IOURING)

list(APPEND CMAKE_REQUIRED_LIBRARIES pthread)
check_symbol_exists(pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
check_symbol_exists(pthread_get_name_np pthread.h HAVE_PTHREAD_GET_NAME_NP)

option(USE_SPLICE "Enable the use of splice(2) for zero copy (default OFF) (linux only)" OFF)

# Check for the splice function
include(CheckFunctionExists)
check_function_exists(splice HAVE_SPLICE)

# Configure USE_SPLICE based on both availability and user option
if(HAVE_SPLICE AND USE_SPLICE)
  message(STATUS "splice is available and enabled.")
  set(TS_USE_LINUX_SPLICE 1) # Use ON for true
else()
  message(STATUS "splice is either not available or disabled by the user.")
  set(TS_USE_LINUX_SPLICE 0) # Use OFF for false
endif()

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
list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES pthread)
# Check ssl functionality
list(APPEND CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
check_symbol_exists(BIO_meth_new "openssl/bio.h" HAVE_BIO_METH_NEW)
check_symbol_exists(BIO_set_data "openssl/bio.h" HAVE_BIO_SET_DATA)
check_symbol_exists(BIO_get_data "openssl/bio.h" HAVE_BIO_GET_DATA)
check_symbol_exists(BIO_get_shutdown "openssl/bio.h" HAVE_BIO_GET_SHUTDOWN)
check_symbol_exists(BIO_get_ex_new_index "openssl/bio.h" HAVE_BIO_GET_EX_NEW_INDEX)
check_symbol_exists(BIO_get_ex_data "openssl/bio.h" HAVE_BIO_GET_EX_DATA)
check_symbol_exists(BIO_set_ex_data "openssl/bio.h" HAVE_BIO_SET_EX_DATA)
check_symbol_exists(BIO_meth_get_ctrl "openssl/bio.h" HAVE_BIO_METH_GET_CTRL)
check_symbol_exists(BIO_meth_get_create "openssl/bio.h" HAVE_BIO_METH_GET_CREATE)
check_symbol_exists(BIO_meth_get_destroy "openssl/bio.h" HAVE_BIO_METH_GET_DESTROY)
check_symbol_exists(CRYPTO_set_ex_data "openssl/bio.h" HAVE_CRYPTO_SET_EX_DATA)
check_symbol_exists(DH_get_2048_256 "openssl/dh.h" TS_USE_GET_DH_2048_256)
check_symbol_exists(OPENSSL_NO_TLS_3 "openssl/ssl.h" TS_NO_USE_TLS12)
check_symbol_exists(SSL_CTX_set_client_hello_cb "openssl/ssl.h" HAVE_SSL_CTX_SET_CLIENT_HELLO_CB)
check_symbol_exists(SSL_CTX_set_select_certificate_cb "openssl/ssl.h" HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB)
check_symbol_exists(SSL_set1_verify_cert_store "openssl/ssl.h" TS_HAS_VERIFY_CERT_STORE)
check_symbol_exists(SSL_get_shared_curve "openssl/ssl.h" HAVE_SSL_GET_SHARED_CURVE)
check_symbol_exists(SSL_get_curve_name "openssl/ssl.h" HAVE_SSL_GET_CURVE_NAME)
check_symbol_exists(SSL_set_max_early_data "openssl/ssl.h" HAVE_SSL_SET_MAX_EARLY_DATA)
check_symbol_exists(SSL_read_early_data "openssl/ssl.h" HAVE_SSL_READ_EARLY_DATA)
check_symbol_exists(SSL_write_early_data "openssl/ssl.h" HAVE_SSL_WRITE_EARLY_DATA)
check_symbol_exists(SSL_in_early_data "openssl/ssl.h" HAVE_SSL_IN_EARLY_DATA)
check_symbol_exists(SSL_error_description "openssl/ssl.h" HAVE_SSL_ERROR_DESCRIPTION)
check_symbol_exists(SSL_CTX_set_ciphersuites "openssl/ssl.h" TS_USE_TLS_SET_CIPHERSUITES)
check_symbol_exists(SSL_CTX_set_keylog_callback "openssl/ssl.h" TS_HAS_TLS_KEYLOGGING)
check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_cb "openssl/ssl.h" HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_CB)
check_symbol_exists(SSL_get_all_async_fds openssl/ssl.h TS_USE_TLS_ASYNC)
check_symbol_exists(OSSL_PARAM_construct_end "openssl/params.h" HAVE_OSSL_PARAM_CONSTRUCT_END)
check_symbol_exists(TLS1_3_VERSION "openssl/ssl.h" TS_USE_TLS13)
check_symbol_exists(MD5_Init "openssl/md5.h" HAVE_MD5_INIT)
check_symbol_exists(ENGINE_load_dynamic "include/openssl/engine.h" HAVE_ENGINE_LOAD_DYNAMIC)
check_symbol_exists(ENGINE_get_default_RSA "include/openssl/engine.h" HAVE_ENGINE_GET_DEFAULT_RSA)
check_symbol_exists(ENGINE_load_private_key "include/openssl/engine.h" HAVE_ENGINE_LOAD_PRIVATE_KEY)
check_symbol_exists(sysctlbyname "sys/sysctl.h" HAVE_SYSCTLBYNAME)

if(SSLLIB_IS_OPENSSL3)
  check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_evp_cb "openssl/ssl.h" TS_HAS_TLS_SESSION_TICKET)
else()
  check_symbol_exists(SSL_CTX_set_tlsext_ticket_key_cb "openssl/ssl.h" TS_HAS_TLS_SESSION_TICKET)
endif()

unset(CMAKE_REQUIRED_FLAGS)

if(HAVE_SSL_CTX_SET_CLIENT_HELLO_CB OR HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB)
  set(TS_USE_HELLO_CB TRUE)
else()
  set(TS_USE_HELLO_CB FALSE)
endif()

if(HAVE_SSL_SET_MAX_EARLY_DATA
   OR HAVE_SSL_READ_EARL_DATA
   OR HAVE_SSL_WRITE_EARLY_DATA
   OR HAVE_SSL_IN_EARLY_DATA
)
  set(TS_HAS_TLS_EARLY_DATA TRUE)
else()
  set(TS_HAS_TLS_EARLY_DATA FALSE)
endif()

check_source_compiles(
  C "#include <openssl/ssl.h>
  int main() { int x = SSL_CTRL_GET_EXTRA_CHAIN_CERTS; return 0; }" HAVE_NATIVE_DUAL_CERT_SUPPORT
)
check_source_compiles(
  C "#include <openssl/ex_data.h>
  int main() { CRYPTO_EX_unused x; return 0; }" HAVE_CRYPTO_EX_UNUSED
)
check_cxx_source_compiles(
  "#include <openssl/crypto.h>
  #if __has_include(<openssl/ex_data.h>)
  #include <openssl/ex_data.h>
  #endif
  int main() {
  CRYPTO_EX_dup *cb = [] (CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from, void **from_d, int idx, long argl, void *argp) -> int { return 0; };
  return 0;
  }"
  HAVE_CRYPTO_EX_DUP_TYPE1
)

set(CMAKE_REQUIRED_INCLUDES netinet/in.h netinet/tcp.h)
check_type_size("struct tcp_info" STRUCT_TCP_INFO)
unset(CMAKE_REQUIRED_INCLUDES)

# Since Linux 2.6.12
check_struct_has_member("struct tcp_info" tcpi_total_retrans "linux/tcp.h" HAVE_STRUCT_TCP_INFO_TCPI_TOTAL_RETRANS)
# Since Linux 4.6
check_struct_has_member("struct tcp_info" tcpi_data_segs_out "linux/tcp.h" HAVE_STRUCT_TCP_INFO_TCPI_DATA_SEGS_OUT)
# Since FreeBSD 6
check_struct_has_member("struct tcp_info" __tcpi_retrans "netinet/tcp.h" HAVE_STRUCT_TCP_INFO___TCPI_RETRANS)
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
  find_program(NETCAT_PROGRAM nc REQUIRED)
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
  set(AUTEST_OPTIONS
      ""
      CACHE STRING "Additional options for autest (default \"\")"
  )
  set(PROXY_VERIFIER_VERSION "v2.12.0")
  set(PROXY_VERIFIER_HASH "SHA1=8e9adc6e0b31251ca8e6fc2064ae54e5d752bb72")
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

if(CMAKE_CXX_COMPILER_ID STREQUAL AppleClang)
  set(CMAKE_MACOSX_RPATH 1)
endif()

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

add_compile_definitions(${HOST_OS} PACKAGE_NAME="Apache Traffic Server" PACKAGE_VERSION="${TS_VERSION_STRING}")
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>)

# Enable fuzzing
if(ENABLE_FUZZING)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing is only supported with clang")
  endif()
endif()

set(TS_ENABLE_FIPS ${ENABLE_FIPS})
if(ENABLE_EVENT_TRACKER)
  add_compile_definitions(ENABLE_EVENT_TRACKER)
endif()

# Common includes for everyone
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)

add_subdirectory(lib)

# Keep this after lib because lib is made up of third party libraries, so if
# there are warnings, we can't do anything about it.
# Note: -DCMAKE_COMPILE_WARNING_AS_ERROR=ON will turn warnings into errors.
add_compile_options(-pipe -Wall -Wextra)
add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-Wno-noexcept-type;-Wsuggest-override;-Wno-vla-extension;-fno-strict-aliasing>"
)
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation>")

if(NOT EXTERNAL_YAML_CPP)
  include(subproject_version)
  subproject_version(YAML_CPP YAMLCPP_LIB_VERSION)
endif()

set(rel_cachedir var/trafficserver)

if(EXISTS "${PROJECT_SOURCE_DIR}/include/ink_autoconf.h")
  message(STATUS "Autoconf build detected in source tree. Removing autoconf headers.")
endif()

set(CMAKE_HOST "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")

# In-tree autoconf configuration causes duplicate definitions of some symbols
# in generated headers. If the files don't exist, no error is emitted.
file(REMOVE "${PROJECT_SOURCE_DIR}/include/tscore/ink_config.h")
file(REMOVE "${PROJECT_SOURCE_DIR}/include/ts/apidefs.h")
file(REMOVE "${PROJECT_SOURCE_DIR}/include/ink_autoconf.h")

configure_file(configs/storage.config.default.in configs/storage.config.default)
configure_file(configs/records.yaml.default.in configs/records.yaml.default)
configure_file(include/tscore/ink_config.h.cmake.in include/tscore/ink_config.h)
configure_file(include/ts/apidefs.h.in include/ts/apidefs.h)

add_subdirectory(src/tscpp/api)
add_subdirectory(src/tsutil)
add_subdirectory(src/tscore)
add_subdirectory(src/records)
add_subdirectory(src/iocore)
add_subdirectory(src/proxy)
add_subdirectory(src/shared)
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

if(ENABLE_CRIPTS)
  add_subdirectory(src/cripts)
endif()
if(ENABLE_AUTEST)
  add_subdirectory(tests)
endif()
if(ENABLE_FUZZING)
  add_subdirectory(tests/fuzzing)
endif()

# set up auto options for experimental plugins
# This must be done before setting up the stable plugins
# because they check whether the experimental plugins
# already found ImageMagick, but not the other way around.
# Doing these in the other order may add ImageMagick::Magick++
# twice which is an error.
include(ExperimentalPlugins)
# This also adds any experimental plugins that have been enabled.
add_subdirectory(plugins)

add_subdirectory(configs)
if(ENABLE_EXAMPLE)
  add_subdirectory(example)
endif()

# add any extra subdirectories to the build here
add_subdirectory(ext)

if(ENABLE_DOCS)
  set(DOC_LANG
      en
      CACHE STRING "Document language option"
  )
  add_subdirectory(doc)
endif()
add_subdirectory(rc)

if(ENABLE_BENCHMARKS)
  message(STATUS "Building benchmarks in tools/benchmark")
  add_subdirectory(tools/benchmark)
endif()

set(GIT_COMMON_DIR git rev-parse --git-common-dir)

add_custom_target(
  clang-format-install
  COMMAND ${CMAKE_SOURCE_DIR}/tools/clang-format.sh --install
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  BYPRODUCTS ${GIT_COMMON_DIR}/fmt/.clang-format-installed
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

# Leave autopep8 for historical reasons, but have it call yapf. It will not do
# to have conflicting Python formatting targets. This can be removed when at
# least CI is updated to use yapf instead of autopep8.
add_custom_target(
  autopep8
  ${CMAKE_SOURCE_DIR}/tools/yapf.sh ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "formatting python files"
  VERBATIM
)

add_custom_target(
  yapf
  ${CMAKE_SOURCE_DIR}/tools/yapf.sh ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "formatting python files"
  VERBATIM
)

add_custom_target(
  cmake-format
  ${CMAKE_SOURCE_DIR}/tools/cmake-format.sh ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "formatting CMakeLists.txt files"
  VERBATIM
)

# Add a format target that runs all the formatters.
add_custom_target(
  format
  DEPENDS clang-format autopep8 cmake-format
  COMMENT "formatting all files"
)

if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
  file(TIMESTAMP ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit PRE_COMMIT_BEFORE)
  configure_file(${CMAKE_SOURCE_DIR}/tools/git/pre-commit ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit COPYONLY)
  file(TIMESTAMP ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit PRE_COMMIT_AFTER)
  if(NOT PRE_COMMIT_BEFORE STREQUAL PRE_COMMIT_AFTER)
    message(STATUS "Installing github hook")
  endif()
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
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_CACHEDIR})

install(CODE "set(OWNER_USER ${TS_PKGSYSUSER})")
install(CODE "set(OWNER_GROUP ${TS_PKGSYSGROUP})")
install(
  CODE "set(CHOWN_DIRS ${CMAKE_INSTALL_FULL_LOGDIR} ${CMAKE_INSTALL_FULL_RUNSTATEDIR} ${CMAKE_INSTALL_FULL_CACHEDIR})"
)
install(SCRIPT cmake/post_install.cmake)

# Generate pkg-config and cmake config files
configure_file(ts.pc.in ts.pc @ONLY)
configure_file(Findtsapi.cmake.in Findtsapi.cmake @ONLY)

install(FILES "${PROJECT_BINARY_DIR}/ts.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES "${PROJECT_BINARY_DIR}/Findtsapi.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)

# Release Targets
set(DISTTMP /tmp/asf-dist)
set(DISTFILENAME trafficserver-${TS_VERSION_STRING})

add_custom_target(
  asf-distdir
  COMMENT "Create distribution tarball for ASF"
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND rm -rf ${DISTTMP} && mkdir /tmp/asf-dist
  COMMAND git archive --format=tar --prefix=${DISTFILENAME}/ HEAD | tar -C ${DISTTMP} -xf -
  COMMAND rm -rf ${DISTTMP}/${DISTFILENAME}/ci
  COMMAND grep -v img.shields.io ${DISTTMP}/${DISTFILENAME}/README.md > ${DISTTMP}/${DISTFILENAME}/README.md.clean
  COMMAND mv ${DISTTMP}/${DISTFILENAME}/README.md.clean ${DISTTMP}/${DISTFILENAME}/README.md
)

add_custom_target(
  asf-dist
  COMMENT "Create distribution tarball for ASF release"
  DEPENDS asf-distdir
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND tar -C ${DISTTMP} -c ${DISTFILENAME} | bzip2 -9 -c > ${DISTFILENAME}.tar.bz2
  COMMAND rm -rf ${DISTTMP}
)

add_custom_target(
  asf-dist-rc
  COMMENT "Create distribution tarball for ASF release candidate"
  DEPENDS asf-distdir
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND tar -C ${DISTTMP} -c ${DISTFILENAME} | bzip2 -9 -c > ${DISTFILENAME}-rc$ENV{RC}.tar.bz2
  COMMAND rm -rf ${DISTTMP}
)

add_custom_target(
  asf-dist-sign
  COMMENT "Create and sign distribution tarball for ASF release"
  DEPENDS asf-dist
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND shasum -a512 -b ${DISTFILENAME}.tar.bz2 > ${DISTFILENAME}.tar.bz2.sha512
  COMMAND gpg --armor --output ${DISTFILENAME}.tar.bz2.asc --detach-sig ${DISTFILENAME}.tar.bz2
)

add_custom_target(
  asf-dist-sign-rc
  COMMENT "Create and sign distribution tarball for ASF release candidate"
  DEPENDS asf-dist-rc
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND shasum -a512 -b ${DISTFILENAME}-rc$ENV{RC}.tar.bz2 > ${DISTFILENAME}-rc$ENV{RC}.tar.bz2.sha512
  COMMAND gpg --armor --output ${DISTFILENAME}-rc$ENV{RC}.tar.bz2.asc --detach-sig ${DISTFILENAME}-rc$ENV{RC}.tar.bz2
)

add_custom_target(
  release
  COMMENT "Create release"
  DEPENDS asf-dist-sign
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND git tag -fs -m "Release ${TS_VERSION_STRING}" ${TS_VERSION_STRING}
)

add_custom_target(
  rel-candidate
  COMMENT "Create release candidate"
  DEPENDS asf-dist-sign-rc
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND git tag -fs -m "Release Candidate ${TS_VERSION_STRING}-rc$ENV{RC}" ${TS_VERSION_STRING}-rc$ENV{RC}
)

# Display build summary
include(CMakePrintHelpers)
message(STATUS "Build Summary:")
cmake_print_variables(CMAKE_HOST)
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
