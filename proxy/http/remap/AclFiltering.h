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

#pragma once

#include "tscore/ink_inet.h"

#include <string>
#include <set>
#include <vector>

// ===============================================================================
// ACL like filtering defs (per one remap rule)

static int const ACL_FILTER_MAX_SRC_IP = 128;
static int const ACL_FILTER_MAX_IN_IP  = 8;
static int const ACL_FILTER_MAX_ARGV   = 512;

struct src_ip_info_t {
  IpAddr start; ///< Minimum value in range.
  IpAddr end;   ///< Maximum value in range.
  bool invert;  ///< Should we "invert" the meaning of this IP range ("not in range")

  void
  reset()
  {
    start.invalidate();
    end.invalidate();
    invert = false;
  }

  /// @return @c true if @a ip is inside @a this range.
  bool
  contains(IpEndpoint const &ip)
  {
    IpAddr addr{ip};
    return addr.cmp(start) >= 0 && addr.cmp(end) <= 0;
  }
};

/**
 *
 **/
class acl_filter_rule
{
private:
  void reset(void);

public:
  acl_filter_rule *next;
  char *filter_name;           // optional filter name
  unsigned int allow_flag : 1, // action allow deny
    src_ip_valid : 1,          // src_ip range valid
    in_ip_valid : 1,
    active_queue_flag : 1, // filter is in active state (used by .useflt directive)
    internal : 1;          // filter internal HTTP requests

  // we need arguments as string array for directive processing
  int argc;                        // argument counter (only for filter defs)
  char *argv[ACL_FILTER_MAX_ARGV]; // argument strings (only for filter defs)

  // methods
  bool method_restriction_enabled;
  std::vector<bool> standard_method_lookup;

  typedef std::set<std::string> MethodMap;
  MethodMap nonstandard_methods;

  // src_ip
  int src_ip_cnt; // how many valid src_ip rules we have
  src_ip_info_t src_ip_array[ACL_FILTER_MAX_SRC_IP];

  // in_ip
  int in_ip_cnt; // how many valid dst_ip rules we have
  src_ip_info_t in_ip_array[ACL_FILTER_MAX_IN_IP];

  acl_filter_rule();
  ~acl_filter_rule();
  void name(const char *_name = nullptr);
  int add_argv(int _argc, char *_argv[]);
  void print(void);

  static acl_filter_rule *find_byname(acl_filter_rule *list, const char *name);
  static void delete_byname(acl_filter_rule **list, const char *name);
  static void requeue_in_active_list(acl_filter_rule **list, acl_filter_rule *rp);
  static void requeue_in_passive_list(acl_filter_rule **list, acl_filter_rule *rp);
};
