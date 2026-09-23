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
#include <openssl/ssl.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>
#include "tscore/Layout.h"
#include "tscore/Filenames.h"
#include "records/RecCore.h"
#include "records/RecordsConfig.h"
#include "info.h"
#include "iocore/eventsystem/RecProcess.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#if TS_USE_HWLOC
#include <hwloc.h>
#endif

#include <zlib.h>

#if HAVE_LZMA_H
#include <lzma.h>
#endif

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#if HAVE_ZSTD_H
#include <zstd.h>
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
static constexpr int ts_has_cert_compression_callbacks = 1;
#else
static constexpr int ts_has_cert_compression_callbacks = 0;
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
static constexpr int ts_has_cert_compression_zlib = 1;
#elif HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE && !defined(OPENSSL_NO_ZLIB)
static constexpr int ts_has_cert_compression_zlib = 1;
#else
static constexpr int ts_has_cert_compression_zlib = 0;
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG && HAVE_BROTLI_ENCODE_H
static constexpr int ts_has_cert_compression_brotli = 1;
#elif !HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG && HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE && !defined(OPENSSL_NO_BROTLI)
static constexpr int ts_has_cert_compression_brotli = 1;
#else
static constexpr int ts_has_cert_compression_brotli = 0;
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG && HAVE_ZSTD_H
static constexpr int ts_has_cert_compression_zstd = 1;
#elif !HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG && HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE && !defined(OPENSSL_NO_ZSTD)
static constexpr int ts_has_cert_compression_zstd = 1;
#else
static constexpr int ts_has_cert_compression_zstd = 0;
#endif

static constexpr int ts_has_cert_compression =
  ts_has_cert_compression_zlib | ts_has_cert_compression_brotli | ts_has_cert_compression_zstd;

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
#ifdef HAVE_ZSTD_H
  print_feature("TS_HAS_ZSTD", 1, json);
#else
  print_feature("TS_HAS_ZSTD", 0, json);
#endif
  print_feature("TS_HAS_CERT_COMPRESSION", ts_has_cert_compression, json);
  print_feature("TS_HAS_CERT_COMPRESSION_CALLBACKS", ts_has_cert_compression_callbacks, json);
  print_feature("TS_HAS_CERT_COMPRESSION_ZLIB", ts_has_cert_compression_zlib, json);
  print_feature("TS_HAS_CERT_COMPRESSION_BROTLI", ts_has_cert_compression_brotli, json);
  print_feature("TS_HAS_CERT_COMPRESSION_ZSTD", ts_has_cert_compression_zstd, json);
  print_feature("TS_HAS_CRIPTS", TS_HAS_CRIPTS, json);
#ifdef F_GETPIPE_SZ
  print_feature("TS_HAS_PIPE_BUFFER_SIZE_CONFIG", 1, json);
#else
  print_feature("TS_HAS_PIPE_BUFFER_SIZE_CONFIG", 0, json);
#endif /* F_GETPIPE_SZ */
  print_feature("TS_HAS_JEMALLOC", TS_HAS_JEMALLOC, json);
  print_feature("TS_HAS_IN6_IS_ADDR_UNSPECIFIED", TS_HAS_IN6_IS_ADDR_UNSPECIFIED, json);
  print_feature("TS_HAS_BACKTRACE", TS_HAS_BACKTRACE, json);
  print_feature("TS_HAS_PROFILER", TS_HAS_PROFILER, json);
  print_feature("TS_USE_FAST_SDK", TS_USE_FAST_SDK, json);
  print_feature("TS_USE_DIAGS", TS_USE_DIAGS, json);
  print_feature("TS_USE_CACHE_SHM", TS_USE_CACHE_SHM, json);
  print_feature("TS_USE_EPOLL", TS_USE_EPOLL, json);
  print_feature("TS_USE_KQUEUE", TS_USE_KQUEUE, json);
  print_feature("TS_USE_POSIX_CAP", TS_USE_POSIX_CAP, json);
  print_feature("TS_USE_TPROXY", TS_USE_TPROXY, json);
  print_feature("TS_HAS_SO_MARK", TS_HAS_SO_MARK, json);
  print_feature("TS_HAS_IP_TOS", TS_HAS_IP_TOS, json);
  print_feature("TS_USE_HWLOC", TS_USE_HWLOC, json);
  print_feature("TS_USE_TLS13", TS_USE_TLS13, json);
  print_feature("TS_USE_QUIC", TS_USE_QUIC, json);
  print_feature("TS_USE_QMUX", TS_USE_QMUX, json);
  print_feature("TS_HAS_OPENSSL_QUIC", TS_HAS_OPENSSL_QUIC, json);
  print_feature("TS_HAS_QUICHE", TS_HAS_QUICHE, json);
  print_feature("TS_HAS_SO_PEERCRED", TS_HAS_SO_PEERCRED, json);
  print_feature("TS_USE_REMOTE_UNWINDING", TS_USE_REMOTE_UNWINDING, json);
  print_feature("SIZEOF_VOIDP", SIZEOF_VOIDP, json);
  print_feature("TS_IP_TRANSPARENT", TS_IP_TRANSPARENT, json);
  print_feature("TS_HAS_128BIT_CAS", TS_HAS_128BIT_CAS, json);
  print_feature("TS_HAS_TESTS", TS_HAS_TESTS, json);
  // Whether PCRE2 can run a pattern on the just-in-time engine. This is a property of the
  // PCRE2 that ATS is linked against, not of ATS, and it decides which resource limit a
  // pathological pattern reaches: the JIT stack, or the interpreter's far larger match,
  // depth and heap limits, because the interpreter keeps its backtracking frames on the
  // heap. Tests that assert on one of those limits need to know.
  //
  // PCRE2_JIT_TEST_ALLOC (PCRE2 10.45) also confirms the JIT can allocate executable
  // memory. PCRE2_CONFIG_JIT does not, and reports success on a hardened runtime where
  // every JIT compile then fails, which is the direction that misleads a test gate.
  {
    uint32_t has_jit = 0;

#ifdef PCRE2_JIT_TEST_ALLOC
    has_jit = pcre2_jit_compile(nullptr, PCRE2_JIT_TEST_ALLOC) == 0;
#else
    // Below 10.45 there is no library-wide probe that also proves the allocator works,
    // and PCRE2_CONFIG_JIT alone is exactly the misleading answer described above. This
    // tree still supports those versions: Rocky 8.10 links PCRE2 10.32, so this is the
    // live path on that CI lane rather than a theoretical one.
    //
    // So ask the question that cannot be wrong about it. JIT compile a real pattern and
    // see whether a JIT block came back, which is what the unit tests already do per
    // pattern in pattern_has_jit(). A hardened runtime fails the compile and reports a
    // zero JIT size, and the feature correctly reads false.
    {
      int         errnum    = 0;
      PCRE2_SIZE  erroffset = 0;
      pcre2_code *code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>("a"), PCRE2_ZERO_TERMINATED, 0, &errnum, &erroffset, nullptr);

      if (code != nullptr) {
        size_t jit_size = 0;

        pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &jit_size);
        pcre2_code_free(code);
        has_jit = jit_size > 0;
      }
      // A pattern as trivial as "a" failing to compile means the library is unusable,
      // and reporting no JIT is the right answer in that case too.
    }
#endif
    print_feature("TS_HAS_PCRE2_JIT", has_jit != 0, json);
  }
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
  RecProcessInit(nullptr /* diags */);
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
  print_var("LOCALSTATEDIR", Layout::get()->localstatedir, json);

  print_var(ts::filename::RECORDS, RecConfigReadConfigPath(nullptr, ts::filename::RECORDS), json);
  print_var(ts::filename::REMAP, RecConfigReadConfigPath("proxy.config.url_remap.filename"), json);
  print_var(ts::filename::PLUGIN, RecConfigReadConfigPath(nullptr, ts::filename::PLUGIN), json);
  print_var(ts::filename::SSL_MULTICERT, RecConfigReadConfigPath("proxy.config.ssl.server.multicert.filename"), json);
  print_var(ts::filename::STORAGE, RecConfigReadConfigPath(nullptr, ts::filename::STORAGE), json);
  print_var(ts::filename::HOSTING, RecConfigReadConfigPath("proxy.config.cache.hosting_filename"), json);
  print_var(ts::filename::IP_ALLOW, RecConfigReadConfigPath("proxy.config.cache.ip_allow.filename"), json, true);
  if (json) {
    printf("}\n");
  }
}

void
produce_versions(bool json)
{
  using LBW = swoc::LocalBufferWriter<128>;
  [[maybe_unused]] static const std::string_view undef{"undef"};

  if (json) {
    printf("{\n");
  }

  print_var("libz", LBW().print("{}", ZLIB_VERSION).view(), json);
  print_var("openssl", LBW().print("{:#x}", OPENSSL_VERSION_NUMBER).view(), json);
  print_var("openssl_str", LBW().print(OPENSSL_VERSION_TEXT).view(), json);
  print_var("pcre2", LBW().print("{}.{}", PCRE2_MAJOR, PCRE2_MINOR).view(), json);
  // These are optional, for now at least.
#if TS_USE_HWLOC
  print_var("hwloc", LBW().print("{:#x}", HWLOC_API_VERSION).view(), json);
  print_var("hwloc.run", LBW().print("{:#x}", hwloc_get_api_version()).view(), json);
#else
  print_var("hwloc", undef, json);
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
#ifdef HAVE_ZSTD_H
  print_var("zstd", LBW().print("{}", ZSTD_versionString()).view(), json);
#else
  print_var("zstd", undef, json);
#endif

  // This should always be last
  print_var("traffic-server", LBW().print(TS_VERSION_STRING).view(), json, true);

  if (json) {
    printf("}\n");
  }
}
