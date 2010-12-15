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



#include "I_MTInteractor.h"
#include "P_EventSystem.h"

// for debugging
#define LOCK_FAIL_RATE 0.05

#define MAYBE_FAIL_TRY_LOCK(_l,_t) \
  if (MUTEX_TAKE_TRY_LOCK(_l, _t)) { \
    if ((uint32_t)_t->generator.random() < \
       (uint32_t)(UINT_MAX * LOCK_FAIL_RATE)) { \
       MUTEX_UNTAKE_LOCK(_l,_t); \
       return false; \
    } else { \
       return true; \
    } \
  } else { \
    return false; \
  }

MTInteractor::MTInteractor(ProxyMutex * amutex)
:Continuation(amutex)
{
  m_lock = amutex;
}

MTInteractor::~MTInteractor()
{
  m_lock = NULL;
}

int
MTInteractor::try_lock()
{
  MAYBE_FAIL_TRY_LOCK(m_lock, this_ethread());
}

void
MTInteractor::unlock()
{
  MUTEX_UNTAKE_LOCK(m_lock, this_ethread());
}

MTClient::MTClient(ProxyMutex * amutex)
:Continuation(amutex)
{
  m_lock = amutex;
}

MTClient::~MTClient()
{
  m_lock = NULL;
}

int
MTClient::try_lock()
{
  MAYBE_FAIL_TRY_LOCK(m_lock, this_ethread());
}

void
MTClient::unlock()
{
  MUTEX_UNTAKE_LOCK(m_lock, this_ethread());
}

void
MTClient::setMTInteractor(MTInteractor * t)
{
  m_mti = t;
}

void
MTClient::unsetMTInteractor()
{
  m_mti = NULL;
}

int
MTClient::startAttach(MTInteractor * t)
{
  m_mti = t;
  ink_release_assert(m_mti);
  SET_HANDLER(&MTClient::handleAttaching);
  return handleAttaching(0, 0);
}

int
MTClient::startDetach()
{
  SET_HANDLER(&MTClient::handleDetaching);
  return handleDetaching(0, 0);
}

int
MTClient::handleDetaching(int event, void *data)
{
  if (!((event == EVENT_INTERVAL && data == m_leave)
        || (event == 0 && data == 0))) {
    // ignore other events.
    return EVENT_CONT;
  }
  ink_release_assert(event != 12345);
  if (!m_mti->detachClient(this)) {
    SET_HANDLER(&MTClient::handleDetaching);
    m_leave = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_CONT;
  }
  // we left.
  SET_HANDLER(&MTClient::handleDetached);
  return handleDetached(MTClient::e_detached, this);
}

int
MTClient::handleAttaching(int event, void *data)
{
  if (!((event == EVENT_INTERVAL && data == m_join)
        || (event == 0 && data == 0))) {
    // ignore other events.
    return EVENT_CONT;
  }
  ink_release_assert(event != 12345);
  if (!m_mti->attachClient(this)) {
    m_join = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_CONT;
  }
  // we joined.
  //printf("MTClient:: %x joined %x\n",this,m_mti);
  SET_HANDLER(&MTClient::handleAttached);
  return handleAttached(MTClient::e_attached, this);
}

int
MTClient::handleAttached(int event, void *data)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(data);
  return EVENT_CONT;
}
int
MTClient::handleDetached(int event, void *data)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(data);
  return EVENT_CONT;
}
