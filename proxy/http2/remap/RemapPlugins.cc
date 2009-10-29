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

#include "RemapPlugins.h"

ClassAllocator<RemapPlugins> pluginAllocator("RemapPluginsAlloc");

RemapPlugins::RemapPlugins():_cur(0)
{

}

RemapPlugins::~RemapPlugins()
{
  _cur = 0;
}

void
RemapPlugins::setMap(url_mapping * m)
{
  _map = m;
}

void
RemapPlugins::setRequestUrl(URL * u)
{
  _request_url = u;
}

void
RemapPlugins::setState(HttpTransact::State * state)
{
  _s = state;
}

void
RemapPlugins::setRequestHeader(HTTPHdr * h)
{
  _request_header = h;
}

void
RemapPlugins::setHostHeaderInfo(host_hdr_info * h)
{
  _hh_ptr = h;
}

int
RemapPlugins::run_plugin(remap_plugin_info * plugin, char *origURLBuf, int origURLBufSize,
                         bool * plugin_modified_host, bool * plugin_modified_port, bool * plugin_modified_path)
{
  int plugin_retcode;
  bool do_x_proto_check = true;
  TSRemapRequestInfo rri;
  const char *requestPath;
  int requestPathLen;
  int requestPort = 0;
  URL *map_from = &_map->fromURL;
  const char *fromPath;
  int fromPathLen;
  URL *map_to = &_map->toURL;

  const char *toHost;
  const char *toPath;
  int toPathLen;
  int toHostLen;

  const char *fromHost;
  int fromHostLen;

  requestPath = _request_url->path_get(&requestPathLen);
  requestPort = _request_url->port_get();

  if (_cur == 0) {
    fromPath = map_from->path_get(&fromPathLen);
    fromHost = map_from->host_get(&fromHostLen);
    toHost = map_to->host_get(&toHostLen);
    toPath = map_to->path_get(&toPathLen);
    rri.remap_from_port = map_from->port_get();
    rri.remap_to_port = map_to->port_get();
    rri.from_scheme = map_from->scheme_get(&rri.from_scheme_len);
    rri.to_scheme = map_to->scheme_get(&rri.to_scheme_len);
    rri.remap_from_host = map_from->host_get(&rri.remap_from_host_size);
  } else {                      //after the first plugin has run, we need to use these in order to "chain" them
    fromPath = _request_url->path_get(&fromPathLen);
    fromHost = _request_url->host_get(&fromHostLen);
    toHost = _request_url->host_get(&toHostLen);
    toPath = _request_url->path_get(&toPathLen);
    rri.remap_from_port = _request_url->port_get();
    rri.remap_to_port = _request_url->port_get();
    rri.from_scheme = _request_url->scheme_get(&rri.from_scheme_len);
    rri.to_scheme = _request_url->scheme_get(&rri.to_scheme_len);
    rri.remap_from_host = _request_url->host_get(&rri.remap_from_host_size);
  }

  // Get request port
  rri.request_port = requestPort;

  // Set request path
  rri.request_path = requestPath;
  rri.request_path_size = requestPathLen;

  // Get request query
  rri.request_query = _request_url->query_get(&rri.request_query_size);

  // Get toURL host
  rri.remap_to_host = toHost;
  rri.remap_to_host_size = toHostLen;

  // Set "remap from" path
  rri.remap_from_path = fromPath;
  rri.remap_from_path_size = fromPathLen;

  // Set "remap to" path        
  rri.remap_to_path = toPath;
  rri.remap_to_path_size = toPathLen;

  rri.size = sizeof(rri);
  rri.orig_url = origURLBuf;
  rri.orig_url_size = origURLBufSize;

  rri.require_ssl = -1;         // By default, the plugin will not modify the to_scheme

  // Used to see if plugin changed anything.
  rri.new_port = (rri.new_host_size = (rri.new_path_size = (rri.redirect_url_size = (rri.new_query_size = 0))));

  // Get request Host
  rri.request_host = _request_url->host_get(&rri.request_host_size);
  // Copy client IP address
  rri.client_ip = _s ? _s->client_info.ip : 0;

  // Get Cookie header
  if (_request_header->presence(MIME_PRESENCE_COOKIE)) {
    rri.request_cookie = _request_header->value_get(MIME_FIELD_COOKIE, MIME_LEN_COOKIE, &rri.request_cookie_size);
  } else {
    rri.request_cookie = NULL;
    rri.request_cookie_size = 0;
  }

  ihandle *ih = _map->get_instance(plugin);

  ink_debug_assert(ih);

  // Prepare State for the future
  if (_s && _cur == 0) {
    _s->fp_tsremap_os_response = plugin->fp_tsremap_os_response;
    _s->remap_plugin_instance = ih;
  }

  plugin_retcode = plugin->fp_tsremap_remap(*ih, _s ? (rhandle) (_s->state_machine) : NULL, &rri);

  // First step after plugin remap must be "redirect url" check
  if ( /*_s->remap_redirect && */ plugin_retcode && rri.redirect_url_size > 0) {
    if (rri.redirect_url[0]) {
      _s->remap_redirect = xstrndup(rri.redirect_url, rri.redirect_url_size);
    } else {
      _s->remap_redirect = xstrdup("http://www.apache.org");
    }
    return 1;
  }

  if (plugin_retcode) {
    // Modify the host
    if (rri.new_host_size > 0) {
      _request_url->host_set(rri.new_host, rri.new_host_size);
      *plugin_modified_host = true;
    }
    // Modify the port, but only if it's different than the request port
    if (rri.new_port != 0) {
      if (requestPort != rri.new_port) {
        _request_url->port_set(rri.new_port);
      }
      *plugin_modified_port = true;
    }
    // Modify the path
    if (rri.new_path_size != 0) {
      if (rri.new_path_size < 0)
        _request_url->path_set(NULL, 0);
      else
        _request_url->path_set(rri.new_path, rri.new_path_size);
      *plugin_modified_path = true;
    }
    // Finally, update the query string. This has a special case, where a negative
    // size value means to remove the query string entirely.
    if (rri.new_query_size != 0) {
      if (rri.new_query_size < 0)
        _request_url->query_set(NULL, 0);
      else
        _request_url->query_set(rri.new_query, rri.new_query_size);
    }
    // If require_ssl is set, make sure our toScheme is SSL'ified. It's done this way
    // to avoid dealing with weirness (say, a plugin trying to modify the "toScheme"
    // from FTP to HTTPS).
    if (rri.require_ssl != -1) {
      // TODO add other protocols here if/when necessary
      if (rri.require_ssl == 1) // Plugin wish to turn on SSL (if not already set)
      {
        if ((rri.to_scheme_len == URL_LEN_HTTP) && (rri.to_scheme == URL_SCHEME_HTTP)) {
          _request_url->scheme_set(URL_SCHEME_HTTPS, URL_LEN_HTTPS);
          do_x_proto_check = false;
          Debug("url_rewrite", "Plugin changed protocol from HTTP to HTTPS");
        }
      } else                    // Plugin wish to turn off SSL (if already set)
      {
        if ((rri.to_scheme_len == URL_LEN_HTTPS) && (rri.to_scheme == URL_SCHEME_HTTPS)) {
          _request_url->scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
          do_x_proto_check = false;
          Debug("url_rewrite", "Plugin changed protocol from HTTPS to HTTP");
        }
      }
    }
    // Check to see if this a cross protocol mapping
    //   If so we need to create a new URL since the URL*
    //   we get from the request_hdr really points to a subclass
    //   of URL that is specific to the protocol
    //
    if (do_x_proto_check && (rri.from_scheme != rri.to_scheme)) {
      _request_url->scheme_set(rri.to_scheme, rri.to_scheme_len);
      if (is_debug_tag_set("url_rewrite")) {
        char tmp_buf[2048];
        Debug("url_rewrite", "Cross protocol mapping to %s in plugin",
              _request_url->string_get_buf(tmp_buf, (int) sizeof(tmp_buf)));
      }
    }
  }

  return plugin_retcode;

}

/**
  This is the equivalent of the old DoRemap().

  @return 1 when you are done doing crap (otherwise, you get re-called
    with scheudle_imm and i hope you have something more to do), else
    0 if you have something more do do (this isnt strict and we check
    there actually *is* something to do).

*/
int
RemapPlugins::run_single_remap()
{
  Debug("url_rewrite", "Running single remap rule for the %d%s time", _cur, _cur == 1 ? "st" : _cur == 2 ? "nd" : _cur == 3 ? "rd" : "th"       //i should patent this
    );
  remap_plugin_info *plugin = NULL;

  bool plugin_modified_host = false;
  bool plugin_modified_port = false;
  bool plugin_modified_path = false;

  int plugin_retcode = 1;

  char *origURLBuf = NULL;
  int origURLBufSize;
  const char *requestPath;
  int requestPathLen;
  int requestPort;
  URL *map_from = &_map->fromURL;
  const char *fromPath;
  int fromPathLen;
  URL *map_to = &_map->toURL;

  const char *fromHost;
  int fromHostLen;

  const char *toHost;
  const char *toPath;
  int toPathLen;
  int toHostLen;
  int redirect_host_len;
  const char *redirect_host;

  // Debugging vars
  bool debug_on = false;
  int retcode = 0;              // 0 - no redirect, !=0 - redirected

  if (_cur == 0 && _s) {        // it is important - we must copy "no_negative_cache" flag before possible plugin call [only do this on our first iteration of this function]
    _s->no_negative_cache = _map->no_negative_cache;
    _s->pristine_host_hdr = _map->pristine_host_hdr;
    _s->remap_chunking_enabled = _map->chunking_enabled;
  }

  requestPath = _request_url->path_get(&requestPathLen);
  requestPort = _request_url->port_get();

  int fromPort = 0;
  int toPort = 0;

  if (_cur == 0) {
    fromPath = map_from->path_get(&fromPathLen);
    fromHost = map_from->host_get(&fromHostLen);
    toHost = map_to->host_get(&toHostLen);
    toPath = map_to->path_get(&toPathLen);
    fromPort = map_from->port_get();
    toPort = map_to->port_get();
  } else {                      //after the first plugin has run, we need to use these in order to "chain" them and previous changes are all in _request_url
    fromHost = _request_url->host_get(&fromHostLen);
    fromPath = _request_url->path_get(&fromPathLen);
    toHost = _request_url->host_get(&toHostLen);
    toPath = _request_url->path_get(&toPathLen);
    toPort = _request_url->port_get();
    fromPort = _request_url->port_get();
  }

  Debug("url_rewrite", "before remap plugin: from: scheme://%.*s:%d/%.*s  *to* scheme://%.*s:%d/%.*s",
        fromHostLen, fromHost, fromPort, fromPathLen, fromPath, toHostLen, toHost, toPort, toPathLen, toPath);

  if (is_debug_tag_set("url_rewrite"))
    debug_on = true;

  if (_request_header)
    plugin = _map->get_plugin(_cur);    //get the nth plugin in our list of plugins

  if (plugin || debug_on) {
    origURLBuf = _request_url->string_get(NULL);
    origURLBufSize = strlen(origURLBuf);
    Debug("url_rewrite", "string is: %s", origURLBuf);
  }

  if (plugin) {
    Debug("url_rewrite", "Remapping rule id: %d matched; running it now", _map->map_id);
    plugin_retcode =
      run_plugin(plugin, origURLBuf, origURLBufSize, &plugin_modified_host, &plugin_modified_port,
                 &plugin_modified_path);
  } else if (_cur > 0) {
    _cur++;
    Debug("url_rewrite",
          "Called into run_single_remap, but there wasnt a plugin available for us to run. Completing all remap processing immediately");
    if (origURLBuf)
      xfree(origURLBuf);
    return 1;
  }

  if ((!plugin && _cur == 0) || plugin_retcode == 0) {
    // Handle cross protocol mapping when there are no remap plugin(s) 
    // or if plugin did not make any modifications.
    Debug("url_rewrite", "no plugins available for this request");
    int to_len, from_len;
    const char *to_scheme = map_to->scheme_get(&to_len);

    if (to_scheme != map_from->scheme_get(&from_len)) {
      _request_url->scheme_set(to_scheme, to_len);
      if (is_debug_tag_set("url_rewrite")) {
        char tmp_buf[2048];
        Debug("url_rewrite", "Cross protocol mapping to %s",
              _request_url->string_get_buf(tmp_buf, (int) sizeof(tmp_buf)));
      }
    }
  }

  if (origURLBuf)
    xfree(origURLBuf);

  if (_s->remap_redirect) {     //if redirect was set, we need to use that.
    return 1;
  }

  if (_cur > 0 && !plugin_modified_host && !plugin_modified_port && !plugin_modified_path &&
      (_cur + 1) < _map->_plugin_count) {
    _cur++;
    Debug("url_rewrite", "Plugin didn't change anything, but we'll try the next one right now");
    return 0;                   //plugin didn't do anything for us, but maybe another will down the chain so lets assume there is something more for us to process
  }

  if (_cur > 0 && (_cur + 1) >= _map->_plugin_count) {  //skip the !plugin_modified_* stuff if we are on our 2nd plugin (or greater) and there's no more plugins
    goto done;
  }

  if (!plugin_modified_host && !plugin_modified_port && !plugin_modified_path) {
    Debug("url_rewrite", "plugin did not change host, port or path");
  }
  // Fall back to "remap" maps if plugin didn't change things already
  if (!plugin_modified_host) {
    _request_url->host_set(toHost, toHostLen);
  }

  if (!plugin_modified_port && (requestPort != map_to->port_get()))
    _request_url->port_set(map_to->port_get_raw());

  // Extra byte is potentially needed for prefix path '/'.
  // Added an extra 3 so that TS wouldn't crash in the field.
  // Allocate a large buffer to avoid problems.
  // Need to figure out why we need the 3 bytes or 512 bytes.
  if (!plugin_modified_path) {
    char newPathTmp[TSREMAP_RRI_MAX_PATH_SIZE];
    char *newPath;
    char *newPathAlloc = NULL;
    int newPathLen = 0;
    int newPathLenNeed = (requestPathLen - fromPathLen) + toPathLen + 512;

    if (newPathLenNeed > TSREMAP_RRI_MAX_PATH_SIZE) {
      newPath = (newPathAlloc = (char *) xmalloc(newPathLenNeed));
      if (debug_on) {
        memset(newPath, 0, newPathLenNeed);
      }
    } else {
      newPath = &newPathTmp[0];
      if (debug_on) {
        memset(newPath, 0, TSREMAP_RRI_MAX_PATH_SIZE);
      }
    }

    *newPath = 0;

    // Purify load run with QT in a reverse proxy indicated
    // a UMR/ABR/MSE in the line where we do a *newPath == '/' and the strncpy
    // that follows it.  The problem occurs if
    // requestPathLen,fromPathLen,toPathLen are all 0; in this case, we never
    // initialize newPath, but still de-ref it in *newPath == '/' comparison.
    // The memset fixes that problem.

    if (toPath) {
      memcpy(newPath, toPath, toPathLen);
      newPathLen += toPathLen;
    }
    // We might need to insert a trailing slash in the new portion of the path
    // if more will be added and none is present and one will be needed.
    if (!fromPathLen && requestPathLen && toPathLen && *(newPath + newPathLen - 1) != '/') {
      *(newPath + newPathLen) = '/';
      newPathLen++;
    }

    if (requestPath) {
      //avoid adding another trailing slash if the requestPath already had one and so does the toPath
      if (requestPathLen < fromPathLen) {
        if (toPathLen && requestPath[requestPathLen - 1] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      } else {
        if (toPathLen && requestPath[fromPathLen] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      }
      // copy the end of the path past what has been mapped
      if ((requestPathLen - fromPathLen) > 0) {
        // strncpy(newPath + newPathLen, requestPath + fromPathLen, requestPathLen - fromPathLen);
        memcpy(newPath + newPathLen, requestPath + fromPathLen, requestPathLen - fromPathLen);
        newPathLen += (requestPathLen - fromPathLen);
      }
    }
    // We need to remove the leading slash in newPath if one is
    // present.
    if (*newPath == '/') {
      memmove(newPath, newPath + 1, --newPathLen);
    }

    _request_url->path_set(newPath, newPathLen);

    if (_map->homePageRedirect && fromPathLen == requestPathLen && _s->remap_redirect) {
      URL redirect_url;
      redirect_url.create(NULL);
      redirect_url.copy(_request_url);

      ink_assert(fromPathLen > 0);

      // Extra byte for trailing '/' in redirect
      if (newPathLen > 0 && newPath[newPathLen - 1] != '/') {
        newPath[newPathLen] = '/';
        newPath[++newPathLen] = '\0';
        redirect_url.path_set(newPath, newPathLen);
      }
      // If we have host header information,
      //   put it back into redirect URL
      //
      if (_hh_ptr != NULL) {
        redirect_url.host_set(_hh_ptr->request_host, _hh_ptr->host_len);
        if (redirect_url.port_get() != _hh_ptr->request_port) {
          redirect_url.port_set(_hh_ptr->request_port);
        }
      }
      // If request came in without a host, send back
      //  the redirect with the name the proxy is known by
      if ((redirect_host = redirect_url.host_get(&redirect_host_len)) == NULL)
        redirect_url.host_set(rewrite_table->ts_name, strlen(rewrite_table->ts_name));

      if ((_s->remap_redirect = redirect_url.string_get(NULL)) != NULL)
        retcode = strlen(_s->remap_redirect);
      Debug("url_rewrite", "Redirected %.*s to %.*s", requestPathLen, requestPath, retcode, _s->remap_redirect);
      redirect_url.destroy();
    }

    if (unlikely(newPathAlloc))
      xfree(newPathAlloc);
  }

done:
  int requestHostLen = 0;
  const char *requestHost = _request_url->host_get(&requestHostLen);
  requestPort = _request_url->port_get();
  requestPath = _request_url->path_get(&requestPathLen);

  Debug("url_rewrite", "after remap plugin: request url now: scheme://%.*s:%d/%.*s",
        requestHostLen, requestHost, requestPort, requestPathLen, requestPath);

  if (_cur > 10) {
    Error("Are you serious?! Called run_single_remap more than 10 times. Stopping this remapping insanity now");
    Debug("url_rewrite",
          "Are you serious?! Called run_single_remap more than 10 times. Stopping this remapping insanity now");
    return 1;
  }

  _cur++;                       //important

  if (_cur >= _map->_plugin_count) {
    //normally, we would callback into this function but we dont have anything more to do!
    Debug("url_rewrite", "we exhausted all available plugins");
    return 1;
  } else {
    Debug("url_rewrite", "Completed single remap. Attempting another via immediate callback");
    return 0;
  }

  ink_debug_assert(!"not reached");
}

int
RemapPlugins::run_remap(int event, Event * e)
{
  Debug("url_rewrite", "Inside RemapPlugins::run_remap with cur = %d", _cur);

  ink_debug_assert(action.continuation);
  ink_assert(action.continuation);

  int ret = 0;

  //EThread * t = mutex->thread_holding;

  /* make sure we weren't cancelled */
  if (action.cancelled) {
    mutex.clear();
    //THREAD_FREE(this, pluginAllocator, t);
    pluginAllocator.free(this); //ugly
    return EVENT_DONE;
  }

  switch (event) {
  case EVENT_IMMEDIATE:
    Debug("url_rewrite", "handling immediate event inside RemapPlugins::run_remap");
    ret = run_single_remap();
                        /**
			 * If ret !=0 then we are done with this processor and we call back into the SM; 
			 * otherwise, we call this function again immediately (which really isn't immediate)
			 * thru the eventProcessor, thus forcing another run of run_single_remap() which will
			 * then operate on _request_url, etc performing additional remaps (mainly another plugin run)
			**/
    if (ret) {
      action.continuation->handleEvent(EVENT_REMAP_COMPLETE, NULL);
      mutex.clear();
      action.mutex.clear();
      mutex = NULL;
      action.mutex = NULL;
      //THREAD_FREE(this, pluginAllocator, t);
      pluginAllocator.free(this);       //ugly
      return EVENT_DONE;
    } else {
      e->schedule_imm(event);
      return EVENT_CONT;
    }

    break;
  default:
    ink_assert(!"unknown event type");
    break;
  };
  return EVENT_DONE;

}
