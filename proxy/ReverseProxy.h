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

/*****************************************************************************
 *
 *  ReverseProxy.h - Interface to code necessary for Reverse Proxy
 *                     (which mostly consists of general purpose
 *                       hostname substitution in URLs)
 *
 *
 ****************************************************************************/

#ifndef _REVERSE_PROXY_H_
#define _REVERSE_PROXY_H_

#include "P_RecProcess.h"

#include "ts/ink_hash_table.h"
#include "ts/ink_defs.h"
#include "HttpTransact.h"
#include "RemapPluginInfo.h"
#include "UrlRewrite.h"
#include "UrlMapping.h"

#define EMPTY_PORT_MAPPING (int32_t) ~0

class url_mapping;
struct host_hdr_info;

// Variables for the CDN URL Remapping Feature
extern int url_remap_mode;

extern UrlRewrite *rewrite_table;
extern remap_plugin_info *remap_pi_list;

// API Functions
int init_reverse_proxy();

// Both Return true if a remapping was made and false otherwise
// ebalsa@ Y! -- this happens in the remapProcessor now for the reverse proxy case (not CDN or BlindTunnel)
bool request_url_remap(HttpTransact::State *s, HTTPHdr *request_header, char **redirect_url,
                       unsigned int filter_mask = URL_REMAP_FILTER_NONE);

mapping_type request_url_remap_redirect(HTTPHdr *request_header, URL *redirect_url);
bool response_url_remap(HTTPHdr *response_header);

// Reload Functions
bool reloadUrlRewrite();

int url_rewrite_CB(const char *name, RecDataT data_type, RecData data, void *cookie);

#endif
