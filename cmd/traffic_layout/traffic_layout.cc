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

#include "ts/ink_platform.h"
#include "ts/ink_args.h"
#include "ts/I_Version.h"
#include "ts/I_Layout.h"
#include "I_RecProcess.h"
#include "RecordsConfig.h"

// Command line arguments (parsing)
struct CommandLineArgs {
  int layout;
  int features;
  int json;
};

static CommandLineArgs cl;

const ArgumentDescription argument_descriptions[] = {
  {"layout", 'l', "Show the layout (this is the default with no options given)", "T", &cl.layout, NULL, NULL},
  {"features", 'f', "Show the compiled features", "T", &cl.features, NULL, NULL},
  {"json", 'j', "Produce output in JSON format (when supported)", "T", &cl.json, NULL, NULL},

  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION()};

// Produce output about compile time features, useful for checking how things were built, as well
// as for our TSQA test harness.
static void
print_feature(const char *name, int value, bool json, bool last = false)
{
  if (json) {
    printf("    \"%s\": %d%s", name, value, last ? "\n" : ",\n");
  } else {
    printf("#define %s %d\n", name, value);
  }
}

static void
print_feature(const char *name, const char *value, bool json, bool last = false)
{
  if (json) {
    printf("    \"%s\": \"%s\"%s", name, value, last ? "\n" : ",\n");
  } else {
    printf("#define %s \"%s\"\n", name, value);
  }
}

static void
produce_features(bool json)
{
  if (json) {
    printf("{\n");
  }
  print_feature("BUILD_MACHINE", BUILD_MACHINE, json);
  print_feature("BUILD_PERSON", BUILD_PERSON, json);
  print_feature("BUILD_GROUP", BUILD_GROUP, json);
  print_feature("BUILD_NUMBER", BUILD_NUMBER, json);
  print_feature("TS_HAS_LIBZ", TS_HAS_LIBZ, json);
  print_feature("TS_HAS_LZMA", TS_HAS_LZMA, json);
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
  print_feature("TS_HAS_SPDY", TS_HAS_SPDY, json);
  print_feature("TS_HAS_IP_TOS", TS_HAS_IP_TOS, json);
  print_feature("TS_USE_HWLOC", TS_USE_HWLOC, json);
  print_feature("TS_USE_TLS_NPN", TS_USE_TLS_NPN, json);
  print_feature("TS_USE_TLS_ALPN", TS_USE_TLS_ALPN, json);
  print_feature("TS_USE_TLS_SNI", TS_USE_TLS_SNI, json);
  print_feature("TS_USE_CERT_CB", TS_USE_CERT_CB, json);
  print_feature("TS_USE_SET_RBIO", TS_USE_SET_RBIO, json);
  print_feature("TS_USE_TLS_ECKEY", TS_USE_TLS_ECKEY, json);
  print_feature("TS_USE_LINUX_NATIVE_AIO", TS_USE_LINUX_NATIVE_AIO, json);
  print_feature("TS_HAS_SO_PEERCRED", TS_HAS_SO_PEERCRED, json);
  print_feature("TS_USE_REMOTE_UNWINDING", TS_USE_REMOTE_UNWINDING, json);
  print_feature("GETHOSTBYNAME_R_GLIBC2", GETHOSTBYNAME_R_GLIBC2, json);
  print_feature("SIZEOF_VOID_POINTER", SIZEOF_VOID_POINTER, json);
  print_feature("TS_IP_TRANSPARENT", TS_IP_TRANSPARENT, json);
  print_feature("TS_HAS_128BIT_CAS", TS_HAS_128BIT_CAS, json);
  print_feature("TS_HAS_TESTS", TS_HAS_TESTS, json);
  print_feature("TS_HAS_WCCP", TS_HAS_WCCP, json);
  print_feature("TS_MAX_THREADS_IN_EACH_THREAD_TYPE", TS_MAX_THREADS_IN_EACH_THREAD_TYPE, json);
  print_feature("TS_MAX_NUMBER_EVENT_THREADS", TS_MAX_NUMBER_EVENT_THREADS, json);
  print_feature("TS_MAX_HOST_NAME_LEN", TS_MAX_HOST_NAME_LEN, json);
  print_feature("TS_MAX_API_STATS", TS_MAX_API_STATS, json);
  print_feature("SPLIT_DNS", SPLIT_DNS, json);
  print_feature("HTTP_CACHE", HTTP_CACHE, json);
  print_feature("TS_PKGSYSUSER", TS_PKGSYSUSER, json);
  print_feature("TS_PKGSYSGROUP", TS_PKGSYSGROUP, json, true);
  if (json) {
    printf("}\n");
  }
}

static void
print_var(const char *name, char *value, bool json, bool free = true, bool last = false)
{
  if (json) {
    printf("    \"%s\": \"%s\"%s", name, value, last ? "\n" : ",\n");
  } else {
    printf("%s: %s\n", name, value);
  }

  if (free) {
    ats_free(value);
  }
}

static void
produce_layout(bool json)
{
  Layout::create();

  RecProcessInit(RECM_STAND_ALONE, NULL /* diags */);
  LibRecordsConfigInit();

  if (json) {
    printf("{\n");
  }
  print_var("PREFIX", Layout::get()->prefix, json, false); // Don't free this
  print_var("BINDIR", RecConfigReadBinDir(), json);
  print_var("SYSCONFDIR", RecConfigReadConfigDir(), json);
  print_var("LIBDIR", Layout::get()->libdir, json, false); // Don't free this
  print_var("LOGDIR", RecConfigReadLogDir(), json);
  print_var("RUNTIMEDIR", RecConfigReadRuntimeDir(), json);
  print_var("PLUGINDIR", RecConfigReadPrefixPath("proxy.config.plugin.plugin_dir"), json);
  print_var("INCLUDEDIR", Layout::get()->includedir, json, false); // Dont' free this
  print_var("SNAPSHOTDIR", RecConfigReadSnapshotDir(), json);

  print_var("records.config", RecConfigReadConfigPath(NULL, REC_CONFIG_FILE), json);
  print_var("remap.config", RecConfigReadConfigPath("proxy.config.url_remap.filename"), json);
  print_var("plugin.config", RecConfigReadConfigPath(NULL, "plugin.config"), json);
  print_var("ssl_multicert.config", RecConfigReadConfigPath("proxy.config.ssl.server.multicert.filename"), json);
  print_var("storage.config", RecConfigReadConfigPath("proxy.config.cache.storage_filename"), json);
  print_var("hosting.config", RecConfigReadConfigPath("proxy.config.cache.hosting_filename"), json);
  print_var("volume.config", RecConfigReadConfigPath("proxy.config.cache.volume_filename"), json);
  print_var("ip_allow.config", RecConfigReadConfigPath("proxy.config.cache.ip_allow.filename"), json, true, true);
  if (json) {
    printf("}\n");
  }
}

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
  AppVersionInfo appVersionInfo;

  appVersionInfo.setup(PACKAGE_NAME, "traffic_layout", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  if (cl.features) {
    produce_features(0 != cl.json);
  } else {
    produce_layout(0 != cl.json);
  }
  exit(0);
}
