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

//-------------------------------------------------------------------------
// include files
//-------------------------------------------------------------------------

#include "ts/ink_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>

#include "P_EventSystem.h"
#include "P_Net.h"

#include "LogUtils.h"
#include "LogSock.h"
#include "LogField.h"
#include "LogFile.h"
#include "LogFormat.h"
#include "LogBuffer.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "Log.h"

#include "LogCollationClientSM.h"

//-------------------------------------------------------------------------
// statics
//-------------------------------------------------------------------------

int LogCollationClientSM::ID = 0;

//-------------------------------------------------------------------------
// LogCollationClientSM::LogCollationClientSM
//-------------------------------------------------------------------------

LogCollationClientSM::LogCollationClientSM(LogHost *log_host)
  : Continuation(new_ProxyMutex()),
    m_host_vc(NULL),
    m_host_vio(NULL),
    m_auth_buffer(NULL),
    m_auth_reader(NULL),
    m_send_buffer(NULL),
    m_send_reader(NULL),
    m_pending_action(NULL),
    m_pending_event(NULL),
    m_abort_vio(NULL),
    m_abort_buffer(NULL),
    m_host_is_up(false),
    m_buffer_send_list(NULL),
    m_buffer_in_iocore(NULL),
    m_flow(LOG_COLL_FLOW_ALLOW),
    m_log_host(log_host),
    m_id(ID++)
{
  Debug("log-coll", "[%d]client::constructor", m_id);

  ink_assert(m_log_host != NULL);

  // allocate send_list before we do anything
  // we can accept logs to send before we're fully initialized
  m_buffer_send_list = new LogBufferList();
  ink_assert(m_buffer_send_list != NULL);

  SET_HANDLER((LogCollationClientSMHandler)&LogCollationClientSM::client_handler);
  client_init(LOG_COLL_EVENT_SWITCH, NULL);
}

//-------------------------------------------------------------------------
// LogCollationClientSM::~LogCollationClientSM
//-------------------------------------------------------------------------

LogCollationClientSM::~LogCollationClientSM()
{
  Debug("log-coll", "[%d]client::destructor", m_id);

  ink_mutex_acquire(&(mutex->the_mutex));
  client_done(LOG_COLL_EVENT_SWITCH, NULL);
  ink_mutex_release(&(mutex->the_mutex));
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// handler
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationClientSM::client_handler
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_handler(int event, void *data)
{
  switch (m_client_state) {
  case LOG_COLL_CLIENT_AUTH:
    return client_auth(event, (VIO *)data);
  case LOG_COLL_CLIENT_DNS:
    return client_dns(event, (HostDBInfo *)data);
  case LOG_COLL_CLIENT_DONE:
    return client_done(event, data);
  case LOG_COLL_CLIENT_FAIL:
    return client_fail(event, data);
  case LOG_COLL_CLIENT_IDLE:
    return client_idle(event, data);
  case LOG_COLL_CLIENT_INIT:
    return client_init(event, data);
  case LOG_COLL_CLIENT_OPEN:
    return client_open(event, (NetVConnection *)data);
  case LOG_COLL_CLIENT_SEND:
    return client_send(event, (VIO *)data);
  default:
    ink_assert(!"unexpcted state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// pubic interface
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationClientSM::send
//-------------------------------------------------------------------------

int
LogCollationClientSM::send(LogBuffer *log_buffer)
{
  ip_port_text_buffer ipb;

  // take lock (can block on call because we're on our own thread)
  ink_mutex_acquire(&(mutex->the_mutex));

  Debug("log-coll", "[%d]client::send", m_id);

  // deny if state is DONE or FAIL
  if (m_client_state == LOG_COLL_CLIENT_DONE || m_client_state == LOG_COLL_CLIENT_FAIL) {
    Debug("log-coll", "[%d]client::send - DONE/FAIL state; rejecting", m_id);
    ink_mutex_release(&(mutex->the_mutex));
    return 0;
  }
  // only allow send if m_flow is ALLOW
  if (m_flow == LOG_COLL_FLOW_DENY) {
    Debug("log-coll", "[%d]client::send - m_flow = DENY; rejecting", m_id);
    ink_mutex_release(&(mutex->the_mutex));
    return 0;
  }
  // add log_buffer to m_buffer_send_list
  ink_assert(log_buffer != NULL);
  ink_assert(m_buffer_send_list != NULL);
  m_buffer_send_list->add(log_buffer);
  Debug("log-coll", "[%d]client::send - new log_buffer to send_list", m_id);

  // disable m_flow if there's too much work to do now
  ink_assert(m_flow == LOG_COLL_FLOW_ALLOW);
  if (m_buffer_send_list->get_size() >= Log::config->collation_max_send_buffers) {
    Debug("log-coll", "[%d]client::send - m_flow = DENY", m_id);
    Note("[log-coll] send-queue full; orphaning logs      "
         "[%s:%u]",
         m_log_host->ip_addr().toString(ipb, sizeof(ipb)), m_log_host->port());
    m_flow = LOG_COLL_FLOW_DENY;
  }
  // compute return value
  //   must be done before call to client_send.  log_buffer may
  //   be converted to network order during that call.
  LogBufferHeader *log_buffer_header = log_buffer->header();
  ink_assert(log_buffer_header != NULL);
  int bytes_to_write = log_buffer_header->byte_count;

  // re-initiate sending if currently idle
  if (m_client_state == LOG_COLL_CLIENT_IDLE) {
    m_client_state = LOG_COLL_CLIENT_SEND;
    ink_assert(m_pending_event == NULL);
    m_pending_event = eventProcessor.schedule_imm(this);
    // eventProcessor.schedule_imm(this);
    // client_send(LOG_COLL_EVENT_SWITCH, NULL);
  }

  ink_mutex_release(&(mutex->the_mutex));
  return bytes_to_write;
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// client states
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationClientSM::client_auth
// next: client_fail || client_send
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_auth(int event, VIO * /* vio ATS_UNUSED */)
{
  ip_port_text_buffer ipb;

  Debug("log-coll", "[%d]client::client_auth", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH: {
    Debug("log-coll", "[%d]client::client_auth - SWITCH", m_id);
    m_client_state = LOG_COLL_CLIENT_AUTH;

    NetMsgHeader nmh;
    int bytes_to_send = (int)strlen(Log::config->collation_secret);
    nmh.msg_bytes     = bytes_to_send;

    // memory copies, I know...  but it happens rarely!!!  ^_^
    ink_assert(m_auth_buffer != NULL);
    m_auth_buffer->write((char *)&nmh, sizeof(NetMsgHeader));
    m_auth_buffer->write(Log::config->collation_secret, bytes_to_send);
    bytes_to_send += sizeof(NetMsgHeader);

    Debug("log-coll", "[%d]client::client_auth - do_io_write(%d)", m_id, bytes_to_send);
    ink_assert(m_host_vc != NULL);
    m_host_vio = m_host_vc->do_io_write(this, bytes_to_send, m_auth_reader);
    ink_assert(m_host_vio != NULL);

    return EVENT_CONT;
  }

  case VC_EVENT_WRITE_READY:
    Debug("log-coll", "[%d]client::client_auth - WRITE_READY", m_id);
    return EVENT_CONT;

  case VC_EVENT_WRITE_COMPLETE:
    Debug("log-coll", "[%d]client::client_auth - WRITE_COMPLETE", m_id);

    Note("[log-coll] host up [%s:%u]", m_log_host->ip_addr().toString(ipb, sizeof(ipb)), m_log_host->port());
    m_host_is_up = true;

    return client_send(LOG_COLL_EVENT_SWITCH, NULL);

  case VC_EVENT_EOS:
  case VC_EVENT_ERROR: {
    Debug("log-coll", "[%d]client::client_auth - EOS|ERROR", m_id);
    int64_t read_avail = m_auth_reader->read_avail();

    if (read_avail > 0) {
      Debug("log-coll", "[%d]client::client_auth - consuming unsent data", m_id);
      m_auth_reader->consume(read_avail);
    }

    return client_fail(LOG_COLL_EVENT_SWITCH, NULL);
  }

  default:
    ink_assert(!"unexpected event");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_dns
// next: client_open || client_done
//-------------------------------------------------------------------------
int
LogCollationClientSM::client_dns(int event, HostDBInfo *hostdb_info)
{
  Debug("log-coll", "[%d]client::client_dns", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    m_client_state = LOG_COLL_CLIENT_DNS;
    if (m_log_host->m_name == 0) {
      return client_done(LOG_COLL_EVENT_SWITCH, NULL);
    }
    hostDBProcessor.getbyname_re(this, m_log_host->m_name, 0,
                                 HostDBProcessor::Options().setFlags(HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD));
    return EVENT_CONT;

  case EVENT_HOST_DB_LOOKUP:
    if (hostdb_info == NULL) {
      return client_done(LOG_COLL_EVENT_SWITCH, NULL);
    }
    m_log_host->m_ip.assign(hostdb_info->ip());
    m_log_host->m_ip.toString(m_log_host->m_ipstr, sizeof(m_log_host->m_ipstr));

    return client_open(LOG_COLL_EVENT_SWITCH, NULL);

  default:
    ink_assert(!"unexpected event");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_done
// next: <none>
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_done(int event, void * /* data ATS_UNUSED */)
{
  ip_port_text_buffer ipb;

  Debug("log-coll", "[%d]client::client_done", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    m_client_state = LOG_COLL_CLIENT_DONE;

    Note("[log-coll] client shutdown [%s:%u]", m_log_host->ip_addr().toString(ipb, sizeof(ipb)), m_log_host->port());

    // close connections
    if (m_host_vc) {
      Debug("log-coll", "[%d]client::client_done - disconnecting!", m_id);
      // do I need to delete this???
      m_host_vc->do_io_close(0);
      m_host_vc = 0;
    }
    // flush unsent logs to orphan
    flush_to_orphan();

    // cancel any pending events/actions
    if (m_pending_action != NULL) {
      m_pending_action->cancel();
    }
    if (m_pending_event != NULL) {
      m_pending_event->cancel();
    }
    // free memory
    if (m_auth_buffer) {
      if (m_auth_reader) {
        m_auth_buffer->dealloc_reader(m_auth_reader);
      }
      free_MIOBuffer(m_auth_buffer);
    }
    if (m_send_buffer) {
      if (m_send_reader) {
        m_send_buffer->dealloc_reader(m_send_reader);
      }
      free_MIOBuffer(m_send_buffer);
    }
    if (m_abort_buffer) {
      free_MIOBuffer(m_abort_buffer);
    }
    if (m_buffer_send_list) {
      delete m_buffer_send_list;
    }

    return EVENT_DONE;

  default:
    ink_assert(!"unexpected event");
    return EVENT_DONE;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_fail
// next: client_fail || client_open
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_fail(int event, void * /* data ATS_UNUSED */)
{
  ip_port_text_buffer ipb;

  Debug("log-coll", "[%d]client::client_fail", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]client::client_fail - SWITCH", m_id);
    m_client_state = LOG_COLL_CLIENT_FAIL;

    // avoid flooding log when host is down
    if (m_host_is_up) {
      Note("[log-coll] host down [%s:%u]", m_log_host->ip_addr().toString(ipb, sizeof ipb), m_log_host->m_port);
      char msg_buf[128];
      snprintf(msg_buf, sizeof(msg_buf), "Collation host %s:%u down", m_log_host->ip_addr().toString(ipb, sizeof ipb),
               m_log_host->m_port);
      RecSignalManager(MGMT_SIGNAL_SAC_SERVER_DOWN, msg_buf);
      m_host_is_up = false;
    }

    // close our NetVConnection (do I need to delete this)
    if (m_host_vc) {
      m_host_vc->do_io_close(0);
      m_host_vc = 0;
    }
    // flush unsent logs to orphan
    flush_to_orphan();

    // call back in collation_retry_sec seconds
    ink_assert(m_pending_event == NULL);
    m_pending_event = eventProcessor.schedule_in(this, HRTIME_SECONDS(Log::config->collation_retry_sec));

    return EVENT_CONT;

  case EVENT_INTERVAL:
    Debug("log-coll", "[%d]client::client_fail - INTERVAL", m_id);
    m_pending_event = NULL;
    return client_open(LOG_COLL_EVENT_SWITCH, NULL);

  default:
    ink_assert(!"unexpected event");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_idle
// next: client_send
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_idle(int event, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]client::client_idle", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    m_client_state = LOG_COLL_CLIENT_IDLE;
    return EVENT_CONT;

  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
    Debug("log-coll", "[%d]client::client_idle - EOS|ERROR", m_id);
    return client_fail(LOG_COLL_EVENT_SWITCH, NULL);

  default:
    ink_assert(!"unexpcted state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_init
// next: client_dns
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_init(int event, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]client::client_init", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    m_client_state = LOG_COLL_CLIENT_INIT;
    ink_assert(m_pending_event == NULL);
    ink_mutex_acquire(&(mutex->the_mutex));
    m_pending_event = eventProcessor.schedule_imm(this);
    ink_mutex_release(&(mutex->the_mutex));
    return EVENT_CONT;

  case EVENT_IMMEDIATE:
    // callback complete, reset m_pending_event
    m_pending_event = NULL;

    // allocate buffers
    m_auth_buffer = new_MIOBuffer();
    ink_assert(m_auth_buffer != NULL);
    m_auth_reader = m_auth_buffer->alloc_reader();
    ink_assert(m_auth_reader != NULL);
    m_send_buffer = new_MIOBuffer();
    ink_assert(m_send_buffer != NULL);
    m_send_reader = m_send_buffer->alloc_reader();
    ink_assert(m_send_reader != NULL);
    m_abort_buffer = new_MIOBuffer();
    ink_assert(m_abort_buffer != NULL);

    // if we don't have an ip already, switch to client_dns
    if (!m_log_host->ip_addr().isValid()) {
      return client_dns(LOG_COLL_EVENT_SWITCH, NULL);
    } else {
      return client_open(LOG_COLL_EVENT_SWITCH, NULL);
    }

  default:
    ink_assert(!"unexpected state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_open
// next: client_auth || client_fail
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_open(int event, NetVConnection *net_vc)
{
  ip_port_text_buffer ipb;
  Debug("log-coll", "[%d]client::client_open", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]client::client_open - SWITCH", m_id);
    m_client_state = LOG_COLL_CLIENT_OPEN;

    {
      IpEndpoint target;
      target.assign(m_log_host->ip_addr(), htons(m_log_host->port()));
      ink_assert(target.isValid());
      Action *connect_action_handle = netProcessor.connect_re(this, &target.sa);

      if (connect_action_handle != ACTION_RESULT_DONE) {
        ink_assert(!m_pending_action);
        m_pending_action = connect_action_handle;
      }
    }

    return EVENT_CONT;

  case NET_EVENT_OPEN:
    Debug("log-coll", "[%d]client::client_open - %s:%u", m_id, m_log_host->ip_addr().toString(ipb, sizeof ipb), m_log_host->port());

    // callback complete, reset m_pending_action
    m_pending_action = NULL;

    ink_assert(net_vc != NULL);
    m_host_vc = net_vc;

    // setup a client reader just for detecting a host disconnnect
    // (iocore should call back this function with and EOS/ERROR)
    m_abort_vio = m_host_vc->do_io_read(this, 1, m_abort_buffer);

    // change states
    return client_auth(LOG_COLL_EVENT_SWITCH, NULL);

  case NET_EVENT_OPEN_FAILED:
    Debug("log-coll", "[%d]client::client_open - OPEN_FAILED", m_id);
    // callback complete, reset m_pending_pending action
    m_pending_action = NULL;
    return client_fail(LOG_COLL_EVENT_SWITCH, NULL);

  default:
    ink_assert(!"unexpected event");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationClientSM::client_send
// next: client_fail || client_idle || client_send
//-------------------------------------------------------------------------

int
LogCollationClientSM::client_send(int event, VIO * /* vio ATS_UNUSED */)
{
  ip_port_text_buffer ipb;

  Debug("log-coll", "[%d]client::client_send", m_id);

  switch (event) {
  case EVENT_IMMEDIATE:
    Debug("log-coll", "[%d]client::client_send - EVENT_IMMEDIATE", m_id);
    // callback complete, reset m_pending_event
    m_pending_event = NULL;

  // fall through to LOG_COLL_EVENT_SWITCH

  case LOG_COLL_EVENT_SWITCH: {
    Debug("log-coll", "[%d]client::client_send - SWITCH", m_id);
    m_client_state = LOG_COLL_CLIENT_SEND;

    // get a buffer off our queue
    ink_assert(m_buffer_send_list != NULL);
    ink_assert(m_buffer_in_iocore == NULL);
    if ((m_buffer_in_iocore = m_buffer_send_list->get()) == NULL) {
      return client_idle(LOG_COLL_EVENT_SWITCH, NULL);
    }
    Debug("log-coll", "[%d]client::client_send - send_list to m_buffer_in_iocore", m_id);
    Debug("log-coll", "[%d]client::client_send - send_list_size(%d)", m_id, m_buffer_send_list->get_size());

    // enable m_flow if we're out of work to do
    if (m_flow == LOG_COLL_FLOW_DENY && m_buffer_send_list->get_size() == 0) {
      Debug("log-coll", "[%d]client::client_send - m_flow = ALLOW", m_id);
      Note("[log-coll] send-queue clear; resuming collation [%s:%u]", m_log_host->ip_addr().toString(ipb, sizeof ipb),
           m_log_host->port());
      m_flow = LOG_COLL_FLOW_ALLOW;
    }
// future work:
// Wrap the buffer in a io_buffer_block and send directly to
// do_io_write to save a memory copy.  But for now, just
// write the lame way.

#if defined(LOG_BUFFER_TRACKING)
    Debug("log-buftrak", "[%d]client::client_send - network write begin", m_buffer_in_iocore->header()->id);
#endif // defined(LOG_BUFFER_TRACKING)

    // prepare to send data
    ink_assert(m_buffer_in_iocore != NULL);
    LogBufferHeader *log_buffer_header = m_buffer_in_iocore->header();
    ink_assert(log_buffer_header != NULL);
    NetMsgHeader nmh;
    int bytes_to_send = log_buffer_header->byte_count;
    nmh.msg_bytes     = bytes_to_send;
    // TODO: We currently don't try to make the log buffers handle little vs big endian. TS-1156.
    // m_buffer_in_iocore->convert_to_network_order();

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_sent_to_network_stat, log_buffer_header->entry_count);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_sent_to_network_stat, log_buffer_header->byte_count);

    // copy into m_send_buffer
    ink_assert(m_send_buffer != NULL);
    m_send_buffer->write((char *)&nmh, sizeof(NetMsgHeader));
    m_send_buffer->write((char *)log_buffer_header, bytes_to_send);
    bytes_to_send += sizeof(NetMsgHeader);

    // send m_send_buffer to iocore
    Debug("log-coll", "[%d]client::client_send - do_io_write(%d)", m_id, bytes_to_send);
    ink_assert(m_host_vc != NULL);
    m_host_vio = m_host_vc->do_io_write(this, bytes_to_send, m_send_reader);
    ink_assert(m_host_vio != NULL);
  }
    return EVENT_CONT;

  case VC_EVENT_WRITE_READY:
    Debug("log-coll", "[%d]client::client_send - WRITE_READY", m_id);
    return EVENT_CONT;

  case VC_EVENT_WRITE_COMPLETE:
    Debug("log-coll", "[%d]client::client_send - WRITE_COMPLETE", m_id);

    ink_assert(m_buffer_in_iocore != NULL);
#if defined(LOG_BUFFER_TRACKING)
    Debug("log-buftrak", "[%d]client::client_send - network write complete", m_buffer_in_iocore->header()->id);
#endif // defined(LOG_BUFFER_TRACKING)

    // done with the buffer, delete it
    Debug("log-coll", "[%d]client::client_send - m_buffer_in_iocore[%p] to delete_list", m_id, m_buffer_in_iocore);
    LogBuffer::destroy(m_buffer_in_iocore);
    m_buffer_in_iocore = NULL;

    // switch back to client_send
    return client_send(LOG_COLL_EVENT_SWITCH, NULL);

  case VC_EVENT_EOS:
  case VC_EVENT_ERROR: {
    Debug("log-coll", "[%d]client::client_send - EOS|ERROR", m_id);
    int64_t read_avail = m_send_reader->read_avail();

    if (read_avail > 0) {
      Debug("log-coll", "[%d]client::client_send - consuming unsent data", m_id);
      m_send_reader->consume(read_avail);
    }

    return client_fail(LOG_COLL_EVENT_SWITCH, NULL);
  }

  default:
    Debug("log-coll", "[%d]client::client_send - default", m_id);
    return client_fail(LOG_COLL_EVENT_SWITCH, NULL);
  }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// support functions
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationClientSM::flush_to_orphan
//-------------------------------------------------------------------------
void
LogCollationClientSM::flush_to_orphan()
{
  Debug("log-coll", "[%d]client::flush_to_orphan", m_id);

  // if in middle of a write, flush buffer_in_iocore to orphan
  if (m_buffer_in_iocore != NULL) {
    Debug("log-coll", "[%d]client::flush_to_orphan - m_buffer_in_iocore to oprhan", m_id);
    // TODO: We currently don't try to make the log buffers handle little vs big endian. TS-1156.
    // m_buffer_in_iocore->convert_to_host_order();
    m_log_host->orphan_write_and_try_delete(m_buffer_in_iocore);
    m_buffer_in_iocore = NULL;
  }
  // flush buffers in send_list to orphan
  LogBuffer *log_buffer;
  ink_assert(m_buffer_send_list != NULL);
  while ((log_buffer = m_buffer_send_list->get()) != NULL) {
    Debug("log-coll", "[%d]client::flush_to_orphan - send_list to orphan", m_id);
    m_log_host->orphan_write_and_try_delete(log_buffer);
  }

  // Now send_list is empty, let's update m_flow to ALLOW status
  Debug("log-coll", "[%d]client::client_send - m_flow = ALLOW", m_id);
  m_flow = LOG_COLL_FLOW_ALLOW;
}
