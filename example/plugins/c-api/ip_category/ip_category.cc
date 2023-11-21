/** @file

  Demonstrate a TS_HTTP_IP_ALLOW_CATEGORY_HOOK plugin.

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

/** A plugin that demnstrates implementing ACME_INTERNAL, ACME_EXTERNAL, and
 * ACME_ALL IP categories.
 *
 *
 *   Usage:
 *     # Place the following in plugin.config:
 *     ip_category.so
 */

#include <string>
#include <string_view>
#include <sys/socket.h>

#include "swoc/BufferWriter.h"
#include "swoc/IPAddr.h"
#include "swoc/IPRange.h"
#include "ts/apidefs.h"
#include "ts/ts.h"

namespace
{

std::string const PLUGIN_NAME = "ip_category";
DbgCtl dbg_ctl{"ip_category"};

/** Return whether the address belongs to the given category.
 *
 * @param category The category to check.
 * @param addr The address to check.
 * @return Whether the address belongs to ACME_INTERNAL.
 */
bool
is_in_category(std::string_view category, sockaddr const &addr)
{
  // The following implementation provides a simple stub for this example. In a
  // real environment, this function could perform a library call to a database,
  // parse a configuration file, or the like.
  if (category == "ACME_INTERNAL") {
    swoc::IPRange internal_range{"172.27.0.0/16"};
    swoc::IPSpace<int> internal_space;
    internal_space.mark(internal_range, 1);
    return internal_space.find(swoc::IPAddr{&addr}) != internal_space.end();
  } else if (category == "ACME_EXTERNAL") {
    swoc::IPRange external_range{"10.1.0.0/24"};
    swoc::IPSpace<int> external_space;
    external_space.mark(external_range, 1);
    return external_space.find(swoc::IPAddr{&addr}) != external_space.end();
  } else if (category == "ACME_ALL") {
    return true;
  } else {
    TSError("[%s] Unknown category %.*s", PLUGIN_NAME.c_str(), static_cast<int>(category.size()), category.data());
    return false;
  }
  return false;
}

void
handle_ip_category(TSHttpIpAllowInfo infop)
{
  std::string_view category;
  TSHttpIpAllowInfoCategoryGet(infop, category);

  sockaddr address;
  TSHttpIpAllowInfoAddrGet(infop, address);

  bool const is_contained = is_in_category(category, address);
  TSHttpIpAllowInfoContainsSet(infop, is_contained);

  swoc::LocalBufferWriter<500> w;
  w.print("Address {} is in category {}: {}", swoc::IPAddr{&address}, category, is_contained);
  Dbg(dbg_ctl, "%s", w.data());
}

int
ip_category_callback(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case tsapi::c::TS_EVENT_HTTP_IP_ALLOW_CATEGORY: {
    TSHttpIpAllowInfo infop = static_cast<TSHttpIpAllowInfo>(edata);
    handle_ip_category(infop);
    break;
  }

  default:
    TSError("[%s] Unknown event %d", PLUGIN_NAME.c_str(), event);
    break;
  }

  return TS_SUCCESS;
}

} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  auto cont = TSContCreate(ip_category_callback, nullptr);
  TSHttpHookAdd(tsapi::c::TS_HTTP_IP_ALLOW_CATEGORY_HOOK, cont);
}
