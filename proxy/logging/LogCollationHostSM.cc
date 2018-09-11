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

#include "tscore/ink_config.h"

#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
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

#include "LogCollationHostSM.h"

//-------------------------------------------------------------------------
// statics
//-------------------------------------------------------------------------

int LogCollationHostSM::ID = 0;

//-------------------------------------------------------------------------
// LogCollationHostSM::LogCollationHostSM
//-------------------------------------------------------------------------

LogCollationHostSM::LogCollationHostSM(NetVConnection *client_vc)
  : Continuation(new_ProxyMutex()),
    m_client_vc(client_vc),
    m_client_vio(nullptr),
    m_client_buffer(nullptr),
    m_client_reader(nullptr),
    m_pending_event(nullptr),
    m_read_buffer(nullptr),
    m_read_bytes_wanted(0),
    m_read_bytes_received(0),
    m_read_buffer_fast_allocator_size(-1),
    m_client_ip(0),
    m_client_port(0),
    m_id(ID++)
{
  RecInt rec_timeout;
  int timeout = 86390;
  Debug("log-coll", "[%d]host::constructor", m_id);

  ink_assert(m_client_vc != nullptr);

  // assign an explicit inactivity timeout so that it will not get the default value later
  if (RecGetRecordInt("proxy.config.log.collation_host_timeout", &rec_timeout) == REC_ERR_OKAY) {
    timeout = rec_timeout;
  }
  m_client_vc->set_inactivity_timeout(HRTIME_SECONDS(timeout));

  // get client info
  m_client_ip   = m_client_vc->get_remote_ip();
  m_client_port = m_client_vc->get_remote_port();
  Note("[log-coll] client connected [%d.%d.%d.%d:%d]", ((unsigned char *)(&m_client_ip))[0], ((unsigned char *)(&m_client_ip))[1],
       ((unsigned char *)(&m_client_ip))[2], ((unsigned char *)(&m_client_ip))[3], m_client_port);

  SET_HANDLER((LogCollationHostSMHandler)&LogCollationHostSM::host_handler);
  host_init(LOG_COLL_EVENT_SWITCH, nullptr);
}

void
LogCollationHostSM::freeReadBuffer()
{
  if (m_read_buffer) {
    if (m_read_buffer_fast_allocator_size >= 0) {
      ioBufAllocator[m_read_buffer_fast_allocator_size].free_void(m_read_buffer);
    } else {
      ats_free(m_read_buffer);
    }
    m_read_buffer = nullptr;
  }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// handlers
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationHostSM::host_handler
//-------------------------------------------------------------------------

int
LogCollationHostSM::host_handler(int event, void *data)
{
  switch (m_host_state) {
  case LOG_COLL_HOST_AUTH:
    return host_auth(event, data);
  case LOG_COLL_HOST_DONE:
    return host_done(event, data);
  case LOG_COLL_HOST_INIT:
    return host_init(event, data);
  case LOG_COLL_HOST_RECV:
    return host_recv(event, data);
  default:
    ink_assert(!"unexpected state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationHostSM::read_handler
//-------------------------------------------------------------------------

int
LogCollationHostSM::read_handler(int event, void *data)
{
  switch (m_read_state) {
  case LOG_COLL_READ_BODY:
    return read_body(event, (VIO *)data);
  case LOG_COLL_READ_HDR:
    return read_hdr(event, (VIO *)data);
  default:
    ink_assert(!"unexpected state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// host states
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationHostSM::host_auth
// next:  host_done || host_recv
//-------------------------------------------------------------------------

int
LogCollationHostSM::host_auth(int event, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]host::host_auth", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]host::host_auth - SWITCH", m_id);
    m_host_state = LOG_COLL_HOST_AUTH;
    return read_start();

  case LOG_COLL_EVENT_READ_COMPLETE:
    Debug("log-coll", "[%d]host::host_auth - READ_COMPLETE", m_id);
    {
      // compare authorization secrets
      ink_assert(m_read_buffer != nullptr);
      int diff = strncmp(m_read_buffer, Log::config->collation_secret, m_read_bytes_received);
      freeReadBuffer();
      if (!diff) {
        Debug("log-coll", "[%d]host::host_auth - authenticated!", m_id);
        return host_recv(LOG_COLL_EVENT_SWITCH, nullptr);
      } else {
        Debug("log-coll", "[%d]host::host_auth - authenticated failed!", m_id);
        Note("[log-coll] authentication failed [%d.%d.%d.%d:%d]", ((unsigned char *)(&m_client_ip))[0],
             ((unsigned char *)(&m_client_ip))[1], ((unsigned char *)(&m_client_ip))[2], ((unsigned char *)(&m_client_ip))[3],
             m_client_port);
        return host_done(LOG_COLL_EVENT_SWITCH, nullptr);
      }
    }

  case LOG_COLL_EVENT_ERROR:
    Debug("log-coll", "[%d]host::host_auth - ERROR", m_id);
    return host_done(LOG_COLL_EVENT_SWITCH, nullptr);

  default:
    ink_assert(!"unexpected state");
    return EVENT_CONT;
  }
}

//-------------------------------------------------------------------------
// LogCollationHostSM::host_done
// next: none
//-------------------------------------------------------------------------

int
LogCollationHostSM::host_done(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]host::host_done", m_id);

  // close connections
  if (m_client_vc) {
    Debug("log-coll", "[%d]host::host_done - disconnecting!", m_id);
    m_client_vc->do_io_close();
    m_client_vc = nullptr;
    Note("[log-coll] client disconnected [%d.%d.%d.%d:%d]", ((unsigned char *)(&m_client_ip))[0],
         ((unsigned char *)(&m_client_ip))[1], ((unsigned char *)(&m_client_ip))[2], ((unsigned char *)(&m_client_ip))[3],
         m_client_port);
  }
  // free memory
  if (m_client_buffer) {
    if (m_client_reader) {
      m_client_buffer->dealloc_reader(m_client_reader);
    }
    free_MIOBuffer(m_client_buffer);
  }
  // delete this state machine and return
  delete this;
  return EVENT_DONE;
}

//-------------------------------------------------------------------------
// LogCollationHostSM::host_init
// next: host_auth || host_done
//-------------------------------------------------------------------------

int
LogCollationHostSM::host_init(int event, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]host::host_init", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    m_host_state    = LOG_COLL_HOST_INIT;
    m_pending_event = eventProcessor.schedule_imm(this);
    return EVENT_CONT;

  case EVENT_IMMEDIATE:
    // allocate memory
    m_client_buffer = new_MIOBuffer();
    ink_assert(m_client_buffer != nullptr);
    m_client_reader = m_client_buffer->alloc_reader();
    ink_assert(m_client_reader != nullptr);
    return host_auth(LOG_COLL_EVENT_SWITCH, nullptr);

  default:
    ink_assert(!"unexpected state");
    return EVENT_DONE;
  }
}

//-------------------------------------------------------------------------
// LogCollationHostSM::host_recv
// next: host_done || host_recv
//-------------------------------------------------------------------------

int
LogCollationHostSM::host_recv(int event, void * /* data ATS_UNUSED */)
{
  Debug("log-coll", "[%d]host::host_recv", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]host::host_recv - SWITCH", m_id);
    m_host_state = LOG_COLL_HOST_RECV;
    return read_start();

  case LOG_COLL_EVENT_READ_COMPLETE:
    Debug("log-coll", "[%d]host::host_recv - READ_COMPLETE", m_id);
    {
      // grab the log_buffer
      LogBufferHeader *log_buffer_header;
      LogBuffer *log_buffer;
      LogFormat *log_format;
      LogObject *log_object;
      unsigned version;

      ink_assert(m_read_buffer != nullptr);
      ink_assert(m_read_bytes_received >= (int64_t)sizeof(LogBufferHeader));
      log_buffer_header = (LogBufferHeader *)m_read_buffer;

      // convert the buffer we just received to host order
      // TODO: We currently don't try to make the log buffers handle little vs big endian. TS-1156.
      // LogBuffer::convert_to_host_order(log_buffer_header);

      version = log_buffer_header->version;
      if (version != LOG_SEGMENT_VERSION) {
        Note("[log-coll] invalid LogBuffer received; invalid version - "
             "buffer = %u, current = %u",
             version, LOG_SEGMENT_VERSION);
        freeReadBuffer();

      } else {
        log_object = Log::match_logobject(log_buffer_header);
        if (!log_object) {
          Note("[log-coll] LogObject not found with fieldlist id; "
               "writing LogBuffer to scrap file");
          log_object = Log::global_scrap_object;
        }
        log_format = log_object->m_format;
        Debug("log-coll", "[%d]host::host_recv - using format '%s'", m_id, log_format->name());

        // make a new LogBuffer (log_buffer_header plus subsequent
        // buffer already converted to host order) and add it to the
        // object's flush queue
        //
        log_buffer = new LogBuffer(log_object, log_buffer_header);

        RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_received_from_network_stat, log_buffer_header->entry_count);

        RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_received_from_network_stat, log_buffer_header->byte_count);

        int idx = log_object->add_to_flush_queue(log_buffer);
        Log::preproc_notify[idx].signal();
      }

#if defined(LOG_BUFFER_TRACKING)
      Debug("log-buftrak", "[%d]host::host_recv - network read complete", log_buffer_header->id);
#endif // defined(LOG_BUFFER_TRACKING)

      // get ready for next read (memory may not be freed!!!)
      m_read_buffer = nullptr;

      return host_recv(LOG_COLL_EVENT_SWITCH, nullptr);
    }

  case LOG_COLL_EVENT_ERROR:
    Debug("log-coll", "[%d]host::host_recv - ERROR", m_id);
    return host_done(LOG_COLL_EVENT_SWITCH, nullptr);

  default:
    ink_assert(!"unexpected state");
    return EVENT_DONE;
  }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//
// read states
//
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// LogCollationHostSM::read_start
// next: read_hdr
//-------------------------------------------------------------------------

int
LogCollationHostSM::read_start()
{
  Debug("log-coll", "[%d]host::read_start", m_id);

  SET_HANDLER((LogCollationHostSMHandler)&LogCollationHostSM::read_handler);
  if (m_read_buffer) {
    ink_assert(!"m_read_buffer still points to something, doh!");
  }
  return read_hdr(LOG_COLL_EVENT_SWITCH, nullptr);
}

//-------------------------------------------------------------------------
// LogCollationHostSM::read_hdr
// next: read_body || read_done
//-------------------------------------------------------------------------

int
LogCollationHostSM::read_hdr(int event, VIO *vio)
{
  Debug("log-coll", "[%d]host::read_hdr", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]host:read_hdr - SWITCH", m_id);
    m_read_state = LOG_COLL_READ_HDR;

    m_read_bytes_wanted   = sizeof(NetMsgHeader);
    m_read_bytes_received = 0;
    m_read_buffer         = (char *)&m_net_msg_header;
    ink_assert(m_client_vc != nullptr);
    Debug("log-coll", "[%d]host:read_hdr - do_io_read(%" PRId64 ")", m_id, m_read_bytes_wanted);
    m_client_vio = m_client_vc->do_io_read(this, m_read_bytes_wanted, m_client_buffer);
    ink_assert(m_client_vio != nullptr);
    return EVENT_CONT;

  case VC_EVENT_IMMEDIATE:
    Debug("log-coll", "[%d]host::read_hdr - IMMEDIATE", m_id);
    return EVENT_CONT;

  case VC_EVENT_READ_READY:
    Debug("log-coll", "[%d]host::read_hdr - READ_READY", m_id);
    read_partial(vio);
    return EVENT_CONT;

  case VC_EVENT_READ_COMPLETE:
    Debug("log-coll", "[%d]host::read_hdr - READ_COMPLETE", m_id);
    read_partial(vio);
    ink_assert(m_read_bytes_wanted == m_read_bytes_received);
    return read_body(LOG_COLL_EVENT_SWITCH, nullptr);

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
    Debug("log-coll", "[%d]host::read_hdr - TIMEOUT|EOS|ERROR", m_id);
    return read_done(LOG_COLL_EVENT_ERROR, nullptr);

  default:
    Debug("log-coll", "[%d]host::read_hdr - default %d", m_id, event);
    return read_done(LOG_COLL_EVENT_ERROR, nullptr);
  }
}

//-------------------------------------------------------------------------
// LogCollationHostSM::read_body
// next: read_body || read_done
//-------------------------------------------------------------------------

int
LogCollationHostSM::read_body(int event, VIO *vio)
{
  Debug("log-coll", "[%d]host::read_body", m_id);

  switch (event) {
  case LOG_COLL_EVENT_SWITCH:
    Debug("log-coll", "[%d]host:read_body - SWITCH", m_id);
    m_read_state = LOG_COLL_READ_BODY;

    m_read_bytes_wanted = m_net_msg_header.msg_bytes;
    ink_assert(m_read_bytes_wanted > 0);
    m_read_bytes_received = 0;
    if (m_read_bytes_wanted <= max_iobuffer_size) {
      m_read_buffer_fast_allocator_size = buffer_size_to_index(m_read_bytes_wanted);
      m_read_buffer                     = (char *)ioBufAllocator[m_read_buffer_fast_allocator_size].alloc_void();
    } else {
      m_read_buffer_fast_allocator_size = -1;
      m_read_buffer                     = (char *)ats_malloc(m_read_bytes_wanted);
    }
    ink_assert(m_read_buffer != nullptr);
    ink_assert(m_client_vc != nullptr);
    Debug("log-coll", "[%d]host:read_body - do_io_read(%" PRId64 ")", m_id, m_read_bytes_wanted);
    m_client_vio = m_client_vc->do_io_read(this, m_read_bytes_wanted, m_client_buffer);
    ink_assert(m_client_vio != nullptr);
    return EVENT_CONT;

  case VC_EVENT_IMMEDIATE:
    Debug("log-coll", "[%d]host::read_body - IMMEDIATE", m_id);
    return EVENT_CONT;

  case VC_EVENT_READ_READY:
    Debug("log-coll", "[%d]host::read_body - READ_READY", m_id);
    read_partial(vio);
    return EVENT_CONT;

  case VC_EVENT_READ_COMPLETE:
    Debug("log-coll", "[%d]host::read_body - READ_COMPLETE", m_id);
    read_partial(vio);
    ink_assert(m_read_bytes_wanted == m_read_bytes_received);
    return read_done(LOG_COLL_EVENT_READ_COMPLETE, nullptr);

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
    Debug("log-coll", "[%d]host::read_body - TIMEOUT|EOS|ERROR", m_id);
    return read_done(LOG_COLL_EVENT_ERROR, nullptr);

  default:
    Debug("log-coll", "[%d]host::read_body - default %d", m_id, event);
    return read_done(LOG_COLL_EVENT_ERROR, nullptr);
  }
}

//-------------------------------------------------------------------------
// LogCollationHostSM::read_done
// next: give control back to host state-machine
//-------------------------------------------------------------------------

int
LogCollationHostSM::read_done(int event, void * /* data ATS_UNUSED */)
{
  SET_HANDLER((LogCollationHostSMHandler)&LogCollationHostSM::host_handler);
  return host_handler(event, nullptr);
}

//-------------------------------------------------------------------------
// LogCollationHostSM::read_partial
//-------------------------------------------------------------------------

void
LogCollationHostSM::read_partial(VIO *vio)
{
  // checks
  ink_assert(vio != nullptr);
  ink_assert(vio->vc_server == m_client_vc);
  ink_assert(m_client_buffer != nullptr);
  ink_assert(m_client_reader != nullptr);

  // careful not to read more than we have memory for
  char *p                    = &(m_read_buffer[m_read_bytes_received]);
  int64_t bytes_wanted_now   = m_read_bytes_wanted - m_read_bytes_received;
  int64_t bytes_received_now = m_client_reader->read(p, bytes_wanted_now);

  m_read_bytes_received += bytes_received_now;
}
