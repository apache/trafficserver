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

   HttpSM_update.cc

   Description:
        An HttpSM sub class for support scheduled update functionality



 ****************************************************************************/

#include "HttpUpdateSM.h"
#include "HttpDebugNames.h"

ClassAllocator<HttpUpdateSM> httpUpdateSMAllocator("httpUpdateSMAllocator");

#define STATE_ENTER(state_name, event, vio)                                                             \
  {                                                                                                     \
    Debug("http", "[%" PRId64 "] [%s, %s]", sm_id, #state_name, HttpDebugNames::get_event_name(event)); \
  }

HttpUpdateSM::HttpUpdateSM() : cb_action(), cb_event(HTTP_SCH_UPDATE_EVENT_ERROR) {}

void
HttpUpdateSM::destroy()
{
  cleanup();
  cb_action = nullptr;
  httpUpdateSMAllocator.free(this);
}

Action *
HttpUpdateSM::start_scheduled_update(Continuation *cont, HTTPHdr *request)
{
  // Use passed continuation's mutex for this state machine
  this->mutex = cont->mutex;
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  // Set up the Action
  cb_cont   = cont;
  cb_action = cont;

  start_sub_sm();

  // Make a copy of the request we being asked to do
  t_state.hdr_info.client_request.create(HTTP_TYPE_REQUEST);
  t_state.hdr_info.client_request.copy(request);

  // Fix ME: What should these be set to since there is not a
  //   real client
  ats_ip4_set(&t_state.client_info.src_addr, htonl(INADDR_LOOPBACK), 0);
  t_state.client_info.port_attribute = HttpProxyPort::TRANSPORT_DEFAULT;

  t_state.req_flavor = HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE;

  // We always deallocate this later so initialize it down
  http_parser_init(&http_parser);

  // We need to call state to add us to the http sm list
  //   but since we can terminate the state machine on this
  //   stack, do this by calling througth the main handler
  //   so the sm will be properly terminated
  this->default_handler = &HttpUpdateSM::state_add_to_list;
  this->handleEvent(EVENT_NONE, nullptr);

  if (cb_occured == 0) {
    return &cb_action;
  } else {
    return ACTION_RESULT_DONE;
  }
}

void
HttpUpdateSM::handle_api_return()
{
  switch (t_state.api_next_action) {
  case HttpTransact::SM_ACTION_API_SM_START:
    call_transact_and_set_next_state(&HttpTransact::ModifyRequest);
    return;
  case HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR:
    // we have further processing to do
    //  based on what t_state.next_action is
    break;
  default:
    HttpSM::handle_api_return();
    return;
  }

  switch (t_state.next_action) {
  case HttpTransact::SM_ACTION_TRANSFORM_READ: {
    if (t_state.cache_info.transform_action == HttpTransact::CACHE_DO_WRITE) {
      // Transform output cachable so initiate the transfer
      //   to the cache
      HttpTunnelProducer *p = setup_transfer_from_transform_to_cache_only();
      tunnel.tunnel_run(p);
    } else {
      // We aren't caching the transformed response abort the
      //  transform

      Debug("http",
            "[%" PRId64 "] [HttpUpdateSM] aborting "
            "transform since result is not cached",
            sm_id);
      HttpTunnelConsumer *c = tunnel.get_consumer(transform_info.vc);
      ink_release_assert(c != nullptr);

      if (tunnel.is_tunnel_active()) {
        default_handler = &HttpUpdateSM::tunnel_handler;
        if (c->alive == true) {
          // We're still streaming data to read
          //  side of the transform so abort it
          tunnel.handleEvent(VC_EVENT_ERROR, c->write_vio);
        } else {
          // The read side of the transform is done but
          //  the tunnel is still going, presumably streaming
          //  to the cache.  Just change the handler and
          //  wait for the tunnel to complete
          ink_assert(transform_info.entry->in_tunnel == false);
        }
      } else {
        // tunnel is not active so caching the untransformed
        //  copy is done - bail out
        ink_assert(transform_info.entry->in_tunnel == false);
        terminate_sm = true;
      }
    }
    break;
  }
  case HttpTransact::SM_ACTION_INTERNAL_CACHE_WRITE:
  case HttpTransact::SM_ACTION_SERVER_READ:
  case HttpTransact::SM_ACTION_INTERNAL_CACHE_NOOP:
  case HttpTransact::SM_ACTION_SEND_ERROR_CACHE_NOOP:
  case HttpTransact::SM_ACTION_SERVE_FROM_CACHE: {
    cb_event                     = HTTP_SCH_UPDATE_EVENT_NOT_CACHED;
    t_state.squid_codes.log_code = SQUID_LOG_TCP_MISS;
    terminate_sm                 = true;
    return;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_DELETE:
  case HttpTransact::SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS: {
    if (t_state.next_action == HttpTransact::SM_ACTION_INTERNAL_CACHE_DELETE) {
      cb_event = HTTP_SCH_UPDATE_EVENT_DELETED;
    } else {
      cb_event = HTTP_SCH_UPDATE_EVENT_UPDATED;
    }

    perform_cache_write_action();
    terminate_sm = true;
    return;
  }

  default: {
    ink_release_assert(!"Should not get here");
  }
  }
}

void
HttpUpdateSM::set_next_state()
{
  if (t_state.cache_info.action == HttpTransact::CACHE_DO_NO_ACTION || t_state.cache_info.action == HttpTransact::CACHE_DO_SERVE) {
    if (t_state.next_action == HttpTransact::SM_ACTION_SERVE_FROM_CACHE) {
      cb_event                     = HTTP_SCH_UPDATE_EVENT_NO_ACTION;
      t_state.squid_codes.log_code = SQUID_LOG_TCP_HIT;
    } else {
      t_state.squid_codes.log_code = SQUID_LOG_TCP_MISS;
    }

    terminate_sm = true;
    ink_assert(tunnel.is_tunnel_active() == false);
    return;
  }

  HttpSM::set_next_state();
}

int
HttpUpdateSM::kill_this_async_hook(int event, void * /* data ATS_UNUSED */)
{
  STATE_ENTER(&HttpUpdateSM::user_cb_handler, event, data);

  MUTEX_TRY_LOCK(lock, cb_action.mutex, this_ethread());

  if (!lock.is_locked()) {
    default_handler = (HttpSMHandler)&HttpUpdateSM::kill_this_async_hook;
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_CALL);
    return EVENT_DONE;
  }

  if (!cb_action.cancelled) {
    Debug("http", "[%" PRId64 "] [HttpUpdateSM] calling back user with event %s", sm_id, HttpDebugNames::get_event_name(cb_event));
    cb_cont->handleEvent(cb_event, nullptr);
  }

  cb_occured = true;

  return HttpSM::kill_this_async_hook(EVENT_NONE, nullptr);
}
