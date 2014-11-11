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

#include "libts.h"
#include "ink_args.h"
#include "I_Version.h"
#include "I_Layout.h"
#include "I_RecProcess.h"
#include "RecordsConfig.h"

const ArgumentDescription argument_descriptions[] = {
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION()
};

static void
printvar(const char * name, char * val)
{
  printf("%s: %s\n", name, val);
  ats_free(val);
}

int
main(int /* argc ATS_UNUSED */, char **argv)
{
  AppVersionInfo appVersionInfo;

  appVersionInfo.setup(PACKAGE_NAME, "traffic_layout", PACKAGE_VERSION,
          __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  Layout::create();
  RecProcessInit(RECM_STAND_ALONE, NULL /* diags */);
  LibRecordsConfigInit();

  printf("%s: %s\n", "PREFIX", Layout::get()->prefix);
  printvar("BINDIR", RecConfigReadBinDir());
  printvar("SYSCONFDIR", RecConfigReadConfigDir());
  printvar("LIBDIR", Layout::get()->libdir);
  printvar("LOGDIR", RecConfigReadLogDir());
  printvar("RUNTIMEDIR", RecConfigReadRuntimeDir());
  printvar("PLUGINDIR", RecConfigReadPrefixPath("proxy.config.plugin.plugin_dir"));
  printvar("INCLUDEDIR", Layout::get()->includedir);
  printvar("SNAPSHOTDIR", RecConfigReadSnapshotDir());

  printvar("records.config", RecConfigReadConfigPath(NULL, REC_CONFIG_FILE));
  printvar("remap.config", RecConfigReadConfigPath("proxy.config.url_remap.filename"));
  printvar("plugin.config", RecConfigReadConfigPath(NULL, "plugin.config"));
  printvar("ssl_multicert.config", RecConfigReadConfigPath("proxy.config.ssl.server.multicert.filename"));
  printvar("storage.config", RecConfigReadConfigPath("proxy.config.cache.storage_filename"));
  printvar("hosting.config", RecConfigReadConfigPath("proxy.config.cache.hosting_filename"));
  printvar("volume.config", RecConfigReadConfigPath("proxy.config.cache.volume_filename"));
  printvar("ip_allow.config", RecConfigReadConfigPath("proxy.config.cache.ip_allow.filename"));

  exit(0);
}
