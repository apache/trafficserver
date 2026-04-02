/** @file
 *
 *  Remap configuration file parsing.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "proxy/http/remap/AclFiltering.h"

class UrlRewrite;

enum class ACLBehaviorPolicy {
  ACL_BEHAVIOR_LEGACY = 0,
  ACL_BEHAVIOR_MODERN,
};

struct BUILD_TABLE_INFO {
  BUILD_TABLE_INFO();
  ~BUILD_TABLE_INFO();

  ACLBehaviorPolicy behavior_policy{ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY}; // Default 0.
  bool              ip_allow_check_enabled_p = true;
  bool              accept_check_p           = true;

  acl_filter_rule *rules_list = nullptr; // all rules defined in config files
  UrlRewrite      *rewrite    = nullptr; // Pointer to the UrlRewrite object we are parsing for.

  // Clear the argument vector.
  void reset();

  // Free acl_filter_rule in the list
  void clear_acl_rules_list();

  // noncopyable
  BUILD_TABLE_INFO(const BUILD_TABLE_INFO &)            = delete; // disabled
  BUILD_TABLE_INFO &operator=(const BUILD_TABLE_INFO &) = delete; // disabled
};

using load_remap_file_func = void (*)(const char *, const char *);

extern load_remap_file_func load_remap_file_cb;

// Helper functions shared between RemapConfig.cc and RemapYamlConfig.cc
bool        is_inkeylist(const char *key, ...);
void        free_directory_list(int n_entries, struct dirent **entrylist);
const char *is_valid_scheme(std::string_view fromScheme, std::string_view toScheme);
