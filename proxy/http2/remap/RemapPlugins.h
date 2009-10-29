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

/**
 * Remap plugins class
**/

#if !defined (_REMAPPLUGINS_h_)
#define _REMAPPLUGINS_h_

#include "inktomi++.h"
#include "I_EventSystem.h"
#include "RemapProcessor.h"
#include "RemapAPI.h"
#include "RemapPluginInfo.h"
#include "HttpTransact.h"
#include "ReverseProxy.h"

/**
 * A class that represents a queue of plugins to run
**/
struct RemapPlugins:Continuation
{
  unsigned int _cur;
  url_mapping *_map;
  URL *_request_url;
  HTTPHdr *_request_header;
    HttpTransact::State * _s;
  host_hdr_info *_hh_ptr;

    RemapPlugins();
   ~RemapPlugins();

  int run_remap(int, Event *);
  int run_single_remap();
  void setMap(url_mapping *);
  void setRequestUrl(URL *);
  void setRequestHeader(HTTPHdr *);
  int run_plugin(remap_plugin_info *, char *, int, bool *, bool *, bool *);
  void setState(HttpTransact::State *);
  void setHostHeaderInfo(host_hdr_info *);
  Action action;
};

#endif
