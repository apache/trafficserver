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

#ifndef _ACL_FILTERING_H_
#define _ACL_FILTERING_H_

#include "Main.h"
//#include "YAddr.h"

// ===============================================================================
// ACL like filtering defs (per one remap rule)

static int const ACL_FILTER_MAX_METHODS = 16;
static int const ACL_FILTER_MAX_SRC_IP = 128;
static int const ACL_FILTER_MAX_ARGV = 512;

struct src_ip_info_t {
  IpEndpoint start; ///< Minimum value in range.
  IpEndpoint end; ///< Maximum value in range.
  bool invert;      ///< Should we "invert" the meaning of this IP range ("not in range")

  void reset() {
    ink_zero(start);
    ink_zero(end);
    invert = false;
  }

  /// @return @c true if @a ip is inside @a this range.
  bool contains(IpEndpoint const& ip) {
    return ats_ip_addr_cmp(&start, &ip) <= 0 && ats_ip_addr_cmp(&ip, &end) <= 0;
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
  int filter_name_size;         // size of optional filter name
  char *filter_name;            // optional filter name
  unsigned int allow_flag:1,    // action allow deny
    method_valid:1,             // method valid for verification
    src_ip_valid:1,             // src_ip range valid
    active_queue_flag:1;        // filter is in active state (used by .useflt directive)

  // we need arguments as string array for directive processing
  int argc;                     // argument counter (only for filter defs)
  char *argv[ACL_FILTER_MAX_ARGV];      // argument strings (only for filter defs)

  // method
  int method_cnt;               // how many valid methods we have
  int method_array[ACL_FILTER_MAX_METHODS];     // any HTTP method (actually only WKSIDX from HTTP.cc)
  int method_idx[ACL_FILTER_MAX_METHODS];       // HTTP method index (actually method flag)

  // src_ip
  int src_ip_cnt;               // how many valid src_ip rules we have
  src_ip_info_t src_ip_array[ACL_FILTER_MAX_SRC_IP];
  acl_filter_rule();
  ~acl_filter_rule();
  int name(const char *_name = NULL);
  int add_argv(int _argc, char *_argv[]);
  void print(void);

  static acl_filter_rule *find_byname(acl_filter_rule *list, const char *name);
  static void delete_byname(acl_filter_rule **list, const char *name);
  static void requeue_in_active_list(acl_filter_rule **list, acl_filter_rule *rp);
  static void requeue_in_passive_list(acl_filter_rule **list, acl_filter_rule *rp);
};

#endif
