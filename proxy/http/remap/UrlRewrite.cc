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

#include "UrlRewrite.h"
#include "ProxyConfig.h"
#include "ReverseProxy.h"
#include "RemapConfig.h"
#include "tscore/I_Layout.h"
#include "HttpSM.h"

#define modulePrefix "[ReverseProxy]"

/**
  Determines where we are in a situation where a virtual path is
  being mapped to a server home page. If it is, we set a special flag
  instructing us to be on the lookout for the need to send a redirect
  to if the request URL is a object, opposed to a directory. We need
  the redirect for an object so that the browser is aware that it is
  real accessing a directory (albeit a virtual one).

*/
static void
SetHomePageRedirectFlag(url_mapping *new_mapping, URL &new_to_url)
{
  int fromLen, toLen;
  const char *from_path = new_mapping->fromURL.path_get(&fromLen);
  const char *to_path   = new_to_url.path_get(&toLen);

  new_mapping->homePageRedirect = (from_path && !to_path) ? true : false;
}

//
// CTOR / DTOR for the UrlRewrite class.
//
UrlRewrite::UrlRewrite()
  : nohost_rules(0),
    reverse_proxy(0),
    ts_name(nullptr),
    http_default_redirect_url(nullptr),
    num_rules_forward(0),
    num_rules_reverse(0),
    num_rules_redirect_permanent(0),
    num_rules_redirect_temporary(0),
    num_rules_forward_with_recv_port(0),
    _valid(false)
{
  ats_scoped_str config_file_path;

  config_file_path = RecConfigReadConfigPath("proxy.config.url_remap.filename", "remap.config");
  if (!config_file_path) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to find proxy.config.url_remap.filename");
    Warning("%s Unable to locate remap.config. No remappings in effect", modulePrefix);
    return;
  }

  this->ts_name = nullptr;
  REC_ReadConfigStringAlloc(this->ts_name, "proxy.config.proxy_name");
  if (this->ts_name == nullptr) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to read proxy.config.proxy_name");
    Warning("%s Unable to determine proxy name.  Incorrect redirects could be generated", modulePrefix);
    this->ts_name = ats_strdup("");
  }

  this->http_default_redirect_url = nullptr;
  REC_ReadConfigStringAlloc(this->http_default_redirect_url, "proxy.config.http.referer_default_redirect");
  if (this->http_default_redirect_url == nullptr) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to read proxy.config.http.referer_default_redirect");
    Warning("%s Unable to determine default redirect url for \"referer\" filter.", modulePrefix);
    this->http_default_redirect_url = ats_strdup("http://www.apache.org");
  }

  REC_ReadConfigInteger(reverse_proxy, "proxy.config.reverse_proxy.enabled");

  if (0 == this->BuildTable(config_file_path)) {
    _valid = true;
    if (is_debug_tag_set("url_rewrite")) {
      Print();
    }
  } else {
    Warning("something failed during BuildTable() -- check your remap plugins!");
  }
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
}

/** Sets the reverse proxy flag. */
void
UrlRewrite::SetReverseFlag(int flag)
{
  reverse_proxy = flag;
  if (is_debug_tag_set("url_rewrite")) {
    Print();
  }
}

/**
  Allocaites via new, and adds a mapping like this map /ink/rh
  http://{backdoor}/ink/rh

  These {backdoor} things are then rewritten in a request-hdr hook.  (In the
  future it might make sense to move the rewriting into HttpSM directly.)

*/
url_mapping *
UrlRewrite::SetupBackdoorMapping()
{
  const char from_url[] = "/ink/rh";
  const char to_url[]   = "http://{backdoor}/ink/rh";

  url_mapping *mapping = new url_mapping;

  mapping->fromURL.create(nullptr);
  mapping->fromURL.parse(from_url, sizeof(from_url) - 1);
  mapping->fromURL.scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);

  mapping->toURL.create(nullptr);
  mapping->toURL.parse(to_url, sizeof(to_url) - 1);

  return mapping;
}

/** Deallocated a hash table and all the url_mappings in it. */
void
UrlRewrite::_destroyTable(std::unique_ptr<URLTable> &h_table)
{
  if (h_table) {
    for (auto it = h_table->begin(); it != h_table->end(); ++it) {
      delete it->second;
    }
  }
}

/** Debugging Method. */
void
UrlRewrite::Print()
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
UrlRewrite::PrintStore(MappingsStore &store)
{
  if (store.hash_lookup) {
    for (auto it = store.hash_lookup->begin(); it != store.hash_lookup->end(); ++it) {
      it->second->Print();
    }
  }

  if (!store.regex_list.empty()) {
    printf("    Regex mappings:\n");
    forl_LL(RegexMapping, list_iter, store.regex_list) { list_iter->url_map->Print(); }
  }
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
  UrlMappingPathIndex *ht_entry = nullptr;
  url_mapping *um               = nullptr;
  int ht_result                 = 0;

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
  const char *toHost;
  int toHostLen;

  toHost = map_to->host_get(&toHostLen);

  Debug("url_rewrite", "%s: Remapping rule id: %d matched", __func__, mapping_container.getMapping()->map_id);

  request_url->host_set(toHost, toHostLen);
  request_url->port_set(map_to->port_get_raw());

  // With the CONNECT method, we have to avoid messing with the scheme and path, because it's not part of
  // the CONNECT request (only host and port is).
  if (HTTP_WKSIDX_CONNECT != method) {
    const char *toScheme;
    int toSchemeLen;
    const char *requestPath;
    int requestPathLen = 0;
    int fromPathLen    = 0;
    const char *toPath;
    int toPathLen;

    toScheme = map_to->scheme_get(&toSchemeLen);
    request_url->scheme_set(toScheme, toSchemeLen);

    map_from->path_get(&fromPathLen);
    toPath      = map_to->path_get(&toPathLen);
    requestPath = request_url->path_get(&requestPathLen);

    // Should be +3, little extra padding won't hurt.
    char newPath[(requestPathLen - fromPathLen) + toPathLen + 8];
    int newPathLen = 0;

    *newPath = 0;
    if (toPath) {
      memcpy(newPath, toPath, toPathLen);
      newPathLen += toPathLen;
    }

    // We might need to insert a trailing slash in the new portion of the path
    // if more will be added and none is present and one will be needed.
    if (!fromPathLen && requestPathLen && newPathLen && toPathLen && *(newPath + newPathLen - 1) != '/') {
      *(newPath + newPathLen) = '/';
      newPathLen++;
    }

    if (requestPath) {
      // avoid adding another trailing slash if the requestPath already had one and so does the toPath
      if (requestPathLen < fromPathLen) {
        if (toPath && requestPath[requestPathLen - 1] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      } else {
        if (toPath && requestPath[fromPathLen] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      }

      // copy the end of the path past what has been mapped
      if ((requestPathLen - fromPathLen) > 0) {
        memcpy(newPath + newPathLen, requestPath + fromPathLen, requestPathLen - fromPathLen);
        newPathLen += (requestPathLen - fromPathLen);
      }
    }

    // Skip any leading / in the path when setting the new URL path
    if (*newPath == '/') {
      request_url->path_set(newPath + 1, newPathLen - 1);
    } else {
      request_url->path_set(newPath, newPathLen);
    }
  }
}

/** Used to do the backwards lookups. */
#define N_URL_HEADERS 4
bool
UrlRewrite::ReverseMap(HTTPHdr *response_header)
{
  const char *location_hdr;
  URL location_url;
  int loc_length;
  bool remap_found = false;
  const char *host;
  int host_len;
  char *new_loc_hdr;
  int new_loc_length;
  int i;
  const struct {
    const char *const field;
    const int len;
  } url_headers[N_URL_HEADERS] = {{MIME_FIELD_LOCATION, MIME_LEN_LOCATION},
                                  {MIME_FIELD_CONTENT_LOCATION, MIME_LEN_CONTENT_LOCATION},
                                  {"URI", 3},
                                  {"Destination", 11}};

  if (unlikely(num_rules_reverse == 0)) {
    ink_assert(reverse_mappings.empty());
    return false;
  }

  for (i = 0; i < N_URL_HEADERS; ++i) {
    location_hdr = response_header->value_get(url_headers[i].field, url_headers[i].len, &loc_length);

    if (location_hdr == nullptr) {
      continue;
    }

    location_url.create(nullptr);
    location_url.parse(location_hdr, loc_length);

    host = location_url.host_get(&host_len);

    UrlMappingContainer reverse_mapping(response_header->m_heap);

    if (reverseMappingLookup(&location_url, location_url.port_get(), host, host_len, reverse_mapping)) {
      if (i == 0) {
        remap_found = true;
      }
      url_rewrite_remap_request(reverse_mapping, &location_url);
      new_loc_hdr = location_url.string_get_ref(&new_loc_length);
      response_header->value_set(url_headers[i].field, url_headers[i].len, new_loc_hdr, new_loc_length);
    }

    location_url.destroy();
  }
  return remap_found;
}

/** Perform fast ACL filtering. */
void
UrlRewrite::PerformACLFiltering(HttpTransact::State *s, url_mapping *map)
{
  if (unlikely(!s || s->acl_filtering_performed || !s->client_connection_enabled)) {
    return;
  }

  s->acl_filtering_performed = true; // small protection against reverse mapping

  if (map->filter) {
    int method               = s->hdr_info.client_request.method_get_wksidx();
    int method_wksidx        = (method != -1) ? (method - HTTP_WKSIDX_CONNECT) : -1;
    bool client_enabled_flag = true;

    ink_release_assert(ats_is_ip(&s->client_info.src_addr));

    for (acl_filter_rule *rp = map->filter; rp && client_enabled_flag; rp = rp->next) {
      bool match = true;

      if (rp->method_restriction_enabled) {
        if (method_wksidx >= 0 && method_wksidx < HTTP_WKSIDX_METHODS_CNT) {
          match = rp->standard_method_lookup[method_wksidx];
        } else if (!rp->nonstandard_methods.empty()) {
          match = false;
        } else {
          int method_str_len;
          const char *method_str = s->hdr_info.client_request.method_get(&method_str_len);
          match                  = rp->nonstandard_methods.count(std::string(method_str, method_str_len));
        }
      }

      if (match && rp->src_ip_valid) {
        match = false;
        for (int j = 0; j < rp->src_ip_cnt && !match; j++) {
          bool in_range = rp->src_ip_array[j].contains(s->client_info.src_addr);
          if (rp->src_ip_array[j].invert) {
            if (!in_range) {
              match = true;
            }
          } else {
            if (in_range) {
              match = true;
            }
          }
        }
      }

      if (match && rp->in_ip_valid) {
        Debug("url_rewrite", "match was true and we have specified a in_ip field");
        match = false;
        for (int j = 0; j < rp->in_ip_cnt && !match; j++) {
          IpEndpoint incoming_addr;
          incoming_addr.assign(s->state_machine->ua_txn->get_netvc()->get_local_addr());
          if (is_debug_tag_set("url_rewrite")) {
            char buf1[128], buf2[128], buf3[128];
            ats_ip_ntop(incoming_addr, buf1, sizeof(buf1));
            rp->in_ip_array[j].start.toString(buf2, sizeof(buf2));
            rp->in_ip_array[j].end.toString(buf3, sizeof(buf3));
            Debug("url_rewrite", "Trying to match incoming address %s in range %s - %s.", buf1, buf2, buf3);
          }
          bool in_range = rp->in_ip_array[j].contains(incoming_addr);
          if (rp->in_ip_array[j].invert) {
            if (!in_range) {
              match = true;
            }
          } else {
            if (in_range) {
              match = true;
            }
          }
        }
      }

      if (rp->internal) {
        match = s->state_machine->ua_txn->get_netvc()->get_is_internal_request();
        Debug("url_rewrite", "%s an internal request", match ? "matched" : "didn't match");
      }

      if (match && client_enabled_flag) { // make sure that a previous filter did not DENY
        Debug("url_rewrite", "matched ACL filter rule, %s request", rp->allow_flag ? "allowing" : "denying");
        client_enabled_flag = rp->allow_flag ? true : false;
      } else {
        if (!client_enabled_flag) {
          Debug("url_rewrite", "Previous ACL filter rule denied request, continuing to deny it");
        } else {
          Debug("url_rewrite", "did NOT match ACL filter rule, %s request", rp->allow_flag ? "denying" : "allowing");
          client_enabled_flag = rp->allow_flag ? false : true;
        }
      }

    } /* end of for(rp = map->filter;rp;rp = rp->next) */

    s->client_connection_enabled = client_enabled_flag;
  }
}

/**
   Determines if a redirect is to occur and if so, figures out what the
   redirect is. This was plaguiarized from UrlRewrite::Remap. redirect_url
   ought to point to the new, mapped URL when the function exits.
*/
mapping_type
UrlRewrite::Remap_redirect(HTTPHdr *request_header, URL *redirect_url)
{
  URL *request_url;
  mapping_type mappingType;
  const char *host = nullptr;
  int host_len = 0, request_port = 0;
  bool prt, trt; // existence of permanent and temporary redirect tables, respectively

  prt = (num_rules_redirect_permanent != 0);
  trt = (num_rules_redirect_temporary != 0);

  if (prt + trt == 0) {
    return NONE;
  }

  // Since are called before request validity checking
  //  occurs, make sure that we have both a valid request
  //  header and a valid URL
  //
  if (request_header == nullptr) {
    Debug("url_rewrite", "request_header was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return NONE;
  }
  request_url = request_header->url_get();
  if (!request_url->valid()) {
    Debug("url_rewrite", "request_url was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return NONE;
  }

  host         = request_url->host_get(&host_len);
  request_port = request_url->port_get();

  if (host_len == 0 && reverse_proxy != 0) { // Server request.  Use the host header to figure out where
                                             // it goes.  Host header parsing is same as in ::Remap
    int host_hdr_len;
    const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_hdr_len);

    if (!host_hdr) {
      host_hdr     = "";
      host_hdr_len = 0;
    }

    const char *tmp = (const char *)memchr(host_hdr, ':', host_hdr_len);

    if (tmp == nullptr) {
      host_len = host_hdr_len;
    } else {
      host_len     = tmp - host_hdr;
      request_port = ink_atoi(tmp + 1, host_hdr_len - host_len);

      // If atoi fails, try the default for the
      //   protocol
      if (request_port == 0) {
        request_port = request_url->port_get();
      }
    }

    host = host_hdr;
  }
  // Temporary Redirects have precedence over Permanent Redirects
  // the rationale behind this is that network administrators might
  // want quick redirects and not want to worry about all the existing
  // permanent rules
  mappingType = NONE;

  UrlMappingContainer redirect_mapping(request_header->m_heap);

  if (trt) {
    if (temporaryRedirectLookup(request_url, request_port, host, host_len, redirect_mapping)) {
      mappingType = TEMPORARY_REDIRECT;
    }
  }
  if ((mappingType == NONE) && prt) {
    if (permanentRedirectLookup(request_url, request_port, host, host_len, redirect_mapping)) {
      mappingType = PERMANENT_REDIRECT;
    }
  }

  if (mappingType != NONE) {
    ink_assert((mappingType == PERMANENT_REDIRECT) || (mappingType == TEMPORARY_REDIRECT));

    // Make a copy of the request url so that we can munge it
    //   for the redirect
    redirect_url->create(nullptr);
    redirect_url->copy(request_url);

    // Perform the actual URL rewrite
    url_rewrite_remap_request(redirect_mapping, redirect_url);

    return mappingType;
  }
  ink_assert(mappingType == NONE);

  return NONE;
}

bool
UrlRewrite::_addToStore(MappingsStore &store, url_mapping *new_mapping, RegexMapping *reg_map, const char *src_host,
                        bool is_cur_mapping_regex, int &count)
{
  bool retval;

  new_mapping->setRank(count); // Use the mapping rules number count for rank
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
  case FORWARD_MAP:
  case FORWARD_MAP_REFERER:
    success = _addToStore(forward_mappings, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_forward);
    if (success) {
      // @todo: is this applicable to regex mapping too?
      SetHomePageRedirectFlag(new_mapping, new_mapping->toURL);
    }
    break;
  case REVERSE_MAP:
    success = _addToStore(reverse_mappings, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_reverse);
    new_mapping->homePageRedirect = false;
    break;
  case PERMANENT_REDIRECT:
    success = _addToStore(permanent_redirects, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_redirect_permanent);
    break;
  case TEMPORARY_REDIRECT:
    success = _addToStore(temporary_redirects, new_mapping, reg_map, src_host, is_cur_mapping_regex, num_rules_redirect_temporary);
    break;
  case FORWARD_MAP_WITH_RECV_PORT:
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

  if (maptype == FORWARD_MAP_WITH_RECV_PORT) {
    success = TableInsert(forward_mappings_with_recv_port.hash_lookup, mapping, src_host);
  } else {
    success = TableInsert(forward_mappings.hash_lookup, mapping, src_host);
  }

  if (success) {
    switch (maptype) {
    case FORWARD_MAP:
    case FORWARD_MAP_REFERER:
    case FORWARD_MAP_WITH_RECV_PORT:
      SetHomePageRedirectFlag(mapping, mapping->toURL);
      break;
    default:
      break;
    }

    (maptype != FORWARD_MAP_WITH_RECV_PORT) ? ++num_rules_forward : ++num_rules_forward_with_recv_port;
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
  BUILD_TABLE_INFO bti;

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
    // XXX handle file reload error
    return 3;
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

  return 0;
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
  char src_host_tmp_buf[1];
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
    Warning("Could not insert new mapping");
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
    Debug("url_rewrite", "Invalid arguments!");
    return false;
  }

  // lowercase
  for (int i = 0; i < request_host_len; ++i) {
    request_host_lower[i] = tolower(request_host[i]);
  }
  request_host_lower[request_host_len] = 0;

  bool retval          = false;
  int rank_ceiling     = -1;
  url_mapping *mapping = _tableLookup(mappings.hash_lookup, request_url, request_port, request_host_lower, request_host_len);
  if (mapping != nullptr) {
    rank_ceiling = mapping->getRank();
    Debug("url_rewrite", "Found 'simple' mapping with rank %d", rank_ceiling);
    mapping_container.set(mapping);
    retval = true;
  }
  if (_regexMappingLookup(mappings.regex_list, request_url, request_port, request_host_lower, request_host_len, rank_ceiling,
                          mapping_container)) {
    Debug("url_rewrite", "Using regex mapping with rank %d", (mapping_container.getMapping())->getRank());
    retval = true;
  }
  return retval;
}

// does not null terminate return string
int
UrlRewrite::_expandSubstitutions(int *matches_info, const RegexMapping *reg_map, const char *matched_string, char *dest_buf,
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
  Debug("url_rewrite_regex", "Expanded substitutions and returning string [%.*s] with length %d", cur_buf_size, dest_buf,
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
  bool retval = false;

  if (rank_ceiling == -1) { // we will now look at all regex mappings
    rank_ceiling = INT_MAX;
    Debug("url_rewrite_regex", "Going to match all regexes");
  } else {
    Debug("url_rewrite_regex", "Going to match regexes with rank <= %d", rank_ceiling);
  }

  int request_scheme_len, reg_map_scheme_len;
  const char *request_scheme = request_url->scheme_get(&request_scheme_len), *reg_map_scheme;

  int request_path_len, reg_map_path_len;
  const char *request_path = request_url->path_get(&request_path_len), *reg_map_path;

  // If the scheme is empty (e.g. because of a CONNECT method), guess it based on port
  // This is equivalent to the logic in UrlMappingPathIndex::_GetTrie().
  if (request_scheme_len == 0) {
    request_scheme     = request_port == 80 ? URL_SCHEME_HTTP : URL_SCHEME_HTTPS;
    request_scheme_len = hdrtoken_wks_to_length(request_scheme);
  }

  // Loop over the entire linked list, or until we're satisfied
  forl_LL(RegexMapping, list_iter, regex_mappings)
  {
    int reg_map_rank = list_iter->url_map->getRank();

    if (reg_map_rank > rank_ceiling) {
      break;
    }

    reg_map_scheme = list_iter->url_map->fromURL.scheme_get(&reg_map_scheme_len);
    if ((request_scheme_len != reg_map_scheme_len) || strncmp(request_scheme, reg_map_scheme, request_scheme_len)) {
      Debug("url_rewrite_regex", "Skipping regex with rank %d as scheme does not match request scheme", reg_map_rank);
      continue;
    }

    if (list_iter->url_map->fromURL.port_get() != request_port) {
      Debug("url_rewrite_regex",
            "Skipping regex with rank %d as regex map port does not match request port. "
            "regex map port: %d, request port %d",
            reg_map_rank, list_iter->url_map->fromURL.port_get(), request_port);
      continue;
    }

    reg_map_path = list_iter->url_map->fromURL.path_get(&reg_map_path_len);
    if ((request_path_len < reg_map_path_len) ||
        strncmp(reg_map_path, request_path, reg_map_path_len)) { // use the shorter path length here
      Debug("url_rewrite_regex", "Skipping regex with rank %d as path does not cover request path", reg_map_rank);
      continue;
    }

    int matches_info[MAX_REGEX_SUBS * 3];
    bool match_result =
      list_iter->regular_expression.exec(std::string_view(request_host, request_host_len), matches_info, countof(matches_info));

    if (match_result == true) {
      Debug("url_rewrite_regex",
            "Request URL host [%.*s] matched regex in mapping of rank %d "
            "with %d possible substitutions",
            request_host_len, request_host, reg_map_rank, match_result);

      mapping_container.set(list_iter->url_map);

      char buf[4096];
      int buf_len;

      // Expand substitutions in the host field from the stored template
      buf_len           = _expandSubstitutions(matches_info, list_iter, request_host, buf, sizeof(buf));
      URL *expanded_url = mapping_container.createNewToURL();
      expanded_url->copy(&((list_iter->url_map)->toURL));
      expanded_url->host_set(buf, buf_len);

      Debug("url_rewrite_regex", "Expanded toURL to [%.*s]", expanded_url->length_get(), expanded_url->string_get_ref());
      retval = true;
      break;
    } else {
      Debug("url_rewrite_regex", "Request URL host [%.*s] did NOT match regex in mapping of rank %d", request_host_len,
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
    if (list_iter->to_url_host_template) {
      ats_free(list_iter->to_url_host_template);
    }
    delete list_iter;
  }
  mappings.clear();
}
