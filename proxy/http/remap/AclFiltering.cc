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

#include "AclFiltering.h"
#include "Main.h"
#include "Error.h"
#include "HTTP.h"

// ===============================================================================
//                              acl_filter_rule
// ===============================================================================

void
acl_filter_rule::reset(void)
{
  int i;
  for (i = (argc = 0); i < ACL_FILTER_MAX_ARGV; i++) {
    argv[i] = (char *)ats_free_null(argv[i]);
  }
  method_restriction_enabled = false;
  for (i = 0; i < HTTP_WKSIDX_METHODS_CNT; i++) {
    standard_method_lookup[i] = false;
  }
  nonstandard_methods.clear();
  for (i = (src_ip_cnt = 0); i < ACL_FILTER_MAX_SRC_IP; i++) {
    src_ip_array[i].reset();
  }
  src_ip_valid = 0;
}

acl_filter_rule::acl_filter_rule():next(NULL), filter_name_size(0), filter_name(NULL), allow_flag(1),
src_ip_valid(0), active_queue_flag(0), argc(0)
{
  standard_method_lookup.resize(HTTP_WKSIDX_METHODS_CNT);
  ink_zero(argv);
  reset();
}

acl_filter_rule::~acl_filter_rule()
{
  reset();
  name();
}

int
acl_filter_rule::add_argv(int _argc, char *_argv[])
{
  int real_cnt = 0;
  if (likely(_argv)) {
    for (int i = 0; i < _argc && argc < ACL_FILTER_MAX_ARGV; i++) {
      if (likely(_argv[i] && (argv[argc] = ats_strdup(_argv[i])) != NULL)) {
        real_cnt++;
        argc++;
      }
    }
  }
  return real_cnt;
}

int
acl_filter_rule::name(const char *_name)
{
  filter_name_size = 0;
  filter_name = (char *)ats_free_null(filter_name);
  if (_name && _name[0] && (filter_name = ats_strdup(_name)) != NULL) {
    filter_name_size = strlen(filter_name);
  }
  return filter_name_size;
}

void
acl_filter_rule::print(void)
{
  int i;
  printf("-----------------------------------------------------------------------------------------\n");
  printf("Filter \"%s\" status: allow_flag=%d, src_ip_valid=%d, active_queue_flag=%d\n",
         filter_name ? filter_name : "<NONAME>", (int) allow_flag,
         (int) src_ip_valid, (int) active_queue_flag);
  printf("standard methods=");
  for (i = 0; i < HTTP_WKSIDX_METHODS_CNT; i++) {
    if (standard_method_lookup[i]) {
      printf("0x%x ", HTTP_WKSIDX_CONNECT + i);
    }
  }
  printf("nonstandard methods=");
  for (MethodMap::iterator iter = nonstandard_methods.begin(), end = nonstandard_methods.end(); iter != end; ++iter) {
    printf("%s ", iter->c_str());
  }
  printf("\n");
  printf("src_ip_cnt=%d\n", src_ip_cnt);
  for (i = 0; i < src_ip_cnt; i++) {
    ip_text_buffer b1, b2;
    printf("%s - %s"
      , ats_ip_ntop(&src_ip_array[i].start.sa, b1, sizeof(b1))
      , ats_ip_ntop(&src_ip_array[i].end.sa, b2, sizeof(b2))
    );
  }
  for (i = 0; i < argc; i++) {
    printf("argv[%d] = \"%s\"\n", i, argv[i]);
  }
}

acl_filter_rule *
acl_filter_rule::find_byname(acl_filter_rule *list, const char *_name)
{
  int _name_size = 0;
  acl_filter_rule *rp = 0;
  if (likely(list && _name && (_name_size = strlen(_name)) > 0)) {
    for (rp = list; rp; rp = rp->next) {
      if (rp->filter_name_size == _name_size && !strcasecmp(rp->filter_name, _name))
        break;
    }
  }
  return rp;
}

void
acl_filter_rule::delete_byname(acl_filter_rule **rpp, const char *_name)
{
  int _name_size = 0;
  acl_filter_rule *rp;
  if (likely(rpp && _name && (_name_size = strlen(_name)) > 0)) {
    for (; (rp = *rpp) != NULL; rpp = &rp->next) {
      if (rp->filter_name_size == _name_size && !strcasecmp(rp->filter_name, _name)) {
        *rpp = rp->next;
        delete rp;
        break;
      }
    }
  }
}

void
acl_filter_rule::requeue_in_active_list(acl_filter_rule **list, acl_filter_rule *rp)
{
  if (likely(list && rp)) {
    if (rp->active_queue_flag == 0) {
      acl_filter_rule *r, **rpp;
      for (rpp = list; ((r = *rpp) != NULL); rpp = &(r->next)) {
        if (r == rp) {
          *rpp = r->next;
          break;
        }
      }
      for (rpp = list; ((r = *rpp) != NULL); rpp = &(r->next)) {
        if (r->active_queue_flag == 0)
          break;
      }
      (*rpp = rp)->next = r;
      rp->active_queue_flag = 1;
    }
  }
}

void
acl_filter_rule::requeue_in_passive_list(acl_filter_rule **list, acl_filter_rule *rp)
{
  if (likely(list && rp)) {
    if (rp->active_queue_flag) {
      acl_filter_rule **rpp;
      for (rpp = list; *rpp; rpp = &((*rpp)->next)) {
        if (*rpp == rp) {
          *rpp = rp->next;
          break;
        }
      }
      for (rpp = list; *rpp; rpp = &((*rpp)->next));
      (*rpp = rp)->next = NULL;
      rp->active_queue_flag = 0;
    }
  }
}
