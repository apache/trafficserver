/** @file

  Virtual Host configuration

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

#pragma once

#include <string>
#include <string_view>

#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "tscore/Ptr.h"

class VirtualHostConfig : public ConfigInfo
{
public:
  VirtualHostConfig() = default;
  VirtualHostConfig(const VirtualHostConfig &other)
    : _entries(other._entries),
      _exact_domains_to_id(other._exact_domains_to_id),
      _wildcard_domains_to_id(other._wildcard_domains_to_id)
  {
  }
  VirtualHostConfig &
  operator=(const VirtualHostConfig &other)
  {
    if (this != &other) {
      _entries                = other._entries;
      _exact_domains_to_id    = other._exact_domains_to_id;
      _wildcard_domains_to_id = other._wildcard_domains_to_id;
    }
    return *this;
  }
  ~VirtualHostConfig() = default;

  struct Entry : public RefCountObjInHeap {
    std::string              id;
    std::vector<std::string> exact_domains;
    std::vector<std::string> wildcard_domains;
    Ptr<UrlRewrite>          remap_table;

    Entry      *acquire() const;
    void        release() const;
    std::string get_id() const;
  };

  bool        load();
  bool        set_entry(std::string_view id, Ptr<Entry> &entry);
  static bool load_entry(std::string_view id, Ptr<Entry> &entry);
  Ptr<Entry>  find_by_id(std::string_view id) const;
  Ptr<Entry>  find_by_domain(std::string_view domain) const;

private:
  using entry_map = std::unordered_map<std::string, Ptr<Entry>>;
  using name_map  = std::unordered_map<std::string, std::string>;

  entry_map _entries;
  name_map  _exact_domains_to_id;
  name_map  _wildcard_domains_to_id;
};

class VirtualHost
{
public:
  using scoped_config = ConfigProcessor::scoped_config<VirtualHost, VirtualHostConfig>;

  static void               startup();
  static int                reconfigure();
  static int                reconfigure(std::string_view id);
  static VirtualHostConfig *acquire();
  static void               release(VirtualHostConfig *config);

private:
  static int config_callback(const char *, RecDataT, RecData, void *);
  static int _configid;
};

struct VirtualHostConfigContinuation : public Continuation {
  VirtualHostConfigContinuation() : Continuation(nullptr) { SET_HANDLER(&VirtualHostConfigContinuation::reconfigure); }

  int
  reconfigure(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    VirtualHost::reconfigure();
    delete this;
    return EVENT_DONE;
  }
};
