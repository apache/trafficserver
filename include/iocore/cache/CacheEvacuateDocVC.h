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

#pragma once

/*
#include "iocore/cache/P_CacheDir.h"
*/

#include "iocore/cache/CacheVC.h"

// eventsystem
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/ProxyAllocator.h"

// tscore
#include "tscore/ink_assert.h"

// ts
#include "ts/DbgCtl.h"

class CacheEvacuateDocVC : public CacheVC
{
public:
  int evacuateDocDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */);
  int evacuateReadHead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */);
};

extern ClassAllocator<CacheEvacuateDocVC> cacheEvacuateDocVConnectionAllocator;

inline CacheEvacuateDocVC *
new_CacheEvacuateDocVC(Continuation *cont)
{
  EThread *t            = cont->mutex->thread_holding;
  CacheEvacuateDocVC *c = THREAD_ALLOC(cacheEvacuateDocVConnectionAllocator, t);
  c->vector.data.data   = &c->vector.data.fast_data[0];
  c->_action            = cont;
  c->mutex              = cont->mutex;
  c->start_time         = ink_get_hrtime();
  c->setThreadAffinity(t);
  ink_assert(c->trigger == nullptr);
  static DbgCtl dbg_ctl{"cache_new"};
  Dbg(dbg_ctl, "new %p", c);
#ifdef CACHE_STAT_PAGES
  ink_assert(!c->stat_link.next);
  ink_assert(!c->stat_link.prev);
#endif
  dir_clear(&c->dir);
  return c;
}
