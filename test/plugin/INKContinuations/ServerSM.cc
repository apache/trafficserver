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

/***************************************************************************
 ServerSM
 ***************************************************************************/

#include "ServerSM.h"

/* stats */
extern RaftList_t *server_stats_queue;

extern GlobalFDTable *global_table;
static int current_sub_sm_id = 0;

int
server_handler(TSCont contp, TSEvent event, void *data)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ServerSMHandler q_current_handler = Server->q_server_current_handler;
  return (*q_current_handler) (contp, event, data);
}

RaftServerStatsObject *
RaftServerStatsObjectCreate(TSCont contp)
{
  RaftServerStatsObject *server_stats = (RaftServerStatsObject *) TSmalloc(sizeof(RaftServerStatsObject));
  server_stats->q_start_time = time(NULL);
  server_stats->q_finished = 0;
  server_stats->q_count_bytes_one_server = 0;
  server_stats->q_count_finished_requests = 0;
  server_stats->q_count_server_pipeline_depth = 0;

  server_stats->next = NULL;

  return server_stats;
}

/***********************************************************************/
/***                 The state machine functions                     ***/
/***********************************************************************/


///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
TSCont
ServerSMCreate(TSMutex pmutex)
{
  ServerSM *Server = (ServerSM *) TSmalloc(sizeof(ServerSM));
  TSCont contp;

  Server->q_mutex = pmutex;
  Server->q_sm_id = server_sm_id_counter++;
  Server->q_pending_action = NULL;
  Server->q_protocol = HTTP_PROTOCOL;

  Server->q_server_name = NULL;
  Server->q_server_ip = 0;
  Server->q_server_port = 0;
/*
    Server->q_server_request_buffer= NULL;
    Server->q_server_request_buffer_reader= NULL;
    Server->q_server_response_buffer= NULL;
    Server->q_server_response_buffer_reader= NULL;
*/
  Server->q_server_vc = NULL;
  Server->q_server_read_vio = NULL;
  Server->q_server_write_vio = NULL;
/*
    Server->q_sending_sms= NULL;
    Server->q_sms_to_call_back= NULL;

    Server->q_reading_sub_contp= NULL;
    //    Server->q_reading_sub_contp_action= NULL;
*/
  Server->q_reading_header = 1;
  Server->q_server_conn_status = NO_CONNECTION;
  Server->q_server_calling_back_status = CONNECTED_IDLE;

  TSDebug("serversm", "[ServerSM][%u] created new server sm", Server->q_sm_id);
  contp = TSContCreate(server_handler, pmutex);
  TSContDataSet(contp, Server);

  // stats
  Server->server_stats = (RaftServerStatsObject *) RaftServerStatsObjectCreate(contp);
  add_item_to_raft_list(&server_stats_queue, (void *) Server->server_stats);
  return contp;
}


//////////////////////////////////////////////////////////////////////////
//
// init_parameters()
//
//
//////////////////////////////////////////////////////////////////////////
void
init_parameters(TSCont contp, int glbl_indx, GlobalFDTableEntry * glbl_ptr, TSCont the_sub_contp)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  Server->q_sending_sms = NULL;
  Server->q_sms_to_call_back = NULL;

  Server->q_reading_sub_contp = NULL;
  Server->q_reading_header = 1;

  Server->q_global_table_index = glbl_indx;
  Server->q_global_table_ptr = glbl_ptr;

/*
    Server->q_server_request_buffer = NULL;
    Server->q_server_request_buffer_reader = NULL;

    Server->q_server_response_buffer = NULL;
    Server->q_server_response_buffer_reader = NULL;


    Server->q_server_response_buffer = new_TSIOBuffer (RAFT_DEFAULT_BUFFER_INDEX_SIZE);
    Server->q_server_response_buffer_reader = Server->q_server_response_buffer->alloc_reader();
*/
  // set the watermark on the buffer to the
  // RAFT response header size so that the
  // net processor only notifies us if the
  // full header is there.
  // API: IOBuffer's water_mark
  TSIOBufferWaterMarkSet(Server->q_server_response_buffer, RAFT_HEADER_SIZE);

  assert(Server->q_server_response_buffer);
  assert(Server->q_server_response_buffer_reader);

  Server->q_server_conn_status = CONNECTED_IDLE;
}

//////////////////////////////////////////////////////////////////////////
//
// init()
//
//
//////////////////////////////////////////////////////////////////////////
void
server_sm_init(TSCont contp,
               char *name,
               int port, Protocols the_protocol, int glbl_indx, GlobalFDTableEntry * glbl_ptr, TSCont the_sub_contp)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  if (name) {
    Server->q_server_name = (char *) TSmalloc(sizeof(char) * (strlen(name) + 1));
    strcpy(Server->q_server_name, name);
  } else {
    Server->q_server_name = NULL;
  }
  Server->q_server_port = port;

  Server->q_protocol = the_protocol;

  // The following two parameters can't be initiated in init_parameters()
  Server->q_server_response_buffer = NULL;
  Server->q_server_response_buffer_reader = NULL;

  Server->q_server_response_buffer = TSIOBufferCreate();       // (RAFT_DEFAULT_BUFFER_INDEX_SIZE);
  Server->q_server_response_buffer_reader = TSIOBufferReaderAlloc(Server->q_server_response_buffer);

  init_parameters(contp, glbl_indx, glbl_ptr, the_sub_contp);
  add_item_to_raft_list(&(Server->q_sms_to_call_back), the_sub_contp);
  assert(Server->q_server_vc == NULL);

  TSDebug("serversm", "[%u][init] No Connection Yet", Server->q_sm_id);
  if (Server->q_server_ip == 0) {
    assert(Server->q_server_name);

    // issue dns lookup of hostname.
    TSDebug("serversm", "[%u][init] No Server IP - issuing DNS lookup of %s", Server->q_sm_id, Server->q_server_name);

    set_handler(Server->q_server_current_handler, &state_dns_lookup);
    Server->q_pending_action = TSDNSLookup(contp, Server->q_server_name, strlen(Server->q_server_name));
  } else {
    assert(Server->q_server_ip > 0);
    assert(Server->q_server_port > 0);

    // issue server connect.
    unsigned char *p = (unsigned char *) (&Server->q_server_ip);
    TSDebug("serversm", "[%u][init] %s has resolved to %d.%d.%d.%d:%d. Connecting",
             Server->q_sm_id, ((Server->q_server_name) ? Server->q_server_name : "<>"),
             p[0], p[1], p[2], p[3], Server->q_server_port);

//      SET_HANDLER((ServerSMHandler)&state_connect_to_server);
    set_handler(Server->q_server_current_handler, &state_connect_to_server);
    Server->q_pending_action = TSNetConnect(contp, Server->q_server_ip, Server->q_server_port);
  }

  return;
}

//////////////////////////////////////////////////////////////////////////
//
// accept_new_raft_command()
//
//
//////////////////////////////////////////////////////////////////////////
int
accept_new_raft_command(TSCont contp, TSCont sub_contp)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  RaftSubSM *SubSM = (RaftSubSM *) TSContDataGet(sub_contp);
  current_sub_sm_id = SubSM->q_sm_id;

  TSDebug("serversm", "[accept_new_raft_command] accept_new_raft_command of sub_sm %d", current_sub_sm_id);

  add_item_to_raft_list(&(Server->q_sms_to_call_back), (void *) sub_contp);
  server_send_request(contp);
  TSDebug("serversm", "leaving accept_new_raft_command");

  return TS_EVENT_IMMEDIATE;
}

//////////////////////////////////////////////////////////////////////////
//
// set_current_reading_sub_sm()
//
//
//////////////////////////////////////////////////////////////////////////
void
server_send_request(TSCont contp)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  assert(Server->q_server_conn_status != CONNECTED_SENDING);
  Server->q_server_conn_status = CONNECTED_SENDING;
/*
    if (Server->q_server_request_buffer != NULL)
	Server->q_server_request_buffer->reset();

    TSDebug("serversm",
	  "[send_request] first pop up from sending_sms and then add it in call back queue");
    RaftSubSM *current_sub_sm = (RaftSubSM *) pop_item_from_raft_list(&Server->q_sending_sms);
    assert(current_sub_sm != NULL);

    add_item_to_raft_list(&Server->q_sms_to_call_back, current_sub_sm);
    Server->q_server_request_buffer = ((RaftSubSM*)current_sub_sm)->Server->q_server_request_buffer;
    Server->q_server_request_buffer_reader =
	((RaftSubSM*)current_sub_sm)->Server->q_server_request_buffer_reader;
*/
  assert(Server->q_server_request_buffer);
  assert(Server->q_server_request_buffer_reader);

  int req_len = TSIOBufferReaderAvail(Server->q_server_request_buffer_reader);
  assert(req_len > 0);

  set_handler(Server->q_server_current_handler, &state_main_event);
  assert(Server->q_server_vc != NULL);
//    Server->q_server_vc->set_inactivity_timeout(RAFT_SERVER_INACTIVITY_TIMEOUT);

  // write down the request now.
  TSDebug("serversm", "[%u][send_request] sending %d bytes of request now.", Server->q_sm_id, req_len);
  Server->q_server_write_vio = TSVConnWrite(Server->q_server_vc, contp,
                                             Server->q_server_request_buffer_reader, req_len);

  assert(Server->q_server_write_vio);
}

//////////////////////////////////////////////////////////////////////////
//
// set_current_reading_sub_sm()
//
//
//////////////////////////////////////////////////////////////////////////
void
set_current_reading_sub_sm(TSCont contp, TSCont * current_contp, unsigned int seq_num)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  TSDebug("serversm", "[%u][set_current_reading_sub_sm] Header says sequence_number is %u.", Server->q_sm_id, seq_num);
  if (Server->q_sms_to_call_back) {
    TSDebug("serversm",
             "[%u][set_current_reading_sub_sm] call back subsm queue is not NULL.", Server->q_sm_id, seq_num);
  } else {
    TSDebug("serversm", "[%u][set_current_reading_sub_sm] call back subsm queue is NULL.", Server->q_sm_id, seq_num);
  }

  // use the sequence number to dequeue the relevant sub sm
  *current_contp = search_raft_list_for_seq_num(&Server->q_sms_to_call_back, seq_num);

  // remove this puppy from the waiting list
  if (*current_contp == NULL) {
    TSDebug("serversm", "[%u][set_current_reading_sub_sm], can't find current_sm matching seq_num", Server->q_sm_id);
    assert(0);
  }

  int successful = remove_item_from_raft_list(&Server->q_sms_to_call_back, *current_contp);
  assert(successful);
}



//////////////////////////////////////////////////////////////////////////
//
// state_main_event(int event, NetTSVConn *vc)
//
//
//////////////////////////////////////////////////////////////////////////
int
state_main_event(TSCont contp, int event, void *data)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_main_event, event);
  TSDebug("serversm", "[%u][state_main_event] entering state_main_event, event is %d", Server->q_sm_id, event);

  int bytes_read = 0;

  switch (event) {
  case TS_EVENT_TIMEOUT:
    TSDebug("serversm", "[%u][state_main_event]state_main_event, INTERVAL", Server->q_sm_id);

    state_call_back_sub_sm(contp, event, (TSVIO) data);
    bytes_read = TSIOBufferReaderAvail(Server->q_server_response_buffer_reader);
    if (bytes_read > 0)
      state_read_response_from_server(contp, TS_EVENT_VCONN_READ_READY, Server->q_server_read_vio);
    bytes_read = TSIOBufferReaderAvail(Server->q_server_response_buffer_reader);
    TSDebug("serversm",
             "[%u][state_main_event]state_main_event, INTERVAL, after read_response_from_server, there are %d bytes in response_buffer)",
             Server->q_sm_id, bytes_read);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug("serversm", "[%u][state_main_event]state_main_event, WRITE_READY/COMPLETE", Server->q_sm_id);

    assert(data != NULL);
    assert((TSVIO) data == Server->q_server_write_vio);
    assert(TSVIOVConnGet((TSVIO) data) == Server->q_server_vc);
    state_wait_for_write(contp, event, (TSVIO) data);
    break;
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    TSDebug("serversm", "[%u][state_main_event]state_main_event, READ_READY/COMPLETE", Server->q_sm_id);

    assert(data != NULL);
    assert((TSVIO) data == Server->q_server_read_vio);
    assert(TSVIOVConnGet((TSVIO) data) == Server->q_server_vc);
    state_read_response_from_server(contp, event, (TSVIO) data);
    bytes_read = TSIOBufferReaderAvail(Server->q_server_response_buffer_reader);
    TSDebug("serversm",
             "[%u][state_main_event]state_main_event, READ_READY/COMPLETE, after read_response_from_server, there are %d bytes in response_buffer)",
             Server->q_sm_id, bytes_read);

    break;
/*       case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
	 TSDebug("serversm","[%u][state_main_event]state_main_event, INACTIVITY_TIME_OUT",
	 Server->q_sm_id);

	 return TS_EVENT_IMMEDIATE;
*/
  default:
    TSDebug("serversm", "[%u][state_main_event]state_main_event, default", Server->q_sm_id);

    // it can be error cases, or other cases that I don't know.
    break;
  }
  TSDebug("serversm",
           "[%u][state_main_event]state_main_event, leaving main_event, current event is %d", Server->q_sm_id, event);

  return TS_EVENT_IMMEDIATE;
}

//////////////////////////////////////////////////////////////////////////
//
// state_wait_for_write
//
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////
int
state_wait_for_write(TSCont contp, int event, TSVIO vio)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_wait_for_write, event);

  assert(event);
  switch (event) {
    // How can it come to WRITE_READY? The whole request should
    // be writen out.
  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug("serversm", "[%u][state_wait_for_write] TS_EVENT_VCONN_WRITE_READY", Server->q_sm_id);

    TSVIOReenable(vio);
    return TS_EVENT_IMMEDIATE;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    assert(vio != NULL);
    assert(vio == Server->q_server_write_vio);
    assert(TSVIOVConnGet(vio) == Server->q_server_vc);
    TSDebug("serversm", "[%u][state_wait_for_write] TS_EVENT_VCONN_WRITE_COMPLETE", Server->q_sm_id);
    assert(Server->q_server_conn_status == CONNECTED_SENDING);
    Server->q_server_conn_status = CONNECTED_IDLE;
    TSDebug("serversm",
             "[%u][state_wait_for_write] Server has read full request of sub_sm %d",
             Server->q_sm_id, current_sub_sm_id);

//          Server->q_server_vc->cancel_inactivity_timeout();

    // ok, we are finally done with the sending_sub_sm.
    // clear the variable, etc. to allow other sub sms
    // to send their requests.
    Server->q_server_request_buffer = NULL;
    Server->q_server_request_buffer_reader = NULL;

    // well, the server has read the request.
    // clear the vio.
    Server->q_server_write_vio = NULL;

    // now we gotta wait for the response. make sure
    // there is a buffer to read the response into.
    // this buffer and its reader are initiated in init()
    assert(Server->q_server_response_buffer);
    assert(Server->q_server_response_buffer_reader);

    TSDebug("serversm", "[%u][state_wait_for_write] Starting server read vio", Server->q_sm_id);

//          SET_HANDLER((ServerSMHandler)&state_main_event);
//          Server->q_server_vc->set_inactivity_timeout(RAFT_SERVER_INACTIVITY_TIMEOUT);
/*
	    Server->q_server_read_vio = Server->q_server_vc->do_io_read(this,
							RAFT_DEFAULT_BUFFER_SIZE,
							Server->q_server_response_buffer);
*/
    assert(Server->q_server_read_vio != NULL);
    TSDebug("serversm", "leaving state_wait_for_write");
    return TS_EVENT_IMMEDIATE;

  default:
    //      Server->q_error_type = RAFT_INTERNAL_PROXY_ERROR;
    TSDebug("serversm", "[%u][state_wait_for_write] unexpected event", Server->q_sm_id);
    assert(0);
    if (Server->q_server_vc) {
      TSVConnAbort(Server->q_server_vc, 1);
      Server->q_server_vc = NULL;
    }
    Server->q_server_read_vio = NULL;
    Server->q_server_write_vio = NULL;

    return call_back_sub_sm_with_error(contp);
  }
}

//////////////////////////////////////////////////////////////////////////
//
// state_read_response_from_server
//
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////
int
state_read_response_from_server(TSCont contp, int event, TSVIO vio)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  int bytes_read = 0;
  ReturnStatusCode parse_error = RAFTERR_SUCCESS;

  ENTER_STATE(serversm, Server->q_sm_id, state_read_response_from_server, event);

  // if ServerSM is blocked in state_call_back_sub_sm, which means
  // data in Server->q_server_response_buffer hasn't been consumed yet,
  // don't read out more data.

//    Server->q_server_vc->cancel_inactivity_timeout();
/*
    assert (Server->q_server_conn_status == CONNECTED_READING ||
			Server->q_server_conn_status == CONNECTED_IDLE);
    Server->q_server_conn_status = CONNECTED_READING;
*/

  assert(Server->q_protocol == RAFT_PROTOCOL);
  assert(vio != NULL);
  assert(vio == Server->q_server_read_vio);
  assert(TSVIOVConnGet(vio) == Server->q_server_vc);
  assert(Server->q_server_response_buffer);
  assert(Server->q_server_response_buffer_reader);

  bytes_read = TSIOBufferReaderAvail(Server->q_server_response_buffer_reader);
  assert(bytes_read >= 0);

  TSDebug("serversm",
           "[%u][state_read_response_from_server] entering this state, event is %d", Server->q_sm_id, event);
  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    while (bytes_read >= 0) {
      if (Server->q_server_calling_back_status == CONNECTED_CALLING_BACK)
        return TS_EVENT_IMMEDIATE;

      bytes_read = TSIOBufferReaderAvail(Server->q_server_response_buffer_reader);
      TSDebug("serversm", "[%u][state_read_response_from_server] bytes_read is %d", Server->q_sm_id, bytes_read);
      if (Server->q_reading_header && (bytes_read >= RAFT_HEADER_SIZE)) {
        TSDebug("serversm", "[%u][state_read_response_from_server] server response reading header", Server->q_sm_id);
        TSDebug("serversm", "********Read Header******");
        RAFT_READ_HEADER(Server->q_hdr_proc, Server->q_hdr_seq_num, Server->q_hdr_nbytes,
                         Server->q_hdr_status, parse_error,
                         Server->q_server_response_buffer, Server->q_server_response_buffer_reader);

        if (parse_error != RAFTERR_SUCCESS) {
          TSDebug("serversm",
                   "[%u][state_read_response_from_server] ERROR (%d) : Invalid header..", Server->q_sm_id, parse_error);

          TSDebug("serversm",
                   "[%u][state_read_response_from_server] Connection Error (event %d), vio: %s",
                   Server->q_sm_id, event,
                   (((TSVIO) vio == Server->q_server_read_vio) ? "server_read" :
                    (((TSVIO) vio == Server->q_server_write_vio) ? "server_write" : "god-knows")));
          assert(0);
          if (Server->q_server_vc) {
            TSVConnAbort(Server->q_server_vc, 1);
            Server->q_server_vc = NULL;
          }
          Server->q_server_read_vio = NULL;
          Server->q_server_write_vio = NULL;

          return call_back_sub_sm_with_error(contp);
        } else {
          TSDebug("serversm",
                   "[%u][state_read_response_from_server] header is: %d %d %d %d",
                   Server->q_sm_id, Server->q_hdr_proc, Server->q_hdr_seq_num,
                   Server->q_hdr_nbytes, Server->q_hdr_status);

          set_current_reading_sub_sm(contp, &Server->q_reading_sub_contp, Server->q_hdr_seq_num);

          Server->q_reading_header = 0;
          // reset the watermark in order to force the
          // callback when there is a full body to read
          TSIOBufferWaterMarkSet(Server->q_server_response_buffer, Server->q_hdr_nbytes);
          TSDebug("serversm",
                   "[state_read_response_from_server] header of seq_num (%d) is totally read in, current Server->q_reading_sub_contp is %p",
                   Server->q_hdr_seq_num, Server->q_reading_sub_contp);
          TSDebug("serversm", "*********Read Header DONE*****");
        }
      } else if (!Server->q_reading_header && (bytes_read >= Server->q_hdr_nbytes)) {
        TSDebug("serversm", "$$$$$$$$Read body$$$$$$");

        TSDebug("serversm",
                 "[%u][state_read_response_from_server] Server response reading body for seq{%d} proc[%d] len is %d, status is %d",
                 Server->q_sm_id, Server->q_hdr_seq_num, Server->q_hdr_proc, Server->q_hdr_nbytes,
                 Server->q_hdr_status);


        assert(Server->q_reading_sub_contp);

        TSDebug("serversm",
                 "[state_read_response_from_server] body of seq_num (%d) is totally read in, current Server->q_reading_sub_contp is %p",
                 Server->q_hdr_seq_num, Server->q_reading_sub_contp);
        TSDebug("serversm", "$$$$$$$$Read body DONE$$$$$$$");
        Server->q_server_calling_back_status = CONNECTED_CALLING_BACK;
        state_call_back_sub_sm(contp, 0, NULL);
        // API: buffer's water mark
        TSIOBufferWaterMarkSet(Server->q_server_response_buffer, RAFT_HEADER_SIZE);
        Server->q_reading_header = 1;

        //return state_main_event(0, NULL);
      }
      // There are not enough data for reading
      else {
        TSDebug("serversm",
                 "[%u][state_read_response_from_server] server response (%s) only %d read - reenabling",
                 Server->q_sm_id, (Server->q_reading_header ? "reading header" : "reading body"), bytes_read);
/*
		    TSDebug("serversm","[%u][state_read_response_from_server] current handler is %s",
			  Server->q_sm_id, this->handler_name);
*/
        TSDebug("serversm", "[%u][state_read_response_from_server] print Server->q_reading_sub_contps list %p",
                 Server->q_sm_id);
        print_list_1(&Server->q_sms_to_call_back);
        // reenable the vio just in case the buffer was full earlier.
        TSVIOReenable(Server->q_server_read_vio);

        return TS_EVENT_IMMEDIATE;
      }
    }
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    assert(!"[serversm], [state_read_response_from_server], invalid events.");
    return call_back_sub_sm_with_error(contp);
  default:
    // there should be some "invalid event" assertion. Think carefully
    // what race condition can happen here.
    break;
  }
  return TS_EVENT_IMMEDIATE;
}


//////////////////////////////////////////////////////////////////////////
//
// state_call_back_sub_sm
//
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////
int
state_call_back_sub_sm(TSCont contp, int event, TSVIO vio)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  RaftSubSM *SubSM = (RaftSubSM *) TSContDataGet(Server->q_reading_sub_contp);

  TSIOBufferBlock blk;
  char *buf;
  int avail;

  ENTER_STATE(serversm, Server->q_sm_id, state_call_back_sub_sm, event);

  assert(Server->q_server_calling_back_status == CONNECTED_CALLING_BACK);
  TSDebug("serversm", "[%u][state_call_back_sub_sm] try to grab subsm's mutex", Server->q_sm_id);

  int lock3;

  TSMutexLockTry(SubSM->q_mutex, &lock3);
  if (!lock3) {
    set_handler(Server->q_server_current_handler, &state_main_event);
    Server->q_pending_action = TSContSchedule(contp, RAFT_SERVER_LOCK_RETRY_TIME, TS_THREAD_POOL_DEFAULT);
    return TS_EVENT_NONE;
  }

  TSDebug("serversm", "[%u][state_call_back_sub_sm] subsm[%d]'s mutex is grabbed", Server->q_sm_id, SubSM->q_sm_id);

  SubSM->q_client_request->response->proc = Server->q_hdr_proc;
  SubSM->q_client_request->response->seq_num = Server->q_hdr_seq_num;
  SubSM->q_client_request->response->nbytes = Server->q_hdr_nbytes;
  SubSM->q_client_request->response->status = Server->q_hdr_status;

  TSDebug("serversm", "[state_call_back_sub_sm] consume %d bytes from response_buffer", Server->q_hdr_nbytes);
  assert(Server->q_server_response_buffer_reader);
  assert(Server->q_server_response_buffer);

  TSDebug("serversm", "[state_call_back_sub_sm] reader_avail is %d",
           TSIOBufferReaderAvail(Server->q_server_response_buffer_reader));

  TSIOBufferCopy(SubSM->q_client_request->response->resp_buffer,
                  Server->q_server_response_buffer_reader, Server->q_hdr_nbytes, 0);
  TSIOBufferReaderConsume(Server->q_server_response_buffer_reader, Server->q_hdr_nbytes);

  TSDebug("serversm",
           "[state_call_back_sub_sm] sub_sm's incoming and outgoing seq_num are %d, %d, proc is %d",
           SubSM->q_incoming_seq_num, SubSM->q_outgoing_seq_num, SubSM->q_client_request->proc);
  //API: TSContCall doesn't know RAFT_EVENT_RESPONSE_RECEIVED, so I have to
  // change to an event in TSEvent list.
  // TSContCall (Server->q_reading_sub_contp, RAFT_EVENT_RESPONSE_RECEIVED, NULL);
  TSDebug("serversm", "[state_call_back_sub_sm] call back current_sub_contp");
  TSMutexUnlock(SubSM->q_mutex);

  // stats
  Server->server_stats->q_count_finished_requests++;
  Server->server_stats->q_count_bytes_one_server += (RAFT_HEADER_SIZE + Server->q_hdr_nbytes);
  Server->server_stats->q_count_server_pipeline_depth--;

//    TSContCall (Server->q_reading_sub_contp, TS_EVENT_CONTINUE, NULL);
  TSContSchedule(Server->q_reading_sub_contp, 0, TS_THREAD_POOL_DEFAULT);

  TSDebug("serversm", "[state_call_back_sub_sm] release sub_sm's mutex");
  Server->q_reading_sub_contp = NULL;
  Server->q_server_calling_back_status = CONNECTED_IDLE;

  TSDebug("serversm", "Server->q_sending_sms is %p ", Server->q_sending_sms);

  return TS_EVENT_IMMEDIATE;
  //return state_main_event(0, NULL);
}


//////////////////////////////////////////////////////////////////////////
//
// call_back_sub_sm_with_error()
//
//
//////////////////////////////////////////////////////////////////////////
int
call_back_sub_sm_with_error(TSCont contp)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  // call back all the current sending and reading sub_sms
  // with an error now. in other words go through the waiting
  // list and move every damn sm from it to the waiting to be
  // called back queue and call each of the buggers back with
  // an error message.


  // this is, of course, done by adding the current reading
  // sub_sms to the waiting_to_be_called_back list and
  // calling the state_call_back_sub_sm function.
  // easy as blueberry pie.
  if (Server->q_reading_sub_contp) {
    // as a sanity check try to remove this from the Server->q_sending_sms list - should fail.
    int successful = remove_item_from_raft_list(&Server->q_sending_sms,
                                                Server->q_reading_sub_contp);
    assert(!successful);

    add_item_to_raft_list(&(Server->q_sms_to_call_back), Server->q_reading_sub_contp);

    Server->q_reading_sub_contp = NULL;
  }
  // we need to iterate over items in Server->q_sending_sms as
  // well and add them to the Server->q_sms_to_call_back, then make
  // sure they all get called back with errors.
  while (Server->q_sending_sms) {
    TSCont waiting_sub_sm = NULL;

    waiting_sub_sm = (TSCont) pop_item_from_raft_list(&Server->q_sending_sms);

    assert(waiting_sub_sm);
    add_item_to_raft_list(&(Server->q_sms_to_call_back), waiting_sub_sm);
  }

  Server->q_server_request_buffer = NULL;
  Server->q_server_request_buffer_reader = NULL;
  Server->q_server_conn_status = CONNECTED_IDLE;

  // well, we encountered an error. if there are some sms
  // left to call back, call them back, else shut down.

  if (Server->q_sms_to_call_back) {
//      SET_HANDLER((ServerSMHandler)&state_call_back_sub_sm);
    set_handler(Server->q_server_current_handler, &state_call_back_sub_sm);
    return state_call_back_sub_sm(contp, 0, NULL);
  } else {
    // actually, we should not shut down if the global_table_ptr
    // refcount is greater than 0. This is because there could be RaftSubSMs
    // waiting around trying to get this server sm's lock and they have a
    // pointer to this server_sm.
//      SET_HANDLER((ServerSMHandler)&state_prepare_to_die);
    set_handler(Server->q_server_current_handler, &state_prepare_to_die);
    return state_prepare_to_die(contp, 0, NULL);
  }
}

//////////////////////////////////////////////////////////////////////////
//
// state_prepare_to_die
//
// Updates global table to indicate that the server connection
// is closed. Sticks around till the refcount goes to 0, then
// shuts down.
//
// Next States:
//  - state_done
//
//////////////////////////////////////////////////////////////////////////
int
state_prepare_to_die(TSCont contp, int event, TSVIO vio)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_prepare_to_die, event);

  int refcount = -1;
  // no connection. this flag will
  // inform any sub_sms trying to grab this
  // server_sm's lock or trying to get it to
  // accept a new command that the connection
  // is broken and therefore to bail out.
  Server->q_server_conn_status = NO_CONNECTION;

  // update the global table entry
  TSDebug("servers,", "[%u][state_prepare_to_die] trying to grab global_table's entry_mutex", Server->q_sm_id);

  int lock;
  TSMutexLockTry(global_table->entry[Server->q_global_table_index].entry_mutex, &lock);
  if (!lock) {
    Server->q_pending_action = TSContSchedule(contp, RAFT_GLOBAL_TABLE_LOCK_RETRY_TIME, TS_THREAD_POOL_DEFAULT);
    return TS_EVENT_NONE;
  }

  Server->q_global_table_ptr->server_fd = -1;
  Server->q_global_table_ptr->conn_state = NO_CONNECTION;
  Server->q_global_table_ptr->server_contp = 0;
  refcount = Server->q_global_table_ptr->refcount;

  TSMutexUnlock(global_table->entry[Server->q_global_table_index].entry_mutex);

  TSDebug("serversm", "[%u][state_prepare_to_die] global_table's entry_mutex is released", Server->q_sm_id);

  // no sub_sms have a pointer to this server_sm.
  // safe to go away.
  if (refcount == 0)
    return server_state_done(contp, 0, NULL);

  // not yet safe to go away. retry later.
  Server->q_pending_action = TSContSchedule(contp, RAFT_SERVER_ATTEMPT_SHUT_DOWN_RETRY_TIME, TS_THREAD_POOL_DEFAULT);
  return TS_EVENT_IMMEDIATE;
}

//////////////////////////////////////////////////////////////////////////
//
// state_done
//
// Shuts down the state machine. Cancels pending actions, closes
// open connections, increments stats, flushes log buffers and
// finally deallocates memory and closes down shop.
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////
int
server_state_done(TSCont contp, int event, TSVIO vio)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_done, event);

  TSDebug("serversm", "[%u][state_done]This state machine is done!", Server->q_sm_id);

  // cancel any pending action
  if (Server->q_pending_action && !TSActionDone(Server->q_pending_action))
    TSActionCancel(Server->q_pending_action);

  Server->q_pending_action = NULL;

  //    Server->q_mutex = NULL;

  if (Server->q_server_name) {
    TSfree(Server->q_server_name);
    Server->q_server_name = NULL;
  }
  // this is a pointer to a buffer in calling raft_sub_sms,
  // so just reset the pointers to the buffer and reader
  if (Server->q_server_request_buffer) {
    Server->q_server_request_buffer = NULL;
    Server->q_server_request_buffer_reader = NULL;
  }
  // this was actually created by the server_sm, so
  // deallocate resources associated with it and
  // reset pointers
  if (Server->q_server_response_buffer) {
    if (Server->q_server_response_buffer_reader)
      TSIOBufferReaderFree(Server->q_server_response_buffer_reader);
    TSIOBufferDestroy(Server->q_server_response_buffer);
    Server->q_server_response_buffer = NULL;
    Server->q_server_response_buffer_reader = NULL;
  }
  //assert (Server->q_server_vc == NULL);
  Server->q_server_vc = NULL;
  Server->q_server_read_vio = NULL;
  Server->q_server_write_vio = NULL;

  assert(Server->q_sending_sms == NULL);
  //??? Server->q_reading_sub_contp should be NULL here
  //assert (Server->q_reading_sub_contp == NULL);
  if (Server->q_reading_sub_contp)
    Server->q_reading_sub_contp = NULL;
  //assert (Server->q_sms_to_call_back == NULL);
  if (Server->q_sms_to_call_back)
    Server->q_sms_to_call_back = NULL;

  // stats
  Server->server_stats->q_finished = 1;
  Server->server_stats->q_end_time = time(NULL);
  Server->server_stats = NULL;

  // delete this state machine and return
  TSfree(Server);
  TSContDestroy(contp);
  return TS_EVENT_NONE;
}

//////////////////////////////////////////////////////////////////////////
//
// state_dns_lookup
//
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////

int
state_dns_lookup(TSCont contp, int event, TSHostDBInfo host_info)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_dns_lookup, event);

  assert(Server->q_server_name);
  if (event != TS_EVENT_DNS_LOOKUP) {
    //      Server->q_error_type = RAFT_INTERNAL_PROXY_ERROR;
    TSDebug("serversm", "[%u][state_dns_lookup], unexpected event", Server->q_sm_id);

    if (Server->q_server_vc) {
      TSVConnAbort(Server->q_server_vc, 1);
      Server->q_server_vc = NULL;
    }
    Server->q_server_read_vio = NULL;
    Server->q_server_write_vio = NULL;

    return call_back_sub_sm_with_error(contp);
  }
  // ok, the dns processor always returns an EVENT_HOST_DB_LOOKUP
  // regardless of whether there was success or not. however, if
  // it calls us back with a null hostdbinfo structure, it means
  // that the lookup was unsuccessful.
  if (!host_info) {
    //        Server->q_error_type = RAFT_DNS_FAILURE;
    TSDebug("serversm", "[%u][state_dns_lookup] Unable to resolve DNS for %s", Server->q_sm_id, Server->q_server_name);

    // FUTURE: we may at some point want to try doing automatic
    // name expansion (appending local domain name or prepending www)
    // and retrying the dns lookup. for now, however, just bail out.

    return call_back_sub_sm_with_error(contp);
  }
  // ok, we have DNS resolution. set the ip address and connect to the server.
//    Server->q_server_ip = TSDNSInfoIPGet (host_info);
  Server->q_server_ip = TSGetIP(host_info);
  assert(Server->q_server_ip > 0);
  assert(Server->q_server_port > 0);

  // issue server connect.
  unsigned char *p = (unsigned char *) &(Server->q_server_ip);
  TSDebug("serversm", "[%u][state_dns_resolve] %s has resolved to %d.%d.%d.%d:%d",
           Server->q_sm_id, Server->q_server_name, p[0], p[1], p[2], p[3], Server->q_server_port);

//    SET_HANDLER((ServerSMHandler)&state_connect_to_server);
  set_handler(Server->q_server_current_handler, &state_connect_to_server);
  Server->q_pending_action = TSNetConnect(contp, Server->q_server_ip, Server->q_server_port);

  return TS_EVENT_IMMEDIATE;
}

//////////////////////////////////////////////////////////////////////////
//
// state_connect_to_server
//
//
// Next States:
//
//////////////////////////////////////////////////////////////////////////
int
state_connect_to_server(TSCont contp, int event, TSVConn vc)
{
  ServerSM *Server = (ServerSM *) TSContDataGet(contp);
  ENTER_STATE(serversm, Server->q_sm_id, state_connect_to_server, event);
  assert(vc != NULL);
  Server->q_server_vc = vc;
  if (event != TS_EVENT_NET_CONNECT) {
    //      Server->q_error_type = RAFT_INTERNAL_PROXY_ERROR;
    TSDebug("serversm", "[%u][state_connect_to_server] unexpected event", Server->q_sm_id);

    if (Server->q_server_vc) {
      TSVConnAbort(Server->q_server_vc, 1);
      Server->q_server_vc = NULL;
    }
    Server->q_server_read_vio = NULL;
    Server->q_server_write_vio = NULL;

    return call_back_sub_sm_with_error(contp);
  }

  TSDebug("serversm",
           "[%u][state_connect_to_server] conn_status is updated to %d", Server->q_sm_id, Server->q_server_conn_status);

  set_handler(Server->q_server_current_handler, &state_main_event);
  assert(Server->q_server_response_buffer);
  Server->q_server_read_vio = TSVConnRead(Server->q_server_vc, contp, Server->q_server_response_buffer, INT_MAX);

  assert(Server->q_server_read_vio);
  server_send_request(contp);
  return TS_EVENT_IMMEDIATE;
}

//////////////////////////////////////////////////////////////////////////
//
// void print_list()
//
//////////////////////////////////////////////////////////////////////////
void
print_list_1(RaftList_t ** the_list)
{
  RaftList_t *start = *the_list;
  int i = 0;

  while (start) {
    TSDebug("serversm", "print_list, current_item[%d] is %p", i, start->item);
    i++;
    start = start->next;
  }
}
