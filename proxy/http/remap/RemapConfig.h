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

#ifndef REMAPCONFIG_H_E862FB4C_EFFC_4F2A_8BF2_9AB6E1E5E9CF
#define REMAPCONFIG_H_E862FB4C_EFFC_4F2A_8BF2_9AB6E1E5E9CF

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

  unsigned long remap_optflg;
  int paramc;
  int argc;
  char *paramv[BUILD_TABLE_MAX_ARGS];
  char *argv[BUILD_TABLE_MAX_ARGS];

  acl_filter_rule *rules_list; // all rules defined in config files as .define_filter foobar @src_ip=.....
  UrlRewrite *rewrite;         // Pointer to the UrlRewrite object we are parsing for.

  // Clear the argument vector.
  void reset();

private:
  BUILD_TABLE_INFO(const BUILD_TABLE_INFO &);            // disabled
  BUILD_TABLE_INFO &operator=(const BUILD_TABLE_INFO &); // disabled
};

const char *remap_parse_directive(BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize);

const char *remap_validate_filter_args(acl_filter_rule **rule_pp, const char **argv, int argc, char *errStrBuf,
                                       size_t errStrBufSize);

unsigned long remap_check_option(const char **argv, int argc, unsigned long findmode = 0, int *_ret_idx = NULL,
                                 const char **argptr = NULL);

bool remap_parse_config(const char *path, UrlRewrite *rewrite);

#endif /* REMAPCONFIG_H_E862FB4C_EFFC_4F2A_8BF2_9AB6E1E5E9CF */
