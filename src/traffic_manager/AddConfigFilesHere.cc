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

#include "tscore/ink_platform.h"
#include "tscore/Filenames.h"
#include "MgmtUtils.h"
#include "tscore/Diags.h"
#include "FileManager.h"

extern FileManager *configFiles;

/****************************************************************************
 *
 *  AddConfigFilesHere.cc - Structs for config files and
 *
 *
 ****************************************************************************/

void
testcall(char *foo, char * /*configName */)
{
  Debug("lm", "Received Callback that %s has changed", foo);
}

void
registerFile(const char *configName, const char *defaultName)
{
  bool found        = false;
  const char *fname = REC_readString(configName, &found);
  if (!found) {
    fname = defaultName;
  }
  configFiles->addFile(fname, configName, false);
}

//
// initializeRegistry()
//
// Code to initialize of registry of objects that represent
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

  registerFile("proxy.config.log.config.filename", ts::filename::LOGGING);
  registerFile("", ts::filename::STORAGE);
  registerFile("proxy.config.socks.socks_config_file", ts::filename::SOCKS);
  registerFile(ts::filename::RECORDS, ts::filename::RECORDS);
  registerFile("proxy.config.cache.control.filename", ts::filename::CACHE);
  registerFile("proxy.config.cache.ip_allow.filename", ts::filename::IP_ALLOW);
  registerFile("proxy.config.http.parent_proxy.file", ts::filename::PARENT);
  registerFile("proxy.config.url_remap.filename", ts::filename::REMAP);
  registerFile("", ts::filename::VOLUME);
  registerFile("proxy.config.cache.hosting_filename", ts::filename::HOSTING);
  registerFile("", ts::filename::PLUGIN);
  registerFile("proxy.config.dns.splitdns.filename", ts::filename::SPLITDNS);
  registerFile("proxy.config.ssl.server.multicert.filename", ts::filename::SSL_MULTICERT);
  registerFile("proxy.config.ssl.servername.filename", ts::filename::SNI);

  configFiles->registerCallback(testcall);
}
