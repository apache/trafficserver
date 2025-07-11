/** @file

  URL rewriting.

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

#include "iocore/eventsystem/Freer.h"
#include "proxy/http/remap/UrlMapping.h"
#include "proxy/http/remap/UrlMappingPathIndex.h"
#include "proxy/http/HttpTransact.h"
#include "tsutil/Regex.h"
#include "proxy/http/remap/PluginFactory.h"
#include "proxy/http/remap/NextHopStrategyFactory.h"
#include "proxy/http/remap/RemapConfig.h"

#include <memory>

#define URL_REMAP_FILTER_NONE         0x00000000
#define URL_REMAP_FILTER_REFERER      0x00000001 /* enable "referer" header validation */
#define URL_REMAP_FILTER_REDIRECT_FMT 0x00010000 /* enable redirect URL formatting */

/**

 * used for redirection, mapping, and reverse mapping
 **/
enum class mapping_type {
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
class UrlRewrite : public RefCountObjInHeap
{
public:
  using URLTable = std::unordered_map<std::string, UrlMappingPathIndex *>;
  UrlRewrite()   = default;
  ~UrlRewrite() override;

  /** Retrieve the configured ACL matching policy.
   *
   * @param[out] policy The configured ACL behavior policy.
   * @return @c true if the policy is configured to an appropriate value, @c
   * false if not.
   */
  static bool get_acl_behavior_policy(ACLBehaviorPolicy &policy);

  /** Load the configuration.
   *
   * This access data in librecords to obtain the information needed for loading the configuration.
   *
   * @return @c true if the instance state is valid, @c false if not.
   */
  bool load();

  /** Build the internal url write tables.
   *
   * @param path Path to configuration file.
   * @return 0 on success, non-zero error code on failure.
   */
  int BuildTable(const char *path);

  mapping_type Remap_redirect(HTTPHdr *request_header, URL *redirect_url);
  bool         ReverseMap(HTTPHdr *response_header);
  void         SetReverseFlag(int flag);
  void         Print() const;

  // The UrlRewrite object is-a RefCountObj, but this is a convenience to make it clear that we
  // don't delete() these objects directly, but via the release() method only.
  UrlRewrite *
  acquire()
  {
    this->refcount_inc();
    return this;
  }

  void
  release()
  {
    if (0 == this->refcount_dec()) {
      // Delete this on an ET_TASK thread, which avoids doing potentially slow things on an ET_NET thread.
      static DbgCtl dc{"url_rewrite"};
      Dbg(dc, "Deleting old configuration immediately");
      new_Deleter(this, 0);
    }
  }

  bool
  is_valid() const
  {
    return _valid;
  };

  /// @return  Number of rules defined.
  int
  rule_count() const
  {
    return num_rules_forward + num_rules_reverse + num_rules_redirect_permanent + num_rules_redirect_temporary +
           num_rules_forward_with_recv_port;
  }

  static constexpr int MAX_REGEX_SUBS = 10;

  struct RegexMapping {
    url_mapping *url_map;
    Regex        regular_expression;

    // we store the host-string-to-substitute here; if a match is found,
    // the substitutions are made and the resulting url is stored
    // directly in toURL's host field
    char *to_url_host_template;
    int   to_url_host_template_len;

    // stores the number of substitutions
    int n_substitutions;

    // these two together point to template string places where
    // substitutions need to be made and the matching substring
    // to use
    int substitution_markers[MAX_REGEX_SUBS];
    int substitution_ids[MAX_REGEX_SUBS];

    LINK(RegexMapping, link);
  };

  using RegexMappingList = Queue<RegexMapping>;

  struct MappingsStore {
    std::unique_ptr<URLTable> hash_lookup;
    RegexMappingList          regex_list;
    bool
    empty()
    {
      return ((hash_lookup == nullptr) && regex_list.empty());
    }
  };

  void        PerformACLFiltering(HttpTransact::State *s, const url_mapping *const mapping);
  void        PrintStore(const MappingsStore &store) const;
  std::string PrintRemapHits();
  std::string PrintRemapHitsStore(MappingsStore &store);

  void
  DestroyStore(MappingsStore &store)
  {
    _destroyTable(store.hash_lookup);
    _destroyList(store.regex_list);
  }

  bool InsertForwardMapping(mapping_type maptype, url_mapping *mapping, const char *src_host);
  bool InsertMapping(mapping_type maptype, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                     bool is_cur_mapping_regex);

  bool TableInsert(std::unique_ptr<URLTable> &h_table, url_mapping *mapping, const char *src_host);

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

  int nohost_rules  = 0;
  int reverse_proxy = 0;

  char *ts_name = nullptr; // Used to send redirects when no host info

  char *http_default_redirect_url        = nullptr; // Used if redirect in "referer" filtering was not defined properly
  int   num_rules_forward                = 0;
  int   num_rules_reverse                = 0;
  int   num_rules_redirect_permanent     = 0;
  int   num_rules_redirect_temporary     = 0;
  int   num_rules_forward_with_recv_port = 0;

  PluginFactory           pluginFactory;
  NextHopStrategyFactory *strategyFactory = nullptr;

private:
  bool              _valid               = false;
  ACLBehaviorPolicy _acl_behavior_policy = ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY;

  bool _mappingLookup(MappingsStore &mappings, URL *request_url, int request_port, const char *request_host, int request_host_len,
                      UrlMappingContainer &mapping_container);
  url_mapping *_tableLookup(std::unique_ptr<URLTable> &h_table, URL *request_url, int request_port, char *request_host,
                            int request_host_len);
  bool         _regexMappingLookup(RegexMappingList &regex_mappings, URL *request_url, int request_port, const char *request_host,
                                   int request_host_len, int rank_ceiling, UrlMappingContainer &mapping_container);
  int          _expandSubstitutions(size_t *matches_info, const RegexMapping *reg_map, const char *matched_string, char *dest_buf,
                                    int dest_buf_size);
  void         _destroyTable(std::unique_ptr<URLTable> &h_table);
  void         _destroyList(RegexMappingList &regexes);
  inline bool  _addToStore(MappingsStore &store, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                           bool is_cur_mapping_regex, int &count);
};

void url_rewrite_remap_request(const UrlMappingContainer &mapping_container, URL *request_url, int scheme = -1);
