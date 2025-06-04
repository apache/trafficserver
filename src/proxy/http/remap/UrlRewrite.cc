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

#include "proxy/http/remap/UrlRewrite.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/ReverseProxy.h"
#include "tscore/Layout.h"
#include "tscore/Filenames.h"
#include "proxy/http/HttpSM.h"

#define modulePrefix "[ReverseProxy]"

namespace
{
DbgCtl dbg_ctl_url_rewrite_regex{"url_rewrite_regex"};
DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};

/**
  Determines where we are in a situation where a virtual path is
  being mapped to a server home page. If it is, we set a special flag
  instructing us to be on the lookout for the need to send a redirect
  to if the request URL is a object, opposed to a directory. We need
  the redirect for an object so that the browser is aware that it is
  real accessing a directory (albeit a virtual one).

*/
void
SetHomePageRedirectFlag(url_mapping *new_mapping, URL &new_to_url)
{
  auto from_path{new_mapping->fromURL.path_get()};
  auto to_path{new_to_url.path_get()};

  new_mapping->homePageRedirect = (!from_path.empty() && to_path.empty()) ? true : false;
}
} // end anonymous namespace

bool
UrlRewrite::get_acl_behavior_policy(ACLBehaviorPolicy &policy)
{
  int behavior_policy = 0;
  behavior_policy     = RecGetRecordInt("proxy.config.url_remap.acl_behavior_policy").value_or(0);
  switch (behavior_policy) {
  case 0:
    policy = ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY;
    break;
  case 1:
    policy = ACLBehaviorPolicy::ACL_BEHAVIOR_MODERN;
    break;
  default:
    Warning("unkown ACL Behavior Policy: %d", behavior_policy);
    return false;
  }
  return true;
}

bool
UrlRewrite::load()
{
  ats_scoped_str config_file_path;

  config_file_path = RecConfigReadConfigPath("proxy.config.url_remap.filename", ts::filename::REMAP);
  if (!config_file_path) {
    Warning("%s Unable to locate %s. No remappings in effect", modulePrefix, ts::filename::REMAP);
    return false;
  }

  this->ts_name = nullptr;
  if (auto rec_str{RecGetRecordStringAlloc("proxy.config.proxy_name")}; rec_str) {
    this->ts_name = ats_stringdup(rec_str);
  }
  if (this->ts_name == nullptr) {
    Warning("%s Unable to determine proxy name.  Incorrect redirects could be generated", modulePrefix);
    this->ts_name = ats_strdup("");
  }

  this->http_default_redirect_url = nullptr;
  if (auto rec_str{RecGetRecordStringAlloc("proxy.config.http.referer_default_redirect")}; rec_str) {
    this->http_default_redirect_url = ats_stringdup(rec_str);
  }
  if (this->http_default_redirect_url == nullptr) {
    Warning("%s Unable to determine default redirect url for \"referer\" filter.", modulePrefix);
    this->http_default_redirect_url = ats_strdup("http://www.apache.org");
  }

  reverse_proxy = RecGetRecordInt("proxy.config.reverse_proxy.enabled").value_or(0);

  /* Initialize the plugin factory */
  pluginFactory.setRuntimeDir(RecConfigReadRuntimeDir()).addSearchDir(RecConfigReadPluginDir());

  // This is "optional", and this configuration is not set by default.
  char buf[PATH_NAME_MAX];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.plugin.compiler_path", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    std::error_code ec;
    fs::path        compilerPath = fs::path(buf);
    fs::file_status status       = fs::status(compilerPath, ec);

    if (ec || !swoc::file::is_regular_file(status)) {
      Error("Configured plugin compiler path '%s' is not a regular file", buf);
      return false;
    } else {
      // This also adds the configuration directory (etc/trafficserver) to find Cripts etc.
      pluginFactory.setCompilerPath(compilerPath).addSearchDir(RecConfigReadConfigDir());
    }
  }

  /* Initialize the next hop strategy factory */
  std::string sf = RecConfigReadConfigPath("proxy.config.url_remap.strategies.filename", "strategies.yaml");
  Dbg(dbg_ctl_url_rewrite_regex, "strategyFactory file: %s", sf.c_str());
  strategyFactory = new NextHopStrategyFactory(sf.c_str());

  if (TS_SUCCESS == this->BuildTable(config_file_path)) {
    int n_rules = this->rule_count(); // Minimum # of rules to be considered a valid configuration.
    int required_rules;
    required_rules = RecGetRecordInt("proxy.config.url_remap.min_rules_required").value_or(0);
    if (n_rules >= required_rules) {
      _valid = true;
      if (dbg_ctl_url_rewrite.on()) {
        Print();
      }
    } else {
      Warning("%s %d rules defined but %d rules required, configuration is invalid.", modulePrefix, n_rules, required_rules);
    }
  } else {
    Warning("something failed during BuildTable() -- check your remap plugins!");
  }

  // ACL Matching Policy
  if (!get_acl_behavior_policy(_acl_behavior_policy)) {
    _valid = false;
  }

  return _valid;
}

UrlRewrite::~UrlRewrite()
{
  ats_free(this->ts_name);
  ats_free(this->http_default_redirect_url);

  DestroyStore(forward_mappings);
  DestroyStore(reverse_mappings);
  DestroyStore(permanent_redirects);
  DestroyStore(temporary_redirects);
  DestroyStore(forward_mappings_with_recv_port);
  _valid = false;

  /* Deactivate the factory when all SM are gone for sure. */
  pluginFactory.deactivate();
  delete strategyFactory;
}

/** Sets the reverse proxy flag. */
void
UrlRewrite::SetReverseFlag(int flag)
{
  reverse_proxy = flag;
  if (dbg_ctl_url_rewrite.on()) {
    Print();
  }
}

/** Deallocated a hash table and all the url_mappings in it. */
void
UrlRewrite::_destroyTable(std::unique_ptr<URLTable> &h_table)
{
  if (h_table) {
    for (auto &it : *h_table) {
      delete it.second;
    }
  }
}

/** Debugging Method. */
void
UrlRewrite::Print() const
{
  printf("URL Rewrite table with %d entries\n", num_rules_forward + num_rules_reverse + num_rules_redirect_temporary +
                                                  num_rules_redirect_permanent + num_rules_forward_with_recv_port);
  printf("  Reverse Proxy is %s\n", (reverse_proxy == 0) ? "Off" : "On");

  printf("  Forward Mapping Table with %d entries\n", num_rules_forward);
  PrintStore(forward_mappings);

  printf("  Reverse Mapping Table with %d entries\n", num_rules_reverse);
  PrintStore(reverse_mappings);

  printf("  Permanent Redirect Mapping Table with %d entries\n", num_rules_redirect_permanent);
  PrintStore(permanent_redirects);

  printf("  Temporary Redirect Mapping Table with %d entries\n", num_rules_redirect_temporary);
  PrintStore(temporary_redirects);

  printf("  Forward Mapping With Recv Port Table with %d entries\n", num_rules_forward_with_recv_port);
  PrintStore(forward_mappings_with_recv_port);

  if (http_default_redirect_url != nullptr) {
    printf("  Referer filter default redirect URL: \"%s\"\n", http_default_redirect_url);
  }
}

/** Debugging method. */
void
UrlRewrite::PrintStore(const MappingsStore &store) const
{
  if (store.hash_lookup) {
    for (auto &it : *store.hash_lookup) {
      it.second->Print();
    }
  }

  if (!store.regex_list.empty()) {
    printf("    Regex mappings:\n");
    forl_LL(RegexMapping, list_iter, store.regex_list)
    {
      list_iter->url_map->Print();
    }
  }
}

std::string
UrlRewrite::PrintRemapHits()
{
  std::string result;
  result += PrintRemapHitsStore(forward_mappings);
  result += PrintRemapHitsStore(reverse_mappings);
  result += PrintRemapHitsStore(permanent_redirects);
  result += PrintRemapHitsStore(temporary_redirects);
  result += PrintRemapHitsStore(forward_mappings_with_recv_port);

  if (result.size() > 2) {
    result.pop_back(); // remove the trailing \n
    result.pop_back(); // remove the trailing ,
    result = "{\"list\": [\n" + result + " \n]}";
  }

  return result;
}

/** Debugging method. */
std::string
UrlRewrite::PrintRemapHitsStore(MappingsStore &store)
{
  std::string result;
  if (store.hash_lookup) {
    for (auto &it : *store.hash_lookup) {
      result += it.second->PrintUrlMappingPathIndex();
    }
  }

  if (!store.regex_list.empty()) {
    forl_LL(RegexMapping, list_iter, store.regex_list)
    {
      result += list_iter->url_map->PrintRemapHitCount();
      result += ",\n";
    }
  }

  return result;
}

/**
  If a remapping is found, returns a pointer to it otherwise NULL is
  returned.

*/
url_mapping *
UrlRewrite::_tableLookup(std::unique_ptr<URLTable> &h_table, URL *request_url, int request_port, char *request_host,
                         int request_host_len)
{
  if (!h_table) {
    h_table.reset(new URLTable);
  }
  UrlMappingPathIndex *ht_entry  = nullptr;
  url_mapping         *um        = nullptr;
  int                  ht_result = 0;

  if (auto it = h_table->find(request_host); it != h_table->end()) {
    ht_result = 1;
    ht_entry  = it->second;
  }

  if (likely(ht_result && ht_entry)) {
    // for empty host don't do a normal search, get a mapping arbitrarily
    um = ht_entry->Search(request_url, request_port, request_host_len ? true : false);
  }
  return um;
}

// This is only used for redirects and reverse rules, and the homepageredirect flag
// can never be set. The end result is that request_url is modified per remap container.
void
url_rewrite_remap_request(const UrlMappingContainer &mapping_container, URL *request_url, int method)
{
  URL *map_to   = mapping_container.getToURL();
  URL *map_from = mapping_container.getFromURL();

  Dbg(dbg_ctl_url_rewrite, "%s: Remapping rule id: %d matched", __func__, mapping_container.getMapping()->map_id);

  request_url->host_set(map_to->host_get());
  request_url->port_set(map_to->port_get_raw());

  // With the CONNECT method, we have to avoid messing with the scheme and path, because it's not part of
  // the CONNECT request (only host and port is).
  if (HTTP_WKSIDX_CONNECT != method) {
    request_url->scheme_set(map_to->scheme_get());

    auto fromPathLen{static_cast<int>(map_from->path_get().length())};
    auto toPath{map_to->path_get()};
    auto toPathLen{static_cast<int>(toPath.length())};
    auto requestPath{request_url->path_get()};
    auto requestPathLen{static_cast<int>(requestPath.length())};

    // Should be +3, little extra padding won't hurt.
    char newPath[(requestPathLen - fromPathLen) + toPathLen + 8];
    int  newPathLen = 0;

    *newPath = 0;
    if (!toPath.empty()) {
      memcpy(newPath, toPath.data(), toPathLen);
      newPathLen += toPathLen;
    }

    // We might need to insert a trailing slash in the new portion of the path
    // if more will be added and none is present and one will be needed.
    if (!fromPathLen && requestPathLen && newPathLen && toPathLen && newPath[newPathLen - 1] != '/') {
      newPath[newPathLen] = '/';
      newPathLen++;
    }

    if (!requestPath.empty()) {
      // avoid adding another trailing slash if the requestPath already had one and so does the toPath
      if (requestPathLen < fromPathLen) {
        if (!toPath.empty() && requestPath[requestPathLen - 1] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      } else if (requestPathLen > fromPathLen) {
        if (!toPath.empty() && requestPath[fromPathLen] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      }

      // copy the end of the path past what has been mapped
      if ((requestPathLen - fromPathLen) > 0) {
        memcpy(newPath + newPathLen, requestPath.data() + fromPathLen, requestPathLen - fromPathLen);
        newPathLen += (requestPathLen - fromPathLen);
      }
    }

    // Skip any leading / in the path when setting the new URL path
    if (*newPath == '/') {
      request_url->path_set({newPath + 1, static_cast<std::string_view::size_type>(newPathLen - 1)});
    } else {
      request_url->path_set({newPath, static_cast<std::string_view::size_type>(newPathLen)});
    }
  }
}

/** Used to do the backwards lookups. */
#define N_URL_HEADERS 4
bool
UrlRewrite::ReverseMap(HTTPHdr *response_header)
{
  URL                    location_url;
  bool                   remap_found = false;
  char                  *new_loc_hdr;
  int                    new_loc_length;
  int                    i;
  const std::string_view url_headers[N_URL_HEADERS] = {
    static_cast<std::string_view>(MIME_FIELD_LOCATION),
    static_cast<std::string_view>(MIME_FIELD_CONTENT_LOCATION),
    "URI"sv,
    "Destination"sv,
  };

  if (unlikely(num_rules_reverse == 0)) {
    ink_assert(reverse_mappings.empty());
    return false;
  }

  for (i = 0; i < N_URL_HEADERS; ++i) {
    auto location_hdr{response_header->value_get(url_headers[i])};

    if (location_hdr.empty()) {
      continue;
    }

    location_url.create(nullptr);
    location_url.parse(location_hdr.data(), static_cast<int>(location_hdr.length()));

    auto host{location_url.host_get()};

    UrlMappingContainer reverse_mapping(response_header->m_heap);

    if (reverseMappingLookup(&location_url, location_url.port_get(), host.data(), static_cast<int>(host.length()),
                             reverse_mapping)) {
      if (i == 0) {
        remap_found = true;
      }
      url_rewrite_remap_request(reverse_mapping, &location_url);
      new_loc_hdr = location_url.string_get_ref(&new_loc_length);
      response_header->value_set(url_headers[i],
                                 std::string_view{new_loc_hdr, static_cast<std::string_view::size_type>(new_loc_length)});
    }

    location_url.destroy();
  }
  return remap_found;
}

/** Perform fast ACL filtering. */
void
UrlRewrite::PerformACLFiltering(HttpTransact::State *s, const url_mapping *const map)
{
  if (unlikely(!s || s->acl_filtering_performed || !s->client_connection_allowed)) {
    return;
  }

  s->acl_filtering_performed = true; // small protection against reverse mapping

  if (map->filter) {
    int method        = s->hdr_info.client_request.method_get_wksidx();
    int method_wksidx = (method != -1) ? (method - HTTP_WKSIDX_CONNECT) : -1;

    const IpEndpoint *src_addr;
    src_addr = &s->client_info.src_addr;

    s->client_connection_allowed = true; // Default is that we allow things unless some filter matches

    int rule_index = 0;
    for (acl_filter_rule *rp = map->filter; rp; rp = rp->next, ++rule_index) {
      bool method_matches = true;

      if (rp->method_restriction_enabled) {
        if (method_wksidx >= 0 && method_wksidx < HTTP_WKSIDX_METHODS_CNT) {
          method_matches = rp->standard_method_lookup[method_wksidx];
        } else if (!rp->nonstandard_methods.empty()) {
          method_matches = false;
        } else {
          auto method{s->hdr_info.client_request.method_get()};
          method_matches = rp->nonstandard_methods.count(std::string{method});
        }
      } else {
        // No method specified, therefore all match.
        method_matches = true;
      }

      bool ip_matches = true;
      if (ats_is_ip(src_addr)) {
        // Is there a @src_ip specified? If so, check it.
        if (rp->src_ip_valid) {
          bool src_ip_matches = false;
          for (int j = 0; j < rp->src_ip_cnt && !src_ip_matches; j++) {
            bool in_range = rp->src_ip_array[j].contains(*src_addr);
            if (rp->src_ip_array[j].invert) {
              if (!in_range) {
                src_ip_matches = true;
              }
            } else {
              if (in_range) {
                src_ip_matches = true;
              }
            }
          }
          Dbg(dbg_ctl_url_rewrite, "Checked the specified src_ip, result: %s", src_ip_matches ? "true" : "false");
          ip_matches &= src_ip_matches;
        }

        // Is there a @src_ip_category specified? If so, check it.
        if (ip_matches && rp->src_ip_category_valid) {
          bool category_ip_matches = false;
          for (int j = 0; j < rp->src_ip_category_cnt && !category_ip_matches; j++) {
            bool in_category = rp->src_ip_category_array[j].contains(*src_addr);
            if (rp->src_ip_category_array[j].invert) {
              if (!in_category) {
                category_ip_matches = true;
              }
            } else {
              if (in_category) {
                category_ip_matches = true;
              }
            }
          }
          Dbg(dbg_ctl_url_rewrite, "Checked the specified src_ip_category, result: %s", category_ip_matches ? "true" : "false");
          ip_matches &= category_ip_matches;
        }
      }

      // Is there an @in_ip specified? If so, check it.
      if (ip_matches && rp->in_ip_valid) {
        bool in_ip_matches = false;
        for (int j = 0; j < rp->in_ip_cnt && !in_ip_matches; j++) {
          IpEndpoint incoming_addr;
          incoming_addr.assign(s->state_machine->get_ua_txn()->get_netvc()->get_local_addr());
          if (dbg_ctl_url_rewrite.on()) {
            char buf1[128], buf2[128], buf3[128];
            ats_ip_ntop(incoming_addr, buf1, sizeof(buf1));
            rp->in_ip_array[j].start.toString(buf2, sizeof(buf2));
            rp->in_ip_array[j].end.toString(buf3, sizeof(buf3));
            Dbg(dbg_ctl_url_rewrite, "Trying to match incoming address %s in range %s - %s.", buf1, buf2, buf3);
          }
          bool in_range = rp->in_ip_array[j].contains(incoming_addr);
          if (rp->in_ip_array[j].invert) {
            if (!in_range) {
              in_ip_matches = true;
            }
          } else {
            if (in_range) {
              in_ip_matches = true;
            }
          }
        }
        Dbg(dbg_ctl_url_rewrite, "Checked the specified in_ip, result: %s", in_ip_matches ? "true" : "false");
        ip_matches &= in_ip_matches;
      }

      if (rp->internal) {
        ip_matches = s->state_machine->get_ua_txn()->get_netvc()->get_is_internal_request();
        Dbg(dbg_ctl_url_rewrite, "%s an internal request", ip_matches ? "matched" : "didn't match");
      }

      Dbg(dbg_ctl_url_rewrite, "%d: ACL filter %s rule matches by ip: %s, by method: %s", rule_index,
          (rp->allow_flag ? "allow" : "deny"), (ip_matches ? "true" : "false"), (method_matches ? "true" : "false"));

      if (_acl_behavior_policy == ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY) {
        s->skip_ip_allow_yaml = false;
        Dbg(dbg_ctl_url_rewrite, "Doing legacy filtering ip:%s method:%s", ip_matches ? "matched" : "didn't match",
            method_matches ? "matched" : "didn't match");
        bool match = ip_matches && method_matches;
        if (match && s->client_connection_allowed) { // make sure that a previous filter did not DENY
          Dbg(dbg_ctl_url_rewrite, "matched ACL filter rule, %s request", rp->allow_flag ? "allowing" : "denying");
          s->client_connection_allowed = rp->allow_flag ? true : false;
        } else {
          if (!s->client_connection_allowed) {
            Dbg(dbg_ctl_url_rewrite, "Previous ACL filter rule denied request, continuing to deny it");
          } else {
            Dbg(dbg_ctl_url_rewrite, "did NOT match ACL filter rule, %s request", rp->allow_flag ? "denying" : "allowing");
            s->client_connection_allowed = rp->allow_flag ? false : true;
          }
        }
      } else if (ip_matches) {
        // The rule matches. Handle the method according to the rule.
        if (method_matches) {
          // Did they specify allowing the listed methods, or denying them?
          Dbg(dbg_ctl_url_rewrite, "matched ACL filter rule, %s request", rp->allow_flag ? "allowing" : "denying");
          s->client_connection_allowed = rp->allow_flag;

          // Since both the IP and method match, this rule will be applied regardless of ACLMatchingPolicy and no need to process
          // other filters nor ip_allow.yaml rules
          s->skip_ip_allow_yaml = true;
          break;
        }

        // @action=add_allow and @action=add_deny behave the same for legacy and
        // modern behavior. The difference in behavior applies to @action=allow
        // and @action=deny. For these, in legacy mode they are synonyms for
        // @action=add_allow and @action=add_deny because that is how they
        // behaved pre-10.x.  For modern behavior, they behave like the
        // corresponding ip_allow actions.
        if (!rp->add_flag && _acl_behavior_policy == ACLBehaviorPolicy::ACL_BEHAVIOR_MODERN) {
          // Flipping the action for unspecified methods.
          Dbg(dbg_ctl_url_rewrite, "ACL rule matched on IP but not on method, action: %s, %s the request",
              rp->get_action_description(), (rp->allow_flag ? "denying" : "allowing"));
          s->client_connection_allowed = !rp->allow_flag;

          // Since IP match and configured policy is MATCH_ON_IP_ONLY, no need to process other filters nor ip_allow.yaml rules.
          s->skip_ip_allow_yaml = true;
          break;
        }
      }
    }
  } /* end of for(rp = map->filter;rp;rp = rp->next) */
}

/**
   Determines if a redirect is to occur and if so, figures out what the
   redirect is. This was plaguiarized from UrlRewrite::Remap. redirect_url
   ought to point to the new, mapped URL when the function exits.
*/
mapping_type
UrlRewrite::Remap_redirect(HTTPHdr *request_header, URL *redirect_url)
{
  URL         *request_url;
  mapping_type mappingType;
  bool         prt, trt; // existence of permanent and temporary redirect tables, respectively

  prt = (num_rules_redirect_permanent != 0);
  trt = (num_rules_redirect_temporary != 0);

  if (prt + trt == 0) {
    return mapping_type::NONE;
  }

  // Since are called before request validity checking
  //  occurs, make sure that we have both a valid request
  //  header and a valid URL
  //
  if (request_header == nullptr) {
    Dbg(dbg_ctl_url_rewrite, "request_header was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return mapping_type::NONE;
  }
  request_url = request_header->url_get();
  if (!request_url->valid()) {
    Dbg(dbg_ctl_url_rewrite, "request_url was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return mapping_type::NONE;
  }

  auto host{request_url->host_get()};
  auto request_port{request_url->port_get()};

  if (host.empty() && reverse_proxy != 0) { // Server request.  Use the host header to figure out where
                                            // it goes.  Host header parsing is same as in ::Remap
    auto host_hdr{request_header->value_get(static_cast<std::string_view>(MIME_FIELD_HOST))};

    const char *tmp = static_cast<const char *>(memchr(host_hdr.data(), ':', host_hdr.length()));

    int host_len;
    if (tmp == nullptr) {
      host_len = static_cast<int>(host_hdr.length());
    } else {
      host_len     = tmp - host_hdr.data();
      request_port = ink_atoi(tmp + 1, static_cast<int>(host_hdr.length()) - host_len);

      // If atoi fails, try the default for the
      //   protocol
      if (request_port == 0) {
        request_port = request_url->port_get();
      }
    }

    host = {host_hdr.data(), static_cast<std::string_view::size_type>(host_len)};
  }
  // Temporary Redirects have precedence over Permanent Redirects
  // the rationale behind this is that network administrators might
  // want quick redirects and not want to worry about all the existing
  // permanent rules
  mappingType = mapping_type::NONE;

  UrlMappingContainer redirect_mapping(request_header->m_heap);

  if (trt) {
    if (temporaryRedirectLookup(request_url, request_port, host.data(), static_cast<int>(host.length()), redirect_mapping)) {
      mappingType = mapping_type::TEMPORARY_REDIRECT;
    }
  }
  if ((mappingType == mapping_type::NONE) && prt) {
    if (permanentRedirectLookup(request_url, request_port, host.data(), static_cast<int>(host.length()), redirect_mapping)) {
      mappingType = mapping_type::PERMANENT_REDIRECT;
    }
  }

  if (mappingType != mapping_type::NONE) {
    ink_assert((mappingType == mapping_type::PERMANENT_REDIRECT) || (mappingType == mapping_type::TEMPORARY_REDIRECT));

    // Make a copy of the request url so that we can munge it
    //   for the redirect
    redirect_url->create(nullptr);
    redirect_url->copy(request_url);

    // Perform the actual URL rewrite
    url_rewrite_remap_request(redirect_mapping, redirect_url);

    return mappingType;
  }
  ink_assert(mappingType == mapping_type::NONE);

  return mapping_type::NONE;
}

bool
UrlRewrite::_addToStore(MappingsStore &store, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                        bool is_cur_mapping_regex, int &count)
{
  bool retval;

  new_mapping->setRank(count); // Use the mapping rules number count for rank
  new_mapping->setRemapKey();  // Used for remap hit stats
  if (is_cur_mapping_regex) {
    store.regex_list.enqueue(reg_map);
    retval = true;
  } else {
    retval = TableInsert(store.hash_lookup, new_mapping, src_host);
  }
  if (retval) {
    ++count;
  }
  return retval;
}

bool
UrlRewrite::InsertMapping(mapping_type maptype, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                          bool is_cur_mapping_regex)
{
  bool success = false;

  // Now add the mapping to appropriate container
  switch (maptype) {
  case mapping_type::FORWARD_MAP:
  case mapping_type::FORWARD_MAP_REFERER:
    success = _addToStore(forward_mappings, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_forward);
    if (success) {
      // @todo: is this applicable to regex mapping too?
      SetHomePageRedirectFlag(new_mapping, new_mapping->toURL);
    }
    break;
  case mapping_type::REVERSE_MAP:
    success = _addToStore(reverse_mappings, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_reverse);
    new_mapping->homePageRedirect = false;
    break;
  case mapping_type::PERMANENT_REDIRECT:
    success = _addToStore(permanent_redirects, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_redirect_permanent);
    break;
  case mapping_type::TEMPORARY_REDIRECT:
    success = _addToStore(temporary_redirects, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_redirect_temporary);
    break;
  case mapping_type::FORWARD_MAP_WITH_RECV_PORT:
    success = _addToStore(forward_mappings_with_recv_port, new_mapping, reg_map, src_host, is_cur_mapping_regex,
                          num_rules_forward_with_recv_port);
    break;
  default:
    // 'default' required to avoid compiler warning; unsupported map
    // type would have been dealt with much before this
    return false;
  }

  return success;
}

bool
UrlRewrite::InsertForwardMapping(mapping_type maptype, url_mapping *mapping, const char *src_host)
{
  bool success;

  if (maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT) {
    success = TableInsert(forward_mappings_with_recv_port.hash_lookup, mapping, src_host);
  } else {
    success = TableInsert(forward_mappings.hash_lookup, mapping, src_host);
  }

  if (success) {
    switch (maptype) {
    case mapping_type::FORWARD_MAP:
    case mapping_type::FORWARD_MAP_REFERER:
    case mapping_type::FORWARD_MAP_WITH_RECV_PORT:
      SetHomePageRedirectFlag(mapping, mapping->toURL);
      break;
    default:
      break;
    }

    (maptype != mapping_type::FORWARD_MAP_WITH_RECV_PORT) ? ++num_rules_forward : ++num_rules_forward_with_recv_port;
  }

  return success;
}

/**
  Reads the configuration file and creates a new hash table.

  @return zero on success and non-zero on failure.

*/
int
UrlRewrite::BuildTable(const char *path)
{
  ink_assert(forward_mappings.empty());
  ink_assert(reverse_mappings.empty());
  ink_assert(permanent_redirects.empty());
  ink_assert(temporary_redirects.empty());
  ink_assert(forward_mappings_with_recv_port.empty());
  ink_assert(num_rules_forward == 0);
  ink_assert(num_rules_reverse == 0);
  ink_assert(num_rules_redirect_permanent == 0);
  ink_assert(num_rules_redirect_temporary == 0);
  ink_assert(num_rules_forward_with_recv_port == 0);

  forward_mappings.hash_lookup.reset(new URLTable);
  reverse_mappings.hash_lookup.reset(new URLTable);
  permanent_redirects.hash_lookup.reset(new URLTable);
  temporary_redirects.hash_lookup.reset(new URLTable);
  forward_mappings_with_recv_port.hash_lookup.reset(new URLTable);

  if (!remap_parse_config(path, this)) {
    return TS_ERROR;
  }

  // Destroy unused tables
  if (num_rules_forward == 0) {
    forward_mappings.hash_lookup.reset(nullptr);
  } else {
    if (forward_mappings.hash_lookup->find("") != forward_mappings.hash_lookup->end()) {
      nohost_rules = 1;
    }
  }

  if (num_rules_reverse == 0) {
    reverse_mappings.hash_lookup.reset(nullptr);
  }

  if (num_rules_redirect_permanent == 0) {
    permanent_redirects.hash_lookup.reset(nullptr);
  }

  if (num_rules_redirect_temporary == 0) {
    temporary_redirects.hash_lookup.reset(nullptr);
  }

  if (num_rules_forward_with_recv_port == 0) {
    forward_mappings_with_recv_port.hash_lookup.reset(nullptr);
  }

  return TS_SUCCESS;
}

/**
  Inserts arg mapping in h_table with key src_host chaining the mapping
  of existing entries bound to src_host if necessary.

*/
bool
UrlRewrite::TableInsert(std::unique_ptr<URLTable> &h_table, url_mapping *mapping, const char *src_host)
{
  if (!h_table) {
    h_table.reset(new URLTable);
  }
  char                 src_host_tmp_buf[1];
  UrlMappingPathIndex *ht_contents;

  if (!src_host) {
    src_host            = &src_host_tmp_buf[0];
    src_host_tmp_buf[0] = 0;
  }
  // Insert the new_mapping into hash table
  if (auto it = h_table->find(src_host); it != h_table->end()) {
    ht_contents = it->second;
    // There is already a path index for this host
    if (it->second == nullptr) {
      // why should this happen?
      Warning("Found entry cannot be null!");
      return false;
    }
  } else {
    ht_contents = new UrlMappingPathIndex();
    h_table->emplace(src_host, ht_contents);
  }
  if (!ht_contents->Insert(mapping)) {
    // Trie::Insert only fails due to an attempt to add a duplicate entry.
    Warning("Could not insert new mapping: duplicated entry exists");
    return false;
  }
  return true;
}

/**  First looks up the hash table for "simple" mappings and then the
     regex mappings.  Only higher-ranked regex mappings are examined if
     a hash mapping is found; or else all regex mappings are examined

     Returns highest-ranked mapping on success, NULL on failure
*/
bool
UrlRewrite::_mappingLookup(MappingsStore &mappings, URL *request_url, int request_port, const char *request_host,
                           int request_host_len, UrlMappingContainer &mapping_container)
{
  char request_host_lower[TS_MAX_HOST_NAME_LEN];

  if (!request_host || !request_url || (request_host_len < 0) || (request_host_len >= TS_MAX_HOST_NAME_LEN)) {
    Dbg(dbg_ctl_url_rewrite, "Invalid arguments!");
    return false;
  }

  // lowercase
  for (int i = 0; i < request_host_len; ++i) {
    request_host_lower[i] = tolower(request_host[i]);
  }
  request_host_lower[request_host_len] = 0;

  bool         retval       = false;
  int          rank_ceiling = -1;
  url_mapping *mapping      = _tableLookup(mappings.hash_lookup, request_url, request_port, request_host_lower, request_host_len);
  if (mapping != nullptr) {
    rank_ceiling = mapping->getRank();
    Dbg(dbg_ctl_url_rewrite, "Found 'simple' mapping with rank %d", rank_ceiling);
    mapping_container.set(mapping);
    retval = true;
  }
  if (_regexMappingLookup(mappings.regex_list, request_url, request_port, request_host_lower, request_host_len, rank_ceiling,
                          mapping_container)) {
    Dbg(dbg_ctl_url_rewrite, "Using regex mapping with rank %d", (mapping_container.getMapping())->getRank());
    retval = true;
  }
  if (retval) {
    (mapping_container.getMapping())->incrementCount();
  }
  return retval;
}

// does not null terminate return string
int
UrlRewrite::_expandSubstitutions(size_t *matches_info, const RegexMapping *reg_map, const char *matched_string, char *dest_buf,
                                 int dest_buf_size)
{
  int cur_buf_size = 0;
  int token_start  = 0;
  int n_bytes_needed;
  int match_index;
  for (int i = 0; i < reg_map->n_substitutions; ++i) {
    // first copy preceding bytes
    n_bytes_needed = reg_map->substitution_markers[i] - token_start;
    if ((cur_buf_size + n_bytes_needed) > dest_buf_size) {
      goto lOverFlow;
    }
    memcpy(dest_buf + cur_buf_size, reg_map->to_url_host_template + token_start, n_bytes_needed);
    cur_buf_size += n_bytes_needed;

    // then copy the sub pattern match
    match_index    = reg_map->substitution_ids[i] * 2;
    n_bytes_needed = matches_info[match_index + 1] - matches_info[match_index];
    if ((cur_buf_size + n_bytes_needed) > dest_buf_size) {
      goto lOverFlow;
    }
    memcpy(dest_buf + cur_buf_size, matched_string + matches_info[match_index], n_bytes_needed);
    cur_buf_size += n_bytes_needed;

    token_start = reg_map->substitution_markers[i] + 2; // skip the place holder
  }

  // copy last few bytes (if any)
  if (token_start < reg_map->to_url_host_template_len) {
    n_bytes_needed = reg_map->to_url_host_template_len - token_start;
    if ((cur_buf_size + n_bytes_needed) > dest_buf_size) {
      goto lOverFlow;
    }
    memcpy(dest_buf + cur_buf_size, reg_map->to_url_host_template + token_start, n_bytes_needed);
    cur_buf_size += n_bytes_needed;
  }
  Dbg(dbg_ctl_url_rewrite_regex, "Expanded substitutions and returning string [%.*s] with length %d", cur_buf_size, dest_buf,
      cur_buf_size);
  return cur_buf_size;

lOverFlow:
  Warning("Overflow while expanding substitutions");
  return 0;
}

bool
UrlRewrite::_regexMappingLookup(RegexMappingList &regex_mappings, URL *request_url, int request_port, const char *request_host,
                                int request_host_len, int rank_ceiling, UrlMappingContainer &mapping_container)
{
  bool         retval = false;
  RegexMatches matches;

  if (rank_ceiling == -1) { // we will now look at all regex mappings
    rank_ceiling = INT_MAX;
    Dbg(dbg_ctl_url_rewrite_regex, "Going to match all regexes");
  } else {
    Dbg(dbg_ctl_url_rewrite_regex, "Going to match regexes with rank <= %d", rank_ceiling);
  }

  auto request_scheme{request_url->scheme_get()};

  // If the scheme is empty (e.g. because of a CONNECT method), guess it based on port
  // This is equivalent to the logic in UrlMappingPathIndex::_GetTrie().
  if (request_scheme.empty()) {
    request_scheme = std::string_view{80 ? URL_SCHEME_HTTP : URL_SCHEME_HTTPS};
  }

  // Loop over the entire linked list, or until we're satisfied
  forl_LL(RegexMapping, list_iter, regex_mappings)
  {
    int reg_map_rank = list_iter->url_map->getRank();

    if (reg_map_rank > rank_ceiling) {
      break;
    }

    if (auto reg_map_scheme{list_iter->url_map->fromURL.scheme_get()}; request_scheme != reg_map_scheme) {
      Dbg(dbg_ctl_url_rewrite_regex, "Skipping regex with rank %d as scheme does not match request scheme", reg_map_rank);
      continue;
    }

    if (list_iter->url_map->fromURL.port_get() != request_port) {
      Dbg(dbg_ctl_url_rewrite_regex,
          "Skipping regex with rank %d as regex map port does not match request port. "
          "regex map port: %d, request port %d",
          reg_map_rank, list_iter->url_map->fromURL.port_get(), request_port);
      continue;
    }

    auto request_path{request_url->path_get()};
    auto reg_map_path{list_iter->url_map->fromURL.path_get()};
    if ((request_path.length() < reg_map_path.length()) ||
        strncmp(reg_map_path.data(), request_path.data(), reg_map_path.length())) { // use the shorter path length here
      Dbg(dbg_ctl_url_rewrite_regex, "Skipping regex with rank %d as path does not cover request path", reg_map_rank);
      continue;
    }

    int match_result = list_iter->regular_expression.exec(std::string_view(request_host, request_host_len), matches);

    if (match_result > 0) {
      Dbg(dbg_ctl_url_rewrite_regex,
          "Request URL host [%.*s] matched regex in mapping of rank %d "
          "with %d possible substitutions",
          request_host_len, request_host, reg_map_rank, match_result);

      mapping_container.set(list_iter->url_map);

      char buf[4096];
      int  buf_len;

      // Expand substitutions in the host field from the stored template
      size_t *matches_info = matches.get_ovector_pointer();
      buf_len              = _expandSubstitutions(matches_info, list_iter, request_host, buf, sizeof(buf));
      URL *expanded_url    = mapping_container.createNewToURL();
      expanded_url->copy(&((list_iter->url_map)->toURL));
      expanded_url->host_set({buf, static_cast<std::string_view::size_type>(buf_len)});

      Dbg(dbg_ctl_url_rewrite_regex, "Expanded toURL to [%.*s]", expanded_url->length_get(), expanded_url->string_get_ref());
      retval = true;
      break;
    } else {
      Dbg(dbg_ctl_url_rewrite_regex, "Request URL host [%.*s] did NOT match regex in mapping of rank %d", request_host_len,
          request_host, reg_map_rank);
    }
  }

  return retval;
}

void
UrlRewrite::_destroyList(RegexMappingList &mappings)
{
  RegexMapping *list_iter;
  while ((list_iter = mappings.pop()) != nullptr) {
    delete list_iter->url_map;
    ats_free(list_iter->to_url_host_template);
    delete list_iter;
  }
  mappings.clear();
}
