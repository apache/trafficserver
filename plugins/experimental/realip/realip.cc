/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include <yaml-cpp/yaml.h>

#include "ts/ts.h"
#include "tscore/ink_config.h"

#include "realip.h"
#include "address_source.h"
#include "address_setter.h"

DbgCtl dbg_ctl{PLUGIN_NAME};

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (argc == 2) {
    try {
      YAML::Node     config = YAML::LoadFile(argv[1]);
      AddressSource *source = AddressSourceBuilder::build(config);
      if (source == nullptr) {
        TSError("[%s] Failed to initialize an address source", PLUGIN_NAME);
        return;
      }
      AddressSetter::set_source(source);
      auto cont = TSContCreate(AddressSetter::event_handler, TSMutexCreate());
      TSReleaseAssert(cont);
      TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
    } catch (YAML::BadFile const &e) {
      TSError("[%s] Cannot load configuration file: %s.", PLUGIN_NAME, e.what());
    } catch (std::exception const &e) {
      TSError("[%s] Unknown error while loading configuration file: %s.", PLUGIN_NAME, e.what());
    }
  } else {
    TSError("[%s] Usage: realip.so <config.yaml>", PLUGIN_NAME);
  }
}
