/** @file

  Internal SDK stuff

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

#include "ts/apidefs.h"
#include "ts/InkAPIPrivateIOCore.h"

#include "tscore/Allocator.h"
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_atomic.h"

// http_remap
#include "proxy/http/remap/RemapPluginInfo.h"

// inkevent
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/Lock.h"
#include "iocore/eventsystem/ProxyAllocator.h"
#include "iocore/eventsystem/VConnection.h"

using namespace tsapi::c;

ClassAllocator<INKContInternal> INKContAllocator("INKContAllocator");

INKContInternal::INKContInternal()
  : DummyVConnection(nullptr),
    mdata(nullptr),
    m_event_func(nullptr),
    m_event_count(0),
    m_closed(1),
    m_deletable(0),
    m_deleted(0),
    m_context(0),
    m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
}

INKContInternal::INKContInternal(TSEventFunc funcp, TSMutex mutexp)
  : DummyVConnection((ProxyMutex *)mutexp),
    mdata(nullptr),
    m_event_func(funcp),
    m_event_count(0),
    m_closed(1),
    m_deletable(0),
    m_deleted(0),
    m_context(0),
    m_free_magic(INKCONT_INTERN_MAGIC_ALIVE)
{
  SET_HANDLER(&INKContInternal::handle_event);
}

void
INKContInternal::init(TSEventFunc funcp, TSMutex mutexp, void *context)
{
  SET_HANDLER(&INKContInternal::handle_event);

  mutex        = (ProxyMutex *)mutexp;
  m_event_func = funcp;
  m_context    = context;
}

void
INKContInternal::clear()
{
}

void
INKContInternal::free()
{
  this->clear();
  this->mutex.clear();
  m_free_magic = INKCONT_INTERN_MAGIC_DEAD;
  THREAD_FREE(this, INKContAllocator, this_thread());
}

void
INKContInternal::destroy()
{
  if (m_free_magic == INKCONT_INTERN_MAGIC_DEAD) {
    ink_release_assert(!"Plugin tries to use a continuation which is deleted");
  }
  m_deleted = 1;
  if (m_deletable) {
    this->free();
  } else {
    // TODO: Should this schedule on some other "thread" ?
    // TODO: we don't care about the return action?
    if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
    EThread *p = this_ethread();

    // If this_thread() returns null, the EThread object for the current thread has been destroyed (or it never existed).
    // Presumably this will only happen during destruction of statically-initialized objects at TS shutdown, so no further
    // action is needed.
    //
    if (p) {
      p->schedule_imm(this);
    }
  }
}

void
INKContInternal::handle_event_count(int event)
{
  if ((event == EVENT_IMMEDIATE) || (event == EVENT_INTERVAL) || event == TS_EVENT_HTTP_TXN_CLOSE) {
    int val = ink_atomic_increment((int *)&m_event_count, -1);
    if (val <= 0) {
      ink_assert(!"not reached");
    }

    m_deletable = (m_closed != 0) && (val == 1);
  }
}

int
INKContInternal::handle_event(int event, void *edata)
{
  if (m_free_magic == INKCONT_INTERN_MAGIC_DEAD) {
    ink_release_assert(!"Plugin tries to use a continuation which is deleted");
  }
  this->handle_event_count(event);
  if (m_deleted) {
    if (m_deletable) {
      this->free();
    } else {
      Debug("plugin", "INKCont Deletable but not deleted %d", m_event_count);
    }
  } else {
    /* set the plugin context */
    auto *previousContext = pluginThreadContext;
    pluginThreadContext   = reinterpret_cast<PluginThreadContext *>(m_context);
    int retval            = m_event_func((TSCont)this, (TSEvent)event, edata);
    pluginThreadContext   = previousContext;
    if (edata && event == EVENT_INTERVAL) {
      Event *e = reinterpret_cast<Event *>(edata);
      if (e->period != 0) {
        // In the interval case, we must re-increment the m_event_count for
        // the next go around.  Otherwise, our event count will go negative.
        ink_release_assert(ink_atomic_increment((int *)&this->m_event_count, 1) >= 0);
      }
    }
    return retval;
  }
  return EVENT_DONE;
}
