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
RemapProcessor::start(int num_threads)
{
  _ET_REMAP = eventProcessor.spawn_event_threads(num_threads);  //_ET_REMAP is a class member
  return 0;
}

/**
  Most of this comes from UrlRewrite::Remap(). Generally, all this does
  is set "map" to the appropriate entry from the global rewrite_table
  such that we will then have access to the correct url_mapping inside
  perform_remap.

*/
bool
RemapProcessor::setup_for_remap(HttpTransact::State * s)
{
  Debug("url_rewrite", "setting up for remap: %x", s);
  URL *request_url = NULL;
  url_mapping *map = NULL;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  char **redirect_url = &s->remap_redirect;
  char **orig_url = &s->unmapped_request_url;
  char *tag = NULL;
  const char *request_url_host;
  int request_url_host_len;
  bool proxy_request = false;

  s->reverse_proxy = rewrite_table->reverse_proxy;

  ink_assert(redirect_url != NULL);

  if (unlikely(rewrite_table->num_rules_forward == 0)) {
    ink_assert(rewrite_table->forward_mappings.empty());
    return false;
  }
  // Since we are called before request validity checking
  // occurs, make sure that we have both a valid request
  // header and a valid URL
  if (unlikely(!request_header || (request_url = request_header->url_get()) == NULL || !request_url->valid())) {
    Error("NULL or invalid request data");
    return false;
  }

  request_url_host = request_url->host_get(&request_url_host_len);
  if (request_url_host_len > 0 || s->reverse_proxy == false) {
    Debug("url_rewrite", "[lookup] attempting proxy lookup");
    // Proxy request.  Use the information from the URL on the
    //  request line.  (Note: we prefer the information in
    //  the request URL since some user-agents send broken
    //  host headers.)
    proxy_request = true;
    map =
      rewrite_table->forwardMappingLookup(request_url, request_url->port_get(), request_url_host,
                                          request_url_host_len, tag);
  } else {
    // Server request.  Use the host header to figure out where
    // it goes
    int host_len, host_hdr_len;
    const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_hdr_len);
    if (!host_hdr) {
      host_hdr = "";
      host_hdr_len = 0;
    }
    char *tmp = (char *) memchr(host_hdr, ':', host_hdr_len);
    int request_port;

    if (tmp == NULL) {
      host_len = host_hdr_len;
      // Get the default port from URL structure
      request_port = request_url->port_get();
    } else {
      host_len = tmp - host_hdr;
      request_port = ink_atoi(tmp + 1, host_hdr_len - host_len);

      // If atoi fails, try the default for the
      //   protocol
      if (request_port == 0) {
        request_port = request_url->port_get();
      }
    }

    Debug("url_rewrite", "[lookup] attempting normal lookup");
    map = rewrite_table->forwardMappingLookup(request_url, request_port, host_hdr, host_len, tag);

    // Save this information for later
    s->hh_info.host_len = host_len;
    s->hh_info.request_host = host_hdr;
    s->hh_info.request_port = request_port;

    // If no rules match, check empty host rules since
    //   they function as default rules for server requests
    if (map == NULL && rewrite_table->nohost_rules && *host_hdr != '\0') {
      Debug("url_rewrite", "[lookup] nothing matched");
      map = rewrite_table->forwardMappingLookup(request_url, 0, "", 0, tag);
    }

    if (map && orig_url) {
      // We need to insert the host so that we have an accurate URL
      if (proxy_request == false) {
        request_url->host_set(s->hh_info.request_host, s->hh_info.host_len);
        // Only set the port if we need to so default ports
        //  do show up in URLs
        if (request_url->port_get() != s->hh_info.request_port) {
          request_url->port_set(s->hh_info.request_port);
        }
      }
      *orig_url = request_url->string_get_ref(NULL);
    }
    // if (!map) {
    //   char * u = request_url->string_get( NULL );
    //   Debug("url_rewrite","RemapProcessor::setup_for_remap had map as NULL ==> ru:%s rp:%d h:%.*s t:%s", u , request_port, host_len,
    //        host_hdr, tag);
    //      if (u)
    //     xfree(u);
    // }
  }

  if (!map) {
    Debug("url_rewrite", "RemapProcessor::setup_for_remap had map as NULL");
  }

  s->url_map = map;

  return (map != NULL);
}

bool
RemapProcessor::finish_remap(HttpTransact::State * s)
{
  url_mapping *map = NULL;
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url = request_header->url_get();
  char **orig_url = &s->unmapped_request_url;
  char **redirect_url = &s->remap_redirect;
  const int host_buf_len = MAXDNAME + 12 + 1 + 1;
  char host_hdr_buf[host_buf_len], tmp_referer_buf[4096], tmp_redirect_buf[4096], tmp_buf[2048], *c;
  const char *remapped_host;
  int remapped_host_len, remapped_port, tmp;
  int from_len;
  bool remap_found = false;
  referer_info *ri;

  map = s->url_map;
  if (!map) {
    return false;
  }
  // Do fast ACL filtering (it is safe to check map here)
  rewrite_table->PerformACLFiltering(s, map);

  // Check referer filtering rules
  if ((s->filter_mask & URL_REMAP_FILTER_REFERER) != 0 && (ri = map->referer_list) != 0) {
    const char *referer_hdr = 0;
    int referer_len = 0;
    bool enabled_flag = map->optional_referer ? true : false;

    if (request_header->presence(MIME_PRESENCE_REFERER) &&
        (referer_hdr = request_header->value_get(MIME_FIELD_REFERER, MIME_LEN_REFERER, &referer_len)) != NULL) {
      if (referer_len >= (int) sizeof(tmp_referer_buf))
        referer_len = (int) (sizeof(tmp_referer_buf) - 1);
      memcpy(tmp_referer_buf, referer_hdr, referer_len);
      tmp_referer_buf[referer_len] = 0;
      for (enabled_flag = false; ri; ri = ri->next) {
        if (ri->any) {
          enabled_flag = true;
          if (!map->negative_referer)
            break;
        } else if (ri->regx_valid && (pcre_exec(ri->regx, NULL, tmp_referer_buf, referer_len, 0, 0, NULL, 0) != -1)) {
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
            c = 0;
            switch (rc->type) {
            case 's':
              c = rc->chunk_str;
              break;
            case 'r':
              c = (referer_len && referer_hdr) ? &tmp_referer_buf[0] : 0;
              break;
            case 'f':
            case 't':
              remapped_host =
                (rc->type == 'f') ? map->fromURL.string_get_buf(tmp_buf, (int) sizeof(tmp_buf),
                                                                &from_len) : map->toURL.string_get_buf(tmp_buf, (int)
                                                                                                       sizeof(tmp_buf),
                                                                                                       &from_len);
              if (remapped_host && from_len > 0) {
                c = &tmp_buf[0];
              }
              break;
            case 'o':
              c = *orig_url;
              break;
            };

            if (c && tmp < (int) (sizeof(tmp_redirect_buf) - 1)) {
              tmp += snprintf(&tmp_redirect_buf[tmp], sizeof(tmp_redirect_buf) - tmp, "%s", c);
            }
          }
          tmp_redirect_buf[sizeof(tmp_redirect_buf) - 1] = 0;
          *redirect_url = xstrdup(tmp_redirect_buf);
        }
      } else {
        *redirect_url = xstrdup(rewrite_table->http_default_redirect_url);
      }

      if (*redirect_url == NULL) {
        *redirect_url =
          xstrdup(map->filter_redirect_url ? map->filter_redirect_url : rewrite_table->http_default_redirect_url);
      }

      return false;
    }
  }

  remap_found = true;

  // We also need to rewrite the "Host:" header if it exists and
  //   pristine host hdr is not enabled
  int host_len;
  const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_len);
  if (request_url && host_hdr != NULL &&
      ((rewrite_table->pristine_host_hdr <= 0 && s->pristine_host_hdr <= 0) ||
       (rewrite_table->pristine_host_hdr > 0 && s->pristine_host_hdr == 0))) {
    remapped_host = request_url->host_get(&remapped_host_len);
    remapped_port = request_url->port_get_raw();

    // Debug code to print out old host header.  This was easier before
    //  the header conversion.  Now we have to copy to gain null
    //  termination for the Debug() call
    if (is_debug_tag_set("url_rewrite")) {
      int old_host_hdr_len;
      char *old_host_hdr = (char *) request_header->value_get(MIME_FIELD_HOST,
                                                              MIME_LEN_HOST,
                                                              &old_host_hdr_len);
      if (old_host_hdr) {
        old_host_hdr = xstrndup(old_host_hdr, old_host_hdr_len);
        Debug("url_rewrite", "Host Header before rewrite %s", old_host_hdr);
        xfree(old_host_hdr);
      }
    }
    //
    // Create the new host header field being careful that our
    //   temporary buffer has adequate length
    //
    if (host_buf_len > remapped_host_len) {
      tmp = remapped_host_len;
      memcpy(host_hdr_buf, remapped_host, remapped_host_len);
      if (remapped_port) {
        tmp += snprintf(host_hdr_buf + remapped_host_len, host_buf_len - remapped_host_len - 1, ":%d", remapped_port);
    }
    } else {
      tmp = host_buf_len;
    }

    // It is possible that the hostname is too long.  If it is punt,
    //   and remove the host header.  If it is too long the HostDB
    //   won't be able to resolve it and the request will not go
    //   through
    if (tmp >= host_buf_len) {
      request_header->field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
      Debug("url_rewrite", "Host Header too long after rewrite");
    } else {
      Debug("url_rewrite", "Host Header after rewrite %s", host_hdr_buf);
      request_header->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host_hdr_buf, tmp);
    }
  }

  return remap_found;
}

Action *
RemapProcessor::perform_remap(Continuation * cont, HttpTransact::State * s)
{
  Debug("url_rewrite", "Beginning RemapProcessor::perform_remap");
  HTTPHdr *request_header = &s->hdr_info.client_request;
  URL *request_url = request_header->url_get();
  url_mapping *map = s->url_map;
  host_hdr_info *hh_info = &(s->hh_info);

  if (!map) {
    Error("Could not find corresponding url_mapping for this transaction %x", s);
    Debug("url_rewrite", "Could not find corresponding url_mapping for this transaction");
    ink_debug_assert(!"this should never happen -- call setup_for_remap first");
    cont->handleEvent(EVENT_REMAP_ERROR, NULL);
    return ACTION_RESULT_DONE;
  }

  // EThread *t = cont->mutex->thread_holding;
  // RemapPlugins *plugins = THREAD_ALLOC_INIT(pluginAllocator, t);

  RemapPlugins *plugins = pluginAllocator.alloc();

  plugins->setMap(map);
  plugins->setRequestUrl(request_url);
  plugins->setRequestHeader(request_header);
  plugins->setState(s);
  plugins->setHostHeaderInfo(hh_info);

  if (!_use_separate_remap_thread) {    // lets not schedule anything on our thread group (_ET_REMAP), instead, just execute inline
    int ret = 0;
    do {
      ret = plugins->run_single_remap();
    } while (ret == 0);
    //THREAD_FREE(plugins, pluginAllocator, t);
    pluginAllocator.free(plugins);
    return ACTION_RESULT_DONE;
  } else {
    ink_debug_assert(cont->mutex->thread_holding == this_ethread());
    plugins->mutex = cont->mutex;
    plugins->action = cont;     //make sure the HTTP SM gets the callback
    SET_CONTINUATION_HANDLER(plugins, &RemapPlugins::run_remap);
    eventProcessor.schedule_imm(plugins, _ET_REMAP);
    return &plugins->action;
  }
}

RemapProcessor::RemapProcessor():_ET_REMAP(0), _use_separate_remap_thread(false)
{

}

RemapProcessor::~RemapProcessor()
{

}
