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

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_unused.h"
#include "Main.h"
#include "MgmtUtils.h"
#include "ConfigParse.h"
#include "Diags.h"
static char INK_UNUSED rcsId__AddConfigFilesHere_cc[] = "@(#)  built on " __DATE__ " " __TIME__;       /* MAGIC_EDITING_TAG */

/****************************************************************************
 *
 *  AddConfigFilesHere.cc - Structs for config files and
 *  
 * 
 ****************************************************************************/

void
testcall(char *foo)
{
  Debug("lm", "Received Callback that %s has changed\n", foo);
}

#if defined(OEM)

bool
pluginInstalled()
{
  Rollback *file_rb;
  version_t ver;
  textBuffer *file_content = NULL;
  bool retval = false;
  if (configFiles->getRollbackObj("plugin.config", &file_rb)) {
    ver = file_rb->getCurrentVersion();
    file_rb->getVersion(ver, &file_content);
    if (strstr(file_content->bufPtr(), "vscan.so") != NULL)
      retval = true;
  }
  if (file_content)
    delete file_content;
  return retval;
}

#endif

//
// initializeRegistry()
//
// Code to initialze of registry of objects that represent
//   Web Editable configuration files
// 
// thread-safe: NO!  - Should only be executed once from the main
//                     web interface thread, before any child
//                     threads have been spawned
void
initializeRegistry()
{
  static int run_already = 0;

  if (run_already == 0) {
    run_already = 1;
  } else {
    ink_assert(!"Configuration Object Registry Initialized More than Once");
  }

  // NOTE: Logic that controls which files are not sync'd around the
  // cluster is located in ClusterCom::constructSharedFilePacket

  configFiles->addFile("logs.config", false);
  configFiles->addFile("log_hosts.config", false);
  configFiles->addFile("logs_xml.config", false);
  configFiles->addFile("storage.config", false);
  configFiles->addFile("socks.config", false);
  configFiles->addFile("proxy.pac", false);
  configFiles->addFile("wpad.dat", false);
  configFiles->addFile("records.config", false);
  configFiles->addFile("vaddrs.config", false);
  configFiles->addFile("cache.config", false);
  configFiles->addFile("icp.config", false);
  configFiles->addFile("mgmt_allow.config", false);
  configFiles->addFile("ip_allow.config", false);
  configFiles->addFile("parent.config", false);
  configFiles->addFile("filter.config", false);
  configFiles->addFile("remap.config", false);
  configFiles->addFile("snmpinfo.dat", false);  // SNMP daemon agent configuration
  configFiles->addFile("update.config", false);
  configFiles->addFile("admin_access.config", false);
  configFiles->addFile("partition.config", false);
  configFiles->addFile("hosting.config", false);
  configFiles->addFile("bypass.config", false);
  configFiles->addFile("congestion.config", false);
  configFiles->addFile("plugin.config", false);
  configFiles->addFile("ipnat.conf", false);
  configFiles->addFile("bypass.config", false);

  configFiles->addFile("splitdns.config", false);
  configFiles->addFile("ssl_multicert.config", false);
  configFiles->addFile("stats.config.xml", false);
#if defined(OEM)
  configFiles->addFile("net.config.xml", true);
  /* only if the vscan plugin is installed would this file be read */
  if (pluginInstalled()) {
    configFiles->addFile("plugins/vscan.config", false);
    configFiles->addFile("plugins/trusted-host.config", false);
    configFiles->addFile("plugins/extensions.config", false);
  }
#endif
  configFiles->registerCallback(testcall);
}
