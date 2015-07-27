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

#include "ts/ink_config.h"
#include "UrlMapping.h"
#include "HttpTransact.h"
#include "ts/Regex.h"

#define URL_REMAP_FILTER_NONE 0x00000000
#define URL_REMAP_FILTER_REFERER 0x00000001      /* enable "referer" header validation */
#define URL_REMAP_FILTER_REDIRECT_FMT 0x00010000 /* enable redirect URL formatting */

struct BUILD_TABLE_INFO;

/**
 * used for redirection, mapping, and reverse mapping
**/
enum mapping_type {
  FORWARD_MAP,
  REVERSE_MAP,
  PERMANENT_REDIRECT,
  TEMPORARY_REDIRECT,
  FORWARD_MAP_REFERER,
  FORWARD_MAP_WITH_RECV_PORT,
  NONE
};

/**
 *
**/
class UrlRewrite
{
public:
  UrlRewrite();
  ~UrlRewrite();

  int BuildTable(const char *path);
  mapping_type Remap_redirect(HTTPHdr *request_header, URL *redirect_url);
  bool ReverseMap(HTTPHdr *response_header);
  void SetReverseFlag(int flag);
  void Print();
  bool
  is_valid() const
  {
    return _valid;
  };
  //  private:

  static const int MAX_REGEX_SUBS = 10;

  struct RegexMapping {
    url_mapping *url_map;
    Regex regular_expression;

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

    LINK(RegexMapping, link);
  };

  typedef Queue<RegexMapping> RegexMappingList;

  struct MappingsStore {
    InkHashTable *hash_lookup;
    RegexMappingList regex_list;
    bool
    empty()
    {
      return ((hash_lookup == NULL) && regex_list.empty());
    }
  };

  void PerformACLFiltering(HttpTransact::State *s, url_mapping *mapping);
  url_mapping *SetupBackdoorMapping();
  void PrintStore(MappingsStore &store);

  void
  DestroyStore(MappingsStore &store)
  {
    _destroyTable(store.hash_lookup);
    _destroyList(store.regex_list);
  }

  bool InsertForwardMapping(mapping_type maptype, url_mapping *mapping, const char *src_host);
  bool InsertMapping(mapping_type maptype, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                     bool is_cur_mapping_regex);

  bool TableInsert(InkHashTable *h_table, url_mapping *mapping, const char *src_host);

  MappingsStore forward_mappings;
  MappingsStore reverse_mappings;
  MappingsStore permanent_redirects;
  MappingsStore temporary_redirects;
  MappingsStore forward_mappings_with_recv_port;

  bool
  forwardMappingLookup(URL *request_url, int request_port, const char *request_host, int request_host_len,
                       UrlMappingContainer &mapping_container)
  {
    return _mappingLookup(forward_mappings, request_url, request_port, request_host, request_host_len, mapping_container);
  }
  bool
  reverseMappingLookup(URL *request_url, int request_port, const char *request_host, int request_host_len,
                       UrlMappingContainer &mapping_container)
  {
    return _mappingLookup(reverse_mappings, request_url, request_port, request_host, request_host_len, mapping_container);
  }
  bool
  permanentRedirectLookup(URL *request_url, int request_port, const char *request_host, int request_host_len,
                          UrlMappingContainer &mapping_container)
  {
    return _mappingLookup(permanent_redirects, request_url, request_port, request_host, request_host_len, mapping_container);
  }
  bool
  temporaryRedirectLookup(URL *request_url, int request_port, const char *request_host, int request_host_len,
                          UrlMappingContainer &mapping_container)
  {
    return _mappingLookup(temporary_redirects, request_url, request_port, request_host, request_host_len, mapping_container);
  }
  bool
  forwardMappingWithRecvPortLookup(URL *request_url, int recv_port, const char *request_host, int request_host_len,
                                   UrlMappingContainer &mapping_container)
  {
    return _mappingLookup(forward_mappings_with_recv_port, request_url, recv_port, request_host, request_host_len,
                          mapping_container);
  }

  int nohost_rules;
  int reverse_proxy;

  // Vars for synthetic health checks
  int mgmt_synthetic_port;

  char *ts_name; // Used to send redirects when no host info

  char *http_default_redirect_url; // Used if redirect in "referer" filtering was not defined properly
  int num_rules_forward;
  int num_rules_reverse;
  int num_rules_redirect_permanent;
  int num_rules_redirect_temporary;
  int num_rules_forward_with_recv_port;

private:
  bool _valid;

  bool _mappingLookup(MappingsStore &mappings, URL *request_url, int request_port, const char *request_host, int request_host_len,
                      UrlMappingContainer &mapping_container);
  url_mapping *_tableLookup(InkHashTable *h_table, URL *request_url, int request_port, char *request_host, int request_host_len);
  bool _regexMappingLookup(RegexMappingList &regex_mappings, URL *request_url, int request_port, const char *request_host,
                           int request_host_len, int rank_ceiling, UrlMappingContainer &mapping_container);
  int _expandSubstitutions(int *matches_info, const RegexMapping *reg_map, const char *matched_string, char *dest_buf,
                           int dest_buf_size);
  void _destroyTable(InkHashTable *h_table);
  void _destroyList(RegexMappingList &regexes);
  inline bool _addToStore(MappingsStore &store, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                          bool is_cur_mapping_regex, int &count);
};

void url_rewrite_remap_request(const UrlMappingContainer &mapping_container, URL *request_url, int scheme = -1);

#endif
