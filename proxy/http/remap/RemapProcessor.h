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
 * Remap plugin processor
**/
#if !defined(_REMAPPROCESSOR_h_)
#define _REMAPPROCESSOR_h_

#include "I_EventSystem.h"
#include "RemapPlugins.h"
#include "RemapPluginInfo.h"
#include "ReverseProxy.h"
#undef std // FIXME: remove dependency on the STL
#include <map>

#define EVENT_REMAP_START (REMAP_EVENT_EVENTS_START + 0)
#define EVENT_REMAP_ERROR (REMAP_EVENT_EVENTS_START + 1)
#define EVENT_REMAP_COMPLETE (REMAP_EVENT_EVENTS_START + 2)

class url_mapping;

class RemapProcessor : public Processor
{
public:
  RemapProcessor() : ET_REMAP(0), _use_separate_remap_thread(false) {}
  ~RemapProcessor() {}
  bool setup_for_remap(HttpTransact::State *s);
  bool finish_remap(HttpTransact::State *s);

  Action *perform_remap(Continuation *cont, HttpTransact::State *s);
  int start(int num_threads, size_t stacksize);
  bool LessThan(HttpTransact::State *, HttpTransact::State *);
  void
  setUseSeparateThread()
  {
    _use_separate_remap_thread = true;
  }
  bool
  using_separate_thread()
  {
    return _use_separate_remap_thread == true;
  }

private:
  EventType ET_REMAP;
  bool _use_separate_remap_thread;
};

/**
 * the global remapProcessor that everyone uses
**/
extern RemapProcessor remapProcessor;

#endif
