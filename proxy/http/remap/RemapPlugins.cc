/** @file

  Class to execute one (or more) remap plugin(s).

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

TSRemapStatus
RemapPlugins::run_plugin(remap_plugin_info *plugin)
{
  ink_assert(_s);

  TSRemapStatus plugin_retcode;
  TSRemapRequestInfo rri;
  url_mapping *map = _s->url_map.getMapping();
  URL *map_from    = _s->url_map.getFromURL();
  URL *map_to      = _s->url_map.getToURL();
  void *ih         = map->get_instance(_cur);

  // This is the equivalent of TSHttpTxnClientReqGet(), which every remap plugin would
  // have to call.
  rri.requestBufp = reinterpret_cast<TSMBuffer>(_request_header);
  rri.requestHdrp = reinterpret_cast<TSMLoc>(_request_header->m_http);

  // Read-only URL's (TSMLoc's to the SDK)
  rri.mapFromUrl = reinterpret_cast<TSMLoc>(map_from->m_url_impl);
  rri.mapToUrl   = reinterpret_cast<TSMLoc>(map_to->m_url_impl);
  rri.requestUrl = reinterpret_cast<TSMLoc>(_request_url->m_url_impl);

  rri.redirect = 0;

  // Prepare State for the future
  if (_cur == 0) {
    _s->fp_tsremap_os_response = plugin->fp_tsremap_os_response;
    _s->remap_plugin_instance  = ih;
  }

  plugin_retcode = plugin->fp_tsremap_do_remap(ih, reinterpret_cast<TSHttpTxn>(_s->state_machine), &rri);
  // TODO: Deal with negative return codes here
  if (plugin_retcode < 0) {
    plugin_retcode = TSREMAP_NO_REMAP;
  }

  // First step after plugin remap must be "redirect url" check
  if ((TSREMAP_DID_REMAP == plugin_retcode || TSREMAP_DID_REMAP_STOP == plugin_retcode) && rri.redirect) {
    _s->remap_redirect = _request_url->string_get(nullptr);
  }

  return plugin_retcode;
}

/**
  This is the equivalent of the old DoRemap().

  @return 1 when you are done doing crap (otherwise, you get re-called
    with schedule_imm and i hope you have something more to do), else
    0 if you have something more do do (this isnt strict and we check
    there actually *is* something to do).

*/
int
RemapPlugins::run_single_remap()
{
  url_mapping *map             = _s->url_map.getMapping();
  remap_plugin_info *plugin    = map->get_plugin(_cur); // get the nth plugin in our list of plugins
  TSRemapStatus plugin_retcode = TSREMAP_NO_REMAP;

  Debug("url_rewrite", "running single remap rule id %d for the %d%s time", map->map_id, _cur,
        _cur == 1 ? "st" : _cur == 2 ? "nd" : _cur == 3 ? "rd" : "th");

  // There might not be a plugin if we are a regular non-plugin map rule. In that case, we will fall through
  // and do the default mapping and then stop.
  if (plugin) {
    plugin_retcode = run_plugin(plugin);
  }

  _cur++;

  // If the plugin redirected, we need to end the remap chain now.
  if (_s->remap_redirect) {
    return 1;
  }

  if (TSREMAP_NO_REMAP == plugin_retcode || TSREMAP_NO_REMAP_STOP == plugin_retcode) {
    // After running the first plugin, rewrite the request URL. This is doing the default rewrite rule
    // to handle the case where no plugin ever rewrites.
    //
    // XXX we could probably optimize this a bit more by keeping a flag and only rewriting the request URL
    // if no plugin has rewritten it already.
    if (_cur == 1) {
      Debug("url_rewrite", "plugin did not change host, port or path, copying from mapping rule");
      url_rewrite_remap_request(_s->url_map, _request_url, _s->hdr_info.client_request.method_get_wksidx());
    }
  }

  if (TSREMAP_NO_REMAP_STOP == plugin_retcode || TSREMAP_DID_REMAP_STOP == plugin_retcode) {
    Debug("url_rewrite", "breaking remap plugin chain since last plugin said we should stop");
    return 1;
  }

  if (_cur > MAX_REMAP_PLUGIN_CHAIN) {
    Error("called %s more than %u times; stopping this remap insanity now", __func__, MAX_REMAP_PLUGIN_CHAIN);
    return 1;
  }

  if (_cur >= map->_plugin_count) {
    // Normally, we would callback into this function but we dont have anything more to do!
    Debug("url_rewrite", "completed all remap plugins for rule id %d", map->map_id);
    return 1;
  }

  Debug("url_rewrite", "completed single remap, attempting another via immediate callback");
  return 0;
}

int
RemapPlugins::run_remap(int event, Event *e)
{
  Debug("url_rewrite", "Inside RemapPlugins::run_remap with cur = %d", _cur);

  ink_assert(action.continuation);
  ink_assert(action.continuation);

  int ret = 0;

  /* make sure we weren't cancelled */
  if (action.cancelled) {
    mutex.clear();
    pluginAllocator.free(this); // ugly
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
      action.continuation->handleEvent(EVENT_REMAP_COMPLETE, nullptr);
      mutex.clear();
      action.mutex.clear();
      mutex        = nullptr;
      action.mutex = nullptr;
      // THREAD_FREE(this, pluginAllocator, t);
      pluginAllocator.free(this); // ugly
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
