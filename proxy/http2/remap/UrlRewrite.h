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

#ifndef _URL_REWRITE_H_
#define _URL_REWRITE_H_

#include "StringHash.h"
#include "UrlMapping.h"
#include "UmsHelper.h"

#define URL_REMAP_FILTER_NONE         0x00000000
#define URL_REMAP_FILTER_REFERER      0x00000001        /* enable "referer" header validation */
#define URL_REMAP_FILTER_REDIRECT_FMT 0x00010000        /* enable redirect URL formatting */
#define REVERSE_RegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc
#define REVERSE_ReadConfigInteger REC_ReadConfigInteger
#define REVERSE_ReadConfigStringAlloc REC_ReadConfigStringAlloc

#define modulePrefix "[ReverseProxy]"
#define tsname_var "proxy.config.proxy_name"
#define rewrite_var "proxy.config.url_remap.filename"
#define reverse_var "proxy.config.reverse_proxy.enabled"
#define ac_port_var "proxy.config.admin.autoconf_port"
#define default_to_pac_var  "proxy.config.url_remap.default_to_server_pac"
#define default_to_pac_port_var  "proxy.config.url_remap.default_to_server_pac_port"
#define pristine_hdr_var  "proxy.config.url_remap.pristine_host_hdr"
#define url_remap_mode_var  "proxy.config.url_remap.url_remap_mode"
#define backdoor_var  "proxy.config.url_remap.handle_backdoor_urls"
#define http_default_redirect_var "proxy.config.http.referer_default_redirect"
#define BUILD_TABLE_MAX_ARGS 2048

/**
 * 
**/
typedef struct s_build_table_info
{
  unsigned long remap_optflg;
  int paramc;
  int argc;
  char *paramv[BUILD_TABLE_MAX_ARGS];
  char *argv[BUILD_TABLE_MAX_ARGS];
  acl_filter_rule *rules_list;  // all rules defined in config files as .define_filter foobar @src_ip=.....
} BUILD_TABLE_INFO;

/**
 * used for redirection, mapping, and reverse mapping
**/
enum mapping_type
{ FORWARD_MAP, REVERSE_MAP, PERMANENT_REDIRECT, TEMPORARY_REDIRECT, FORWARD_MAP_REFERER, NONE };

/**
 * 
**/
class UrlRewrite
{
public:
  UrlRewrite(char *file_var_in);
   ~UrlRewrite();
  int BuildTable();
  void CreateLookupHelper(InkHashTable * h_table);
  bool Remap(HttpTransact::State * s, HTTPHdr * request_header, char **redirect_url, char **orig_url,
             char *tag = NULL, unsigned int filter_mask = URL_REMAP_FILTER_NONE);
  mapping_type Remap_redirect(HTTPHdr * request_header, char **redirect_url, char **orig_url, char *tag = NULL);
  bool ReverseMap(HTTPHdr * response_header, char *tag = NULL);
  void SetReverseFlag(int flag);
  void SetPristineFlag(int flag);
  void Print();
  url_mapping_ext *forwardTableLookupExt(URL * request_url,
                                         int request_port, const char *request_host, int host_len, char *tag = 0);
  url_mapping_ext *reverseTableLookupExt(URL * request_url,
                                         int request_port, const char *request_host, int host_len, char *tag = 0);
//  private:
  void PerformACLFiltering(HttpTransact::State * s, url_mapping * mapping);
  url_mapping *SetupPacMapping();       // manager proxy-autconfig mapping
  url_mapping *SetupBackdoorMapping();
  void PrintTable(InkHashTable * h_table);
  void DestroyTable(InkHashTable * h_table);
  void TableInsert(InkHashTable * h_table, url_mapping * mapping, char *src_host);
  url_mapping *TableLookup(InkHashTable * h_table, URL * request_url,
                           int request_port, const char *request_host, int host_len, char *tag = NULL);
  int DoRemap(HttpTransact::State * s, HTTPHdr * request_header, url_mapping * mapPtr,
              URL * request_url, char **redirect = NULL, host_hdr_info * hh_ptr = NULL);
  int UrlWhack(char *toWhack, int *origLength);
  void RemoveTrailingSlash(URL * url);

  // Moved this from util, since we need to associate the remap_plugin_info list with the url rewrite map.
  int load_remap_plugin(char *argv[], int argc, url_mapping * mp, char *errbuf, int errbufsize, int jump_to_argc,
                        int *plugin_found_at);

  InkHashTable *lookup_table;
  InkHashTable *reverse_table;
  InkHashTable *permanent_redirect_table;
  InkHashTable *temporary_redirect_table;
  int nohost_rules;
  int reverse_proxy;
  int pristine_host_hdr;
  int backdoor_enabled;

  // Vars for PAC mapping
  int mgmt_autoconf_port;
  int default_to_pac;
  int default_to_pac_port;

  char config_file_path[PATH_NAME_MAX];
  char *file_var;
  char *ts_name;                // Used to send redirects when no host info

  char *http_default_redirect_url;      // Used if redirect in "referer" filtering was not defined properly
  int num_rules_forward;
  int num_rules_reverse;
  int num_rules_redirect_permanent;
  int num_rules_redirect_temporary;
  remap_plugin_info *remap_pi_list;
};

#endif
