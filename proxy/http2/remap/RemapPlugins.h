/** @file

  Declarations for the RemapPlugins class.

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

/**
 * Remap plugins class
**/

#if !defined (_REMAPPLUGINS_h_)
#define _REMAPPLUGINS_h_

#include "inktomi++.h"
#include "I_EventSystem.h"
#include "RemapProcessor.h"
#include "api/ts/remap.h"
#include "RemapPluginInfo.h"
#include "HttpTransact.h"
#include "ReverseProxy.h"

static const unsigned int MAX_REMAP_PLUGIN_CHAIN = 10;

/**
 * A class that represents a queue of plugins to run
**/
struct RemapPlugins: public Continuation
{
  RemapPlugins()
    : _cur(0)
    { }

  ~RemapPlugins() { _cur = 0; }

  // Some basic setters
  void setMap(UrlMappingContainer* m) { _map_container = m; }
  void setRequestUrl(URL* u) { _request_url = u; }
  void setState(HttpTransact::State* state) { _s = state; }
  void setRequestHeader(HTTPHdr* h) {  _request_header = h; }
  void setHostHeaderInfo(host_hdr_info* h) { _hh_ptr = h; }

  int run_remap(int, Event *);
  int run_single_remap();
  int run_plugin(remap_plugin_info *, char *, int, bool *, bool *, bool *);

  Action action;

 private:
  unsigned int _cur;
  UrlMappingContainer *_map_container;
  URL *_request_url;
  HTTPHdr *_request_header;
  HttpTransact::State * _s;
  host_hdr_info *_hh_ptr;
};

#endif
