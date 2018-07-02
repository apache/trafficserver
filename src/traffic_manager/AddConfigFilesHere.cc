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
#include "MgmtUtils.h"
#include "ts/Diags.h"
#include "FileManager.h"

extern FileManager *configFiles;

/****************************************************************************
 *
 *  AddConfigFilesHere.cc - Structs for config files and
 *
 *
 ****************************************************************************/

void
testcall(char *foo, bool /* incVersion */)
{
  Debug("lm", "Received Callback that %s has changed", foo);
}

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

  configFiles->addFile("logging.config", false);
  configFiles->addFile("storage.config", false);
  configFiles->addFile("socks.config", false);
  configFiles->addFile("records.config", false);
  configFiles->addFile("cache.config", false);
  configFiles->addFile("ip_allow.config", false);
  configFiles->addFile("parent.config", false);
  configFiles->addFile("remap.config", false);
  configFiles->addFile("volume.config", false);
  configFiles->addFile("hosting.config", false);
  configFiles->addFile("plugin.config", false);
  configFiles->addFile("splitdns.config", false);
  configFiles->addFile("ssl_multicert.config", false);
  configFiles->addFile(SSL_SERVER_NAME_CONFIG, false);
  configFiles->registerCallback(testcall);
}
