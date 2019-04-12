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

#include "AclFiltering.h"

class UrlRewrite;

#define BUILD_TABLE_MAX_ARGS 2048

// Remap inline options
#define REMAP_OPTFLG_MAP_WITH_REFERER 0x0001u /* "map_with_referer" option */
#define REMAP_OPTFLG_PLUGIN 0x0002u           /* "plugin=" option (per remap plugin) */
#define REMAP_OPTFLG_PPARAM 0x0004u           /* "pparam=" option (per remap plugin option) */
#define REMAP_OPTFLG_METHOD 0x0008u           /* "method=" option (used for ACL filtering) */
#define REMAP_OPTFLG_SRC_IP 0x0010u           /* "src_ip=" option (used for ACL filtering) */
#define REMAP_OPTFLG_ACTION 0x0020u           /* "action=" option (used for ACL filtering) */
#define REMAP_OPTFLG_INTERNAL 0x0040u         /* only allow internal requests to hit this remap */
#define REMAP_OPTFLG_IN_IP 0x0080u            /* "in_ip=" option (used for ACL filtering)*/
#define REMAP_OPTFLG_MAP_ID 0x0800u           /* associate a map ID with this rule */
#define REMAP_OPTFLG_INVERT 0x80000000u       /* "invert" the rule (for src_ip at least) */
#define REMAP_OPTFLG_ALL_FILTERS (REMAP_OPTFLG_METHOD | REMAP_OPTFLG_SRC_IP | REMAP_OPTFLG_ACTION | REMAP_OPTFLG_INTERNAL)

struct BUILD_TABLE_INFO {
  BUILD_TABLE_INFO();
  ~BUILD_TABLE_INFO();

  unsigned long remap_optflg = 0;
  int paramc                 = 0;
  int argc                   = 0;
  char *paramv[BUILD_TABLE_MAX_ARGS];
  char *argv[BUILD_TABLE_MAX_ARGS];

  bool ip_allow_check_enabled_p = true;
  bool accept_check_p           = true;
  acl_filter_rule *rules_list   = nullptr; // all rules defined in config files as .define_filter foobar @src_ip=.....
  UrlRewrite *rewrite           = nullptr; // Pointer to the UrlRewrite object we are parsing for.

  // Clear the argument vector.
  void reset();

  // noncopyable
  BUILD_TABLE_INFO(const BUILD_TABLE_INFO &) = delete;            // disabled
  BUILD_TABLE_INFO &operator=(const BUILD_TABLE_INFO &) = delete; // disabled
};

const char *remap_parse_directive(BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize);

const char *remap_validate_filter_args(acl_filter_rule **rule_pp, const char **argv, int argc, char *errStrBuf,
                                       size_t errStrBufSize);

unsigned long remap_check_option(const char **argv, int argc, unsigned long findmode = 0, int *_ret_idx = nullptr,
                                 const char **argptr = nullptr);

bool remap_parse_config(const char *path, UrlRewrite *rewrite);

typedef void (*load_remap_file_func)(const char *);

extern load_remap_file_func load_remap_file_cb;
