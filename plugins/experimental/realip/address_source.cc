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
#include <arpa/inet.h>

#include "realip.h"
#include "address_source.h"
#include "simple.h"

AddressSource *
AddressSourceBuilder::build(YAML::Node config)
{
  AddressSource *source = nullptr;
  std::string    source_name;

  for (YAML::iterator it = config.begin(); it != config.end(); ++it) {
    if (source != nullptr) {
      delete source;
      source = nullptr;
      TSError("[%s] Multiple sources are configured.", PLUGIN_NAME);
      break;
    }

    source_name = it->first.as<std::string>();
    if (source_name == "simple") {
      source = new SimpleAddressSource(it->second);
    } else {
      Dbg(dbg_ctl, "Unsupported source: %s", source_name.c_str());
    }
  }

  if (source != nullptr) {
    Dbg(dbg_ctl, "Address source \"%s\" was configured", source_name.c_str());
  }

  return source;
}

int
AddressSource::inet_pton46(std::string str, struct sockaddr_storage *addr)
{
  int ret = 0;
  if (str.find(':') != std::string::npos) {
    struct sockaddr_in6 *addr6 = reinterpret_cast<struct sockaddr_in6 *>(addr);
    addr6->sin6_family         = AF_INET6;
    addr6->sin6_port           = 0;
    ret                        = inet_pton(AF_INET6, str.c_str(), &(addr6->sin6_addr));
  } else {
    struct sockaddr_in *addr4 = reinterpret_cast<struct sockaddr_in *>(addr);
    addr4->sin_family         = AF_INET;
    addr4->sin_port           = 0;
    ret                       = inet_pton(AF_INET, str.c_str(), &(addr4->sin_addr));
  }
  return ret;
}
