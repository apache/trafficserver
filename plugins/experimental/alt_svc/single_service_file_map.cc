/**
  @file
  @brief A particular IPHostMap implementation which takes a static file and routes client IPs based on that file.

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

#include "ip_host_map.h"
#include "prefix_parser.h"
#include <fstream>
#include <algorithm>

using namespace std;

const string ssfm_empty = "";

char *
SingleServiceFileMap::findHostForIP(const sockaddr *ip) const noexcept
{
  char *data = nullptr;
  this->host_map.contains(ip, (void **)&data);
  return data;
}

bool
SingleServiceFileMap::isValid() const noexcept
{
  return _isValid;
}

// Lifted from ControlMatcher.cc
void
SingleServiceFileMap::print_the_map() const noexcept
{
  TS_DEBUG(PLUGIN_NAME, "\tIp Matcher with %zu ranges.\n", this->host_map.getCount());
  for (auto &spot : this->host_map) {
    char b1[INET6_ADDRSTRLEN], b2[INET6_ADDRSTRLEN];
    TS_DEBUG(PLUGIN_NAME, "\tRange %s - %s ", ats_ip_ntop(spot.min(), b1, sizeof b1), ats_ip_ntop(spot.max(), b2, sizeof b2));
    TS_DEBUG(PLUGIN_NAME, "Host: %s \n", static_cast<char *>(spot.data()));
  }
}

SingleServiceFileMap::SingleServiceFileMap(ts::string_view filename)
{
  // Read file
  bool fail = 0;
  ifstream config_file{filename.data()};
  if (config_file.fail()) {
    TS_DEBUG(PLUGIN_NAME, "Cannot find a config file at: %s", filename.data());
    fail = 1;
  } else {
    // Parse file into plugin-local IpMap
    string ip_with_prefix, buff;
    auto hostname_iterator = this->hostnames.end();
    while (!getline(config_file, buff).eof()) {
      bool is_host = (buff[0] != ' ');
      buff.erase(remove_if(buff.begin(), buff.end(), ::isspace), buff.end());
      if (is_host) {
        hostname_iterator = this->hostnames.emplace(buff).first;
        continue;
      }
      ip_with_prefix = buff;

      size_t slash;
      slash = ip_with_prefix.find('/');
      if (slash == string::npos) {
        TSError("Cannot find a slash in the provided configuration prefix: %s", ip_with_prefix.c_str());
        fail = 1;
        continue;
      } else if (hostname_iterator == this->hostnames.end()) {
        TSError("Did not find a hostname before the provided configuration prefix: %s", ip_with_prefix.c_str());
        fail = 1;
        continue;
      }

      string ip      = ip_with_prefix.substr(0, slash);
      int prefix_num = stoi(ip_with_prefix.substr(slash + 1));
      sockaddr_storage lower, upper;
      if (parse_addresses(ip.c_str(), prefix_num, &lower, &upper) == PrefixParseError::ok) {
        // We should be okay adding this to the map!
        TS_DEBUG(PLUGIN_NAME, "Mapping %s to host %s", ip_with_prefix.c_str(), hostname_iterator->c_str());
        this->host_map.mark(reinterpret_cast<sockaddr *>(&lower), reinterpret_cast<sockaddr *>(&upper),
                            const_cast<void *>(reinterpret_cast<const void *>(hostname_iterator->c_str())));
      } else {
        // Error message should already be logged by now, just make fail be 1.
        fail = 1;
        continue;
      }
    }
  }
  if (fail) {
    TSError("Alt-Svc plugin initialization failed, this plugin is disabled");
  }

  // Let the plugin know that its configuration is invalid.
  _isValid = fail;
}
