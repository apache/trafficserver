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

#include "I_IOBuffer.h"
#include "I_VConnection.h"
#include "I_VIO.h"
#include "ProxyTransaction.h"

class HttpSM;
using HttpSMHandler = int (HttpSM::*)(int, void *);

enum HttpVC_t {
  HTTP_UNKNOWN = 0,
  HTTP_UA_VC,
  HTTP_SERVER_VC,
  HTTP_TRANSFORM_VC,
  HTTP_CACHE_READ_VC,
  HTTP_CACHE_WRITE_VC,
  HTTP_RAW_SERVER_VC
};

struct HttpVCTableEntry {
  VConnection *vc;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio;
  VIO *write_vio;
  HttpSMHandler vc_read_handler;
  HttpSMHandler vc_write_handler;
  HttpVC_t vc_type;
  HttpSM *sm;
  bool eos;
  bool in_tunnel;
};

class HttpUserAgent
{
public:
  HttpVCTableEntry *get_entry() const;
  void set_entry(HttpVCTableEntry *entry);

  IOBufferReader *get_raw_buffer_reader();
  void set_raw_buffer_reader(IOBufferReader *raw_buffer_reader);

  ProxyTransaction *get_txn() const;
  void set_txn(ProxyTransaction *txn);

private:
  HttpVCTableEntry *m_entry{nullptr};
  IOBufferReader *m_raw_buffer_reader{nullptr};
  ProxyTransaction *m_txn{nullptr};
};

inline HttpVCTableEntry *
HttpUserAgent::get_entry() const
{
  return m_entry;
}

inline void
HttpUserAgent::set_entry(HttpVCTableEntry *entry)
{
  m_entry = entry;
}

inline IOBufferReader *
HttpUserAgent::get_raw_buffer_reader()
{
  return m_raw_buffer_reader;
}

inline void
HttpUserAgent::set_raw_buffer_reader(IOBufferReader *raw_buffer_reader)
{
  m_raw_buffer_reader = raw_buffer_reader;
}

inline ProxyTransaction *
HttpUserAgent::get_txn() const
{
  return m_txn;
}

inline void
HttpUserAgent::set_txn(ProxyTransaction *txn)
{
  m_txn = txn;
}
