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
#include "ts/ink_platform.h"

#include "ts/apidefs.h"
#include "api/ts/remap.h"

#define TSREMAP_FUNCNAME_INIT "TSRemapInit"
#define TSREMAP_FUNCNAME_CONFIG_RELOAD "TSRemapConfigReload"
#define TSREMAP_FUNCNAME_DONE "TSRemapDone"
#define TSREMAP_FUNCNAME_NEW_INSTANCE "TSRemapNewInstance"
#define TSREMAP_FUNCNAME_DELETE_INSTANCE "TSRemapDeleteInstance"
#define TSREMAP_FUNCNAME_DO_REMAP "TSRemapDoRemap"
#define TSREMAP_FUNCNAME_OS_RESPONSE "TSRemapOSResponse"

class url_mapping;

/**
 *
 **/
class remap_plugin_info
{
public:
  typedef TSReturnCode _tsremap_init(TSRemapInterface *api_info, char *errbuf, int errbuf_size);
  typedef void _tsremap_config_reload();
  typedef void _tsremap_done(void);
  typedef TSReturnCode _tsremap_new_instance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size);
  typedef void _tsremap_delete_instance(void *);
  typedef TSRemapStatus _tsremap_do_remap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri);
  typedef void _tsremap_os_response(void *ih, TSHttpTxn rh, int os_response_type);

  remap_plugin_info *next;
  char *path;
  int path_size;
  void *dlh; /* "handle" for the dynamic library */
  _tsremap_init *fp_tsremap_init;
  _tsremap_config_reload *fp_tsremap_config_reload;
  _tsremap_done *fp_tsremap_done;
  _tsremap_new_instance *fp_tsremap_new_instance;
  _tsremap_delete_instance *fp_tsremap_delete_instance;
  _tsremap_do_remap *fp_tsremap_do_remap;
  _tsremap_os_response *fp_tsremap_os_response;

  remap_plugin_info(char *_path);
  ~remap_plugin_info();

  remap_plugin_info *find_by_path(char *_path);
  void add_to_list(remap_plugin_info *pi);
  void delete_my_list();
  void indicate_reload();
};

/**
 * struct host_hdr_info;
 * Used to store info about host header
 **/
struct host_hdr_info {
  const char *request_host;
  int host_len;
  int request_port;
};
