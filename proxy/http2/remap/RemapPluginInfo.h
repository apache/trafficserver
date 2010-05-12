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

#if !defined (_REMAPPLUGININFO_h_)
#define _REMAPPLUGININFO_h_
#include "inktomi++.h"
#include "api/ts/remap.h"

// Remap inline options
#define REMAP_OPTFLG_MAP_WITH_REFERER 0x01      /* "map_with_referer" option */
#define REMAP_OPTFLG_PLUGIN           0x02      /* "plugin=" option (per remap plugin) */
#define REMAP_OPTFLG_PPARAM           0x04      /* "pparam=" option (per remap plugin option) */
#define REMAP_OPTFLG_METHOD           0x08      /* "method=" option (used for ACL filtering) */
#define REMAP_OPTFLG_SRC_IP           0x10      /* "src_ip=" option (used for ACL filtering) */
#define REMAP_OPTFLG_ACTION           0x20      /* "action=" option (used for ACL filtering) */
#define REMAP_OPTFLG_NONEGCACHE       0x40      /* "no_negative_cache" option */
#define REMAP_OPTFLG_PRISTINEHOST_HDR_ENABLED 0x80      /* if pristinem header is enabled */
#define REMAP_OPTFLG_PRISTINEHOST_HDR_DISABLED 0x100    /*if pristine header is disabled */
#define REMAP_OPTFLG_CHUNKING_ENABLED 0x200     /* enable chunking for this map rule */
#define REMAP_OPTFLG_CHUNKING_DISABLED 0x400    /* disable chunking for this map rule */
#define REMAP_OPTFLG_MAP_ID          0x800      /* associate a map ID with this rule */
#define REMAP_OPTFLG_INVERT           0x80000000        /* "invert" the rule (for src_ip at least) */
#define REMAP_OPTFLG_ALL_FILTERS (REMAP_OPTFLG_METHOD|REMAP_OPTFLG_SRC_IP|REMAP_OPTFLG_ACTION)

class url_mapping;

/**
 * 
**/
class remap_plugin_info
{
public:
  static pthread_mutex_t global_list_mutex;

    Link<remap_plugin_info *>link;   //so i can queue these up

  remap_plugin_info *next;
  char *path;
  int path_size;
  void *dlh;                    /* "handle" for the dynamic library */
  _tsremap_init *fp_tsremap_init;
  _tsremap_done *fp_tsremap_done;
  _tsremap_new_instance *fptsremap_new_instance;
  _tsremap_delete_instance *fp_tsremap_delete_instance;
  _tsremap_remap *fp_tsremap_remap;
  _tsremap_os_response *fp_tsremap_os_response;

    remap_plugin_info(char *_path);
   ~remap_plugin_info();

  remap_plugin_info *find_by_path(char *_path);
  void add_to_list(remap_plugin_info * pi);
  void delete_my_list();
};


/**
 * 
**/
class remap_plugin_info_init
{
public:
  remap_plugin_info_init();
};


/**
 * struct host_hdr_info;
 * Used to store info about host header
**/
struct host_hdr_info
{
  const char *request_host;
  int host_len;
  int request_port;
};


#endif
