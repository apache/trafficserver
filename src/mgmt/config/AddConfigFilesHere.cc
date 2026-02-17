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
#include "../../records/P_RecCore.h"
#include "tscore/Diags.h"
#include "mgmt/config/FileManager.h"

static constexpr bool REQUIRED{true};
static constexpr bool NOT_REQUIRED{false};
/****************************************************************************
 *
 *  AddConfigFilesHere.cc - Structs for config files and
 *
 *
 ****************************************************************************/
void
registerFile(const char *configName, const char *defaultName, bool isRequired)
{
  auto fname{RecGetRecordStringAlloc(configName)};
  FileManager::instance().addFile(fname ? ats_as_c_str(fname) : defaultName, configName, false, isRequired);
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

  // NOTE: Just to keep track of the files that are registered here. I'll remove this once I can.

  // logging.yaml: now registered via ConfigRegistry::register_config() in LogConfig.cc
  registerFile("", ts::filename::STORAGE, REQUIRED);
  registerFile("proxy.config.socks.socks_config_file", ts::filename::SOCKS, NOT_REQUIRED);
  registerFile(ts::filename::RECORDS, ts::filename::RECORDS, NOT_REQUIRED);
  // cache.config: now registered via ConfigRegistry::register_config() in CacheControl.cc
  // ip_allow: now registered via ConfigRegistry::register_config() in IPAllow.cc
  // ip_categories: registered via ConfigRegistry::add_file_dependency() in IPAllow.cc
  // parent.config: now registered via ConfigRegistry::register_config() in ParentSelection.cc
  // remap.config: now registered via ConfigRegistry::register_config() in ReverseProxy.cc
  registerFile("", ts::filename::VOLUME, NOT_REQUIRED);
  // hosting.config: now registered via ConfigRegistry::register_config() in Cache.cc (open_done)
  registerFile("", ts::filename::PLUGIN, NOT_REQUIRED);
  // splitdns.config: now registered via ConfigRegistry::register_config() in SplitDNS.cc
  // ssl_multicert.config: now registered via ConfigRegistry::add_file_and_node_dependency() in SSLClientCoordinator.cc
  // sni.yaml: now registered via ConfigRegistry::add_file_and_node_dependency() in SSLClientCoordinator.cc
  registerFile("proxy.config.jsonrpc.filename", ts::filename::JSONRPC, NOT_REQUIRED);
}
