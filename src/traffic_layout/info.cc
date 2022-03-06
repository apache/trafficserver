/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <fcntl.h>
#include <openssl/crypto.h>
#include "tscore/I_Layout.h"
#include "tscore/Filenames.h"
#include "tscore/BufferWriter.h"
#include "records/I_RecProcess.h"
#include "RecordsConfig.h"
#include "info.h"

#if TS_USE_HWLOC
#include <hwloc.h>
#endif

#if HAVE_ZLIB_H
#include <zlib.h>
#endif

#if HAVE_LZMA_H
#include <lzma.h>
#endif

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

// Produce output about compile time features, useful for checking how things were built
static void
print_feature(std::string_view name, int value, bool json, bool last = false)
{
  if (json) {
    printf("    \"%.*s\": %d%s", static_cast<int>(name.size()), name.data(), value, last ? "\n" : ",\n");
  } else {
    printf("#define %.*s %d\n", static_cast<int>(name.size()), name.data(), value);
  }
}

static void
print_feature(std::string_view name, std::string_view value, bool json, bool last = false)
{
  if (json) {
    printf(R"(    "%.*s": "%.*s"%s)", static_cast<int>(name.size()), name.data(), static_cast<int>(value.size()), value.data(),
           last ? "\n" : ",\n");
  } else {
    printf("#define %.*s \"%.*s\"\n", static_cast<int>(name.size()), name.data(), static_cast<int>(value.size()), value.data());
  }
}

void
produce_features(bool json)
{
  if (json) {
    printf("{\n");
  }
  print_feature("BUILD_MACHINE", BUILD_MACHINE, json);
  print_feature("BUILD_PERSON", BUILD_PERSON, json);
  print_feature("BUILD_GROUP", BUILD_GROUP, json);
  print_feature("BUILD_NUMBER", BUILD_NUMBER, json);
#if HAVE_ZLIB_H
  print_feature("TS_HAS_LIBZ", 1, json);
#else
  print_feature("TS_HAS_LIBZ", 0, json);
#endif
#if HAVE_LZMA_H
  print_feature("TS_HAS_LZMA", 1, json);
#else
  print_feature("TS_HAS_LZMA", 0, json);
#endif
#if HAVE_BROTLI_ENCODE_H
  print_feature("TS_HAS_BROTLI", 1, json);
#else
  print_feature("TS_HAS_BROTLI", 0, json);
#endif
#ifdef F_GETPIPE_SZ
  print_feature("TS_HAS_PIPE_BUFFER_SIZE_CONFIG", 1, json);
#else
  print_feature("TS_HAS_PIPE_BUFFER_SIZE_CONFIG", 0, json);
#endif /* F_GETPIPE_SZ */
  print_feature("TS_HAS_JEMALLOC", TS_HAS_JEMALLOC, json);
  print_feature("TS_HAS_TCMALLOC", TS_HAS_TCMALLOC, json);
  print_feature("TS_HAS_IN6_IS_ADDR_UNSPECIFIED", TS_HAS_IN6_IS_ADDR_UNSPECIFIED, json);
  print_feature("TS_HAS_BACKTRACE", TS_HAS_BACKTRACE, json);
  print_feature("TS_HAS_PROFILER", TS_HAS_PROFILER, json);
  print_feature("TS_USE_FAST_SDK", TS_USE_FAST_SDK, json);
  print_feature("TS_USE_DIAGS", TS_USE_DIAGS, json);
  print_feature("TS_USE_EPOLL", TS_USE_EPOLL, json);
  print_feature("TS_USE_KQUEUE", TS_USE_KQUEUE, json);
  print_feature("TS_USE_PORT", TS_USE_PORT, json);
  print_feature("TS_USE_POSIX_CAP", TS_USE_POSIX_CAP, json);
  print_feature("TS_USE_TPROXY", TS_USE_TPROXY, json);
  print_feature("TS_HAS_SO_MARK", TS_HAS_SO_MARK, json);
  print_feature("TS_HAS_IP_TOS", TS_HAS_IP_TOS, json);
  print_feature("TS_USE_HWLOC", TS_USE_HWLOC, json);
  print_feature("TS_USE_SET_RBIO", TS_USE_SET_RBIO, json);
  print_feature("TS_USE_TLS13", TS_USE_TLS13, json);
  print_feature("TS_USE_QUIC", TS_USE_QUIC, json);
  print_feature("TS_USE_LINUX_NATIVE_AIO", TS_USE_LINUX_NATIVE_AIO, json);
  print_feature("TS_HAS_SO_PEERCRED", TS_HAS_SO_PEERCRED, json);
  print_feature("TS_USE_REMOTE_UNWINDING", TS_USE_REMOTE_UNWINDING, json);
  print_feature("TS_USE_TLS_OCSP", TS_USE_TLS_OCSP, json);
  print_feature("SIZEOF_VOIDP", SIZEOF_VOIDP, json);
  print_feature("TS_IP_TRANSPARENT", TS_IP_TRANSPARENT, json);
  print_feature("TS_HAS_128BIT_CAS", TS_HAS_128BIT_CAS, json);
  print_feature("TS_HAS_TESTS", TS_HAS_TESTS, json);
  print_feature("TS_HAS_WCCP", TS_HAS_WCCP, json);
  print_feature("TS_MAX_THREADS_IN_EACH_THREAD_TYPE", TS_MAX_THREADS_IN_EACH_THREAD_TYPE, json);
  print_feature("TS_MAX_NUMBER_EVENT_THREADS", TS_MAX_NUMBER_EVENT_THREADS, json);
  print_feature("TS_MAX_HOST_NAME_LEN", TS_MAX_HOST_NAME_LEN, json);
  print_feature("TS_PKGSYSUSER", TS_PKGSYSUSER, json);
  print_feature("TS_PKGSYSGROUP", TS_PKGSYSGROUP, json, true);
  if (json) {
    printf("}\n");
  }
}

void
print_var(std::string_view const &name, std::string_view const &value, bool json, bool last = false)
{
  if (json) {
    printf(R"(    "%.*s": "%.*s"%s)", static_cast<int>(name.size()), name.data(), static_cast<int>(value.size()), value.data(),
           last ? "\n" : ",\n");
  } else {
    printf("%.*s: %.*s\n", static_cast<int>(name.size()), name.data(), static_cast<int>(value.size()), value.data());
  }
}

void
produce_layout(bool json)
{
  RecProcessInit(RECM_STAND_ALONE, nullptr /* diags */);
  LibRecordsConfigInit();

  if (json) {
    printf("{\n");
  }
  print_var("PREFIX", Layout::get()->prefix, json);
  print_var("BINDIR", RecConfigReadBinDir(), json);
  print_var("SYSCONFDIR", RecConfigReadConfigDir(), json);
  print_var("LIBDIR", Layout::get()->libdir, json);
  print_var("LOGDIR", RecConfigReadLogDir(), json);
  print_var("RUNTIMEDIR", RecConfigReadRuntimeDir(), json);
  print_var("PLUGINDIR", RecConfigReadPluginDir(), json);
  print_var("INCLUDEDIR", Layout::get()->includedir, json);

  print_var(ts::filename::RECORDS, RecConfigReadConfigPath(nullptr, ts::filename::RECORDS), json);
  print_var(ts::filename::REMAP, RecConfigReadConfigPath("proxy.config.url_remap.filename"), json);
  print_var(ts::filename::PLUGIN, RecConfigReadConfigPath(nullptr, ts::filename::PLUGIN), json);
  print_var(ts::filename::SSL_MULTICERT, RecConfigReadConfigPath("proxy.config.ssl.server.multicert.filename"), json);
  print_var(ts::filename::STORAGE, RecConfigReadConfigPath(nullptr, ts::filename::STORAGE), json);
  print_var(ts::filename::HOSTING, RecConfigReadConfigPath("proxy.config.cache.hosting_filename"), json);
  print_var(ts::filename::VOLUME, RecConfigReadConfigPath("proxy.config.cache.volume_filename"), json);
  print_var(ts::filename::IP_ALLOW, RecConfigReadConfigPath("proxy.config.cache.ip_allow.filename"), json, true);
  if (json) {
    printf("}\n");
  }
}

void
produce_versions(bool json)
{
  using LBW = ts::LocalBufferWriter<128>;
  [[maybe_unused]] static const std::string_view undef{"undef"};

  if (json) {
    printf("{\n");
  }

  print_var("openssl", LBW().print("{:#x}", OPENSSL_VERSION_NUMBER).view(), json);
  print_var("openssl_str", LBW().print(OPENSSL_VERSION_TEXT).view(), json);
  print_var("pcre", LBW().print("{}.{}", PCRE_MAJOR, PCRE_MINOR).view(), json);
  // These are optional, for now at least.
#if TS_USE_HWLOC
  print_var("hwloc", LBW().print("{:#x}", HWLOC_API_VERSION).view(), json);
  print_var("hwloc.run", LBW().print("{:#x}", hwloc_get_api_version()).view(), json);
#else
  print_var("hwloc", undef, json);
#endif
#if HAVE_ZLIB_H
  print_var("libz", LBW().print("{}", ZLIB_VERSION).view(), json);
#else
  print_var("libz", undef, json);
#endif
#if HAVE_LZMA_H
  print_var("lzma", LBW().print("{}", LZMA_VERSION_STRING).view(), json);
  print_var("lzma.run", LBW().print("{}", lzma_version_string()).view(), json);
#else
  print_var("lzma", undef, json);
#endif
#if HAVE_BROTLI_ENCODE_H
  print_var("brotli", LBW().print("{:#x}", BrotliEncoderVersion()).view(), json);
#else
  print_var("brotli", undef, json);
#endif

  // This should always be last
  print_var("traffic-server", LBW().print(TS_VERSION_STRING).view(), json, true);

  if (json) {
    printf("}\n");
  }
}
