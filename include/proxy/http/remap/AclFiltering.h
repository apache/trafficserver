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

#include "swoc/IPAddr.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

// ===============================================================================
// ACL like filtering defs (per one remap rule)

static int const ACL_FILTER_MAX_SRC_IP = 128;
static int const ACL_FILTER_MAX_IN_IP  = 8;
static int const ACL_FILTER_MAX_ARGV   = 512;

struct src_ip_info_t {
  IpAddr start;               ///< Minimum value in range.
  IpAddr end;                 ///< Maximum value in range.
  bool   invert;              ///< Should we "invert" the meaning of this IP range ("not in range")
  bool   match_all_addresses; ///< This rule should match all IP addresses.

  void
  reset()
  {
    start.invalidate();
    end.invalidate();
    invert              = false;
    match_all_addresses = false;
  }

  /// @return @c true if @a ip is inside @a this range.
  bool
  contains(IpEndpoint const &ip) const
  {
    if (match_all_addresses) {
      return true;
    }
    IpAddr addr{ip};
    return addr.cmp(start) >= 0 && addr.cmp(end) <= 0;
  }
};

struct src_ip_category_info_t {
  std::string category;       ///< The IP category for this remap rule.
  bool        invert = false; ///< Should we "invert" the meaning of these IP categories ("not in categories")

  void
  reset()
  {
    category.clear();
    invert = false;
  }

  /// @return @c true if @a ip is inside @a this categories.
  bool
  contains(IpEndpoint const &ip) const
  {
    return ask_ip_allow_about_category(category, swoc::IPAddr{ip});
  }

private:
  bool ask_ip_allow_about_category(std::string const &category, swoc::IPAddr const &addr) const;
};

/**
 *
 **/
class acl_filter_rule
{
private:
  void reset();

public:
  acl_filter_rule *next        = nullptr;
  char            *filter_name = nullptr; // optional filter name
  unsigned int     allow_flag : 1,        // action allow or add_allow (1); or deny or add_deny (0)
    add_flag                  : 1,        // add_allow/add_deny (1) or allow/deny (0)
    src_ip_valid              : 1,        // src_ip (client's src IP) range is specified and valid
    src_ip_category_valid     : 1,        // src_ip_category (client's src IP category) is specified and valid
    in_ip_valid               : 1,        // in_ip (client's dest IP) range is specified and valid
    active_queue_flag         : 1,        // filter is in active state (used by .useflt directive)
    internal                  : 1;        // filter internal HTTP requests

  // we need arguments as string array for directive processing
  int   argc = 0;                  // argument counter (only for filter defs)
  char *argv[ACL_FILTER_MAX_ARGV]; // argument strings (only for filter defs)

  // methods
  bool              method_restriction_enabled;
  std::vector<bool> standard_method_lookup;

  using MethodMap = std::set<std::string>;
  MethodMap nonstandard_methods;

  // src_ip
  int           src_ip_cnt; // how many valid src_ip rules we have
  src_ip_info_t src_ip_array[ACL_FILTER_MAX_SRC_IP];

  int                    src_ip_category_cnt = 0; // how many valid src_ip rules we have
  src_ip_category_info_t src_ip_category_array[ACL_FILTER_MAX_SRC_IP];

  // in_ip
  int           in_ip_cnt; // how many valid dest_ip rules we have
  src_ip_info_t in_ip_array[ACL_FILTER_MAX_IN_IP];

  acl_filter_rule();
  ~acl_filter_rule();
  void name(const char *_name = nullptr);
  int  add_argv(int _argc, char *_argv[]);
  void print();

  /** Return a description of the action.
   *
   * @return "allow", "add_allow", "deny", or "add_deny", as appropriate.
   */
  char const *get_action_description() const;

  static acl_filter_rule *find_byname(acl_filter_rule *list, const char *name);
  static void             delete_byname(acl_filter_rule **list, const char *name);
  static void             requeue_in_active_list(acl_filter_rule **list, acl_filter_rule *rp);
  static void             requeue_in_passive_list(acl_filter_rule **list, acl_filter_rule *rp);
};
