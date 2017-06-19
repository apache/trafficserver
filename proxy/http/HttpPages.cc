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

   HttpPages.cc

   Description:
       Data structurs and stat page generators for http info


 ****************************************************************************/
#include "HttpPages.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"

HttpSMListBucket HttpSMList[HTTP_LIST_BUCKETS];

HttpPagesHandler::HttpPagesHandler(Continuation *cont, HTTPHdr *header)
  : BaseStatPagesHandler(new_ProxyMutex()), request(nullptr), list_bucket(0), state(HP_INIT), sm_id(0)
{
  action = cont;

  URL *url;
  int length;

  url     = header->url_get();
  request = (char *)url->path_get(&length);
  request = arena.str_store(request, length);

  if (strncmp(request, "sm_details", sizeof("sm_details")) == 0) {
    arena.str_free(request);
    request = (char *)url->query_get(&length);
    request = arena.str_store(request, length);
    SET_HANDLER(&HttpPagesHandler::handle_smdetails);

  } else {
    SET_HANDLER(&HttpPagesHandler::handle_smlist);
  }
}

HttpPagesHandler::~HttpPagesHandler()
{
}

int64_t
HttpPagesHandler::extract_id(const char *query)
{
  char *p;
  int64_t id;

  p = (char *)strstr(query, "id=");
  if (!p) {
    return -1;
  }
  p += sizeof("id=") - 1;

  id = ink_atoi64(p);

  // Check to see if we found the id
  if (id == 0) {
    if (*p == '0' && *(p + 1) == '\0') {
      return 0;
    } else {
      return -1;
    }
  } else {
    return id;
  }
}

void
HttpPagesHandler::dump_hdr(HTTPHdr *hdr, const char *desc)
{
  if (hdr->valid()) {
    resp_add("<h4> %s </h4>\n<pre>\n", desc);
    char b[4096];
    int used, tmp, offset;
    int done;
    offset = 0;
    do {
      used = 0;
      tmp  = offset;
      done = hdr->print(b, 4095, &used, &tmp);
      offset += used;
      b[used] = '\0';
      resp_add(b);
    } while (!done);
    resp_add("</pre>\n");
  }
}

void
HttpPagesHandler::dump_tunnel_info(HttpSM *sm)
{
  HttpTunnel *t = sm->get_tunnel();

  resp_add("<h4> Tunneling Info </h4>");

  resp_add("<p> Producers </p>");
  resp_begin_table(1, 4, 60);
  for (auto &producer : t->producers) {
    if (producer.vc != nullptr) {
      resp_begin_row();

      // Col 1 - name
      resp_begin_column();
      resp_add(producer.name);
      resp_end_column();

      // Col 2 - alive
      resp_begin_column();
      resp_add("%d", producer.alive);
      resp_end_column();

      // Col 3 - ndone
      resp_begin_column();
      if (producer.alive && producer.read_vio) {
        resp_add("%d", producer.read_vio->ndone);
      } else {
        resp_add("%d", producer.bytes_read);
      }
      resp_end_column();

      // Col 4 - nbytes
      resp_begin_column();
      if (producer.alive && producer.read_vio) {
        resp_add("%d", producer.read_vio->nbytes);
      } else {
        resp_add("-");
      }
      resp_end_column();

      resp_end_row();
    }
  }
  resp_end_table();

  resp_add("<p> Consumers </p>");
  resp_begin_table(1, 5, 60);
  for (auto &consumer : t->consumers) {
    if (consumer.vc != nullptr) {
      resp_begin_row();

      // Col 1 - name
      resp_begin_column();
      resp_add(consumer.name);
      resp_end_column();

      // Col 2 - alive
      resp_begin_column();
      resp_add("%d", consumer.alive);
      resp_end_column();

      // Col 3 - ndone
      resp_begin_column();
      if (consumer.alive && consumer.write_vio) {
        resp_add("%d", consumer.write_vio->ndone);
      } else {
        resp_add("%d", consumer.bytes_written);
      }
      resp_end_column();

      // Col 4 - nbytes
      resp_begin_column();
      if (consumer.alive && consumer.write_vio) {
        resp_add("%d", consumer.write_vio->nbytes);
      } else {
        resp_add("-");
      }
      resp_end_column();

      // Col 5 - read avail
      resp_begin_column();
      if (consumer.alive && consumer.buffer_reader) {
        resp_add("%d", consumer.buffer_reader->read_avail());
      } else {
        resp_add("-");
      }
      resp_end_column();

      resp_end_row();
    }
  }
  resp_end_table();
}

void
HttpPagesHandler::dump_history(HttpSM *sm)
{
  resp_add("<h4> History</h4>");
  resp_begin_table(1, 3, 60);

  int size;

  // Figure out how big the history is and look
  //  for wrap around
  if (sm->history_pos > HISTORY_SIZE) {
    size = HISTORY_SIZE;
  } else {
    size = sm->history_pos;
  }

  for (int i = 0; i < size; i++) {
    char buf[256];
    resp_begin_row();

    resp_begin_column();
    resp_add("%s", sm->history[i].location.str(buf, sizeof(buf)));
    resp_end_column();

    resp_begin_column();
    resp_add("%u", (unsigned int)sm->history[i].event);
    resp_end_column();

    resp_begin_column();
    resp_add("%d", (int)sm->history[i].reentrancy);
    resp_end_column();

    resp_end_row();
  }

  resp_end_table();
}

int
HttpPagesHandler::dump_sm(HttpSM *sm)
{
  // Dump the current state
  const char *sm_state = HttpDebugNames::get_action_name(sm->t_state.next_action);

  resp_begin_item();
  resp_add("Current State: %s", sm_state);
  resp_end_item();

  dump_hdr(&sm->t_state.hdr_info.client_request, "Client Request");
  dump_hdr(&sm->t_state.hdr_info.server_request, "Server Request");
  dump_hdr(&sm->t_state.hdr_info.server_response, "Server Response");
  dump_hdr(&sm->t_state.hdr_info.client_response, "Client Response");

  dump_tunnel_info(sm);
  dump_history(sm);

  return EVENT_DONE;
}

int
HttpPagesHandler::handle_smdetails(int event, void * /* data ATS_UNUSED */)
{
  EThread *ethread = this_ethread();
  HttpSM *sm       = nullptr;

  switch (event) {
  case EVENT_NONE:
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    break;
  default:
    ink_assert(0);
    break;
  }

  // Do initial setup if necessary
  if (state == HP_INIT) {
    state = HP_RUN;

    // Get our SM id
    sm_id = extract_id(request);

    if (sm_id < 0) {
      resp_begin("Http Pages Error");
      resp_add("<b>Unable to extract id</b>\n");
      resp_end();
      return handle_callback(EVENT_NONE, nullptr);
    }

    resp_begin("Http:SM Details");
    resp_begin_item();
    resp_add("Details for SM id  %" PRId64 "", sm_id);
    resp_end_item();
  }

  for (; list_bucket < HTTP_LIST_BUCKETS; list_bucket++) {
    MUTEX_TRY_LOCK(lock, HttpSMList[list_bucket].mutex, ethread);

    if (!lock.is_locked()) {
      eventProcessor.schedule_in(this, HTTP_LIST_RETRY, ET_CALL);
      return EVENT_DONE;
    }

    sm = HttpSMList[list_bucket].sm_list.head;

    while (sm != nullptr) {
      if (sm->sm_id == sm_id) {
        // In this block we try to get the lock of the
        //   state machine
        {
          MUTEX_TRY_LOCK(sm_lock, sm->mutex, ethread);
          if (sm_lock.is_locked()) {
            dump_sm(sm);
            resp_end();
            return handle_callback(EVENT_NONE, nullptr);
          } else {
            // We missed the lock so retry
            eventProcessor.schedule_in(this, HTTP_LIST_RETRY, ET_CALL);
            return EVENT_DONE;
          }
        }
      }

      sm = sm->debug_link.next;
    }
  }

  // If we got here, we did not find our state machine
  resp_add("<h2>Id %" PRId64 " not found</h2>", sm_id);
  resp_end();
  return handle_callback(EVENT_NONE, nullptr);
}

int
HttpPagesHandler::handle_smlist(int event, void * /* data ATS_UNUSED */)
{
  EThread *ethread = this_ethread();
  HttpSM *sm;

  switch (event) {
  case EVENT_NONE:
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    break;
  default:
    ink_assert(0);
    break;
  }

  if (state == HP_INIT) {
    resp_begin("Http:SM List");
    state = HP_RUN;
  }

  for (; list_bucket < HTTP_LIST_BUCKETS; list_bucket++) {
    MUTEX_TRY_LOCK(lock, HttpSMList[list_bucket].mutex, ethread);

    if (!lock.is_locked()) {
      eventProcessor.schedule_in(this, HTTP_LIST_RETRY, ET_CALL);
      return EVENT_DONE;
    }

    sm = HttpSMList[list_bucket].sm_list.head;

    while (sm != nullptr) {
      char *url          = nullptr;
      const char *method = nullptr;
      int method_len;
      const char *sm_state = nullptr;

      // In this block we try to get the lock of the
      //   state machine
      {
        MUTEX_TRY_LOCK(sm_lock, sm->mutex, ethread);
        if (sm_lock.is_locked()) {
          if (sm->t_state.hdr_info.client_request.valid()) {
            sm_state = HttpDebugNames::get_action_name(sm->t_state.next_action);

            method = sm->t_state.hdr_info.client_request.method_get(&method_len);
            method = arena.str_store(method, method_len);
            URL *u = sm->t_state.hdr_info.client_request.url_get();
            if (u->valid()) {
              url = u->string_get(&arena);
            }
          }

          if (url == nullptr) {
            url      = arena.str_store("-", 1);
            sm_state = "READ_REQUEST";
          }
        } else {
          url      = arena.str_store("-", 1);
          sm_state = "LOCKED";
        }
      }

      resp_begin_item();
      resp_add("id: <a href=\"./sm_details?id=%" PRId64 "\"> %" PRId64 " </a> | %s %s | %s\n", sm->sm_id, sm->sm_id,
               method ? method : "", url, sm_state ? sm_state : "");
      resp_end_item();
      arena.str_free(url);

      sm = sm->debug_link.next;
    }
  }

  resp_end();
  handle_callback(EVENT_NONE, nullptr);

  return EVENT_DONE;
}

int
HttpPagesHandler::handle_callback(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  MUTEX_TRY_LOCK(trylock, action.mutex, this_ethread());
  if (!trylock.is_locked()) {
    SET_HANDLER(&HttpPagesHandler::handle_callback);
    eventProcessor.schedule_in(this, HTTP_LIST_RETRY, ET_CALL);
    return EVENT_DONE;
  }

  if (!action.cancelled) {
    if (response) {
      StatPageData data;

      data.data   = response;
      data.type   = ats_strdup("text/html");
      data.length = response_length;
      response    = nullptr;

      action.continuation->handleEvent(STAT_PAGE_SUCCESS, &data);
    } else {
      action.continuation->handleEvent(STAT_PAGE_FAILURE, nullptr);
    }
  }

  delete this;
  return EVENT_DONE;
}

static Action *
http_pages_callback(Continuation *cont, HTTPHdr *header)
{
  HttpPagesHandler *handler;

  handler = new HttpPagesHandler(cont, header);
  eventProcessor.schedule_imm(handler, ET_CALL);

  return &handler->action;
}

void
http_pages_init()
{
  statPagesManager.register_http("http", http_pages_callback);

  // Create the mutexes for http list protection
  for (auto &i : HttpSMList) {
    i.mutex = new_ProxyMutex();
  }
}
