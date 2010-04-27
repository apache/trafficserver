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

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif
#include <list>

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
  UrlRewrite(const char *file_var_in);
   ~UrlRewrite();
  int BuildTable();
  bool Remap(HttpTransact::State * s, HTTPHdr * request_header, char **redirect_url, char **orig_url,
             char *tag = NULL, unsigned int filter_mask = URL_REMAP_FILTER_NONE);
  mapping_type Remap_redirect(HTTPHdr * request_header, char **redirect_url, char **orig_url, char *tag = NULL);
  bool ReverseMap(HTTPHdr * response_header, char *tag = NULL);
  void SetReverseFlag(int flag);
  void SetPristineFlag(int flag);
  void Print();
  url_mapping_ext *forwardTableLookupExt(URL * request_url,
                                         int request_port, char *request_host, int host_len, char *tag = 0);
  url_mapping_ext *reverseTableLookupExt(URL * request_url,
                                         int request_port, char *request_host, int host_len, char *tag = 0);
//  private:

  static const int MAX_REGEX_SUBS = 10;

  struct RegexMapping
  {
    url_mapping *url_map;
    pcre *re;
    pcre_extra *re_extra;
    
    // we store the host-string-to-substitute here; if a match is found,
    // the substitutions are made and the resulting url is stored
    // directly in toURL's host field
    char *to_url_host_template; 
    int to_url_host_template_len;

    // stores the number of substitutions
    int n_substitutions;     
    
    // these two together point to template string places where
    // substitutions need to be made and the matching substring
    // to use
    int substitution_markers[MAX_REGEX_SUBS]; 
    int substitution_ids[MAX_REGEX_SUBS];
  };
  
  typedef std::list<RegexMapping> RegexMappingList;
  
  struct MappingsStore 
  {
    InkHashTable *hash_lookup;
    RegexMappingList regex_list;
    bool empty() 
    {
      return ((hash_lookup == NULL) && (regex_list.size() == 0));
    }
  };
  
  void PerformACLFiltering(HttpTransact::State * s, url_mapping * mapping);
  url_mapping *SetupPacMapping();       // manager proxy-autconfig mapping
  url_mapping *SetupBackdoorMapping();
  void PrintTable(InkHashTable * h_table);
  void DestroyStore(MappingsStore &store) 
  {
    _destroyTable(store.hash_lookup);
    _destroyList(store.regex_list);
  }
  bool TableInsert(InkHashTable *h_table, url_mapping *mapping, const char *src_host);

  MappingsStore forward_mappings;
  MappingsStore reverse_mappings;
  MappingsStore permanent_redirects;
  MappingsStore temporary_redirects;

  url_mapping *forwardMappingLookup(URL *request_url, int request_port, const char *request_host, 
                                    int request_host_len, char *tag = NULL) 
  {
    return _mappingLookup(forward_mappings, request_url, request_port, request_host, request_host_len, tag);
  }
  url_mapping *reverseMappingLookup(URL *request_url, int request_port, const char *request_host, 
                                    int request_host_len, char *tag = NULL) 
  {
    return _mappingLookup(reverse_mappings, request_url, request_port, request_host, request_host_len, tag);
  }
  url_mapping *permanentRedirectLookup(URL *request_url, int request_port, const char *request_host, 
                                       int request_host_len, char *tag = NULL) 
  {
    return _mappingLookup(permanent_redirects, request_url, request_port, request_host, request_host_len, tag);
  }
  url_mapping *temporaryRedirectLookup(URL *request_url, int request_port, const char *request_host, 
                                       int request_host_len, char *tag = NULL) 
  {
    return _mappingLookup(temporary_redirects, request_url, request_port, request_host, request_host_len, tag);
  }
  int DoRemap(HttpTransact::State * s, HTTPHdr * request_header, url_mapping * mapPtr,
              URL * request_url, char **redirect = NULL, host_hdr_info * hh_ptr = NULL);
  int UrlWhack(char *toWhack, int *origLength);
  void RemoveTrailingSlash(URL * url);

  // Moved this from util, since we need to associate the remap_plugin_info list with the url rewrite map.
  int load_remap_plugin(char *argv[], int argc, url_mapping * mp, char *errbuf, int errbufsize, int jump_to_argc,
                        int *plugin_found_at);

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

private:
  url_mapping *_mappingLookup(MappingsStore &mappings, URL *request_url,
                              int request_port, const char *request_host, int request_host_len, char *tag);
  url_mapping *_tableLookup(InkHashTable * h_table, URL * request_url,
                            int request_port, char *request_host, int request_host_len, char *tag);
  url_mapping *_regexMappingLookup(RegexMappingList &regex_mappings,
                                   URL * request_url, int request_port, const char *request_host, 
                                   int request_host_len, char *tag, int rank_ceiling);
  int _expandSubstitutions(int *matches_info, const RegexMapping &reg_map,
                           const char *matched_string, char *dest_buf, int dest_buf_size);
  bool _processRegexMappingConfig(const char *from_host_lower, url_mapping *new_mapping, RegexMapping &reg_map);
  void _destroyTable(InkHashTable *h_table);
  void _destroyList(RegexMappingList &regexes);
  inline bool _addToStore(MappingsStore &store, url_mapping *new_mapping, RegexMapping &reg_map,
                          char *src_host, bool is_cur_mapping_regex, int &count);

  static const int MAX_URL_STR_SIZE = 1024;
};

#endif
