/** @file

  HostFile class for processing a hosts file

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

#include "HostFile.h"
#include "P_HostDBProcessor.h"

/* Container for temporarily holding data from the host file. For each FQDN there is a vector of IPv4
 * and IPv6 addresses. These are used to generate the HostDBRecord instances that are stored persistently.
 */
using HostAddrMap = std::unordered_map<std::string_view, std::tuple<std::vector<IpAddr>, std::vector<IpAddr>>>;
using AddrHostMap = std::unordered_map<IpAddr, std::string_view, IpAddr::Hasher>;

namespace
{
constexpr unsigned IPV4_IDX = 0;
constexpr unsigned IPV6_IDX = 1;
} // namespace

static void
ParseHostLine(ts::TextView line, HostAddrMap &map, AddrHostMap &rmap)
{
  // Elements should be the address then a list of host names.
  ts::TextView addr_text = line.take_prefix_if(&isspace);
  IpAddr addr;

  // Don't use RecHttpLoadIp because the address *must* be literal.
  if (TS_SUCCESS != addr.load(addr_text)) {
    return;
  }

  while (!line.ltrim_if(&isspace).empty()) {
    ts::TextView name = line.take_prefix_if(&isspace);
    if (addr.isIp6()) {
      std::get<IPV6_IDX>(map[name]).push_back(addr);
    } else if (addr.isIp4()) {
      std::get<IPV4_IDX>(map[name]).push_back(addr);
    }
    // use insert here so only the first mapping in the file is used to provide a stable view between reloads
    rmap.insert(std::pair(addr, name));
  }
}

HostDBRecord::Handle
HostFile::lookup(const HostDBHash &hash)
{
  HostDBRecord::Handle result;

  // If looking for an IPv4 or IPv6 address, check the host file.
  if (hash.db_mark == HOSTDB_MARK_IPV6 || hash.db_mark == HOSTDB_MARK_IPV4) {
    if (auto spot = forward.find(hash.host_name); spot != forward.end()) {
      result = (hash.db_mark == HOSTDB_MARK_IPV4) ? spot->second.record_4 : spot->second.record_6;
    }
  } else if (hash.db_mark != HOSTDB_MARK_SRV) {
    if (auto spot = reverse.find(hash.ip); spot != reverse.end()) {
      result = spot->second;
    }
  }

  return result;
}

std::shared_ptr<HostFile>
ParseHostFile(ts::file::path const &path, ts_seconds interval)
{
  std::shared_ptr<HostFile> hf;

  Debug_bw("hostdb", R"(Loading host file "{}")", path);

  if (!path.empty()) {
    std::error_code ec;
    std::string content = ts::file::load(path, ec);
    if (!ec) {
      HostAddrMap addr_map;
      AddrHostMap host_map;
      ts::TextView text{content};
      while (text) {
        auto line = text.take_prefix_at('\n').ltrim_if(&isspace);
        if (line.empty() || '#' == *line) {
          continue;
        }
        ParseHostLine(line, addr_map, host_map);
      }
      // @a map should be loaded with all of the data, create the records.
      hf = std::make_shared<HostFile>(interval);
      // Common loading function for creating a record from the address vector.
      auto loader = [](ts::TextView key, std::vector<IpAddr> const &v) -> HostDBRecord::Handle {
        HostDBRecord::Handle record{HostDBRecord::alloc(key, v.size())};
        record->af_family = v.front().family(); // @a v is presumed family homogeneous
        auto rr_info      = record->rr_info();
        auto spot         = v.begin();
        for (auto &item : rr_info) {
          item.assign(*spot++);
        }
        return record;
      };
      // Walk the temporary map and create the corresponding records for the persistent map.
      for (auto const &[key, value] : addr_map) {
        // Bit of subtlety to be able to search records with a view and not a string - the key
        // must point at stable memory for the name, which is available in the record itself.
        // Therefore the lookup for adding the record must be done using a view based in the record.
        // It doesn't matter if it's the IPv4 or IPv6 record that's used, both are stable and equal
        // to each other.
        // IPv4
        if (auto const &v = std::get<IPV4_IDX>(value); v.size() > 0) {
          auto r                               = loader(key, v);
          hf->forward[r->name_view()].record_4 = r;
        }
        // IPv6
        if (auto const &v = std::get<IPV6_IDX>(value); v.size() > 0) {
          auto r                               = loader(key, v);
          hf->forward[r->name_view()].record_6 = r;
        }
      }

      // Walk the temporary reverse map and create the reverse index.
      for (auto const &[ip, host] : host_map) {
        auto lup = hf->forward.find(host);
        if (lup != hf->forward.end()) {
          if (ip.isIp4()) {
            hf->reverse[ip] = lup->second.record_4;
          } else {
            hf->reverse[ip] = lup->second.record_6;
          }
        }
      }
    }
  }

  return hf;
}
