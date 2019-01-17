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

#include "RemapProcessor.h"

RemapProcessor remapProcessor;
extern ClassAllocator<RemapPlugins> pluginAllocator;

int
RemapProcessor::start(int num_threads, size_t stacksize)
{
  if (_use_separate_remap_thread) {
    ET_REMAP = eventProcessor.spawn_event_threads("ET_REMAP", num_threads, stacksize); // ET_REMAP is a class member
  }

  return 0;
}

/**
  Most of this comes from UrlRewrite::Remap(). Generally, all this does
  is set "map" to the appropriate entry from the HttpSM's leased m_remap
  such that we will then have access to the correct url_mapping inside
  perform_remap.

*/
bool
RemapProcessor::setup_for_remap(HttpTransact::State *s, UrlRewrite *table)
{
  Debug("url_rewrite", "setting up for remap: %p", s);
  URL *request_url        = nullptr;
  bool mapping_found      = false;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  char **redirect_url     = &s->remap_redirect;
  const char *request_host;
  int request_host_len;
  int request_port;
  bool proxy_request = false;

  s->reverse_proxy = table->reverse_proxy;
  s->url_map.set(s->hdr_info.client_request.m_heap);

  ink_assert(redirect_url != nullptr);

  if (unlikely((table->num_rules_forward == 0) && (table->num_rules_forward_with_recv_port == 0))) {
    ink_assert(table->forward_mappings.empty() && table->forward_mappings_with_recv_port.empty());
    Debug("url_rewrite", "[lookup] No forward mappings found; Skipping...");
    return false;
  }

  // Since we are called before request validity checking
  // occurs, make sure that we have both a valid request
  // header and a valid URL
  if (unlikely(!request_header || (request_url = request_header->url_get()) == nullptr || !request_url->valid())) {
    Error("NULL or invalid request data");
    return false;
  }

  request_host  = request_header->host_get(&request_host_len);
  request_port  = request_header->port_get();
  proxy_request = request_header->is_target_in_url() || !s->reverse_proxy;
  // Default to empty host.
  if (!request_host) {
    request_host     = "";
    request_host_len = 0;
  }

  Debug("url_rewrite", "[lookup] attempting %s lookup", proxy_request ? "proxy" : "normal");

  if (table->num_rules_forward_with_recv_port) {
    Debug("url_rewrite", "[lookup] forward mappings with recv port found; Using recv port %d", s->client_info.dst_addr.port());
    if (table->forwardMappingWithRecvPortLookup(request_url, s->client_info.dst_addr.port(), request_host, request_host_len,
                                                s->url_map)) {
      Debug("url_rewrite", "Found forward mapping with recv port");
      mapping_found = true;
    } else if (table->num_rules_forward == 0) {
      ink_assert(table->forward_mappings.empty());
      Debug("url_rewrite", "No forward mappings left");
      return false;
    }
  }

  if (!mapping_found) {
    mapping_found = table->forwardMappingLookup(request_url, request_port, request_host, request_host_len, s->url_map);
  }

  // If no rules match and we have a host, check empty host rules since
  // they function as default rules for server requests.
  // If there's no host, we've already done this.
  if (!mapping_found && table->nohost_rules && request_host_len) {
    Debug("url_rewrite", "[lookup] nothing matched");
    mapping_found = table->forwardMappingLookup(request_url, 0, "", 0, s->url_map);
  }

  if (!proxy_request) { // do extra checks on a server request

    // Save this information for later
    // @amc: why is this done only for requests without a host in the URL?
    s->hh_info.host_len     = request_host_len;
    s->hh_info.request_host = request_host;
    s->hh_info.request_port = request_port;

    if (mapping_found) {
      // Downstream mapping logic (e.g., self::finish_remap())
      // apparently assumes the presence of the target in the URL, so
      // we need to copy it. Perhaps it's because it's simpler to just
      // do the remap on the URL and then fix the field at the end.
      request_header->set_url_target_from_host_field();
    }
  }

  if (mapping_found) {
    request_header->mark_target_dirty();
  } else {
    Debug("url_rewrite", "RemapProcessor::setup_for_remap did not find a mapping");
  }

  return mapping_found;
}

bool
RemapProcessor::finish_remap(HttpTransact::State *s, UrlRewrite *table)
{
  url_mapping *map        = nullptr;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url        = request_header->url_get();
  char **redirect_url     = &s->remap_redirect;
  char host_hdr_buf[TS_MAX_HOST_NAME_LEN], tmp_referer_buf[4096], tmp_redirect_buf[4096], tmp_buf[2048], *c;
  const char *remapped_host;
  int remapped_host_len, remapped_port, tmp;
  int from_len;
  bool remap_found = false;
  referer_info *ri;

  map = s->url_map.getMapping();
  if (!map) {
    return false;
  }
  // Do fast ACL filtering (it is safe to check map here)
  table->PerformACLFiltering(s, map);

  // Check referer filtering rules
  if ((s->filter_mask & URL_REMAP_FILTER_REFERER) != 0 && (ri = map->referer_list) != nullptr) {
    const char *referer_hdr = nullptr;
    int referer_len         = 0;
    bool enabled_flag       = map->optional_referer ? true : false;

    if (request_header->presence(MIME_PRESENCE_REFERER) &&
        (referer_hdr = request_header->value_get(MIME_FIELD_REFERER, MIME_LEN_REFERER, &referer_len)) != nullptr) {
      if (referer_len >= (int)sizeof(tmp_referer_buf)) {
        referer_len = (int)(sizeof(tmp_referer_buf) - 1);
      }
      memcpy(tmp_referer_buf, referer_hdr, referer_len);
      tmp_referer_buf[referer_len] = 0;
      for (enabled_flag = false; ri; ri = ri->next) {
        if (ri->any) {
          enabled_flag = true;
          if (!map->negative_referer) {
            break;
          }
        } else if (ri->regx_valid && (pcre_exec(ri->regx, nullptr, tmp_referer_buf, referer_len, 0, 0, nullptr, 0) != -1)) {
          enabled_flag = ri->negative ? false : true;
          break;
        }
      }
    }

    if (!enabled_flag) {
      if (!map->default_redirect_url) {
        if ((s->filter_mask & URL_REMAP_FILTER_REDIRECT_FMT) != 0 && map->redir_chunk_list) {
          redirect_tag_str *rc;
          tmp_redirect_buf[(tmp = 0)] = 0;
          for (rc = map->redir_chunk_list; rc; rc = rc->next) {
            c = nullptr;
            switch (rc->type) {
            case 's':
              c = rc->chunk_str;
              break;
            case 'r':
              c = (referer_len && referer_hdr) ? &tmp_referer_buf[0] : nullptr;
              break;
            case 'f':
            case 't':
              remapped_host = (rc->type == 'f') ?
                                map->fromURL.string_get_buf(tmp_buf, (int)sizeof(tmp_buf), &from_len) :
                                ((s->url_map).getToURL())->string_get_buf(tmp_buf, (int)sizeof(tmp_buf), &from_len);
              if (remapped_host && from_len > 0) {
                c = &tmp_buf[0];
              }
              break;
            case 'o':
              c = s->unmapped_url.string_get_ref(nullptr);
              break;
            };

            if (c && tmp < (int)(sizeof(tmp_redirect_buf) - 1)) {
              tmp += snprintf(&tmp_redirect_buf[tmp], sizeof(tmp_redirect_buf) - tmp, "%s", c);
            }
          }
          tmp_redirect_buf[sizeof(tmp_redirect_buf) - 1] = 0;
          *redirect_url                                  = ats_strdup(tmp_redirect_buf);
        }
      } else {
        *redirect_url = ats_strdup(table->http_default_redirect_url);
      }

      if (*redirect_url == nullptr) {
        *redirect_url = ats_strdup(map->filter_redirect_url ? map->filter_redirect_url : table->http_default_redirect_url);
      }
      if (HTTP_STATUS_NONE == s->http_return_code) {
        s->http_return_code = HTTP_STATUS_MOVED_TEMPORARILY;
      }
      return false;
    }
  }

  remap_found = true;

  // We also need to rewrite the "Host:" header if it exists and
  //   pristine host hdr is not enabled
  int host_len;
  const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_len);

  if (request_url && host_hdr != nullptr && s->txn_conf->maintain_pristine_host_hdr == 0) {
    if (is_debug_tag_set("url_rewrite")) {
      int old_host_hdr_len;
      char *old_host_hdr = (char *)request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &old_host_hdr_len);
      if (old_host_hdr) {
        Debug("url_rewrite", "Host: Header before rewrite %.*s", old_host_hdr_len, old_host_hdr);
      }
    }
    //
    // Create the new host header field being careful that our
    //   temporary buffer has adequate length
    //
    remapped_host = request_url->host_get(&remapped_host_len);
    remapped_port = request_url->port_get_raw();

    if (TS_MAX_HOST_NAME_LEN > remapped_host_len) {
      tmp = remapped_host_len;
      memcpy(host_hdr_buf, remapped_host, remapped_host_len);
      if (remapped_port) {
        tmp += snprintf(host_hdr_buf + remapped_host_len, TS_MAX_HOST_NAME_LEN - remapped_host_len - 1, ":%d", remapped_port);
      }
    } else {
      tmp = TS_MAX_HOST_NAME_LEN;
    }

    // It is possible that the hostname is too long.  If it is punt,
    //   and remove the host header.  If it is too long the HostDB
    //   won't be able to resolve it and the request will not go
    //   through
    if (tmp >= TS_MAX_HOST_NAME_LEN) {
      request_header->field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
      Debug("url_rewrite", "Host: Header too long after rewrite");
    } else {
      Debug("url_rewrite", "Host: Header after rewrite %.*s", tmp, host_hdr_buf);
      request_header->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host_hdr_buf, tmp);
    }
  }

  request_header->mark_target_dirty();

  return remap_found;
}

Action *
RemapProcessor::perform_remap(Continuation *cont, HttpTransact::State *s)
{
  Debug("url_rewrite", "Beginning RemapProcessor::perform_remap");
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url        = request_header->url_get();
  url_mapping *map        = s->url_map.getMapping();
  host_hdr_info *hh_info  = &(s->hh_info);

  if (!map) {
    Error("Could not find corresponding url_mapping for this transaction %p", s);
    Debug("url_rewrite", "Could not find corresponding url_mapping for this transaction");
    ink_assert(!"this should never happen -- call setup_for_remap first");
    cont->handleEvent(EVENT_REMAP_ERROR, nullptr);
    return ACTION_RESULT_DONE;
  }

  if (_use_separate_remap_thread) {
    RemapPlugins *plugins = pluginAllocator.alloc();

    plugins->setState(s);
    plugins->setRequestUrl(request_url);
    plugins->setRequestHeader(request_header);
    plugins->setHostHeaderInfo(hh_info);

    // Execute "inline" if not using separate remap threads.
    ink_assert(cont->mutex->thread_holding == this_ethread());
    plugins->mutex  = cont->mutex;
    plugins->action = cont;
    SET_CONTINUATION_HANDLER(plugins, &RemapPlugins::run_remap);
    eventProcessor.schedule_imm(plugins, ET_REMAP);

    return &plugins->action;
  } else {
    RemapPlugins plugins(s, request_url, request_header, hh_info);

    while (!plugins.run_single_remap())
      ; // EMPTY

    return ACTION_RESULT_DONE;
  }
}
