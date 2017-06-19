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

#include "P_Cache.h"

Action *
Cache::link(Continuation *cont, const CacheKey *from, const CacheKey *to, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_LINK_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);

  CacheVC *c         = new_CacheVC(cont);
  c->vol             = key_to_vol(from, hostname, host_len);
  c->write_len       = sizeof(*to); // so that the earliest_key will be used
  c->f.use_first_key = 1;
  c->first_key       = *from;
  c->earliest_key    = *to;

  c->buf = new_IOBufferData(BUFFER_SIZE_INDEX_512);
#ifdef DEBUG
  Doc *doc = (Doc *)c->buf->data();
  memcpy(doc->data(), to, sizeof(*to)); // doublecheck
#endif

  SET_CONTINUATION_HANDLER(c, &CacheVC::linkWrite);

  if (c->do_write_lock() == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  } else {
    return &c->_action;
  }
}

int
CacheVC::linkWrite(int event, Event * /* e ATS_UNUSED */)
{
  ink_assert(event == AIO_EVENT_DONE);
  set_io_not_in_progress();
  dir_insert(&first_key, vol, &dir);
  if (_action.cancelled) {
    goto Ldone;
  }
  if (io.ok()) {
    _action.continuation->handleEvent(CACHE_EVENT_LINK, nullptr);
  } else {
    _action.continuation->handleEvent(CACHE_EVENT_LINK_FAILED, nullptr);
  }
Ldone:
  return free_CacheVC(this);
}

Action *
Cache::deref(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_DEREF_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);

  Vol *vol = key_to_vol(key, hostname, host_len);
  Dir result;
  Dir *last_collision = nullptr;
  CacheVC *c          = nullptr;
  {
    MUTEX_TRY_LOCK(lock, vol->mutex, cont->mutex->thread_holding);
    if (lock.is_locked()) {
      if (!dir_probe(key, vol, &result, &last_collision)) {
        cont->handleEvent(CACHE_EVENT_DEREF_FAILED, (void *)-ECACHE_NO_DOC);
        return ACTION_RESULT_DONE;
      }
    }
    c = new_CacheVC(cont);
    SET_CONTINUATION_HANDLER(c, &CacheVC::derefRead);
    c->first_key = c->key = *key;
    c->vol                = vol;
    c->dir                = result;
    c->last_collision     = last_collision;

    if (!lock.is_locked()) {
      c->mutex->thread_holding->schedule_in_local(c, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
      return &c->_action;
    }

    switch (c->do_read_call(&c->key)) {
    case EVENT_DONE:
      return ACTION_RESULT_DONE;
    case EVENT_RETURN:
      goto Lcallreturn;
    default:
      return &c->_action;
    }
  }
Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  } else {
    return &c->_action;
  }
}

int
CacheVC::derefRead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Doc *doc = nullptr;

  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }
  if (!buf) {
    goto Lcollision;
  }
  if ((int)io.aio_result != (int)io.aiocb.aio_nbytes) {
    goto Ldone;
  }
  if (!dir_agg_valid(vol, &dir)) {
    last_collision = nullptr;
    goto Lcollision;
  }
  doc = (Doc *)buf->data();
  if (!(doc->first_key == key)) {
    goto Lcollision;
  }
#ifdef DEBUG
  ink_assert(!memcmp(doc->data(), &doc->key, sizeof(doc->key)));
#endif
  _action.continuation->handleEvent(CACHE_EVENT_DEREF, (void *)&doc->key);
  return free_CacheVC(this);

Lcollision : {
  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }
  if (dir_probe(&key, vol, &dir, &last_collision)) {
    int ret = do_read_call(&first_key);
    if (ret == EVENT_RETURN) {
      goto Lcallreturn;
    }
    return ret;
  }
}
Ldone:
  _action.continuation->handleEvent(CACHE_EVENT_DEREF_FAILED, (void *)-ECACHE_NO_DOC);
  return free_CacheVC(this);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr); // hopefully a tail call
}
