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

#pragma once

//-------------------------------------------------------------------------
// includes
//-------------------------------------------------------------------------

#include "P_EventSystem.h"
#include "LogCollationBase.h"

//-------------------------------------------------------------------------
// pre-declarations
//-------------------------------------------------------------------------

struct LogBufferHeader;

//-------------------------------------------------------------------------
// LogCollationHostSM
//-------------------------------------------------------------------------

class LogCollationHostSM : public LogCollationBase, public Continuation
{
public:
  LogCollationHostSM(NetVConnection *client_vc);

  // handlers
  int host_handler(int event, void *data);
  int read_handler(int event, void *data);

private:
  enum HostState {
    LOG_COLL_HOST_NULL,
    LOG_COLL_HOST_AUTH,
    LOG_COLL_HOST_DONE,
    LOG_COLL_HOST_INIT,
    LOG_COLL_HOST_RECV,
  };

  enum ReadState {
    LOG_COLL_READ_NULL,
    LOG_COLL_READ_BODY,
    LOG_COLL_READ_HDR,
  };

private:
  // host states
  int host_init(int event, void *data);
  int host_auth(int event, void *data);
  int host_recv(int event, void *data);
  int host_done(int event, void *data);
  HostState m_host_state;

  // read states
  int read_hdr(int event, VIO *vio);
  int read_body(int event, VIO *vio);
  int read_done(int event, void *data);
  int read_start();
  void freeReadBuffer();
  ReadState m_read_state;

  // helper for read states
  void read_partial(VIO *vio);

  // iocore stuff
  NetVConnection *m_client_vc;
  VIO *m_client_vio;
  MIOBuffer *m_client_buffer;
  IOBufferReader *m_client_reader;
  Event *m_pending_event;

  // read_state stuff
  NetMsgHeader m_net_msg_header;
  char *m_read_buffer;
  int64_t m_read_bytes_wanted;
  int64_t m_read_bytes_received;
  int64_t m_read_buffer_fast_allocator_size;

  // client info
  int m_client_ip;
  int m_client_port;

  // debugging
  static int ID;
  int m_id;
};

typedef int (LogCollationHostSM::*LogCollationHostSMHandler)(int, void *);
