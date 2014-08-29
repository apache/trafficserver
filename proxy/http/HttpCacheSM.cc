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

/****************************************************************************

   HttpCacheSM.cc

   Description:


 ****************************************************************************/

#include "HttpCacheSM.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"

#define STATE_ENTER(state_name, event) { \
        REMEMBER(event, -1); \
        Debug("http_cache", "[%" PRId64 "] [%s, %s]", master_sm->sm_id, \
        #state_name, HttpDebugNames::get_event_name(event)); }

#define __REMEMBER(x)  #x
#define _REMEMBER(x)   __REMEMBER(x)

#define REMEMBER(e,r) master_sm->add_history_entry(__FILE__ ":" _REMEMBER (__LINE__), e, r);


HttpCacheAction::HttpCacheAction()
  : sm(NULL)
{
}

void
HttpCacheAction::cancel(Continuation * c)
{
  ink_assert(c == NULL || c == sm->master_sm);
  ink_assert(this->cancelled == 0);

  this->cancelled = 1;
  if (sm->pending_action)
    sm->pending_action->cancel();
}

HttpCacheSM::HttpCacheSM():
  Continuation(NULL),
  cache_read_vc(NULL), cache_write_vc(NULL),
  read_locked(false), write_locked(false),
  readwhilewrite_inprogress(false),
  master_sm(NULL), pending_action(NULL),
  captive_action(),
  open_read_cb(false), open_write_cb(false), open_read_tries(0),
  read_request_hdr(NULL), read_config(NULL),
  read_pin_in_cache(0), retry_write(true), open_write_tries(0),
  lookup_url(NULL), lookup_max_recursive(0), current_lookup_level(0)
{
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
  pending_action = NULL;

  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    HTTP_INCREMENT_DYN_STAT(http_current_cache_connections_stat);
    ink_assert(cache_read_vc == NULL);
    open_read_cb = true;
    cache_read_vc = (CacheVConnection *) data;
    master_sm->handleEvent(event, data);
    break;

  case CACHE_EVENT_OPEN_READ_FAILED:
    if (data == (void *) -ECACHE_DOC_BUSY) {
      // Somebody else is writing the object
      if (open_read_tries <= master_sm->t_state.txn_conf->max_cache_open_read_retries) {
        // Retry to read; maybe the update finishes in time
        open_read_cb = false;
        do_schedule_in();
      } else {
        // Give up; the update didn't finish in time
        // HttpSM will inform HttpTransact to 'proxy-only'
        open_read_cb = true;
        master_sm->handleEvent(event, data);
      }
    } else {
      // Simple miss in the cache.
      open_read_cb = true;
      master_sm->handleEvent(event, data);
    }
    break;

  case EVENT_INTERVAL:
    // Retry the cache open read if the number retries is less
    // than or equal to the max number of open read retries,
    // else treat as a cache miss.
    ink_assert(open_read_tries <= master_sm->t_state.txn_conf->max_cache_open_read_retries || write_locked);
    Debug("http_cache", "[%" PRId64 "] [state_cache_open_read] cache open read failure %d. "
          "retrying cache open read...", master_sm->sm_id, open_read_tries);

    do_cache_open_read();
    break;

  default:
    ink_assert(0);
  }

  return VC_EVENT_CONT;
}

int
HttpCacheSM::state_cache_open_write(int event, void *data)
{
  STATE_ENTER(&HttpCacheSM::state_cache_open_write, event);
  ink_assert(captive_action.cancelled == 0);
  pending_action = NULL;

  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    HTTP_INCREMENT_DYN_STAT(http_current_cache_connections_stat);
    ink_assert(cache_write_vc == NULL);
    cache_write_vc = (CacheVConnection *) data;
    open_write_cb = true;
    master_sm->handleEvent(event, data);
    break;

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    // The cache is hosed or full or something.
    // Forward the failure to the main sm
    open_write_cb = true;
    master_sm->handleEvent(event, data);
    break;

  default:
    ink_release_assert(0);
  }

  return VC_EVENT_CONT;
}

void
HttpCacheSM::do_schedule_in()
{
  ink_assert(pending_action == NULL);
  Action *action_handle =
    mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(master_sm->t_state.txn_conf->cache_open_read_retry_time));

  if (action_handle != ACTION_RESULT_DONE) {
    pending_action = action_handle;
  }

  return;
}

Action *
HttpCacheSM::do_cache_open_read()
{
  open_read_tries++;
  ink_assert(pending_action == NULL);
  if (write_locked) {
    open_read_cb = false;
  } else {
    ink_assert(open_read_cb == false);
  }
  //Initialising read-while-write-inprogress flag
  this->readwhilewrite_inprogress = false;
  Action *action_handle = cacheProcessor.open_read(this, this->lookup_url, master_sm->t_state.cache_control.cluster_cache_local, this->read_request_hdr, this->read_config,
                                                   this->read_pin_in_cache);

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
    ink_assert(pending_action != NULL || write_locked == true);
    return &captive_action;
  }
}

Action *
HttpCacheSM::open_read(URL * url, HTTPHdr * hdr, CacheLookupHttpConfig * params, time_t pin_in_cache)
{
  Action *act_return;

  lookup_url = url;
  read_request_hdr = hdr;
  read_config = params;
  read_pin_in_cache = pin_in_cache;
  ink_assert(pending_action == NULL);
  SET_HANDLER(&HttpCacheSM::state_cache_open_read);

  lookup_max_recursive++;
  current_lookup_level++;
  open_read_cb = false;
  act_return = do_cache_open_read();
  // the following logic is based on the assumption that the secnod
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

    if (current_lookup_level == 0)
      lookup_max_recursive = 0;

    return ACTION_RESULT_DONE;
  }
}

Action *
HttpCacheSM::open_write(URL * url, HTTPHdr * request, CacheHTTPInfo * old_info, time_t pin_in_cache,
                        bool retry, bool allow_multiple)
{
  SET_HANDLER(&HttpCacheSM::state_cache_open_write);
  ink_assert(pending_action == NULL);
  ink_assert(cache_write_vc == NULL);
  // INKqa12119
  open_write_cb = false;
  open_write_tries++;
  this->retry_write = retry;

  // We should be writing the same document we did
  //  a lookup on
  // this is no longer true for multiple cache lookup
  //ink_assert(url == lookup_url || lookup_url == NULL);
  ink_assert(request == read_request_hdr || read_request_hdr == NULL);
  this->lookup_url = url;
  this->read_request_hdr = request;

  // Make sure we are not stuck in a loop where the write
  //  fails but the retry read succeeds causing to issue
  //  a new write (could happen on a very busy document
  //  that must be revalidated every time)
  // Changed by YTS Team, yamsat Plugin
  if (open_write_tries > master_sm->redirection_tries &&
      open_write_tries > master_sm->t_state.http_config_param->max_cache_open_write_retries) {
    master_sm->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -ECACHE_DOC_BUSY);
    return ACTION_RESULT_DONE;
  }

  Action *action_handle = cacheProcessor.open_write(this,
                                                    0,
                                                    url,
                                                    master_sm->t_state.cache_control.cluster_cache_local,
                                                    request,
                                                    // INKqa11166
                                                    allow_multiple ? (CacheHTTPInfo *) CACHE_ALLOW_MULTIPLE_WRITES :
                                                    old_info,
                                                    pin_in_cache);

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
    ink_assert(pending_action != NULL);
    return &captive_action;
  }
}
