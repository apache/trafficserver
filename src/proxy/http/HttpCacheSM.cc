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

#include "proxy/http/HttpCacheSM.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/HttpDebugNames.h"

#include "iocore/cache/Cache.h"

#define SM_REMEMBER(sm, e, r)                          \
  {                                                    \
    sm->history.push_back(MakeSourceLocation(), e, r); \
  }

#define STATE_ENTER(state_name, event)                                                                                       \
  {                                                                                                                          \
    SM_REMEMBER(master_sm, event, NO_REENTRANT);                                                                             \
    Dbg(dbg_ctl_http_cache, "[%" PRId64 "] [%s, %s]", master_sm->sm_id, #state_name, HttpDebugNames::get_event_name(event)); \
  }

namespace
{
DbgCtl dbg_ctl_http_cache{"http_cache"};
} // end anonymous namespace

////
// HttpCacheAction
//
void
HttpCacheAction::cancel(Continuation *c)
{
  ink_assert(c == nullptr || c == _cache_sm->master_sm);
  ink_assert(this->cancelled == false);

  this->cancelled = true;
  if (_cache_sm->pending_action) {
    _cache_sm->pending_action->cancel();
  }
}

////
// HttpCacheSM
//
/**
  Reset captive_action and counters for another cache operations.
  - e.g. following redirect starts over from cache lookup
 */
void
HttpCacheSM::reset()
{
  captive_action.reset();
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpCacheSM::state_cache_open_read()
//
//  State the cache calls back the state machine into on a open_read
//  call. The set of allowed events (from the cache) are:
// - CACHE_EVENT_OPEN_READ
//   - document matching request is in the cache
// - CACHE_EVENT_OPEN_READ_FAILED
//   if (data != ECACHEDOCBUSY)
//   - document matching request is not in the cache
//   if (data == ECACHEDOCBUSY)
//   - document with same URL as request is in the cache
//     but is being currently updated by another state
//     machine. Keep in mind that the document may NOT
//     match any of the request headers - it just matches
//     the URL. In other words, the document that is being
//     written to by another state machine may be an
//     alternate of the document the request wants.
// - EVENT_INTERVAL
//   - a previous open_read returned "failed_in_progress". we
//     decided to retry the open read. we scheduled the event
//     processor to call us back after n msecs so that we can
//     reissue the open_read. this is the call from the event
//     processor.
//
//////////////////////////////////////////////////////////////////////////
int
HttpCacheSM::state_cache_open_read(int event, void *data)
{
  STATE_ENTER(&HttpCacheSM::state_cache_open_read, event);
  ink_assert(captive_action.cancelled == 0);
  pending_action = nullptr;

  if (captive_action.cancelled == 1) {
    return VC_EVENT_CONT; // SM gave up on us
  }

  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    Metrics::Gauge::increment(http_rsb.current_cache_connections);
    ink_assert((cache_read_vc == nullptr) || master_sm->t_state.redirect_info.redirect_in_process);
    if (cache_read_vc) {
      // redirect follow in progress, close the previous cache_read_vc
      close_read();
    }
    open_read_cb  = true;
    cache_read_vc = static_cast<CacheVConnection *>(data);
    master_sm->handleEvent(event, &captive_action);
    break;

  case CACHE_EVENT_OPEN_READ_RWW:
    set_readwhilewrite_inprogress(true);
    break;

  case CACHE_EVENT_OPEN_READ_FAILED:
    err_code = reinterpret_cast<intptr_t>(data);
    if ((intptr_t)data == -ECACHE_DOC_BUSY) {
      // Somebody else is writing the object
      if (open_read_tries <= master_sm->t_state.txn_conf->max_cache_open_read_retries) {
        // Retry to read; maybe the update finishes in time
        open_read_cb = false;
        do_schedule_in();
      } else {
        // Give up; the update didn't finish in time
        // HttpSM will inform HttpTransact to 'proxy-only'
        open_read_cb = true;
        master_sm->handleEvent(event, &captive_action);
      }
    } else {
      // Simple miss in the cache.
      open_read_cb = true;
      master_sm->handleEvent(event, &captive_action);
    }
    break;

  case EVENT_INTERVAL:
    // Retry the cache open read if the number retries is less
    // than or equal to the max number of open read retries,
    // else treat as a cache miss.
    ink_assert(open_read_tries <= master_sm->t_state.txn_conf->max_cache_open_read_retries);
    Dbg(dbg_ctl_http_cache,
        "[%" PRId64 "] [state_cache_open_read] cache open read failure %d. "
        "retrying cache open read...",
        master_sm->sm_id, open_read_tries);

    do_cache_open_read(cache_key);
    break;

  default:
    ink_assert(0);
  }

  return VC_EVENT_CONT;
}

bool
HttpCacheSM::write_retry_done() const
{
  MgmtInt const timeout_ms = master_sm->t_state.txn_conf->max_cache_open_write_retry_timeout;
  if (0 < timeout_ms && 0 < open_write_start) {
    ink_hrtime const elapsed = ink_get_hrtime() - open_write_start;
    MgmtInt const    msecs   = ink_hrtime_to_msec(elapsed);
    return timeout_ms < msecs;
  } else {
    return master_sm->t_state.txn_conf->max_cache_open_write_retries < open_write_tries;
  }
}

int
HttpCacheSM::state_cache_open_write(int event, void *data)
{
  STATE_ENTER(&HttpCacheSM::state_cache_open_write, event);
  ink_assert(captive_action.cancelled == 0);
  pending_action = nullptr;

  if (captive_action.cancelled == 1) {
    return VC_EVENT_CONT; // SM gave up on us
  }
  bool read_retry_on_write_fail = false;

  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    Metrics::Gauge::increment(http_rsb.current_cache_connections);
    ink_assert(cache_write_vc == nullptr);
    cache_write_vc = static_cast<CacheVConnection *>(data);
    open_write_cb  = true;
    master_sm->handleEvent(event, &captive_action);
    break;

  case CACHE_EVENT_OPEN_WRITE_FAILED: {
    if (master_sm->t_state.txn_conf->cache_open_write_fail_action ==
        static_cast<MgmtByte>(CacheOpenWriteFailAction_t::READ_RETRY)) {
      // fall back to open_read_tries
      // Note that when CacheOpenWriteFailAction_t::READ_RETRY is configured, max_cache_open_write_retries
      // is automatically ignored. Make sure to not disable max_cache_open_read_retries
      // with CacheOpenWriteFailAction_t::READ_RETRY as this results in proxy'ing to origin
      // without write retries in both a cache miss or a cache refresh scenario.

      if (write_retry_done()) {
        Dbg(dbg_ctl_http_cache, "[%" PRId64 "] [state_cache_open_write] cache open write failure %d. read retry triggered",
            master_sm->sm_id, open_write_tries);
        if (master_sm->t_state.txn_conf->max_cache_open_read_retries <= 0) {
          Dbg(dbg_ctl_http_cache,
              "[%" PRId64 "] [state_cache_open_write] invalid config, cache write fail set to"
              " read retry, but, max_cache_open_read_retries is not enabled",
              master_sm->sm_id);
        }
        open_read_tries = 0;

        read_retry_on_write_fail = true;
        // make sure it doesn't loop indefinitely
        open_write_tries            = master_sm->t_state.txn_conf->max_cache_open_write_retries + 1;
        MgmtInt const retry_timeout = master_sm->t_state.txn_conf->max_cache_open_write_retry_timeout;
        if (0 < retry_timeout) {
          open_write_start = ink_get_hrtime() - ink_hrtime_from_msec(retry_timeout);
        }
      }
    }

    if (read_retry_on_write_fail || !write_retry_done()) {
      // Retry open write;
      open_write_cb = false;
      do_schedule_in();
    } else {
      // The cache is hosed or full or something.
      // Forward the failure to the main sm
      Dbg(dbg_ctl_http_cache,
          "[%" PRId64 "] [state_cache_open_write] cache open write failure %d. "
          "done retrying...",
          master_sm->sm_id, open_write_tries);
      open_write_cb = true;
      err_code      = reinterpret_cast<intptr_t>(data);
      master_sm->handleEvent(event, &captive_action);
    }
  } break;

  case EVENT_INTERVAL:
    if (master_sm->t_state.txn_conf->cache_open_write_fail_action ==
        static_cast<MgmtByte>(CacheOpenWriteFailAction_t::READ_RETRY)) {
      Dbg(dbg_ctl_http_cache,
          "[%" PRId64 "] [state_cache_open_write] cache open write failure %d. "
          "falling back to read retry...",
          master_sm->sm_id, open_write_tries);
      open_read_cb = false;
      master_sm->handleEvent(CACHE_EVENT_OPEN_READ, &captive_action);
    } else {
      Dbg(dbg_ctl_http_cache,
          "[%" PRId64 "] [state_cache_open_write] cache open write failure %d. "
          "retrying cache open write...",
          master_sm->sm_id, open_write_tries);

      // Retry the cache open write if the number retries is less
      // than or equal to the max number of open write retries
      ink_assert(!write_retry_done());

      open_write(&cache_key, lookup_url, read_request_hdr, master_sm->t_state.cache_info.object_read,
                 static_cast<time_t>(
                   (master_sm->t_state.cache_control.pin_in_cache_for < 0) ? 0 : master_sm->t_state.cache_control.pin_in_cache_for),
                 retry_write, false);
    }
    break;

  default:
    ink_release_assert(0);
  }

  return VC_EVENT_CONT;
}

void
HttpCacheSM::do_schedule_in()
{
  ink_assert(pending_action == nullptr);
  Action *action_handle =
    mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(master_sm->t_state.txn_conf->cache_open_read_retry_time));

  if (action_handle != ACTION_RESULT_DONE) {
    pending_action = action_handle;
  }

  return;
}

Action *
HttpCacheSM::do_cache_open_read(const HttpCacheKey &key)
{
  open_read_tries++;
  ink_assert(pending_action == nullptr);
  ink_assert(open_read_cb == false);
  // Initialising read-while-write-inprogress flag
  this->readwhilewrite_inprogress = false;
  Action *action_handle           = cacheProcessor.open_read(this, &key, this->read_request_hdr, &http_params);

  if (action_handle != ACTION_RESULT_DONE) {
    pending_action = action_handle;
  }
  // Check to see if we've already called the user back
  //  If we have then it's ACTION_RESULT_DONE, other wise
  //  return our captive action and ensure that we are actually
  //  doing something useful
  if (open_read_cb == true) {
    return ACTION_RESULT_DONE;
  } else {
    ink_assert(pending_action != nullptr);
    captive_action.cancelled = 0; // Make sure not cancelled before we hand it out
    return &captive_action;
  }
}

Action *
HttpCacheSM::open_read(const HttpCacheKey *key, URL *url, HTTPHdr *hdr, const OverridableHttpConfigParams *params,
                       time_t pin_in_cache)
{
  Action *act_return;

  cache_key         = *key;
  lookup_url        = url;
  read_request_hdr  = hdr;
  http_params       = params;
  read_pin_in_cache = pin_in_cache;
  ink_assert(pending_action == nullptr);
  SET_HANDLER(&HttpCacheSM::state_cache_open_read);

  lookup_max_recursive++;
  current_lookup_level++;
  open_read_cb = false;
  act_return   = do_cache_open_read(cache_key);
  // the following logic is based on the assumption that the second
  // lookup won't happen if the HttpSM hasn't been called back for the
  // first lookup
  if (current_lookup_level == lookup_max_recursive) {
    current_lookup_level--;
    ink_assert(current_lookup_level >= 0);
    if (current_lookup_level == 0) {
      lookup_max_recursive = 0;
    }
    return act_return;
  } else {
    ink_assert(current_lookup_level < lookup_max_recursive);
    current_lookup_level--;

    if (current_lookup_level == 0) {
      lookup_max_recursive = 0;
    }

    return ACTION_RESULT_DONE;
  }
}

Action *
HttpCacheSM::open_write(const HttpCacheKey *key, URL *url, HTTPHdr *request, CacheHTTPInfo *old_info, time_t pin_in_cache,
                        bool retry, bool allow_multiple)
{
  SET_HANDLER(&HttpCacheSM::state_cache_open_write);
  ink_assert(pending_action == nullptr);
  ink_assert((cache_write_vc == nullptr) || master_sm->t_state.redirect_info.redirect_in_process);
  // INKqa12119
  open_write_cb = false;
  open_write_tries++;
  if (0 == open_write_start) {
    open_write_start = ink_get_hrtime();
  }
  this->retry_write = retry;

  // We should be writing the same document we did
  //  a lookup on
  // this is no longer true for multiple cache lookup
  // ink_assert(url == lookup_url || lookup_url == NULL);
  ink_assert(request == read_request_hdr || read_request_hdr == nullptr);
  this->lookup_url       = url;
  this->read_request_hdr = request;
  cache_key              = *key;

  // Make sure we are not stuck in a loop where the write
  //  fails but the retry read succeeds causing to issue
  //  a new write (could happen on a very busy document
  //  that must be revalidated every time)
  // Changed by YTS Team, yamsat Plugin
  // two criteria, either write retries over the amount OR timeout
  if (open_write_tries > master_sm->redirection_tries && write_retry_done()) {
    err_code = -ECACHE_DOC_BUSY;
    master_sm->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, &captive_action);
    return ACTION_RESULT_DONE;
  }

  // INKqa11166
  CacheHTTPInfo *info          = allow_multiple ? reinterpret_cast<CacheHTTPInfo *>(CACHE_ALLOW_MULTIPLE_WRITES) : old_info;
  Action        *action_handle = cacheProcessor.open_write(this, key, info, pin_in_cache);

  if (action_handle != ACTION_RESULT_DONE) {
    pending_action = action_handle;
  }
  // Check to see if we've already called the user back
  //  If we have then it's ACTION_RESULT_DONE, other wise
  //  return our captive action and ensure that we are actually
  //  doing something useful
  if (open_write_cb == true) {
    return ACTION_RESULT_DONE;
  } else {
    ink_assert(pending_action != nullptr);
    captive_action.cancelled = 0; // Make sure not cancelled before we hand it out
    return &captive_action;
  }
}
